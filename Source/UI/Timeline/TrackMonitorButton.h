#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The audio track header's speaker button beside R (docs/timeline/tracks.md#monitoring): whether the track plays its
// own input live. Unlit while Auto has nothing to hear (not armed) and while Off; an accent outline while Auto and
// armed; solid accent while On. A click switches On and Auto (from Off it turns On); it only reports the click, the
// header writes the track. Like M/S/R it is not a Tab stop -- the row's bare I does the same -- so a click never
// pulls focus off the row.
class TrackMonitorButton : public juce::Button {
public:
    TrackMonitorButton();

    /** Shows `mode` for an `armed` track called `trackName` (the tooltip and screen-reader value follow). */
    void setState(synth::MonitorMode mode, bool armed, const juce::String& trackName);
    synth::MonitorMode getMode() const noexcept { return mode_; }
    /** What the button shows: 0 unlit, 1 outlined (Auto, armed), 2 solid (On). */
    int getLitLevel() const noexcept;
    /** The mode as the screen reader says it ("Auto, hearing the input", "On", "Off"). */
    juce::String getModeText() const;

    /** A right-click: the row opens its menu instead of the button toggling. */
    std::function<void()> onPopupMenuRequested;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    synth::MonitorMode mode_ = synth::MonitorMode::Auto;
    bool armed_ = false;
};

/** The mode a click (or the row's I key) moves `mode` to: On becomes Auto, anything else becomes On. */
inline synth::MonitorMode nextMonitorMode(synth::MonitorMode mode) noexcept {
    return mode == synth::MonitorMode::On ? synth::MonitorMode::Auto : synth::MonitorMode::On;
}

} // namespace synth::ui
