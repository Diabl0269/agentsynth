#pragma once

// The 26x18 button that shows a modulation curve and opens its editor: in the mod dot's rows, in the Mod Matrix and
// beside a cable's knob. A real Tab stop with the accent focus ring; Return and Space open it.

#include "ModCurveEditTarget.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** Strokes `curve` as a small polyline inside `area` (input left to right, output bottom to top). */
void paintModCurveGlyph(juce::Graphics& g, juce::Rectangle<float> area, const synth::ModCurve& curve,
                        juce::Colour colour, float thickness = 1.5f);

/** A named preset (Linear, Exp...) in the box and the window: its lit text stays readable on the accent in every theme.
 */
class ModCurvePresetButton final : public juce::TextButton {
public:
    using juce::TextButton::TextButton;
    void lookAndFeelChanged() override;
    void parentHierarchyChanged() override;
};

class ModCurveChip final : public juce::Button {
public:
    static constexpr int kWidth = 26;
    static constexpr int kHeight = 18;

    ModCurveChip();

    /** Which attenuverter this chip edits; null clears it. `sourceName` words the screen-reader name and tooltip. */
    void setTarget(std::shared_ptr<ModCurveEditTarget> target, const juce::String& sourceName);
    /** Words the screen-reader name and tooltip: "<source> curve". */
    void setSourceName(const juce::String& sourceName);
    std::shared_ptr<ModCurveEditTarget> target() const { return target_; }
    /** Re-reads the curve (cheap; safe to call every tick). */
    void refresh();
    bool showsBentCurve() const noexcept { return bent_; }

    bool keyPressed(const juce::KeyPress& key) override;
    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    std::shared_ptr<ModCurveEditTarget> target_;
    synth::ModCurve shown_;
    bool bent_ = false;
};

} // namespace synth::ui
