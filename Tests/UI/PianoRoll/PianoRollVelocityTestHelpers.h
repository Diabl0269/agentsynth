#pragma once

// Shared fixture for the velocity-strip suites (PianoRollVelocityLaneTests.cpp,
// PianoRollVelocityToolbarTests.cpp). Header-only; not registered in Tests/CMakeLists.txt.

#include "PianoRollTestHelpers.h"

#include "UI/PianoRoll/VelocityLane/PianoRollVelocityLane.h"
#include <initializer_list>
#include <vector>

// A roll tall enough for a usable grid above the 64 px strip, with the strip SHOWN (the shared
// PianoRollFixture hides it so every grid test keeps its grid-to-bottom geometry).
struct VelocityLaneFixture : PianoRollFixture {
    VelocityLaneFixture() {
        roll.setVelocityLaneVisible(true);
        roll.setSize(900, 300);
    }

    synth::ui::PianoRollVelocityLane& lane() { return roll.getVelocityLane(); }

    struct Bed {
        ClipId clipId;
        std::vector<NoteId> notes;
    };

    // One note per velocity, at beats 1, 2, 3, ... on rising pitches, in an 8-beat clip opened at
    // 40 px per beat (so beat b sits at x == 44 + 40 * b).
    Bed makeBed(std::initializer_list<int> velocities) {
        Bed bed;
        const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        bed.clipId = doc.addClip(trackId, 0.0, 8.0, "Clip");
        int i = 0;
        for (const int v : velocities) {
            auto note = makeNote(1.0 + i, 60 + 2 * i, 0.5);
            note.velocity = v;
            bed.notes.push_back(doc.addNote(bed.clipId, note));
            ++i;
        }
        open(bed.clipId);
        return bed;
    }

    // A point in the STRIP's coordinates: x at the absolute beat (through the roll's own mapping),
    // y at the given velocity's stick height.
    juce::Point<float> at(double absBeat, int velocity) {
        auto& l = lane();
        return {(float)(roll.beatToX(absBeat) - l.getX()), l.yForVelocity(velocity)};
    }

    int velocityOf(NoteId id) const {
        const auto* note = doc.getNote(id);
        return note != nullptr ? note->velocity : -1;
    }
};
