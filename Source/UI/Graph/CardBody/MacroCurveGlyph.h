#pragma once

// The tiny curve glyph a Macros knob shows beside its value: the knob's own response curve as a small polyline in the
// knob's bottom corner. Muted while the curve is straight, in the accent once it is bent. It is a child of the knob,
// takes no mouse and no focus and is not announced (the knob's own value is), and it holds a copy of the curve so it
// never reaches into a module that undo or a delete may free.

#include "Modules/ModCurve.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

class MacroCurveGlyph final
    : public juce::Component
    , private juce::ComponentListener {
public:
    static constexpr int kWidth = 18;
    static constexpr int kHeight = 11;

    /** Adds itself to `knob` and follows its size. */
    explicit MacroCurveGlyph(juce::Component& knob);
    ~MacroCurveGlyph() override;

    /** The glyph `knob` carries, or null. */
    static MacroCurveGlyph* of(juce::Component& knob);

    void setCurve(const synth::ModCurve& curve);
    const synth::ModCurve& getCurve() const noexcept { return curve_; }
    bool isBent() const noexcept { return !curve_.isLinear(); }

    void paint(juce::Graphics& g) override;

private:
    void componentMovedOrResized(juce::Component& component, bool wasMoved, bool wasResized) override;
    void place();

    juce::Component& knob_;
    synth::ModCurve curve_;
};

} // namespace synth
