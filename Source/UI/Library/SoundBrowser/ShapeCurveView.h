#pragma once

#include "Modules/Shapes/ShapeLibrary.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** The curve of a shape as a polyline across `box` (the caller insets it), `samples` points wide. */
juce::Path makeShapeCurvePath(const synth::LfoCustomWave& curve, juce::Rectangle<float> box, int samples);

/** The footer's picture of the selected shape: its curve in the accent colour in a framed box, drawn in from the left
 *  over 190 ms (a fade over 80 ms under Reduce Motion, at once with Animations off). Not a focus stop. */
class ShapeCurveView : public juce::Component {
public:
    ShapeCurveView();
    ~ShapeCurveView() override;

    /** Shows `shape`'s curve and draws it in; none clears the box. */
    void setShape(const std::optional<synth::shapes::LibraryShape>& shape);
    /** 0 while the curve is not drawn, 1 when fully drawn. */
    float getReveal() const { return reveal_; }
    bool isRevealRunning() const { return revealAnim_.isRunning(); }
    void finishRevealForTest();

    void paint(juce::Graphics& g) override;

private:
    std::optional<synth::shapes::LibraryShape> shape_;
    float reveal_ = 1.0f;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver revealAnim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShapeCurveView)
};

} // namespace synth::ui
