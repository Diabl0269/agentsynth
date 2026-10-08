// CardGlideAnimatorGhosts.cpp
//
// Delete and undo animation for canvas cards (docs/layout/animation.md "Delete and undo animation"). The model change
// is already final and synchronous; this is a paint-only layer on top of the same overlay the glide uses:
//   exit  - a card the mutation removed was snapshotted beforehand (noteExit) and shrinks toward its centre;
//   enter - a card an undo/redo created is hidden, then grows back from its centre and gets a fading accent outline.
// The phases (ExitEnterTimeline.h) run one after another, so the cards that moved close the gap only after the exit,
// and make room before the grow: no frame shows two cards overlapping.

#include "CardGlideAnimator.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"

#include <algorithm>

bool CardGlideAnimator::canAnimate() const { return forceAnimate_ || (hooks_.canAnimate && hooks_.canAnimate()); }

void CardGlideAnimator::noteExit(juce::Component* comp, uint32_t nodeUid) {
    if (depth_ == 0 || comp == nullptr || nodeUid == 0 || !canAnimate() || !comp->isVisible() ||
        comp->getBounds().isEmpty() || !comp->getBounds().intersects(card_glide_detail::visibleCanvasArea(*comp)))
        return;
    if (std::any_of(candidates_.begin(), candidates_.end(),
                    [nodeUid](const Candidate& c) { return c.nodeUid == nodeUid; }))
        return;
    const float scale = hooks_.snapshotScale ? hooks_.snapshotScale() : 1.0f;
    candidates_.push_back({comp, nodeUid, comp->getBounds(), snapshotOf(*comp, scale)});
}

// A restore only tears the cards down when it frees a node, so this is where a card it removes can still be
// pictured; a parameter-only undo, or one that only moves cards, never gets here and pictures nothing. Snapshotting
// every card when the Scope opened made each such undo stall for the length of a full repaint of every card on screen.
void CardGlideAnimator::noteExitsBeforeTeardown() {
    if (depth_ == 0 || !restoring_ || !hooks_.cards)
        return;
    for (const auto& e : hooks_.cards())
        noteExit(e.comp, e.nodeUid);
}

// A card a restore hid without tearing it down (it is still alive, so noteExitsBeforeTeardown never saw it) is
// pictured now, from where it stood when the Scope opened.
void CardGlideAnimator::noteRestoreExits(const std::vector<Entry>& now) {
    const float scale = hooks_.snapshotScale ? hooks_.snapshotScale() : 1.0f;
    for (const auto& c : before_) {
        auto* comp = c.comp.getComponent();
        if (comp == nullptr || c.nodeUid == 0 || !c.bounds.intersects(card_glide_detail::visibleCanvasArea(*comp)))
            continue;
        const bool known = std::any_of(candidates_.begin(), candidates_.end(),
                                       [&c](const Candidate& cand) { return cand.nodeUid == c.nodeUid; });
        const bool survives = std::any_of(now.begin(), now.end(), [&c](const Entry& e) {
            return e.nodeUid == c.nodeUid && e.comp != nullptr && e.comp->isVisible();
        });
        if (!known && !survives)
            candidates_.push_back({comp, c.nodeUid, c.bounds, snapshotOf(*comp, scale)});
    }
}

// The card's own raster (ZoomFrozenCachedImage) is a full picture of it at the canvas zoom, so taking it costs nothing;
// only a card with none (never painted, or resized since) is rendered. The raster can be one paint stale where the
// card invalidated it since, which a shrinking or growing ghost does not show.
juce::Image CardGlideAnimator::snapshotOf(juce::Component& comp, float scale) {
    ++snapshotCount_;
    if (auto* cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(comp.getCachedComponentImage()))
        if (auto raster = cache->lastRaster(); raster.isValid())
            return raster;
    ++renderedSnapshotCount_;
    return comp.createComponentSnapshot(comp.getLocalBounds(), true, scale);
}

bool CardGlideAnimator::hasMoveItems() const noexcept {
    return std::any_of(items_.begin(), items_.end(), [](const Item& it) { return it.kind == Kind::Move; });
}

// Drops the ghosts of an earlier animation: a new one replaces them rather than retargeting.
void CardGlideAnimator::landGhosts() noexcept {
    for (auto& item : items_)
        if (item.kind == Kind::Enter)
            if (auto* comp = item.comp.getComponent(); comp != nullptr && comp->getAlpha() == 0.0f)
                comp->setAlpha(item.savedAlpha);
    items_.erase(std::remove_if(items_.begin(), items_.end(), [](const Item& it) { return it.kind != Kind::Move; }),
                 items_.end());
}

void CardGlideAnimator::dropGhostsFor(const std::vector<uint32_t>& nodeUids) {
    const auto listed = [&nodeUids](const Item& it) {
        return it.kind != Kind::Move && std::find(nodeUids.begin(), nodeUids.end(), it.nodeUid) != nodeUids.end();
    };
    for (auto& item : items_)
        if (listed(item) && item.kind == Kind::Enter)
            if (auto* comp = item.comp.getComponent(); comp != nullptr && comp->getAlpha() == 0.0f)
                comp->setAlpha(item.savedAlpha);
    items_.erase(std::remove_if(items_.begin(), items_.end(), listed), items_.end());
    timeline_.hasExit = exitGhostCount() > 0;
    timeline_.hasEnter = enterGhostCount() > 0;
}

bool CardGlideAnimator::armGhosts(const std::vector<Captured>& before, const std::vector<Entry>& now,
                                  float snapshotScale) {
    landGhosts();
    phased_ = false;
    bool anyExit = false, anyEnter = false;
    if (restoring_)
        noteRestoreExits(now);

    for (auto& cand : candidates_) {
        const bool survives = std::any_of(now.begin(), now.end(), [&cand](const Entry& e) {
            return e.nodeUid == cand.nodeUid && e.comp != nullptr && e.comp->isVisible();
        });
        if (survives)
            continue;
        Item item;
        item.kind = Kind::Exit;
        item.nodeUid = cand.nodeUid;
        item.from = item.to = cand.rect;
        item.snapshot = cand.snapshot;
        items_.push_back(std::move(item));
        anyExit = true;
    }

    if (restoring_) {
        for (const auto& e : now) {
            if (e.nodeUid == 0 || e.comp == nullptr || !e.comp->isVisible() || e.comp->getBounds().isEmpty() ||
                e.comp->isMouseButtonDown(true))
                continue;
            const bool existed = std::any_of(before.begin(), before.end(), [&e](const Captured& c) {
                return c.comp.getComponent() == e.comp || c.nodeUid == e.nodeUid;
            });
            if (existed || !e.comp->getBounds().intersects(card_glide_detail::visibleCanvasArea(*e.comp)))
                continue;
            Item item;
            item.kind = Kind::Enter;
            item.comp = e.comp;
            item.nodeUid = e.nodeUid;
            item.from = item.to = e.comp->getBounds();
            item.savedAlpha = e.comp->getAlpha();
            item.snapshot = snapshotOf(*e.comp, snapshotScale);
            e.comp->setAlpha(0.0f);
            items_.push_back(std::move(item));
            anyEnter = true;
        }
    }

    if (!anyExit && !anyEnter)
        return false;
    phased_ = true;
    reducedMotion_ = synth::ui::prefersReducedMotion();
    timeline_ = {};
    timeline_.hasExit = anyExit;
    timeline_.hasEnter = anyEnter;
    timeline_.hasGap = hasMoveItems();
    frame_ = {};
    progress_ = 0.0f;
    return true;
}

void CardGlideAnimator::applyTimelineAtMs(double elapsedMs) {
    if (!phased_)
        return;
    frame_ = timeline_.at(elapsedMs);
    progress_ = timeline_.hasGap ? frame_.gap : 0.0f;
    pruneItems();
    // An entered card goes live again the moment it has grown; only its outline remains.
    if (frame_.grow >= 1.0f)
        for (auto& item : items_)
            if (item.kind == Kind::Enter && !item.grown) {
                item.grown = true;
                if (auto* comp = item.comp.getComponent(); comp != nullptr && comp->getAlpha() == 0.0f)
                    comp->setAlpha(item.savedAlpha);
            }
}

void CardGlideAnimator::paintGhost(juce::Graphics& g, const Item& item) const {
    using synth::ui::ExitEnterTimeline;
    if (item.kind == Kind::Exit) {
        const float scale = ExitEnterTimeline::ghostScale(frame_.exit, true, reducedMotion_);
        const float alpha = ExitEnterTimeline::ghostAlpha(frame_.exit, true, reducedMotion_);
        if (scale <= 0.0f || alpha <= 0.0f || item.snapshot.isNull())
            return;
        juce::Graphics::ScopedSaveState state(g);
        g.setOpacity(alpha);
        g.drawImage(item.snapshot, ExitEnterTimeline::scaledAboutCentre(item.from, scale),
                    juce::RectanglePlacement::stretchToFit);
        return;
    }
    if (!item.grown && frame_.grow > 0.0f && !item.snapshot.isNull()) {
        const float scale = ExitEnterTimeline::ghostScale(frame_.grow, false, reducedMotion_);
        const float alpha = ExitEnterTimeline::ghostAlpha(frame_.grow, false, reducedMotion_);
        if (scale > 0.0f && alpha > 0.0f) {
            juce::Graphics::ScopedSaveState state(g);
            g.setOpacity(alpha);
            g.drawImage(item.snapshot, ExitEnterTimeline::scaledAboutCentre(item.to, scale),
                        juce::RectanglePlacement::stretchToFit);
        }
    }
    if (frame_.grow >= 1.0f && frame_.outline < 1.0f && hooks_.accent) {
        g.setColour(hooks_.accent().withAlpha(1.0f - frame_.outline));
        g.drawRoundedRectangle(item.to.toFloat().expanded(0.5f), 4.0f, 1.0f);
    }
}

int CardGlideAnimator::exitGhostCount() const noexcept {
    return static_cast<int>(
        std::count_if(items_.begin(), items_.end(), [](const Item& it) { return it.kind == Kind::Exit; }));
}

int CardGlideAnimator::enterGhostCount() const noexcept {
    return static_cast<int>(
        std::count_if(items_.begin(), items_.end(), [](const Item& it) { return it.kind == Kind::Enter; }));
}

juce::Rectangle<float> CardGlideAnimator::ghostRectFor(uint32_t nodeUid) const noexcept {
    using synth::ui::ExitEnterTimeline;
    for (const auto& item : items_) {
        if (item.nodeUid != nodeUid || item.kind == Kind::Move)
            continue;
        if (item.kind == Kind::Exit)
            return ExitEnterTimeline::scaledAboutCentre(
                item.from, ExitEnterTimeline::ghostScale(frame_.exit, true, reducedMotion_));
        if (item.grown)
            return item.to.toFloat();
        if (frame_.grow > 0.0f)
            return ExitEnterTimeline::scaledAboutCentre(
                item.to, ExitEnterTimeline::ghostScale(frame_.grow, false, reducedMotion_));
    }
    return {};
}
