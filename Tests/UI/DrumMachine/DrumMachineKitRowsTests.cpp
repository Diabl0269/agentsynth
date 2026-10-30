// The drum machine's rows for a Drum Kit track: one per shown pad, a hidden pad's row gone though its hits stay, the
// caption that counts them, and the Show all button that switches them back on.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/DrumMachine/DrumLanes.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"

using synth::ui::DrumMachineComponent;
using synth::ui::KitRows;

namespace {

struct KitRowsFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumKitModule kit;
    DrumMachineComponent grid;
    ClipId clip;
    int showAllCalls = 0;

    KitRowsFixture() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        const auto track = doc.addTrack(TrackKind::Midi, "Drums");
        clip = doc.addClip(track, 0.0, 8.0, "Beat 1");
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, 320);
        grid.kitRowsProvider = [this](KitRows& rows) {
            rows = synth::ui::kitRowsFor(kit);
            return true;
        };
        grid.onShowAllKitPads = [this] {
            ++showAllCalls;
            kit.showAllPads();
        };
    }
    void open() { grid.openClip(clip); }
    int notePad(int pad) { return kit.pad(pad).note; }
};

} // namespace

TEST(DrumMachineKitRows, ARowPerShownPadAndNoneForAHiddenOne) {
    KitRowsFixture f;
    f.open();
    for (int pad = 0; pad < 8; ++pad)
        EXPECT_GE(f.grid.rowOfPitch(f.notePad(pad)), 0) << "pad " << pad;
    f.kit.setPadShown(7, false);
    f.grid.refreshKitRows();
    EXPECT_LT(f.grid.rowOfPitch(f.notePad(7)), 0) << "the cowbell's row is gone";
    EXPECT_GE(f.grid.rowOfPitch(f.notePad(6)), 0);
    EXPECT_EQ(f.grid.kitRowsCaption(), juce::String::fromUTF8("7 of 8 drums \xC2\xB7 1 hidden"));
}

TEST(DrumMachineKitRows, AHiddenDrumsHitsStayInTheClipAndAreNotDrawn) {
    KitRowsFixture f;
    f.doc.addNote(f.clip, makeNote(1.0, f.notePad(7), 0.25));
    f.kit.setPadShown(7, false);
    f.open();
    EXPECT_LT(f.grid.rowOfPitch(f.notePad(7)), 0) << "no row, though the clip holds the note";
    ASSERT_EQ(f.doc.getClip(f.clip)->notes.size(), 1u) << "the hit is still there and still plays";
    f.kit.showAllPads();
    f.grid.refreshKitRows();
    EXPECT_GE(f.grid.rowOfPitch(f.notePad(7)), 0) << "its row comes back with the hit on it";
}

TEST(DrumMachineKitRows, ShowAllIsOffUntilARowIsHiddenThenSwitchesThemBackOn) {
    KitRowsFixture f;
    f.open();
    auto& button = f.grid.getShowAllButton();
    EXPECT_EQ(button.getButtonText(), "Show all");
    EXPECT_FALSE(button.isEnabled()) << "nothing hidden";
    f.kit.setPadShown(1, false);
    f.kit.setPadShown(2, false);
    f.grid.refreshKitRows();
    EXPECT_TRUE(button.isEnabled());
    EXPECT_EQ(button.getTitle(), juce::String::fromUTF8("Show all, 6 of 8 drums \xC2\xB7 2 hidden"));
    button.onClick();
    EXPECT_EQ(f.showAllCalls, 1);
    EXPECT_GE(f.grid.rowOfPitch(f.notePad(1)), 0);
    EXPECT_FALSE(button.isEnabled());
}

TEST(DrumMachineKitRows, AClipWithoutAKitKeepsTheGeneralMidiRows) {
    KitRowsFixture f;
    f.grid.kitRowsProvider = nullptr;
    f.open();
    EXPECT_EQ(f.grid.kitRows(), nullptr);
    EXPECT_EQ(f.grid.getShowAllButton().getButtonText(), "Show all drums");
    EXPECT_GE(f.grid.rowOfPitch(36), 0);
}

// Regression test for FRO1304: the picked row's pad was switched off, and the lane below the grid kept editing it.
TEST(DrumMachineKitRows, HidingThePickedDrumMovesThePickToARowThatIsStillThere) {
    KitRowsFixture f;
    f.open();
    const int cowbell = f.notePad(7);
    f.grid.setSelectedPitch(cowbell);
    ASSERT_EQ(f.grid.getSelectedPitch(), cowbell);
    f.kit.setPadShown(7, false);
    f.grid.refreshKitRows();
    EXPECT_NE(f.grid.getSelectedPitch(), cowbell);
    EXPECT_GE(f.grid.rowOfPitch(f.grid.getSelectedPitch()), 0) << "the pick is on a row that exists";
}
