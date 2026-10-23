// ShapeCurveView.cpp -- the footer's larger picture of the selected shape and its draw-in.
#include "UI/Library/SoundBrowser/ShapeCurveView.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kLineWidth = 1.8f;
constexpr float kInset = 6.0f;
constexpr float kCornerRadius = 4.0f;
} // namespace

juce::Path makeShapeCurvePath(const synth::LfoCustomWave& curve, juce::Rectangle<float> box, int samples) {
    juce::Path line;
    const int n = juce::jmax(2, samples);
    for (int i = 0; i < n; ++i) {
        const float x = (float)i / (float)(n - 1);
        const float y = box.getBottom() - curve.evaluate(x) * box.getHeight();
        if (i == 0)
            line.startNewSubPath(box.getX(), y);
        else
            line.lineTo(box.getX() + x * box.getWidth(), y);
    }
    return line;
}

ShapeCurveView::ShapeCurveView() {
    setTitle("Shape curve");
    setInterceptsMouseClicks(false, false);
}

ShapeCurveView::~ShapeCurveView() { revealAnim_.stop(vblank_); }

void ShapeCurveView::setShape(const std::optional<synth::shapes::LibraryShape>& shape) {
    const bool same = shape.has_value() && shape_.has_value() && shape->kind == shape_->kind &&
                      shape->name == shape_->name && shape->factory == shape_->factory && shape->curve == shape_->curve;
    shape_ = shape;
    if (same || !shape_.has_value()) {
        repaint();
        return;
    }
    const double ms = motionMs(190.0, 80.0);
    if (ms <= 0.0) {
        reveal_ = 1.0f;
        repaint();
        return;
    }
    reveal_ = 0.0f;
    revealAnim_.start(
        vblank_, ms, easeOutCubic,
        [this](float t) {
            reveal_ = t;
            repaint();
        },
        [this] {
            reveal_ = 1.0f;
            repaint();
        });
}

void ShapeCurveView::finishRevealForTest() {
    revealAnim_.completeForTest();
    reveal_ = 1.0f;
}

void ShapeCurveView::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(c.bg0);
    g.fillRoundedRectangle(bounds, kCornerRadius);
    g.setColour(c.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kCornerRadius, 1.0f);
    if (!shape_.has_value())
        return;
    const auto inner = bounds.reduced(kInset);
    juce::Graphics::ScopedSaveState state(g);
    const bool slide = !prefersReducedMotion();
    if (slide)
        g.reduceClipRegion(inner.withWidth(reveal_ * inner.getWidth()).getSmallestIntegerContainer());
    g.setColour(c.accent.withMultipliedAlpha(slide ? 1.0f : reveal_));
    g.strokePath(makeShapeCurvePath(shape_->curve, inner, juce::jmax(2, (int)inner.getWidth())),
                 juce::PathStrokeType(kLineWidth));
}

} // namespace synth::ui
