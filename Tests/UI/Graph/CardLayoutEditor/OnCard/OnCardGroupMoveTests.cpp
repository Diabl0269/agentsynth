// OnCardGroupMoveTests.cpp -- the pure plan behind moving and renaming a group: where blocks stand after a move,
// the layout change, the announcement.

#include "UI/Graph/CardLayoutEditor/OnCard/OnCardGroupMove.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::ui::oncard;

namespace {

CardLayout threeGroups() {
    CardLayout layout;
    for (const auto* id : {"pitch", "unison", "output"}) {
        CardSection section;
        section.id = id;
        section.title = juce::String(id);
        layout.sections.push_back(section);
    }
    CardSection footer;
    footer.id = CardSection::kFooterId;
    layout.sections.push_back(footer);
    return layout;
}

} // namespace

TEST(OnCardGroupMove, BlocksStackFromTheFirstTopInTheNewOrder) {
    const std::vector<GroupBlock> blocks{{40, 70}, {110, 90}, {200, 50}};
    const auto tops = stackedTops(blocks, orderAfterMoving(3, 1, 2));
    EXPECT_EQ(tops, (std::vector<int>{40, 160, 110})) << "Unison (index 1) now stands last, Output (2) second";
    EXPECT_EQ(stackedTops(blocks, orderAfterMoving(3, 0, 0)), (std::vector<int>{40, 110, 200})) << "no move, no change";
}

TEST(OnCardGroupMove, MovingOneBlockKeepsTheOthersInOrder) {
    EXPECT_EQ(orderAfterMoving(4, 3, 0), (std::vector<int>{3, 0, 1, 2}));
    EXPECT_EQ(orderAfterMoving(4, 0, 3), (std::vector<int>{1, 2, 3, 0}));
    EXPECT_TRUE(isIdentityOrder(orderAfterMoving(4, 2, 2)));
    EXPECT_TRUE(isIdentityOrder(orderAfterMoving(4, 9, 1))) << "out of range changes nothing";
}

TEST(OnCardGroupMove, TheMoveIsAnnouncedWithTheGroupItPassed) {
    const std::vector<juce::String> titles{"Pitch", "Unison", "Output"};
    EXPECT_EQ(describeGroupMove(titles, orderAfterMoving(3, 1, 2), 1), "Unison moved below Output");
    EXPECT_EQ(describeGroupMove(titles, orderAfterMoving(3, 2, 0), 2), "Output moved above Pitch");
    EXPECT_EQ(describeGroupMove(titles, orderAfterMoving(3, 1, 1), 1), "");
}

TEST(OnCardGroupMove, ReorderingWritesTheSectionsIntoTheSameSlotsAndLeavesTheFooter) {
    const auto layout = threeGroups();
    const auto moved = withGroupsReordered(layout, {0, 1, 2}, orderAfterMoving(3, 1, 2));
    ASSERT_EQ(moved.sections.size(), 4u);
    EXPECT_EQ(moved.sections[0].id, "pitch");
    EXPECT_EQ(moved.sections[1].id, "output");
    EXPECT_EQ(moved.sections[2].id, "unison");
    EXPECT_EQ(moved.sections[3].id, CardSection::kFooterId);
    EXPECT_EQ(moved.sections[2].title, layout.sections[1].title) << "a section keeps its title and items";
}

TEST(OnCardGroupMove, SectionsBetweenTheRunsSlotsStayWhereTheyAre) {
    auto layout = threeGroups();
    layout.sections.insert(layout.sections.begin() + 1, CardSection{.id = "hidden-look"});
    // Groups at layout indices 0, 2, 3; the section at 1 is not part of the run.
    const auto moved = withGroupsReordered(layout, {0, 2, 3}, orderAfterMoving(3, 2, 0));
    EXPECT_EQ(moved.sections[0].id, "output");
    EXPECT_EQ(moved.sections[1].id, "hidden-look");
    EXPECT_EQ(moved.sections[2].id, "pitch");
    EXPECT_EQ(moved.sections[3].id, "unison");
}

TEST(OnCardGroupMove, ABadSlotOrOrderChangesNothing) {
    const auto layout = threeGroups();
    EXPECT_EQ(withGroupsReordered(layout, {0, 1, 9}, {0, 1, 2}), layout);
    EXPECT_EQ(withGroupsReordered(layout, {0, 1, 2}, {0, 1, 7}), layout);
    EXPECT_EQ(withGroupsReordered(layout, {0, 1}, {0, 1, 2}), layout);
}

TEST(OnCardGroupMove, ARenameTrimsAndAnEmptyTitleKeepsTheOldName) {
    const auto layout = threeGroups();
    EXPECT_EQ(withGroupTitle(layout, 1, "  Voices ").sections[1].title, juce::String("Voices"));
    EXPECT_EQ(withGroupTitle(layout, 1, "   "), layout);
    EXPECT_EQ(withGroupTitle(layout, 9, "Voices"), layout);
}
