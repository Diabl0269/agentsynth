// TimelineSectionStripMultiSelectTests.cpp -- several section blocks selected at once: Finder-rules clicks through the
// strip's real mouse handlers, Cmd+A, the verbs that act on the whole set as one undo step, what a screen reader and
// the tooltip say, and a group drag whose preview is its commit.
#include "ShortcutManager/ShortcutManager.h"
#include "TimelineSectionStripFixture.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kCmd = juce::ModifierKeys::commandModifier;

// Intro over 0..8, Verse over 8..12, Chorus over 12..20, Outro over 20..24, a named clip at each start.
struct FourSections : StripFixture {
    std::vector<PlacementId> ids;
    FourSections() {
        const std::vector<std::pair<double, double>> spans{{0.0, 8.0}, {8.0, 12.0}, {12.0, 20.0}, {20.0, 24.0}};
        const char* names[] = {"Intro", "Verse", "Chorus", "Outro"};
        for (std::size_t i = 0; i < spans.size(); ++i) {
            doc.addClip(track, spans[i].first, 2.0, names[i]);
            doc.makeSection(spans[i].first, spans[i].second, names[i], 0xff56D4C0, 0.5);
        }
        strip.refresh();
        for (const auto& placement : doc.getPlacements())
            ids.push_back(placement.id);
    }
    juce::Point<float> on(PlacementId id) const { return {x(doc.getPlacement(id)->startBeat + 1.0), 12.0f}; }
    void click(PlacementId id, int flags = 0) {
        const auto at = on(id);
        strip.mouseDown(event(at, at, false, flags));
        strip.mouseUp(event(at, at, false, flags));
    }
    double startOf(PlacementId id) const { return doc.getPlacement(id)->startBeat; }
    std::vector<PlacementId> pick(std::initializer_list<int> which) const {
        std::vector<PlacementId> out;
        for (const int i : which)
            out.push_back(ids[(std::size_t)i]);
        return out;
    }
    // Selects `which` with a plain click on the first and Cmd-clicks on the rest.
    void selectThese(std::initializer_list<int> which) {
        bool first = true;
        for (const int i : which) {
            click(ids[(std::size_t)i], first ? 0 : kCmd);
            first = false;
        }
    }
};

} // namespace

TEST(TimelineSectionStripMultiSelectTest, ClickCmdClickAndShiftClickFollowFinderRules) {
    FourSections f;
    f.click(f.ids[0]);
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({0}));
    f.click(f.ids[2], kCmd);
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({0, 2})) << "Cmd adds one";
    f.click(f.ids[3], kShift);
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({0, 2, 3})) << "Shift runs from the anchor (the Chorus)";
    f.click(f.ids[0], kCmd);
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({2, 3})) << "Cmd takes one out";
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.ids[2]) << "the anchor left the set: the first selected acts";
    f.click(f.ids[1]);
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({1})) << "a plain click selects just one";
    const juce::Point<float> empty{f.x(30.0), 12.0f};
    f.strip.mouseDown(f.event(empty, empty, false));
    f.strip.mouseUp(f.event(empty, empty, false));
    EXPECT_TRUE(f.strip.getSelectedPlacements().empty()) << "a click on empty strip clears";
}

TEST(TimelineSectionStripMultiSelectTest, APlainClickOnOneOfSeveralSelectedNarrowsToItOnRelease) {
    FourSections f;
    f.selectThese({0, 2});
    const auto at = f.on(f.ids[2]);
    f.strip.mouseDown(f.event(at, at, false));
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({0, 2})) << "the press keeps the set, so it can drag it";
    f.strip.mouseUp(f.event(at, at, false));
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({2}));
}

TEST(TimelineSectionStripMultiSelectTest, CmdASelectsEveryBlockAndShiftArrowsExtend) {
    FourSections f;
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)));
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.ids);
    f.click(f.ids[1]);
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({1, 2, 3}));
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({1, 2})) << "the far end comes back";
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({2})) << "a plain arrow selects one again";
}

TEST(TimelineSectionStripMultiSelectTest, DeleteRemovesEverySelectedBlockAndItsBarsAsOneUndoStep) {
    FourSections f;
    f.selectThese({0, 2});
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    ASSERT_EQ(f.doc.getPlacements().size(), 2u);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[1]), 0.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[3]), 4.0) << "both gaps closed";
    ASSERT_TRUE(f.undo.undo());
    f.strip.refresh();
    ASSERT_EQ(f.doc.getPlacements().size(), 4u) << "one undo brings both back";
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[2].startBeat, 12.0);
    EXPECT_FALSE(f.undo.undo()) << "the delete was one step";
}

TEST(TimelineSectionStripMultiSelectTest, CmdDPlacesEverySelectedBlockAgainAfterTheLastAndSelectsTheCopies) {
    FourSections f;
    f.selectThese({1, 0});
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)));
    ASSERT_EQ(f.doc.getPlacements().size(), 6u);
    const auto copies = f.strip.getSelectedPlacements();
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_DOUBLE_EQ(f.startOf(copies[0]), 12.0) << "from the end of the Verse, the later of the two";
    EXPECT_DOUBLE_EQ(f.startOf(copies[1]), 20.0);
    EXPECT_EQ(f.doc.getPlacement(copies[0])->section, f.doc.getPlacement(f.ids[0])->section) << "time order";
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[2]), 24.0) << "make room";
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getPlacements().size(), 4u);
    EXPECT_FALSE(f.undo.undo());
}

TEST(TimelineSectionStripMultiSelectTest, OptionArrowsMoveTheGroupPastTheNeighbouringUnselectedBlock) {
    FourSections f;
    f.selectThese({0, 1});
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)));
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[2]), 0.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[0]), 8.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[1]), 16.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[3]), 20.0);
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({0, 1})) << "the group stays selected";
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0)));
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[0]), 0.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[1]), 8.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[2]), 12.0);
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0)))
        << "at the start the key is still handled";
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[0]), 0.0) << "...and moves nothing";
    ASSERT_TRUE(f.undo.undo());
    ASSERT_TRUE(f.undo.undo());
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[2]), 12.0);
    EXPECT_FALSE(f.undo.undo()) << "each move was one step";
}

TEST(TimelineSectionStripMultiSelectTest, OptionRightPassesTheBlockAfterTheLastSelectedOne) {
    FourSections f;
    f.selectThese({0, 2}); // the Verse sits between them and is not selected
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)));
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[1]), 0.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[3]), 4.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[0]), 8.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[2]), 16.0);
}

TEST(TimelineSectionStripMultiSelectTest, TheMenuOnASelectedBlockActsOnAllAndOnAnotherBlockSelectsIt) {
    FourSections f;
    f.selectThese({1, 3});
    const auto at = f.on(f.ids[3]);
    f.strip.mouseDown(f.event(at, at, false, juce::ModifierKeys::rightButtonModifier));
    EXPECT_EQ(f.strip.blockMenus, 1);
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({1, 3})) << "the set stays";
    EXPECT_TRUE(f.strip.applySectionMenuChoice(f.ids[3], TimelineSectionStrip::SectionMenuChoice::RemoveKeepMusic));
    EXPECT_EQ(f.doc.getPlacements().size(), 2u) << "both lost their placement";
    EXPECT_EQ(f.doc.getTrack(f.track)->clips.size(), 4u) << "and kept their music";

    const auto other = f.on(f.ids[0]);
    f.strip.mouseDown(f.event(other, other, false, juce::ModifierKeys::rightButtonModifier));
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({0}));
}

TEST(TimelineSectionStripMultiSelectTest, EveryProxyReportsItsSelectedStateAndTheTooltipSaysHowMany) {
    FourSections f;
    f.selectThese({0, 2});
    EXPECT_TRUE(f.strip.isProxySelectedForTest(0));
    EXPECT_FALSE(f.strip.isProxySelectedForTest(1));
    EXPECT_TRUE(f.strip.isProxySelectedForTest(2));
    EXPECT_FALSE(f.strip.isProxySelectedForTest(3));
    EXPECT_TRUE(f.strip.tooltipAt(f.on(f.ids[2]))
                    .endsWith(", 2 selected: drag, Delete or " + platformCommandKeyName() + " + D acts on all"));
    EXPECT_FALSE(f.strip.tooltipAt(f.on(f.ids[1])).contains("selected")) << "an unselected block says nothing";
    f.click(f.ids[2]);
    EXPECT_FALSE(f.strip.isProxySelectedForTest(0));
    EXPECT_FALSE(f.strip.tooltipAt(f.on(f.ids[2])).contains("selected")) << "one selected block says nothing";
}

// Every group, moved or copied, make-room or cover, grabbed by its later member and dropped at every beat along the
// strip: the doc after the drop is exactly the plan the drag showed.
TEST(TimelineSectionStripMultiSelectTest, TheGroupPreviewIsTheCommitForEveryModeAndPosition) {
    const std::vector<std::vector<int>> groups{{0, 1}, {0, 2}, {1, 3}, {0, 1, 2}};
    for (const auto& members : groups)
        for (const bool copy : {false, true})
            for (const bool cover : {false, true})
                for (double left = 0.0; left <= 30.0; left += 1.0) {
                    FourSections f;
                    std::vector<PlacementId> group;
                    for (const int i : members)
                        group.push_back(f.ids[(std::size_t)i]);
                    f.click(group.front());
                    for (std::size_t i = 1; i < group.size(); ++i)
                        f.click(group[i], kCmd);
                    const auto grabbed = group.back();
                    const auto from = f.on(grabbed);
                    // The group's left edge at `left`: the grabbed member rides after the others.
                    double lead = 0.0;
                    for (std::size_t i = 0; i + 1 < group.size(); ++i)
                        lead += f.doc.getSection(f.doc.getPlacement(group[i])->section)->lengthBeats;
                    const juce::Point<float> to{f.x(left + lead + 1.0), 12.0f};
                    const int flags = (copy ? kAlt : 0) | (cover ? kShift : 0);
                    f.strip.mouseDown(f.event(from, from, false));
                    f.strip.mouseDrag(f.event(to, from, true, flags));
                    if (!f.strip.isBlockDraggingForTest())
                        continue; // too small a move to start a drag
                    ASSERT_EQ(f.strip.getDragMembersForTest(), group);
                    const auto plan = f.strip.getBlockDragPlanForTest();
                    f.strip.mouseUp(f.event(to, from, true, flags));
                    std::string where = "group";
                    for (const int i : members)
                        where += " " + std::to_string(i);
                    where += std::string(copy ? " copy" : " move") + (cover ? " cover" : " make-room") + " at " +
                             std::to_string(left);
                    ASSERT_EQ(plan.group.size(), group.size()) << where;
                    std::vector<const SectionPlacement*> placed;
                    for (const auto& placement : f.doc.getPlacements())
                        if (copy && std::find(f.ids.begin(), f.ids.end(), placement.id) == f.ids.end())
                            placed.push_back(&placement);
                    for (std::size_t i = 0; i < group.size(); ++i) {
                        const auto* now =
                            copy ? (i < placed.size() ? placed[i] : nullptr) : f.doc.getPlacement(group[i]);
                        ASSERT_NE(now, nullptr) << where << ": member " << i;
                        EXPECT_DOUBLE_EQ(now->startBeat, plan.group[i].second) << where << ": member " << i;
                    }
                    EXPECT_DOUBLE_EQ(plan.group.front().second, plan.landing) << where;
                    for (const auto& other : plan.others) {
                        const auto* now = f.doc.getPlacement(PlacementId{other.id});
                        if (other.covered) {
                            EXPECT_EQ(now, nullptr) << where << ": covered " << other.id;
                        } else {
                            ASSERT_NE(now, nullptr) << where << ": lost " << other.id;
                            EXPECT_DOUBLE_EQ(now->startBeat, other.start) << where << ": other " << other.id;
                        }
                    }
                    if (!copy)
                        EXPECT_EQ(f.strip.getSelectedPlacements(), group) << where << ": the group stays selected";
                }
}

TEST(TimelineSectionStripMultiSelectTest, TheGroupsGhostsRideBackToBackAndEscCancelsWithNoChange) {
    FourSections f;
    f.selectThese({0, 2});
    const auto from = f.on(f.ids[2]);
    const juce::Point<float> to{f.x(26.0), 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(to, from, true));
    ASSERT_TRUE(f.strip.isBlockDraggingForTest());
    ASSERT_EQ(f.strip.getDragMembersForTest(), f.pick({0, 2}));
    const auto intro = f.strip.getDragMemberGhostBoundsForTest(0);
    const auto chorus = f.strip.getDragMemberGhostBoundsForTest(1);
    EXPECT_FLOAT_EQ(chorus.getX(), f.strip.getDragGhostBoundsForTest().getX())
        << "the grabbed one is under the pointer";
    EXPECT_FLOAT_EQ(chorus.getX() - intro.getX(), (float)(8.0 * f.state.pixelsPerBeat)) << "the Intro just before it";
    EXPECT_TRUE(f.strip.getTooltip().isEmpty()) << "no tooltip over a drag in flight";
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.strip.isBlockDraggingForTest());
    f.strip.mouseUp(f.event(to, from, true));
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[0]), 0.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[2]), 12.0);
    EXPECT_FALSE(f.undo.undo()) << "no undo entry was made";
    EXPECT_EQ(f.strip.getSelectedPlacements(), f.pick({0, 2})) << "a cancelled drag keeps the set";
}

TEST(TimelineSectionStripMultiSelectTest, ReleasingAGroupDragAboveTheStripCancels) {
    FourSections f;
    f.selectThese({0, 1});
    const auto from = f.on(f.ids[1]);
    const juce::Point<float> to{f.x(26.0), -10.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(to, from, true));
    f.strip.mouseUp(f.event(to, from, true));
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[0]), 0.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.ids[1]), 8.0);
    EXPECT_FALSE(f.undo.undo());
}
