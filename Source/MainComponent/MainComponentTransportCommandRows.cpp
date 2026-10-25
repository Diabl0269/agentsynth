// MainComponentTransportCommandRows.cpp -- the transport rows of the MainComponent command table
// (MainComponentCommandTable.cpp appends them after the focus and help rows).
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Transport/MarkerJump.h"

namespace {
// The current marker beats, for jumpToAdjacentMarker (MarkerJump.h) -- getMarkers() is
// already sorted by beat, but the search itself doesn't require that, so this stays a plain copy
// rather than trusting the ordering invariant twice.
std::vector<double> markerBeats(const synth::TimelineDoc& doc) {
    std::vector<double> beats;
    beats.reserve(doc.getMarkers().size());
    for (const auto& marker : doc.getMarkers())
        beats.push_back(marker.beat);
    return beats;
}

} // namespace

// ---- Transport: promoted to command-dispatched actions (docs/control/midi-remote.md#action-targets's
// prerequisite) so a MIDI Remote action target can invokeDirectly() them. Every row below reuses
// the transport bar's OWN choke point -- triggerClick() on its buttons for play/stop TOGGLE, loop,
// metronome and record, exactly the idiom togglePlayback established in MainComponentCommandTable.cpp -- so a
// command invocation and a mouse click on the bar can never disagree, and record still reaches
// MainComponent::handleRecordToggle's armed-track gate rather than bypassing it. Play/Stop as
// SEPARATE, direction-committing actions (rather than toggles) go straight to TransportService,
// guarded on the current snapshot so invoking "Play" while already playing is a no-op.
// Appended LAST in commandTable() (never interleaved -- see the snapSet id comment in
// AppCommands.h for why that is always safe) so MainComponentCommandTableTests.cpp's
// kExpectedOrder only gains a suffix.
std::vector<MainComponent::CommandSpec> MainComponent::buildTransportCommandRows() {
    return {
        {AppCommands::transportPlay,
         "Play",
         "Start the timeline transport",
         "Transport",
         "transportPlay",
         {},
         [](MainComponent& m) {
             auto& transport = m.audioEngine.getTransport();
             if (!transport.getPositionSnapshot().playing) {
                 transport.play();
                 m.countUsage(synth::telemetry::Feature::TimelineUsed); // the user started the timeline transport
             }
             return true;
         }},
        {AppCommands::transportStop,
         "Stop",
         "Stop the timeline transport",
         "Transport",
         "transportStop",
         {},
         [](MainComponent& m) {
             auto& transport = m.audioEngine.getTransport();
             if (transport.getPositionSnapshot().playing)
                 transport.stop();
             return true;
         }},
        {AppCommands::transportToggleLoop,
         "Toggle Looping",
         "Toggle looping using the current loop locators",
         "Transport",
         "transportToggleLoop",
         {},
         [](MainComponent& m) {
             m.timelinePanel.getTransportBar().getLoopButton().triggerClick();
             return true;
         }},
        {AppCommands::transportRecord,
         "Record",
         "Toggle recording (armed-track gated, same as the transport bar's Record button)",
         "Transport",
         "transportRecord",
         {},
         [](MainComponent& m) {
             m.timelinePanel.getTransportBar().getRecordButton().triggerClick();
             return true;
         }},
        {AppCommands::transportToggleMetronome,
         "Toggle Metronome",
         "Toggle the metronome click",
         "Transport",
         "transportToggleMetronome",
         {},
         [](MainComponent& m) {
             m.timelinePanel.getTransportBar().getMetronomeButton().triggerClick();
             return true;
         }},
        {AppCommands::setSongKey,
         "Set Song Key",
         "Open the song key picker in the transport bar",
         "Transport",
         "setSongKey",
         {},
         [](MainComponent& m) {
             m.focusRegions_.focusRegionById("timeline");
             m.timelinePanel.getTransportBar().getSongKeyField().openPicker();
             return true;
         }},
        {AppCommands::transportReturnToStart,
         "Return to Start",
         "Locate the transport to beat 0",
         "Transport",
         "transportReturnToStart",
         {},
         [](MainComponent& m) {
             return synth::locateTransportTracked(m.audioEngine.getTransport(), m.transportNudge_, 0.0);
         }},
        // Cursor moves and loop-locator jumps. Nudges accumulate through transportNudge_ so
        // several firing inside one audio block (a jog wheel) don't lose steps; see TransportNudge.h.
        {AppCommands::transportNudgeBackBeat,
         "Move Cursor Back (Beat)",
         "Move the transport cursor back one beat",
         "Transport",
         "transportNudgeBackBeat",
         {},
         [](MainComponent& m) {
             return synth::nudgeTransportCursor(m.audioEngine.getTransport(), m.transportNudge_, -1.0);
         }},
        {AppCommands::transportNudgeForwardBeat,
         "Move Cursor Forward (Beat)",
         "Move the transport cursor forward one beat",
         "Transport",
         "transportNudgeForwardBeat",
         {},
         [](MainComponent& m) {
             return synth::nudgeTransportCursor(m.audioEngine.getTransport(), m.transportNudge_, 1.0);
         }},
        {AppCommands::transportNudgeBackBar,
         "Move Cursor Back (Bar)",
         "Move the transport cursor back one bar",
         "Transport",
         "transportNudgeBackBar",
         {},
         [](MainComponent& m) {
             return synth::nudgeTransportCursorBars(m.audioEngine.getTransport(), m.transportNudge_, -1.0);
         }},
        {AppCommands::transportNudgeForwardBar,
         "Move Cursor Forward (Bar)",
         "Move the transport cursor forward one bar",
         "Transport",
         "transportNudgeForwardBar",
         {},
         [](MainComponent& m) {
             return synth::nudgeTransportCursorBars(m.audioEngine.getTransport(), m.transportNudge_, 1.0);
         }},
        {AppCommands::transportJumpToLoopStart,
         "Jump to Loop Start",
         "Locate the transport to the loop start (no-op without a loop range)",
         "Transport",
         "transportJumpToLoopStart",
         {},
         [](MainComponent& m) {
             return synth::jumpToLoopLocator(m.audioEngine.getTransport(), m.transportNudge_, false);
         }},
        {AppCommands::transportJumpToLoopEnd,
         "Jump to Loop End",
         "Locate the transport to the loop end (no-op without a loop range)",
         "Transport",
         "transportJumpToLoopEnd",
         {},
         [](MainComponent& m) {
             return synth::jumpToLoopLocator(m.audioEngine.getTransport(), m.transportNudge_, true);
         }},
        // Jump to the next/previous timeline marker relative to the current position, using
        // the same transportNudge_ accumulate state as the cursor moves above (see MarkerJump.h) so
        // repeated presses step marker-by-marker even before the audio thread applies the first jump.
        {AppCommands::transportJumpToNextMarker,
         "Jump to Next Marker",
         "Locate the transport to the next timeline marker (no-op past the last one)",
         "Transport",
         "transportJumpToNextMarker",
         {},
         [](MainComponent& m) {
             return synth::jumpToAdjacentMarker(m.audioEngine.getTransport(), m.transportNudge_,
                                                markerBeats(m.getTimelineDoc()), true);
         }},
        {AppCommands::transportJumpToPreviousMarker,
         "Jump to Previous Marker",
         "Locate the transport to the previous timeline marker (no-op before the first one)",
         "Transport",
         "transportJumpToPreviousMarker",
         {},
         [](MainComponent& m) {
             return synth::jumpToAdjacentMarker(m.audioEngine.getTransport(), m.transportNudge_,
                                                markerBeats(m.getTimelineDoc()), false);
         }},
    };
}
