#pragma once

// Shared fixture and helpers for the TimelineDoc test suite (Tests/Timeline/TimelineDoc/TimelineDoc*Tests.cpp).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <limits>

using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::Clip;
using synth::ClipId;
using synth::LaneId;
using synth::Marker;
using synth::MarkerId;
using synth::MidiNote;
using synth::NoteId;
using synth::TimelineDoc;
using synth::Track;
using synth::TrackId;
using synth::TrackKind;

class CountingListener : public TimelineDoc::Listener {
public:
    void timelineChanged(const TimelineDoc&) override { ++calls; }
    int calls = 0;
};

inline MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0, int velocity = 100, int channel = 1) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

inline AutomationLane::RangeSnapshot makeRange(float minValue, float maxValue, float defaultValue) {
    AutomationLane::RangeSnapshot range;
    range.minValue = minValue;
    range.maxValue = maxValue;
    range.defaultValue = defaultValue;
    return range;
}

// Deep equality by serialised form: both docs emit their properties in the same order, so an
// identical JSON string means identical content (ids, counters and all).
inline juce::String dump(const TimelineDoc& doc) { return juce::JSON::toString(doc.toVar()); }

// Two tracks, clips out of order, notes, and two lanes with breakpoints — the fixture the
// round-trip tests serialise.
inline void buildPopulatedDoc(TimelineDoc& doc) {
    const auto lead = doc.addTrack(TrackKind::Midi, "Lead");
    const auto bass = doc.addTrack(TrackKind::Midi, "Bass");
    doc.setTrackColour(lead, 0xff112233);
    doc.setTrackBinding(lead, "uuid-track-in-1");
    doc.setTrackMuted(bass, true);

    const auto clipB = doc.addClip(lead, 8.0, 4.0, "B");
    const auto clipA = doc.addClip(lead, 0.0, 4.0, "A");
    doc.addNote(clipA, makeNote(2.0, 67));
    doc.addNote(clipA, makeNote(0.0, 60, 0.5, 90, 2));
    doc.addNote(clipB, makeNote(1.25, 72, 2.0, 1, 16));

    doc.addClip(bass, 4.0, 8.0, "Bassline");

    const auto cutoff = doc.addLane(lead, "uuid-filter", "cutoff", makeRange(20.0f, 20000.0f, 1000.0f));
    doc.addBreakpoint(cutoff, 4.0, 8000.0);
    doc.addBreakpoint(cutoff, 0.0, 500.0, 0.25f, static_cast<int>(BreakpointCurve::Hold));

    const auto res = doc.addLane(bass, "uuid-filter", "resonance", makeRange(0.0f, 1.0f, 0.5f));
    doc.addBreakpoint(res, 2.0, 0.75, -0.5f, static_cast<int>(BreakpointCurve::Bezier));

    // Markers, added out of order so the round-trip tests also pin the (beat, id) sort. One with an
    // empty label, which is legal and is the shape most likely to be mishandled.
    doc.addMarker(16.0, "Chorus", 0xff44aa88);
    doc.addMarker(0.0, "Intro", 0xffE0A33D);
    doc.addMarker(8.5, "", 0xff112244);
}

class TimelineDocTest : public ::testing::Test {
protected:
    TimelineDoc doc;
    CountingListener listener;

    void SetUp() override { doc.addListener(&listener); }
    void TearDown() override { doc.removeListener(&listener); }
};
