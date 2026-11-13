#pragma once

#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/IconGlyphs.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// A modulator row's small picture of its LFO's waveform (sine, triangle, sawtooth, square, sample and hold, custom).
// Display only: the shape combo beside it edits. Named for a screen reader ("Sine shape") and tooltipped the same.
// Changing the shape cross-fades the picture. Message thread only.
class ModulatorShapeIcon
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    ModulatorShapeIcon();

    /** The LFO's shape index (the order of its Shape parameter); an unknown index shows the custom picture. */
    void setShape(int shapeIndex);
    int getShape() const noexcept { return shape_; }
    /** The glyph for a shape index, and its plain name ("Sine", "Sample and hold"). */
    static synth::theme::Glyph glyphForShape(int shapeIndex);
    static juce::String nameForShape(int shapeIndex);

    /** The picture showing once any cross-fade has finished. */
    synth::theme::Glyph getGlyph() const noexcept { return glyphForShape(shape_); }

    void paint(juce::Graphics& g) override;

private:
    int shape_ = 0;
    int previousShape_ = 0;
    float fade_ = 1.0f; // 0 = all previous shape, 1 = all current
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver driver_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorShapeIcon)
};

} // namespace synth::ui
