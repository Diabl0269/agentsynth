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
    grow_.stop();
    exitDriver_.stop();
    liftDriver_.stop();
    outline_.stop();
    growT_ = 1.0f;
    exitActive_ = false;
    lift_ = 0.0f;
    outlined_.clear();
    outlineAlpha_ = 0.0f;
}

bool SectionStripMotion::isAnimating() const noexcept {
    return grow_.isRunning() || exitDriver_.isRunning() || liftDriver_.isRunning() || outline_.isRunning();
}

void SectionStripMotion::stepForTest(double ms) {
    grow_.stepForTest(ms);
    exitDriver_.stepForTest(ms);
    liftDriver_.stepForTest(ms);
    outline_.stepForTest(ms);
}

//==============================================================================
void SectionStripMotion::startGrow(synth::PlacementId id, bool fromRight) {
    growId_ = id;
    growFromRight_ = fromRight;
    growFade_ = prefersReducedMotion();
    growMs_ = 0.0;
    if (!canAnimate()) {
        growT_ = 1.0f;
        return;
    }
    growMs_ = motionMs(kGrowMs, kReducedMs);
    growT_ = 0.0f;
    grow_.start(
        updater_, growMs_, easeOutCubic,
        [this](float t) {
            growT_ = t;
            frame();
        },
        [this] {
            growT_ = 1.0f;
            frame();
        });
}

float SectionStripMotion::growScale(synth::PlacementId id) const {
    return id == growId_ && !growFade_ ? std::max(0.02f, growT_) : 1.0f;
}

float SectionStripMotion::growAlpha(synth::PlacementId id) const { return id == growId_ && growFade_ ? growT_ : 1.0f; }

//==============================================================================
void SectionStripMotion::startExit(const Ghost& ghost) {
    exitDriver_.stop();
    exitActive_ = false;
    if (!canAnimate())
        return; // nothing to show: the block is simply gone
    exit_ = ghost;
    exitFade_ = prefersReducedMotion();
    exitT_ = 0.0f;
    exitActive_ = true;
    exitDriver_.start(
        updater_, motionMs(kExitMs, kReducedMs), easeInCubic,
        [this](float t) {
            exitT_ = t;
            frame();
        },
        [this] {
            exitActive_ = false;
            frame();
        });
}

float SectionStripMotion::exitScale() const noexcept { return exitFade_ ? 1.0f : std::max(0.0f, 1.0f - exitT_); }

float SectionStripMotion::exitAlpha() const noexcept { return exitFade_ ? 1.0f - exitT_ : 1.0f; }

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
