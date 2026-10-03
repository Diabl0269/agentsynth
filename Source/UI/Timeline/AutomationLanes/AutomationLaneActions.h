#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/AutomationLanes/LaneTarget.h"
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth::ui {

struct TrackHeaderHost;

// The edits and reads an automation lane row offers, kept out of the lane header so a test drives
// them without a component and so the header never runs code after its own lane is gone.
// Every edit is ONE undo step when `undo` is non-null and applies directly when it is null.
// Message thread only.

/** Sets the lane's record mode (a LaneRecordMode value). False when nothing changed. */
bool setLaneRecordModeUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane, int mode);

/** Moves the lane, and `companions` (its modulators' amount lanes), to `dest`, removing an Automation track
 *  the move leaves empty in the same step. */
bool moveLaneUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane, synth::TrackId dest,
                      const std::vector<synth::LaneId>& companions = {});

/**
 * The amount lanes that travel with `lane` when it moves to another track: one per routing into it that has
 * one (a routing drives exactly one parameter, so an amount lane is never shared). `host` may be null, when
 * there are none.
 */
std::vector<synth::LaneId> amountLanesTravellingWith(const synth::TimelineDoc& doc, TrackHeaderHost* host,
                                                     synth::LaneId lane);

/** Deletes the lane, removing an Automation track it leaves empty in the same step. */
bool deleteLaneUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane);

/** Points the lane at `target` (TimelineDoc::retargetLane): its curve keeps its shape, a parameter that already
 *  has a lane is refused. False when nothing changed. */
bool retargetLaneUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane, const LaneTarget& target);

/** Adds a copy of `lane` bound to `target` directly below it (TimelineDoc::duplicateLane); the new lane's id,
 *  invalid when nothing was created. */
synth::LaneId duplicateLaneUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane,
                                    const LaneTarget& target);

/** Every MIDI and Audio track the lane could move to, in track order (never its own track). */
std::vector<synth::TrackId> laneMoveTargets(const synth::TimelineDoc& doc, synth::LaneId lane);

/** What a lane row is called: the parameter, and the module it belongs to. */
struct LaneLabels {
    juce::String parameter;
    juce::String module;
};
/** `host` may be null; each label then falls back to the lane's own ids. */
LaneLabels laneLabelsFor(const synth::AutomationLane& lane, TrackHeaderHost* host);

/** The lane's curve value at `beat`, from the same kernel playback uses. */
double laneValueAt(const synth::AutomationLane& lane, double beat);

/** The owning track's colour; `unassigned` for a lane on the Automation track (or a lane that is gone). */
juce::Colour laneColourFor(const synth::TimelineDoc& doc, synth::LaneId lane, juce::Colour unassigned);

/** `value` as the parameter shows it when `host` can say, else as a number. `host` may be null. */
juce::String laneValueText(const synth::AutomationLane& lane, double value, TrackHeaderHost* host);
// The lane value a typed text stands for: the parameter's own parse through `host`, else a plain number with an
// optional unit; nullopt when it is neither.
std::optional<double> laneTextToValue(const synth::AutomationLane& lane, const juce::String& text,
                                      TrackHeaderHost* host);
// The lane's parameter name, falling back to its parameter id.
juce::String laneParameterName(const synth::AutomationLane& lane, TrackHeaderHost* host);

} // namespace synth::ui
