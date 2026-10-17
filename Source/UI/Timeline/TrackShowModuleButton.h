#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The small target-glyph button on a track header that does what Ctrl+E does: shows the track's module on the
// canvas and opens or closes an instrument's window. A real Tab stop (Return/Space press it), so a click never
// pulls focus off the row. A mixer column header uses the same button for "Edit on canvas" with its own words.
class TrackShowModuleButton : public juce::Button {
public:
    static constexpr int kSize = 20; // square hit area, px

    TrackShowModuleButton();

    /** Rebuilds the screen-reader name and the tooltip; `shortcutText` is the action's current binding. */
    void setTrack(const juce::String& trackName, const juce::String& shortcutText);
    /** Another surface's words for the same button: the screen-reader name and the tooltip. */
    void setTexts(const juce::String& title, const juce::String& tooltip);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    bool keyPressed(const juce::KeyPress& key) override;
};

} // namespace synth::ui
