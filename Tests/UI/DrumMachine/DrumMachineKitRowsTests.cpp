// The drum machine's rows for a Drum Kit track: one per shown pad, a hidden pad's row gone though its hits stay, the
// caption that counts them, the Show all toggle that switches them back on and hides the same ones again, and the row
// menu's Hide pad and Show pad.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/DrumMachine/DrumLanes.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadShown.h"
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
    int shownCalls = 0;

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
        // The host's side of the two callbacks: the shared helpers the card uses (no graph here, so no undo step).
        grid.onToggleShowAllKitPads = [this] {
            ++showAllCalls;
            synth::ui::toggleShowAllPadsUndoably(nullptr, nullptr, kit);
        };
        grid.onSetKitPadsShown = [this](const std::vector<int>& pads, bool shown) {
            ++shownCalls;
            synth::ui::setPadsShownUndoably(nullptr, nullptr, kit, pads, shown);
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
    EXPECT_TRUE(button.isEnabled()) << "lit, it can hide them again";
    EXPECT_TRUE(button.getToggleState());
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

namespace {

struct RowMenuCapture {
    juce::PopupMenu menu;
    std::function<void(int)> choose;
    RowMenuCapture() {
        synth::ui::test_hooks::drumRowMenuHookForTest() = [this](int, juce::PopupMenu m, std::function<void(int)> c) {
            menu = std::move(m);
            choose = std::move(c);
        };
    }
    ~RowMenuCapture() { synth::ui::test_hooks::drumRowMenuHookForTest() = nullptr; }
};

// The item titled `text` (in a submenu too): its id and whether it is enabled; id 0 when it is not there.
std::pair<int, bool> rowItem(const juce::PopupMenu& menu, const juce::String& text) {
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
        if (it.getItem().text == text)
            return {it.getItem().itemID, it.getItem().isEnabled};
    return {0, false};
}

} // namespace

// Regression test for FRO1394: the clip editor's Show all only switched pads on, and the editor could not hide a pad.
TEST(DrumMachineKitRows, ShowAllIsAToggleThatHidesTheSameRowsAgain) {
    KitRowsFixture f;
    f.open();
    for (int pad : {1, 4, 6})
        f.kit.setPadShown(pad, false);
    f.grid.refreshKitRows();
    auto& button = f.grid.getShowAllButton();
    EXPECT_FALSE(button.getToggleState());
    button.onClick();
    for (int pad : {1, 4, 6})
        EXPECT_GE(f.grid.rowOfPitch(f.notePad(pad)), 0) << "pad " << pad << " returns";
    EXPECT_TRUE(button.getToggleState());
    EXPECT_EQ(button.getTitle(), juce::String::fromUTF8("Show all, 8 of 8 drums \xC2\xB7 3 shown again"));
    EXPECT_TRUE(button.getTooltip().contains("Hide the 3 pads")) << button.getTooltip();

    button.onClick();
    for (int pad = 0; pad < 8; ++pad)
        EXPECT_EQ(f.grid.rowOfPitch(f.notePad(pad)) >= 0, !(pad == 1 || pad == 4 || pad == 6)) << "pad " << pad;
    EXPECT_FALSE(button.getToggleState());
    EXPECT_EQ(f.showAllCalls, 2);
}

TEST(DrumMachineKitRows, TheShowAllButtonIsTheTogglePillInBothModes) {
    KitRowsFixture f;
    f.kit.setPadShown(7, false); // enabled, so it is a Tab stop (a disabled button never is)
    f.open();
    auto& button = f.grid.getShowAllButton();
    EXPECT_TRUE((bool)button.getProperties()[synth::theme::AppLookAndFeel::kTogglePillProperty]);
    EXPECT_EQ(button.getWidth(), synth::theme::AppLookAndFeel::togglePillWidth("Show all"));
    EXPECT_FALSE(button.getTooltip().isEmpty());
    EXPECT_FALSE(button.getTitle().isEmpty());
    EXPECT_TRUE(button.getWantsKeyboardFocus());

    f.grid.kitRowsProvider = nullptr;
    f.grid.refreshKitRows();
    f.open();
    EXPECT_EQ(button.getButtonText(), "Show all drums");
    EXPECT_EQ(button.getWidth(), synth::theme::AppLookAndFeel::togglePillWidth("Show all drums"));
    button.onClick();
    EXPECT_TRUE(f.grid.isShowAllDrums());
    EXPECT_TRUE(button.getToggleState()) << "lit while the extra rows show";
    EXPECT_GE(f.grid.rowOfPitch(51), 0) << "the ride row is there";
    button.onClick();
    EXPECT_FALSE(f.grid.isShowAllDrums());
    EXPECT_FALSE(button.getToggleState());
}

TEST(DrumMachineKitRows, TheRowMenuHidesThePadAndShowsAHiddenOneBack) {
    KitRowsFixture f;
    f.open();
    RowMenuCapture capture;
    const int clap = f.notePad(2);
    f.grid.showRowMenu(clap, std::nullopt);
    const auto hide = rowItem(capture.menu, "Hide pad");
    ASSERT_NE(hide.first, 0);
    EXPECT_TRUE(hide.second);
    EXPECT_FALSE(rowItem(capture.menu, "Show pad").second) << "nothing hidden yet";
    capture.choose(hide.first);
    EXPECT_EQ(f.shownCalls, 1);
    EXPECT_FALSE(f.kit.padShown(2));
    EXPECT_LT(f.grid.rowOfPitch(clap), 0) << "its row is gone";
    EXPECT_EQ(f.grid.getLastAnnouncement(), "Clap hidden");

    f.grid.showRowMenu(f.notePad(0), std::nullopt);
    const auto show = rowItem(capture.menu, "Clap");
    ASSERT_NE(show.first, 0) << "the Show pad submenu lists the hidden pad by name";
    capture.choose(show.first);
    EXPECT_TRUE(f.kit.padShown(2));
    EXPECT_GE(f.grid.rowOfPitch(clap), 0) << "its row is back";
}

TEST(DrumMachineKitRows, HidingARowHidesEveryPadOnItsNote) {
    KitRowsFixture f;
    auto twin = f.kit.pad(3);
    twin.note = f.notePad(2); // pad 4 now plays the clap's note too
    ASSERT_TRUE(f.kit.setPad(3, twin));
    f.open();
    RowMenuCapture capture;
    f.grid.showRowMenu(f.notePad(2), std::nullopt);
    capture.choose(rowItem(capture.menu, "Hide pad").first);
    EXPECT_FALSE(f.kit.padShown(2));
    EXPECT_FALSE(f.kit.padShown(3));
    EXPECT_LT(f.grid.rowOfPitch(f.notePad(2)), 0) << "the row went, not just one of its pads";
}

TEST(DrumMachineKitRows, AClipWithoutAKitHasNoPadItems) {
    KitRowsFixture f;
    f.grid.kitRowsProvider = nullptr;
    f.open();
    RowMenuCapture capture;
    f.grid.showRowMenu(36, std::nullopt);
    EXPECT_EQ(rowItem(capture.menu, "Hide pad").first, 0);
    EXPECT_EQ(rowItem(capture.menu, "Reset").first, DrumMachineComponent::kRowMenuReset) << "the rest is unchanged";
}
