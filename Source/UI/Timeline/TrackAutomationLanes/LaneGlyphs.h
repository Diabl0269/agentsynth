#pragma once

#include "UI/Timeline/TrackAutomationLanes/LaneToolMapping.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// Drawn vector glyphs for the timeline toolbar's automation controls (the Draw tool's curve
// selector, the global-automation toggle, the automation-follows-clips toggle), plus the small
// button that paints one. Theme tokens only; headless-safe fallbacks when no AppLookAndFeel exists.
namespace synth::ui {

/** The curve's glyph, as a stroke path fitted into `area`. */
juce::Path laneCurveGlyph(LaneCurve curve, juce::Rectangle<float> area);
/** A lane curve under a bar: the global automation strip. */
juce::Path globalAutomationGlyph(juce::Rectangle<float> area);
/** A clip block with a curve riding along under it: automation follows clips. */
juce::Path followsClipsGlyph(juce::Rectangle<float> area);

/** A toolbar button painting a stroked glyph; lit (toolActive background) while its toggle state is on. */
class LaneGlyphButton : public juce::Button {
public:
    using GlyphFn = std::function<juce::Path(juce::Rectangle<float>)>;
    LaneGlyphButton(const juce::String& name, GlyphFn glyph);

    void setGlyph(GlyphFn glyph);
    /** Short text drawn at the right of the glyph (e.g. a lane count); empty draws none. */
    void setBadgeText(const juce::String& text);
    const juce::String& getBadgeText() const noexcept { return badge_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    GlyphFn glyph_;
    juce::String badge_;
};

} // namespace synth::ui
