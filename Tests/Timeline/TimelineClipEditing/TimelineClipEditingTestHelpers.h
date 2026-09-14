#pragma once

// Shared fixture and helpers for the TimelineDoc-model half of the clip-editing test suite
// (Tests/Timeline/TimelineClipEditing/TimelineClipEditing{Notes,Quantise,SplitJoin,Serialization}Tests.cpp):
// note identity + the note-editing/clip-operations API on TimelineDoc — removeNote/moveNote/
// resizeNote/setNoteVelocity/quantiseNotes and splitClip/joinClips/duplicateClip.
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>

using synth::ClipId;
using synth::MidiNote;
using synth::NoteId;
using synth::TimelineDoc;
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

class TimelineClipEditingTest : public ::testing::Test {
protected:
    TimelineDoc doc;
    CountingListener listener;

    void SetUp() override { doc.addListener(&listener); }
    void TearDown() override { doc.removeListener(&listener); }
};
