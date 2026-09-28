// The piano roll's "MIDI" chip flow: importing a .mid into the open clip asks about CC data only
// when the file has some, and the answer path is one undo step (docs/timeline/piano-roll-lanes.md).

#include "PianoRollTestHelpers.h"
#include "Timeline/MidiClipFile.h"

namespace {

// Records the prompt instead of opening a real AlertWindow (no message loop headless).
struct PromptingRoll : PianoRollComponent {
    using PianoRollComponent::PianoRollComponent;
    int prompts = 0;
    ClipId promptedClip;
    void promptMidiControllerImport(ClipId clipId, const synth::MidiClipFile::ImportResult&) override {
        ++prompts;
        promptedClip = clipId;
    }
};

synth::MidiClipFile::ImportResult fileFrom(const TimelineDoc& doc, ClipId clip) {
    juce::MemoryOutputStream out;
    EXPECT_TRUE(synth::MidiClipFile::exportClip(doc, clip, out));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    return synth::MidiClipFile::importFromStream(in);
}

struct MidiFixture {
    TimelineDoc source;
    ClipId sourceClip;
    TimelineDoc doc;
    TimelineViewState state;
    AppUndoManager undo;
    PromptingRoll roll{state};
    ClipId clip;

    MidiFixture() {
        const auto t = source.addTrack(TrackKind::Midi, "S");
        sourceClip = source.addClip(t, 0.0, 4.0, "s");
        source.addNote(sourceClip, makeNote(1.0, 64));
        const auto track = doc.addTrack(TrackKind::Midi, "T");
        clip = doc.addClip(track, 0.0, 4.0, "c");
        roll.setTimelineDoc(&doc);
        roll.setUndoManager(&undo);
        roll.setSize(900, 300);
        roll.openClip(clip);
    }
};

} // namespace

TEST(PianoRollMidiFileTest, NotesOnlyFileImportsWithoutAsking) {
    MidiFixture f;
    ASSERT_TRUE(f.roll.importMidiIntoOpenClip(fileFrom(f.source, f.sourceClip)));
    EXPECT_EQ(f.roll.prompts, 0);
    EXPECT_EQ(f.doc.getClip(f.clip)->notes.size(), 1u);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_TRUE(f.doc.getClip(f.clip)->notes.empty()) << "one undo step";
}

TEST(PianoRollMidiFileTest, CcFileAsksAndEachAnswerDoesWhatItSays) {
    MidiFixture f;
    ASSERT_TRUE(f.source.setControllerLanePoints(f.sourceClip, 1, {{0.0, 90.0, 0}}));
    const auto file = fileFrom(f.source, f.sourceClip);
    ASSERT_TRUE(f.roll.importMidiIntoOpenClip(file));
    EXPECT_EQ(f.roll.prompts, 1);
    EXPECT_EQ(f.roll.promptedClip, f.clip);
    EXPECT_TRUE(f.doc.getClip(f.clip)->notes.empty()) << "nothing happens until the user answers";

    f.roll.applyMidiImportAnswer(f.roll.promptedClip, file, /*withControllers=*/false);
    EXPECT_EQ(f.doc.getClip(f.clip)->notes.size(), 1u);
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 1), nullptr) << "Notes only";

    f.undo.undo();
    f.roll.applyMidiImportAnswer(f.roll.promptedClip, file, /*withControllers=*/true);
    ASSERT_NE(f.doc.getControllerLane(f.clip, 1), nullptr) << "Import with controller data";
    EXPECT_DOUBLE_EQ(f.doc.getControllerLane(f.clip, 1)->points[0].value, 90.0);
}

TEST(PianoRollMidiFileTest, MidiChipSitsAfterTheLanesChip) {
    MidiFixture f;
    EXPECT_GT(f.roll.getMidiButtonBounds().getX(), f.roll.getLanesButtonBounds().getRight());
    EXPECT_FALSE(f.roll.getTooltipFor(f.roll.getMidiButtonBounds().getCentre()).isEmpty());
}
