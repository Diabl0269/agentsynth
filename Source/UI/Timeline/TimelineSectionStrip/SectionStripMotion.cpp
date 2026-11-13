// SectionStripMotion.cpp
//
// Every effect follows docs/layout/animation.md: it asks canAnimate() once when it starts (an off-screen strip gets no
// VBlank frames, and Animations: Off lands at once), a retargeted lift starts from where it is, and each driver stops
// by itself so nothing repaints once the strip is at rest.

#include "UI/Timeline/TimelineSectionStrip/SectionStripMotion.h"

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>

namespace synth::ui {

SectionStripMotion::SectionStripMotion(juce::Component& host, std::function<void()> onFrame)
    : host_(host)
    , onFrame_(std::move(onFrame))
    , updater_(&host) {}

SectionStripMotion::~SectionStripMotion() { stopAll(); }

bool SectionStripMotion::canAnimate() const { return FadeVisibility::canAnimateIn(&host_); }

void SectionStripMotion::stopAll() {
    for (auto& [id, grow] : grows_)
        grow.driver.stop();
    grows_.clear();
    for (auto& [id, exit] : exits_)
        exit.driver.stop();
    exits_.clear();
    liftDriver_.stop();
    outline_.stop();
    hintSwapDriver_.stop();
    hintSwap_ = 1.0f;
    lift_ = 0.0f;
    outlined_.clear();
    outlineAlpha_ = 0.0f;
}

bool SectionStripMotion::isGrowingForTest() const noexcept {
    return std::any_of(grows_.begin(), grows_.end(), [](const auto& kv) { return kv.second.driver.isRunning(); });
}

bool SectionStripMotion::isExitingForTest() const noexcept {
    return std::any_of(exits_.begin(), exits_.end(), [](const auto& kv) { return kv.second.driver.isRunning(); });
}

bool SectionStripMotion::isAnimating() const noexcept {
    return isGrowingForTest() || isExitingForTest() || liftDriver_.isRunning() || outline_.isRunning() ||
           hintSwapDriver_.isRunning();
}

void SectionStripMotion::stepForTest(double ms) {
    for (auto& [id, grow] : grows_)
        grow.driver.stepForTest(ms);
    for (auto& [id, exit] : exits_)
        exit.driver.stepForTest(ms);
    liftDriver_.stepForTest(ms);
    outline_.stepForTest(ms);
    hintSwapDriver_.stepForTest(ms);
}

// Drops the grows and exits that have landed. Only called when a new one starts (a doc change), never from a frame or
// completion callback: a driver is never destroyed while its updater may be running it.
void SectionStripMotion::pruneLanded() {
    std::erase_if(grows_, [](const auto& kv) { return !kv.second.driver.isRunning() && kv.second.t >= 1.0f; });
    std::erase_if(exits_, [](const auto& kv) { return !kv.second.active; });
}

//==============================================================================
void SectionStripMotion::startGrow(synth::PlacementId id, bool fromRight) {
    pruneLanded();
    growMs_ = 0.0;
    if (!canAnimate()) {
        grows_.erase(id.value);
        return;
    }
    auto& grow = grows_[id.value];
    grow.fromRight = fromRight;
    grow.fade = prefersReducedMotion();
    growMs_ = motionMs(kGrowMs, kReducedMs);
    grow.t = 0.0f;
    auto* item = &grow; // std::map nodes never move
    grow.driver.start(
        updater_, growMs_, easeOutCubic,
        [this, item](float t) {
            item->t = t;
            frame();
        },
        [this, item] {
            item->t = 1.0f;
            frame();
        });
}

float SectionStripMotion::growScale(synth::PlacementId id) const {
    const auto it = grows_.find(id.value);
    return it != grows_.end() && !it->second.fade ? std::max(0.02f, it->second.t) : 1.0f;
}

float SectionStripMotion::growAlpha(synth::PlacementId id) const {
    const auto it = grows_.find(id.value);
    return it != grows_.end() && it->second.fade ? it->second.t : 1.0f;
}

bool SectionStripMotion::growsFromRight(synth::PlacementId id) const {
    const auto it = grows_.find(id.value);
    return it != grows_.end() && it->second.fromRight;
}

//==============================================================================
void SectionStripMotion::startExit(synth::PlacementId id, const Ghost& ghost) {
    pruneLanded();
    if (const auto it = exits_.find(id.value); it != exits_.end()) {
        it->second.driver.stop();
        it->second.active = false;
    }
    if (!canAnimate())
        return; // nothing to show: the block is simply gone
    auto& exit = exits_[id.value];
    exit.ghost = ghost;
    exit.fade = prefersReducedMotion();
    exit.t = 0.0f;
    exit.active = true;
    ++exitsStarted_;
    auto* item = &exit;
    exit.driver.start(
        updater_, motionMs(kExitMs, kReducedMs), easeInCubic,
        [this, item](float t) {
            item->t = t;
            frame();
        },
        [this, item] {
            item->active = false;
            frame();
        });
}

std::vector<SectionStripMotion::ExitFrame> SectionStripMotion::exitFrames() const {
    std::vector<ExitFrame> frames;
    for (const auto& [id, exit] : exits_)
        if (exit.active)
            frames.push_back(
                {exit.ghost, exit.fade ? 1.0f : std::max(0.0f, 1.0f - exit.t), exit.fade ? 1.0f - exit.t : 1.0f});
    return frames;
}

bool SectionStripMotion::hasExitGhost() const noexcept {
    return std::any_of(exits_.begin(), exits_.end(), [](const auto& kv) { return kv.second.active; });
}

//==============================================================================
void SectionStripMotion::liftTo(bool lifted) {
    const float to = lifted ? 1.0f : 0.0f;
    liftDriver_.stop();
    if (!canAnimate() || prefersReducedMotion()) {
        lift_ = to;
        frame();
        return;
    }
    const float from = lift_;
    liftDriver_.start(
        updater_, lifted ? kLiftMs : kSettleMs, easeOutCubic,
        [this, from, to](float t) {
            lift_ = from + (to - from) * t;
            frame();
        },
        [this, to] {
            lift_ = to;
            frame();
        });
}

//==============================================================================
void SectionStripMotion::startHintSwap() {
    hintSwapDriver_.stop();
    if (!canAnimate()) {
        hintSwap_ = 1.0f;
        frame();
        return;
    }
    hintSwap_ = 0.0f;
    hintSwapDriver_.start(
        updater_, motionMs(kHintSwapMs, kReducedMs), [](float t) { return t; },
        [this](float t) {
            hintSwap_ = t;
            frame();
        },
        [this] {
            hintSwap_ = 1.0f;
            frame();
        });
}

//==============================================================================
void SectionStripMotion::startOutline(const std::vector<synth::PlacementId>& placements) {
    outline_.stop();
    outlined_.clear();
    outlineAlpha_ = 0.0f;
    if (!canAnimate() || placements.empty())
        return;
    outlined_ = placements;
    outlineAlpha_ = 1.0f;
    outlineMs_ = motionMs(kOutlineMs, kReducedMs);
    outline_.start(
        updater_, outlineMs_, [](float t) { return t; },
        [this](float t) {
            outlineAlpha_ = 1.0f - t;
            frame();
        },
        [this] {
            outlined_.clear();
            outlineAlpha_ = 0.0f;
            frame();
        });
}

bool SectionStripMotion::isOutlined(synth::PlacementId id) const {
    return outlineAlpha_ > 0.0f && std::find(outlined_.begin(), outlined_.end(), id) != outlined_.end();
}

} // namespace synth::ui
