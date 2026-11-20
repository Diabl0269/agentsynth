// TimelineSectionStripDeleteFocusTests.cpp -- after a delete the keys stay on the strip: the next section (else the
// one before) is selected and carries the one focus ring, never the whole strip. Guard for the bug kind
// delete-drops-focus on this surface: every way to delete (Delete, Backspace, both menu verbs, a group) crossed with
// first, middle, last and only.
#include "TimelineSectionStripThreeBlocksFixture.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDeleteSurvivor.h"
#include <functional>
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

using Choice = TimelineSectionStrip::SectionMenuChoice;

struct Way {
    const char* name;
    std::function<void(ThreeBlocks&, PlacementId)> run;
};

std::vector<Way> everyWayToDelete() {
    return {
        {"Delete key",
         [](ThreeBlocks& f, PlacementId id) {
             f.strip.selectPlacement(id);
             f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
         }},
        {"Backspace key",
         [](ThreeBlocks& f, PlacementId id) {
             f.strip.selectPlacement(id);
             f.strip.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey));
         }},
        {"menu: delete with bars",
         [](ThreeBlocks& f, PlacementId id) {
             f.strip.selectPlacement(id);
             f.strip.applySectionMenuChoice(id, Choice::DeleteWithBars);
         }},
        {"menu: remove, keep the music",
         [](ThreeBlocks& f, PlacementId id) {
             f.strip.selectPlacement(id);
             f.strip.applySectionMenuChoice(id, Choice::RemoveKeepMusic);
         }},
    };
}

} // namespace

TEST(TimelineSectionStripDeleteFocusTest, EveryWayToDeleteSelectsTheNextSectionElseThePreviousOne) {
    for (const auto& way : everyWayToDelete()) {
        {
            ThreeBlocks f;
            way.run(f, f.intro);
            EXPECT_EQ(f.strip.getSelectedPlacement(), f.verse) << way.name << ": delete the first";
            EXPECT_EQ(f.strip.getSelectedPlacements().size(), 1u) << way.name;
        }
        {
            ThreeBlocks f;
            way.run(f, f.verse);
            EXPECT_EQ(f.strip.getSelectedPlacement(), f.bridge) << way.name << ": delete the middle";
        }
        {
            ThreeBlocks f;
            way.run(f, f.bridge);
            EXPECT_EQ(f.strip.getSelectedPlacement(), f.verse) << way.name << ": delete the last";
        }
    }
}

TEST(TimelineSectionStripDeleteFocusTest, DeletingTheOnlySectionLeavesNothingSelectedSoTheWholeStripRings) {
    ThreeBlocks f;
    f.strip.selectPlacement(f.intro);
    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    f.strip.selectPlacement(f.verse);
    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    ASSERT_EQ(f.strip.getSelectedPlacement(), f.bridge);
    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    EXPECT_TRUE(f.doc.getPlacements().empty());
    EXPECT_FALSE(f.strip.getSelectedPlacement().isValid());
    EXPECT_TRUE(f.strip.getSelectedPlacements().empty());
}

TEST(TimelineSectionStripDeleteFocusTest, DeletingSeveralPicksOneSurvivorAfterTheLastOneElseBeforeTheFirst) {
    {
        ThreeBlocks f;
        f.strip.selectPlacements({f.intro, f.bridge}, f.intro); // non-adjacent: the Verse is the only one left
        f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
        EXPECT_EQ(f.strip.getSelectedPlacement(), f.verse);
        EXPECT_EQ(f.strip.getSelectedPlacements().size(), 1u);
    }
    {
        ThreeBlocks f;
        f.strip.selectPlacements({f.verse, f.bridge}, f.verse); // nothing after: the one before
        f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
        EXPECT_EQ(f.strip.getSelectedPlacement(), f.intro);
    }
    {
        ThreeBlocks f;
        EXPECT_EQ(SectionDeleteSurvivor::pick(f.doc, {f.intro, f.verse, f.bridge}), PlacementId{});
        EXPECT_EQ(SectionDeleteSurvivor::pick(f.doc, {}), PlacementId{});
    }
}

TEST(TimelineSectionStripDeleteFocusTest, TheSurvivorAloneCarriesTheFocusRingAndAnUndoThenARedoKeepAnOwner) {
    ThreeBlocks f;
    f.strip.selectPlacement(f.verse);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    ASSERT_EQ(f.strip.getSelectedPlacement(), f.bridge);
    EXPECT_TRUE(f.strip.isPlacementSelected(f.bridge));
    EXPECT_FALSE(f.strip.isPlacementSelected(f.verse));
    ASSERT_TRUE(f.undo.undo());
    f.strip.refresh();
    EXPECT_TRUE(f.strip.getSelectedPlacement().isValid()) << "an undo leaves a block selected";
    EXPECT_NE(f.doc.getPlacement(f.verse), nullptr);
    ASSERT_TRUE(f.undo.redo());
    f.strip.refresh();
    EXPECT_EQ(f.doc.getPlacement(f.verse), nullptr);
    const auto after = f.strip.getSelectedPlacement();
    EXPECT_TRUE(after.isValid() && f.doc.getPlacement(after) != nullptr) << "a redo never leaves a dead selection";
}
