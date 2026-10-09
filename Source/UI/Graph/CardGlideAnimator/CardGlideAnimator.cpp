// CardGlideAnimator.cpp
//
// The glide overlay. Real cards keep their FINAL bounds the moment a mutation lands (hit-tests, hulls and cable
// geometry all read them), so the slide is an illusion: each moved card is hidden with alpha 0 (JUCE paints nothing
// for it, yet mouse routing still resolves to its final rect) and a snapshot of it is drawn interpolating from the old
// rect to the new one, on top of the canvas. Time-bounded: the driver repaints only while it runs.

#include "CardGlideAnimator.h"

#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Layout/CableCurve.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"

#include <algorithm>
#include <limits>

namespace {
constexpr double kGlideMs = 160.0;

juce::Rectangle<float> lerpRect(juce::Rectangle<int> a, juce::Rectangle<int> b, float t) {
    auto lerp = [t](int x, int y) { return static_cast<float>(x) + static_cast<float>(y - x) * t; };
    return {lerp(a.getX(), b.getX()), lerp(a.getY(), b.getY()), lerp(a.getWidth(), b.getWidth()),
            lerp(a.getHeight(), b.getHeight())};
}
} // namespace

namespace card_glide_detail {
// The part of the canvas the card's viewer shows, in canvas coordinates: the canvas (the card's parent) is a
// transformed child of the editor that frames it. Everything when the card has no such grandparent.
juce::Rectangle<int> visibleCanvasArea(const juce::Component& card) {
    auto* canvas = card.getParentComponent();
    auto* view = canvas != nullptr ? canvas->getParentComponent() : nullptr;
    if (view == nullptr)
        return {std::numeric_limits<int>::min() / 2, std::numeric_limits<int>::min() / 2,
                std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
    return canvas->getLocalArea(view, view->getLocalBounds());
}

} // namespace card_glide_detail
using card_glide_detail::visibleCanvasArea;

CardGlideAnimator::CardGlideAnimator()
    : fold_(std::make_unique<MacroFoldAnimator>()) {}

MacroFoldAnimator& CardGlideAnimator::fold() noexcept { return *fold_; }
const MacroFoldAnimator& CardGlideAnimator::fold() const noexcept { return *fold_; }

CardGlideAnimator::~CardGlideAnimator() {
    if (hooks_.updater != nullptr)
        driver_.stop(*hooks_.updater);
}

void CardGlideAnimator::setHooks(Hooks hooks) {
    hooks_ = std::move(hooks);
    MacroFoldAnimator::Hooks foldHooks;
    foldHooks.updater = hooks_.updater;
    foldHooks.canAnimate = [this] { return canAnimate(); };
    foldHooks.repaint = hooks_.repaint;
    foldHooks.repaintArea = hooks_.repaintArea;
    foldHooks.visibleCables = hooks_.visibleCables;
    foldHooks.categoryColour = hooks_.categoryColour;
    foldHooks.paintCable = hooks_.paintCable;
    fold_->setHooks(std::move(foldHooks));
}

std::vector<CardGlideAnimator::Captured> CardGlideAnimator::capture(const std::vector<Entry>& cards) {
    std::vector<Captured> out;
    for (const auto& e : cards)
        if (e.comp != nullptr && e.comp->isVisible() && !e.comp->getBounds().isEmpty())
            out.push_back({juce::Component::SafePointer<juce::Component>(e.comp), e.nodeUid, e.comp->getBounds()});
    return out;
}

void CardGlideAnimator::noteStartRect(juce::Component* comp, uint32_t nodeUid, juce::Rectangle<int> from) {
    if (depth_ == 0 || comp == nullptr || from.isEmpty())
        return;
    before_.push_back({juce::Component::SafePointer<juce::Component>(comp), nodeUid, from});
}

juce::Rectangle<float> CardGlideAnimator::currentRect(const Item& item) const noexcept {
    return lerpRect(item.from, item.to, progress_);
}

// A card already gliding retargets from where it is drawn right now (docs/layout/animation.md "Interruption"), and
// keeps the alpha saved when it was first hidden: re-saving would record the 0 this class set. Every other live card
// is rebased the same way because the driver restarts its clock at 0.
// A card whose whole path (the union of its old and new rects, which every in-between rect stays inside) is outside
// the visible canvas is neither hidden nor snapshotted: nothing of it can be seen, and rendering a snapshot per moved
// card is what made an undo of a many-track arrange stall for most of a second. It keeps an item without a snapshot,
// so cables touching it still slide (offsetFor).
bool CardGlideAnimator::arm(const std::vector<Captured>& before, const std::vector<Entry>& now, float snapshotScale) {
    std::vector<const Entry*> changed;
    std::vector<juce::Rectangle<int>> fromRects;
    for (const auto& e : now) {
        if (e.comp == nullptr || !e.comp->isVisible() || e.comp->getBounds().isEmpty() ||
            e.comp->isMouseButtonDown(true))
            continue;
        // The same component, or (a restore that rebuilt every card) the card now standing for the same node.
        const auto was = std::find_if(before.begin(), before.end(), [&e](const Captured& c) {
            return c.comp.getComponent() == e.comp || (e.nodeUid != 0 && c.comp == nullptr && c.nodeUid == e.nodeUid);
        });
        if (was == before.end() || was->bounds == e.comp->getBounds())
            continue;
        changed.push_back(&e);
        fromRects.push_back(was->bounds);
    }
    if (changed.empty())
        return false;

    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [](const Item& it) { return it.kind == Kind::Move && it.comp == nullptr; }),
                 items_.end());
    for (auto& item : items_)
        if (item.kind == Kind::Move)
            item.from = currentRect(item).toNearestInt();
    const auto visible = visibleCanvasArea(*changed.front()->comp);
    for (size_t i = 0; i < changed.size(); ++i) {
        auto* comp = changed[i]->comp;
        auto existing = std::find_if(items_.begin(), items_.end(), [comp](const Item& it) {
            return it.kind == Kind::Move && it.comp.getComponent() == comp;
        });
        if (existing != items_.end()) {
            existing->to = comp->getBounds(); // `from` already rebased to the drawn position
            if (existing->snapshot.isNull() && existing->from.getUnion(existing->to).intersects(visible)) {
                existing->snapshot = snapshotOf(*comp, snapshotScale);
                comp->setAlpha(0.0f); // was off-screen, now crosses the view: its alpha was never touched
            }
            continue;
        }
        Item item;
        item.comp = comp;
        item.nodeUid = changed[i]->nodeUid;
        item.from = fromRects[i];
        item.to = comp->getBounds();
        item.savedAlpha = comp->getAlpha();
        if (item.from.getUnion(item.to).intersects(visible)) {
            item.snapshot = snapshotOf(*comp, snapshotScale);
            comp->setAlpha(0.0f);
        }
        items_.push_back(std::move(item));
    }
    progress_ = 0.0f;
    ++armCount_;
    return true;
}

void CardGlideAnimator::applyTweenAt(float t) noexcept {
    progress_ = juce::jlimit(0.0f, 1.0f, t);
    pruneItems();
}

void CardGlideAnimator::pruneItems() {
    // A card the user grabs mid-glide stops gliding: it is theirs now.
    for (auto& item : items_)
        if (item.kind == Kind::Move && item.comp != nullptr && item.comp->isMouseButtonDown(true)) {
            item.comp->setAlpha(item.savedAlpha);
            item.comp = nullptr;
            item.snapshot = {};
        }
    // An off-screen card's item has no snapshot but a live card; one whose card is gone or grabbed has neither.
    items_.erase(std::remove_if(
                     items_.begin(), items_.end(),
                     [](const Item& it) { return it.snapshot.isNull() && it.comp == nullptr && it.border == nullptr; }),
                 items_.end());
}

void CardGlideAnimator::finish() noexcept {
    for (auto& item : items_)
        if (auto* comp = item.comp.getComponent(); comp != nullptr && comp->getAlpha() == 0.0f)
            comp->setAlpha(item.savedAlpha);
    items_.clear();
    progress_ = 0.0f;
    frame_ = {};
    phased_ = false;
}

void CardGlideAnimator::paint(juce::Graphics& g) const {
    fold_->paint(g);
    for (const auto& item : items_) {
        if (item.kind != Kind::Move) {
            paintGhost(g, item);
            continue;
        }
        if (item.snapshot.isNull())
            continue;
        g.drawImage(item.snapshot, currentRect(item), juce::RectanglePlacement::stretchToFit);
    }
}

juce::Point<float> CardGlideAnimator::offsetFor(uint32_t nodeUid) const noexcept {
    if (nodeUid == 0)
        return {};
    for (const auto& item : items_)
        if (item.kind == Kind::Move && item.nodeUid == nodeUid) {
            const auto cur = currentRect(item);
            return {cur.getX() - static_cast<float>(item.to.getX()), cur.getY() - static_cast<float>(item.to.getY())};
        }
    return {};
}

// Every chain endpoint is a real src/dst node (an attenuverter chain carries its hidden node in attenUid only), so
// shifting p1/p2 by the card offset keeps both the jack anchor and a knob-landing anchor on the gliding card.
void CardGlideAnimator::applyTo(std::vector<graph_editor_types::VisibleCable>& cables) {
    applied_.clear();
    if (items_.empty())
        return;
    for (auto& c : cables) {
        c.p1 += offsetFor(c.id.srcUid);
        c.p2 += offsetFor(c.id.dstUid);
    }
    applied_ = currentOffsets();
}

// The offset of every gliding module card that has one, by node id.
std::vector<std::pair<uint32_t, juce::Point<float>>> CardGlideAnimator::currentOffsets() const {
    std::vector<std::pair<uint32_t, juce::Point<float>>> out;
    for (const auto& item : items_)
        if (item.kind == Kind::Move && item.nodeUid != 0 && !isMacroKey(item.nodeUid))
            if (const auto offset = offsetFor(item.nodeUid); offset != juce::Point<float>())
                out.emplace_back(item.nodeUid, offset);
    return out;
}

// Moves the cables in the memo that touch a gliding card from the offsets it holds (applied_) to this frame's, and
// returns the area they were drawn in before and are drawn in now. A card that stopped gliding (landed, grabbed)
// goes back to its own place, so its cables move by minus what they held. Only the moved cables are touched: the
// memo is not rebuilt, which on a big patch costs more than the whole partial repaint.
juce::Rectangle<int> CardGlideAnimator::followCables(std::vector<graph_editor_types::VisibleCable>& cables) {
    auto now = currentOffsets();
    std::vector<std::pair<uint32_t, juce::Point<float>>> delta;
    for (const auto& [uid, offset] : applied_)
        delta.emplace_back(uid, -offset);
    for (const auto& [uid, offset] : now) {
        auto it = std::find_if(delta.begin(), delta.end(), [uid = uid](const auto& d) { return d.first == uid; });
        if (it == delta.end())
            delta.emplace_back(uid, offset);
        else
            it->second += offset;
    }
    applied_ = std::move(now);
    auto deltaFor = [&delta](uint32_t uid) {
        for (const auto& [id, d] : delta)
            if (id == uid)
                return d;
        return juce::Point<float>();
    };
    juce::Rectangle<float> area;
    for (auto& c : cables) {
        const auto d1 = deltaFor(c.id.srcUid), d2 = deltaFor(c.id.dstUid);
        if (d1.isOrigin() && d2.isOrigin())
            continue;
        area = area.getUnion(synth::ui::cablePaintBounds(c.p1, c.p2));
        c.p1 += d1;
        c.p2 += d2;
        area = area.getUnion(synth::ui::cablePaintBounds(c.p1, c.p2));
    }
    return area.getSmallestIntegerContainer();
}

juce::Rectangle<int> CardGlideAnimator::dirtyArea() const noexcept {
    juce::Rectangle<int> area;
    for (const auto& item : items_)
        area = area.getUnion(item.from.getUnion(item.to));
    return area.isEmpty() ? area : area.expanded(2);
}

juce::Rectangle<int> CardGlideAnimator::currentRectFor(const juce::Component* comp) const noexcept {
    for (const auto& item : items_)
        if (item.kind == Kind::Move && item.comp.getComponent() == comp)
            return currentRect(item).toNearestInt();
    return {};
}

// One frame as the VBlank runs it: `t` is the driver's eased value, the timeline's linear progress when phased.
void CardGlideAnimator::frameAt(float t) {
    const auto drawnBefore = dirtyArea();
    if (phased_)
        applyTimelineAtMs(static_cast<double>(t) * timeline_.totalMs());
    else
        applyTweenAt(t);
    ++repaintCount_;
    requestFrameRepaint(drawnBefore);
}

void CardGlideAnimator::stepFrameForTest(float t) { frameAt(t); }

void CardGlideAnimator::finishFrame() {
    const auto drawnBefore = dirtyArea();
    finish();
    ++repaintCount_;
    requestFrameRepaint(drawnBefore);
}

// A frame repaints only what it can change (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch):
// every ghost and snapshot's whole path before and after the step (a card grabbed mid-glide drops its item, so the
// place it was drawn must still be cleared) and the cables that follow a gliding card, moved in the memo rather than
// rebuilt. Repainting the whole canvas and rebuilding every cable each frame cost as much as the idle tick's full
// paint, at 60 to 120 frames a second, on a patch of hundreds of cables. When the memo is not valid, a full repaint
// is pending anyway, so this frame joins it.
void CardGlideAnimator::requestFrameRepaint(juce::Rectangle<int> drawnBefore) {
    auto* cables = hooks_.liveCables ? hooks_.liveCables() : nullptr;
    if (cables == nullptr || !hooks_.repaintArea) {
        lastFrameArea_ = {};
        if (hooks_.repaint)
            hooks_.repaint();
        return;
    }
    lastFrameArea_ = drawnBefore.getUnion(dirtyArea()).getUnion(followCables(*cables));
    if (!lastFrameArea_.isEmpty())
        hooks_.repaintArea(lastFrameArea_);
}

void CardGlideAnimator::startDriver() {
    if (hooks_.updater == nullptr)
        return; // no VBlank to drive it: the test seams advance and finish by hand
    if (phased_)
        driver_.start(
            *hooks_.updater, timeline_.totalMs(), [](float t) { return t; }, [this](float t) { frameAt(t); },
            [this]() { finishFrame(); });
    else
        driver_.start(
            *hooks_.updater, kGlideMs, synth::ui::easeOutCubic, [this](float t) { frameAt(t); },
            [this]() { finishFrame(); });
}

CardGlideAnimator::Scope::Scope(CardGlideAnimator& animator, bool restore)
    : animator_(animator) {
    if (animator_.depth_++ != 0 || !animator_.hooks_.cards)
        return;
    const auto entries = animator_.hooks_.cards();
    animator_.before_ = capture(entries);
    animator_.restoring_ = restore && animator_.canAnimate();
    animator_.candidates_.clear();
    animator_.enterRequests_.clear();
    animator_.borders_.clear();
    animator_.preexistingMacros_.clear();
    for (const auto& e : entries)
        if (isMacroKey(e.nodeUid))
            animator_.preexistingMacros_.insert(e.nodeUid);
    if (animator_.restoring_)
        animator_.noteMacroBorders();
}

CardGlideAnimator::Scope::~Scope() {
    if (--animator_.depth_ != 0 || !animator_.hooks_.cards)
        return;
    const float scale = animator_.hooks_.snapshotScale ? animator_.hooks_.snapshotScale() : 1.0f;
    const auto now = animator_.hooks_.cards();
    const bool moved = animator_.arm(animator_.before_, now, scale);
    // A Scope that changed nothing leaves a running delete or undo animation alone; any other change arms the new one
    // from what is drawn now (armGhosts).
    const bool somethingNew = moved || !animator_.candidates_.empty() || animator_.restoring_ ||
                              !animator_.borders_.empty() || !animator_.enterRequests_.empty();
    const bool ghosts = somethingNew && animator_.armGhosts(animator_.before_, now, scale);
    animator_.before_.clear();
    animator_.candidates_.clear();
    animator_.enterRequests_.clear();
    animator_.borders_.clear();
    animator_.preexistingMacros_.clear();
    animator_.restoring_ = false;
    if (!moved && !ghosts)
        return;
    if (animator_.hooks_.repaint)
        animator_.hooks_.repaint();
    animator_.startDriver();
}
