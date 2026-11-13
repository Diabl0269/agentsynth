#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The "+ Add automation..." row that closes a track's lane rows in the header column (the Unassigned
// section's too); once the track has a lane it shrinks to a "+" in the gutter beside the last lane's header. It is how
// a person puts a lane on a track from the timeline itself, without going to the graph to right-click a knob: pressing
// it asks the panel for the track's parameter picker.
//
// A real Tab stop so the keyboard path exists: Return and Space press it in the same event (juce::Button
// posts its own Return click to the message queue and has no Space handling), the accent focus ring shows
// on it, and its screen-reader name and tooltip say whose lane it adds. A click does not take keyboard
// focus off the clips, whose Cmd+X/C/V the app routes by where real focus sits. Holds no track state:
// the panel gives it the track's id and current name.
class AddAutomationRow : public juce::Button {
public:
    static constexpr int kBaseHeight = 24;  // px at 100% row zoom; scaled like a lane row
    static constexpr int kCompactSize = 14; // px square of the "+" once the track has a lane

    explicit AddAutomationRow(synth::TrackId track);

    synth::TrackId getTrackId() const noexcept { return track_; }

    /** A track with a lane shows just a "+" in the empty gutter left of its last lane header's colour stripe instead of
     *  a whole row; the panel sets this each time it places the button. */
    void setCompact(bool compact);
    bool isCompact() const noexcept { return compact_; }

    /** Re-words the name and tooltip for the track's current name. */
    void setTrackName(const juce::String& trackName);

    /** The row was pressed (click, Return or Space); the panel opens the picker anchored on it. */
    std::function<void(synth::TrackId)> onAddRequested;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void paintCompact(juce::Graphics& g, bool highlighted);

    synth::TrackId track_;
    bool compact_ = false;
};

} // namespace synth::ui
