// StepGridMotion.cpp -- every animation of the step grid, each time-bounded and started by a change (never a
// free-running timer): lanes folding in and out (160 ms, the card's height following), the columns sliding to a
// page (160 ms), bars gliding to values changed from outside a drag (160 ms with a small per-step stagger) and steps
// dimming past Steps (120 ms). Reduce Motion shortens them to 80 ms without the stagger; Animations Off lands at once
// (ReducedMotion.h, docs/layout/animation.md).
#include "StepGridComponent.h"
#include "UI/Layout/FadeVisibility.h"

namespace synth::ui {

using namespace stepgrid;

namespace {

constexpr double kBodyMs = 160.0;
constexpr double kReducedMs = 80.0;
constexpr double kDimMs = 120.0;
constexpr double kStaggerMs = 4.0;

double glideTotalMs() {
    return motionMs(kBodyMs, kReducedMs) + (prefersReducedMotion() ? 0.0 : kStaggerMs * (kColumns - 1));
}

} // namespace

bool StepGridComponent::isMotionRunning() const {
    return revealAnim_.isRunning() || pageAnim_.isRunning() || glideAnim_.isRunning() || dimAnim_.isRunning();
}

void StepGridComponent::finishMotionForTest() {
    revealAnim_.completeForTest();
    pageAnim_.completeForTest();
    glideAnim_.completeForTest();
    dimAnim_.completeForTest();
}

// The card's height follows every frame; the neighbours make room ONCE per direction: a lane opening asks for its
// final footprint when it starts (the card is measured at that size for the call), a lane closing gives the room
// back when it has gone.
void StepGridComponent::startRevealMotion(std::uint16_t from, std::uint16_t to) {
    revealTarget_ = to;
    const auto target = revealsOf(to);
    const bool grows = computeLayout(target, getWidth()).total > computeLayout(reveal_, getWidth()).total;
    revealAnim_.stop(vblank_);
    auto notify = [this] {
        if (hooks_.layoutChanged)
            hooks_.layoutChanged();
    };
    if (!FadeVisibility::canAnimateIn(this)) {
        reveal_ = target;
        layoutParts();
        notify();
        if (hooks_.makeRoom)
            hooks_.makeRoom();
        return;
    }
    juce::ignoreUnused(from);
    revealFrom_ = reveal_;
    revealTo_ = target;
    if (grows) {
        settling_ = true;
        notify();
        if (hooks_.makeRoom)
            hooks_.makeRoom();
        settling_ = false;
    }
    revealAnim_.start(
        vblank_, motionMs(kBodyMs, kReducedMs), easeOutCubic,
        [this, notify](float t) {
            for (size_t i = 0; i < reveal_.size(); ++i)
                reveal_[i] = revealFrom_[i] + (revealTo_[i] - revealFrom_[i]) * t;
            layoutParts();
            notify();
        },
        [this, notify, grows] {
            reveal_ = revealTo_;
            layoutParts();
            notify();
            if (!grows && hooks_.makeRoom)
                hooks_.makeRoom();
        });
}

void StepGridComponent::startPageSlide(float toPage) {
    pageAnim_.stop(vblank_);
    if (!FadeVisibility::canAnimateIn(this)) {
        slidePos_ = toPage;
        repaint();
        return;
    }
    pageFrom_ = slidePos_;
    pageAnim_.start(
        vblank_, motionMs(kBodyMs, kReducedMs), easeOutCubic,
        [this, toPage](float t) {
            slidePos_ = pageFrom_ + (toPage - pageFrom_) * t;
            repaint();
        },
        [this, toPage] {
            slidePos_ = toPage;
            repaint();
        });
}

void StepGridComponent::startGlide(const SequencerPattern& from) {
    glideAnim_.stop(vblank_);
    gliding_ = false;
    if (!FadeVisibility::canAnimateIn(this))
        return;
    glideFrom_ = from;
    startLabelFade();
    gliding_ = true;
    glideElapsed_ = 0.0f;
    glideAnim_.start(
        vblank_, glideTotalMs(), [](float t) { return t; },
        [this](float t) {
            glideElapsed_ = t;
            repaint();
        },
        [this] {
            gliding_ = false;
            repaint();
        });
}

// How far step `step`'s bar has travelled from the old value to the new one (0..1, eased).
float StepGridComponent::glideMix(int step) const {
    if (!gliding_)
        return 1.0f;
    const double total = glideTotalMs();
    const double elapsed = (double)glideElapsed_ * total;
    const double stagger = prefersReducedMotion() ? 0.0 : kStaggerMs * (double)(step % kColumns);
    const double body = motionMs(kBodyMs, kReducedMs);
    const float local = (float)juce::jlimit(0.0, 1.0, (elapsed - stagger) / std::max(1.0, body));
    return prefersReducedMotion() ? local : easeOutCubic(local);
}

void StepGridComponent::startDim(int fromSteps) {
    dimAnim_.stop(vblank_);
    dimFromSteps_ = fromSteps;
    if (!FadeVisibility::canAnimateIn(this)) {
        dimT_ = 1.0f;
        repaint();
        return;
    }
    dimT_ = 0.0f;
    dimAnim_.start(
        vblank_, motionMs(kDimMs, kReducedMs), [](float t) { return t; },
        [this](float t) {
            dimT_ = t;
            repaint();
        },
        [this] {
            dimT_ = 1.0f;
            repaint();
        });
}

// 0 for a step that plays, 1 for one past Steps, in between while Steps has just changed.
float StepGridComponent::dimAmount(int step) const {
    const float to = step >= stepCount() ? 1.0f : 0.0f;
    if (dimT_ >= 1.0f)
        return to;
    const float from = step >= dimFromSteps_ ? 1.0f : 0.0f;
    return from + (to - from) * dimT_;
}

} // namespace synth::ui
