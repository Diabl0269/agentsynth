// LoadRevealAnimator.cpp -- starting, stepping and landing the reveal, and what the canvas paint asks of it. The
// groups and their wave levels are built in LoadRevealAnimatorGroups.cpp.

#include "LoadRevealAnimator.h"

#include "UI/Layout/CableCurve.h"
#include "UI/Layout/ControlMotion.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include <algorithm>

namespace lr = synth::ui::load_reveal;
namespace motion = synth::ui::control_motion;

namespace {
// A pop starts from nothing, but a component transform must stay invertible.
constexpr float kMinPopScale = 0.02f;

synth::ui::ZoomFrozenCachedImage* rasterOf(juce::Component* card) {
    return card != nullptr ? dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(card->getCachedComponentImage()) : nullptr;
}

void unpinRaster(juce::Component& card) {
    if (auto* cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(card.getCachedComponentImage()))
        cache->unpin();
}
} // namespace

LoadRevealAnimator::~LoadRevealAnimator() {
    if (hooks_.updater != nullptr)
        driver_.stop(*hooks_.updater);
}

void LoadRevealAnimator::setHooks(Hooks hooks) { hooks_ = std::move(hooks); }

double LoadRevealAnimator::clockMs() const { return juce::Time::getMillisecondCounterHiRes(); }

// Every card is hidden by the first frame, before the canvas paints again, so nothing shows at full size first. A
// card's raster is pinned at its real scale for the pop: it is painted for the first time while scaled down, and a
// raster taken then would stay soft (ZoomFrozenCachedImage::pinScale).
void LoadRevealAnimator::start(lr::Motion motion, const std::set<uint32_t>& pendingNodes, bool drive) {
    finish();
    motion_ = motion;
    drive_ = drive;
    buildGroups(pendingNodes);
    outlines_ = !pendingNodes_.empty();
    live_ = true;
    nowMs_ = 0.0;
    startClockMs_ = clockMs();
    const float scale = motion == lr::Motion::full && hooks_.rasterScale ? hooks_.rasterScale() : 0.0f;
    for (auto& group : groups_)
        for (auto& card : group.cards)
            if (auto* cache = rasterOf(card.getComponent()); cache != nullptr && scale > 0.0f)
                cache->pinScale(scale);
    if (motion == lr::Motion::reduced && hooks_.setCanvasAlpha)
        hooks_.setCanvasAlpha(0.0f);
    frameAt(0.0);
    if (live_ && drive_)
        startDriver();
}

void LoadRevealAnimator::setNodeReady(uint32_t nodeUid) {
    if (!live_ || pendingNodes_.erase(nodeUid) == 0)
        return;
    const int g = groupOfNode(nodeUid);
    if (g < 0 || --groups_[(size_t)g].pendingNodes > 0)
        return;
    const double now = drive_ ? clockMs() - startClockMs_ : nowMs_;
    timeline_.markReady(g, now);
    frameAt(now);
    if (live_ && drive_)
        startDriver();
}

// Restores every card to identity, full opacity and its own raster scale. Inside a frame the driver is left to run
// out on its own (stopping it would free the animator calling us); its later frames see live_ == false and return.
void LoadRevealAnimator::finish() {
    if (!live_)
        return;
    live_ = false;
    outlines_ = false;
    if (!inFrame_ && hooks_.updater != nullptr)
        driver_.stop(*hooks_.updater);
    {
        const juce::ScopedValueSetter<bool> applying(applying_, true);
        for (auto& group : groups_)
            for (auto& card : group.cards)
                if (auto* c = card.getComponent()) {
                    c->setTransform({});
                    c->setAlpha(1.0f);
                    unpinRaster(*c);
                }
    }
    if (motion_ == lr::Motion::reduced && hooks_.setCanvasAlpha)
        hooks_.setCanvasAlpha(1.0f);
    groups_.clear();
    groupOfNode_.clear();
    groupOfHull_.clear();
    cableGroups_.clear();
    pendingNodes_.clear();
    if (hooks_.repaintAll)
        hooks_.repaintAll();
}

bool LoadRevealAnimator::refuseEdit() {
    if (!blockingEdits_)
        return false;
    if (hooks_.reportStatus)
        hooks_.reportStatus("Still loading");
    return true;
}

// The driver is a clock that runs only while something scheduled is still moving: to the last scheduled pop or cable.
// A group waiting for its assets schedules nothing, so a slow load costs no frames until setNodeReady.
void LoadRevealAnimator::startDriver() {
    if (hooks_.updater == nullptr)
        return;
    double until = motion_ == lr::Motion::reduced ? lr::kReducedFadeMs : 0.0;
    for (int g = 0; g < (int)groups_.size(); ++g)
        if (timeline_.isScheduled(g))
            until = std::max(until, timeline_.appearedMs(g));
    for (const auto& [a, b] : cableGroups_)
        if (timeline_.isScheduled(a) && timeline_.isScheduled(b))
            until = std::max(until, std::max(timeline_.appearedMs(a), timeline_.appearedMs(b)) + timeline_.cableMs());
    const double remaining = until - nowMs_;
    if (remaining <= 0.0)
        return;
    const auto frame = [this](float) {
        if (!live_)
            return;
        const juce::ScopedValueSetter<bool> in(inFrame_, true);
        frameAt(clockMs() - startClockMs_);
    };
    driver_.start(*hooks_.updater, remaining + 16.0, [](float t) { return t; }, frame, [frame] { frame(1.0f); });
}

void LoadRevealAnimator::applyAtMs(double elapsedMs) {
    if (!live_)
        return;
    const juce::ScopedValueSetter<bool> in(inFrame_, true);
    frameAt(elapsedMs);
}

// One frame: every group whose pop moved gets its cards' alpha and scale, and only what changed is repainted (the
// cards repaint themselves; a fading border and a drawing cable are asked for here). Lands once everything has.
void LoadRevealAnimator::frameAt(double elapsedMs) {
    const double previous = nowMs_;
    nowMs_ = std::max(previous, elapsedMs);
    juce::Rectangle<int> dirty;
    for (int g = 0; g < (int)groups_.size(); ++g) {
        auto& group = groups_[(size_t)g];
        const float progress = timeline_.popProgress(g, nowMs_);
        if (progress == group.applied)
            continue;
        applyGroup(group, progress);
        for (const auto& hull : group.hulls)
            if (hooks_.hullBounds)
                dirty = dirty.getUnion(hooks_.hullBounds(hull).expanded(24));
    }
    dirty = dirty.getUnion(cableArea(previous, nowMs_));
    if (motion_ == lr::Motion::reduced && hooks_.setCanvasAlpha)
        hooks_.setCanvasAlpha((float)std::min(1.0, nowMs_ / lr::kReducedFadeMs));
    if (!dirty.isEmpty() && hooks_.repaintArea)
        hooks_.repaintArea(dirty);
    const double fadeEnd = motion_ == lr::Motion::reduced ? lr::kReducedFadeMs : 0.0;
    if (nowMs_ >= std::max(endMs(), fadeEnd)) {
        auto landed = onLanded;
        finish();
        if (landed)
            landed();
    }
}

// The real card, scaled about its centre by a component transform (control_motion's grow, 0 -> 1.08 -> 1) with its
// alpha rising ahead of it. Its bounds never change, so cables, hit-tests and the canvas frame see it in place.
void LoadRevealAnimator::applyGroup(Group& group, float progress) {
    const juce::ScopedValueSetter<bool> applying(applying_, true);
    group.applied = progress;
    for (auto& card : group.cards) {
        auto* c = card.getComponent();
        if (c == nullptr)
            continue;
        if (progress >= 1.0f) {
            c->setTransform({});
            c->setAlpha(1.0f);
            unpinRaster(*c);
        } else if (progress <= 0.0f) {
            c->setTransform({});
            c->setAlpha(0.0f);
        } else {
            const float scale = std::max(kMinPopScale, motion::growScale(progress));
            c->setAlpha(synth::ui::easeOutCubic(std::min(1.0f, progress * 1.5f)));
            c->setTransform(motion::scaleAbout(c->getBounds(), scale, motion::Axis::both));
        }
    }
}

// The cables whose drawn length changed between two frames, as one area.
juce::Rectangle<int> LoadRevealAnimator::cableArea(double fromMs, double toMs) const {
    if (!hooks_.cables || fromMs == toMs)
        return {};
    juce::Rectangle<float> area;
    for (const auto& cable : hooks_.cables()) {
        const int a = groupOfNode(cable.id.srcUid);
        const int b = groupOfNode(cable.id.dstUid);
        if (timeline_.cableProgress(a, b, fromMs) != timeline_.cableProgress(a, b, toMs))
            area = area.getUnion(synth::ui::cablePaintBounds(cable.p1, cable.p2));
    }
    return area.getSmallestIntegerContainer();
}

double LoadRevealAnimator::endMs() const noexcept { return timeline_.endMs(cableGroups_); }

int LoadRevealAnimator::groupOfNode(uint32_t nodeUid) const noexcept {
    const auto it = groupOfNode_.find(nodeUid);
    return it != groupOfNode_.end() ? it->second : -1;
}

int LoadRevealAnimator::groupOfCard(const juce::Component* card) const noexcept {
    for (size_t g = 0; g < groups_.size(); ++g)
        for (const auto& c : groups_[g].cards)
            if (c.getComponent() == card)
                return (int)g;
    return -1;
}

float LoadRevealAnimator::cableProgress(uint32_t srcUid, uint32_t dstUid) const noexcept {
    if (!live_)
        return 1.0f;
    return timeline_.cableProgress(groupOfNode(srcUid), groupOfNode(dstUid), nowMs_);
}

float LoadRevealAnimator::hullAlpha(const juce::String& macroId) const noexcept {
    if (!live_)
        return 1.0f;
    const auto it = groupOfHull_.find(macroId);
    if (it == groupOfHull_.end())
        return 1.0f;
    return synth::ui::easeOutCubic(timeline_.popProgress(it->second, nowMs_));
}

int LoadRevealAnimator::outlineCount() const noexcept {
    if (!live_ || !outlines_)
        return 0;
    int count = 0;
    for (int g = 0; g < (int)groups_.size(); ++g)
        if (timeline_.popProgress(g, nowMs_) <= 0.0f)
            for (const auto& card : groups_[(size_t)g].cards)
                count += card != nullptr ? 1 : 0;
    return count;
}

// Inside each waiting card's own bounds, so the card's first frame repaints over its outline.
void LoadRevealAnimator::paintOutlines(juce::Graphics& g) const {
    if (!live_ || !outlines_)
        return;
    const auto colour = hooks_.outlineColour ? hooks_.outlineColour() : juce::Colours::grey;
    const auto clip = g.getClipBounds();
    for (int index = 0; index < (int)groups_.size(); ++index) {
        if (timeline_.popProgress(index, nowMs_) > 0.0f)
            continue;
        for (const auto& card : groups_[(size_t)index].cards) {
            const auto* c = card.getComponent();
            if (c == nullptr || !clip.intersects(c->getBounds()))
                continue;
            const auto box = c->getBounds().toFloat().reduced(1.0f);
            g.setColour(colour.withAlpha(0.05f));
            g.fillRoundedRectangle(box, 8.0f);
            g.setColour(colour.withAlpha(0.35f));
            g.drawRoundedRectangle(box, 8.0f, 1.0f);
        }
    }
}
