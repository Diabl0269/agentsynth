#include "CanvasFrame.h"

namespace {

int roundUpToStep(int v, int step) { return ((v + step - 1) / step) * step; }

juce::Rectangle<float> lerpRect(juce::Rectangle<float> a, juce::Rectangle<float> b, float t) {
    auto l = [t](float x, float y) { return x + (y - x) * t; };
    return {l(a.getX(), b.getX()), l(a.getY(), b.getY()), l(a.getWidth(), b.getWidth()),
            l(a.getHeight(), b.getHeight())};
}

} // namespace

CanvasFrame::CanvasFrame(juce::VBlankAnimatorUpdater& updater)
    : updater_(updater) {}

juce::Rectangle<int> CanvasFrame::targetFor(juce::Rectangle<int> contentUnion) {
    if (contentUnion.isEmpty())
        return {0, 0, kStartW, kStartH};
    const int w = juce::jmax(kStartW, roundUpToStep(contentUnion.getRight() + kPad, kStep));
    const int h = juce::jmax(kStartH, roundUpToStep(contentUnion.getBottom() + kPad, kStep));
    return {0, 0, w, h};
}

void CanvasFrame::update(juce::Rectangle<int> contentUnion, Mode mode) {
    auto next = targetFor(contentUnion);
    if (mode == Mode::GrowOnly)
        next = {0, 0, juce::jmax(next.getWidth(), target_.getWidth()),
                juce::jmax(next.getHeight(), target_.getHeight())};

    // A GrowOnly tick (30 Hz timer) must not eat a pending snap: it runs between a project's detach and its rebuild.
    const bool snap = mode == Mode::Snap || (mode != Mode::GrowOnly && snapPending_);
    if (mode != Mode::GrowOnly)
        snapPending_ = false;

    if (next == target_ && !(snap && driver_.isRunning()))
        return;

    if (snap)
        snapTo(next);
    else
        glideTo(next);
}

void CanvasFrame::snapTo(juce::Rectangle<int> t) {
    driver_.stop(updater_);
    target_ = t;
    current_ = from_ = t.toFloat();
    notify();
}

void CanvasFrame::glideTo(juce::Rectangle<int> t) {
    target_ = t;
    from_ = current_;
    driver_.start(
        updater_, kGlideMs, synth::ui::easeOutCubic,
        [this](float p) {
            current_ = lerpRect(from_, target_.toFloat(), p);
            notify();
        },
        [this]() {
            current_ = target_.toFloat();
            notify();
        });
}

void CanvasFrame::shiftCurrentBy(juce::Point<float> d) {
    current_ = current_.translated(d.x, d.y);
    from_ = from_.translated(d.x, d.y);
    notify();
}

void CanvasFrame::finishAnimationForTest() {
    driver_.stop(updater_);
    current_ = from_ = target_.toFloat();
    notify();
}
