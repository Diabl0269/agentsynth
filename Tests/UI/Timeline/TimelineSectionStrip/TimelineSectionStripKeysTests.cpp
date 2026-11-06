// TimelineSectionStripKeysTests.cpp -- the Sections strip's keyboard: every key, through keyPressed.
#include "ShortcutManager/ShortcutManager.h"
#include "TimelineSectionStripFixture.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

struct ThreeBlocks : StripFixture {
    PlacementId a, b, c;
    ThreeBlocks() {
        doc.addClip(track, 0.0, 4.0, "A");
        doc.addClip(track, 8.0, 4.0, "B");
        doc.addClip(track, 16.0, 4.0, "C");
        doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, 0.5);
        doc.makeSection(8.0, 16.0, "Verse", 0xff4FC1FF, 0.5);
        doc.makeSection(16.0, 24.0, "Chorus", 0xffC792EA, 0.5);
        strip.refresh();
        a = doc.getPlacements()[0].id;
        b = doc.getPlacements()[1].id;
        c = doc.getPlacements()[2].id;
    }
    bool press(int code, int mods = 0) { return strip.keyPressed(juce::KeyPress(code, juce::ModifierKeys(mods), 0)); }
};

constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;

} // namespace

TEST(TimelineSectionStripKeysTest, TheStripTakesKeyboardFocus) {
    ThreeBlocks f;
    EXPECT_TRUE(f.strip.getWantsKeyboardFocus());
}

TEST(TimelineSectionStripKeysTest, LeftRightHomeEndWalkTheBlocks) {
    ThreeBlocks f;
    EXPECT_TRUE(f.press(juce::KeyPress::rightKey));
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.a) << "Right with nothing selected lands on the first";
    f.press(juce::KeyPress::rightKey);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.b);
    f.press(juce::KeyPress::rightKey);
    f.press(juce::KeyPress::rightKey);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.c) << "stays on the last";
    f.press(juce::KeyPress::leftKey);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.b);
    f.press(juce::KeyPress::homeKey);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.a);
    f.press(juce::KeyPress::endKey);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.c);
}

TEST(TimelineSectionStripKeysTest, LeftWithNothingSelectedLandsOnTheLast) {
    ThreeBlocks f;
    f.press(juce::KeyPress::leftKey);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.c);
}

TEST(TimelineSectionStripKeysTest, ReturnAndF2RenameTheFocusedBlockButNothingFocusedDoesNothing) {
    ThreeBlocks f;
    EXPECT_FALSE(f.press(juce::KeyPress::returnKey)) << "an empty selection starts nothing";
    EXPECT_EQ(f.strip.getRenameEditorForTest(), nullptr);
    f.strip.selectPlacement(f.b);
    EXPECT_TRUE(f.press(juce::KeyPress::returnKey));
    ASSERT_NE(f.strip.getRenameEditorForTest(), nullptr);
    EXPECT_EQ(f.strip.getRenameEditorForTest()->getText(), "Verse");
    f.strip.getRenameEditorForTest()->onEscapeKey();
    EXPECT_TRUE(f.press(juce::KeyPress::F2Key));
    EXPECT_NE(f.strip.getRenameEditorForTest(), nullptr);
}

TEST(TimelineSectionStripKeysTest, CmdDPlacesTheSectionAgainAfterTheBlockAsOneUndoStep) {
    ThreeBlocks f;
    f.strip.selectPlacement(f.a);
    EXPECT_TRUE(f.press('d', kCmd));
    EXPECT_EQ(f.doc.getPlacements().size(), 4u);
    EXPECT_EQ(f.doc.placementsOf(f.doc.getPlacement(f.a)->section).size(), 2u);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getPlacements().size(), 3u);
}

TEST(TimelineSectionStripKeysTest, DeleteAndBackspaceDeleteTheSectionAndItsBars) {
    for (const int code : {(int)juce::KeyPress::deleteKey, (int)juce::KeyPress::backspaceKey}) {
        ThreeBlocks f;
        f.strip.selectPlacement(f.b);
        EXPECT_TRUE(f.press(code));
        EXPECT_EQ(f.doc.getPlacements().size(), 2u);
        EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.c)->startBeat, 8.0) << "the gap closes";
        EXPECT_TRUE(f.undo.undo());
        EXPECT_EQ(f.doc.getPlacements().size(), 3u);
    }
}

TEST(TimelineSectionStripKeysTest, OptionArrowsMoveTheBlockOneSectionLaterOrEarlier) {
    ThreeBlocks f;
    f.strip.selectPlacement(f.a);
    EXPECT_TRUE(f.press(juce::KeyPress::rightKey, kAlt));
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.a)->startBeat, 8.0) << "after the Verse";
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.b)->startBeat, 0.0) << "the Verse takes its place";
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.c)->startBeat, 16.0);
    EXPECT_TRUE(f.press(juce::KeyPress::leftKey, kAlt));
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.a)->startBeat, 0.0);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.undo.undo());
    EXPECT_FALSE(f.undo.undo()) << "one step each";
}

TEST(TimelineSectionStripKeysTest, MovingPastEitherEndChangesNothing) {
    ThreeBlocks f;
    f.strip.selectPlacement(f.a);
    EXPECT_TRUE(f.press(juce::KeyPress::leftKey, kAlt));
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.a)->startBeat, 0.0);
    EXPECT_FALSE(f.undo.undo());
}

TEST(TimelineSectionStripKeysTest, TheMenuKeyOpensTheBlockMenuOnlyWhenABlockIsFocused) {
    ThreeBlocks f;
    EXPECT_FALSE(f.strip.showContextMenuForKeyboardFocus());
    f.strip.selectPlacement(f.b);
    EXPECT_TRUE(f.strip.showContextMenuForKeyboardFocus());
    EXPECT_EQ(f.strip.blockMenus, 1);
}

TEST(TimelineSectionStripKeysTest, EscapeIsNotOursWhenNoGestureIsInFlight) {
    ThreeBlocks f;
    EXPECT_FALSE(f.press(juce::KeyPress::escapeKey));
}

TEST(TimelineSectionStripKeysTest, TheStripsKeysFollowTheRebindableActionsTheyShare) {
    ShortcutManager manager;
    manager.resetToDefaults();
    ThreeBlocks f;
    f.strip.setShortcutManager(&manager);
    f.strip.selectPlacement(f.a);
    manager.setBinding("timelineClipMoveLater", juce::KeyPress('l', kCmd, 0));
    EXPECT_FALSE(f.press(juce::KeyPress::rightKey, kAlt)) << "the old chord is no longer the action's";
    EXPECT_TRUE(f.press('l', kCmd));
    EXPECT_DOUBLE_EQ(f.doc.getPlacement(f.a)->startBeat, 8.0);
    f.strip.setShortcutManager(nullptr);
}
