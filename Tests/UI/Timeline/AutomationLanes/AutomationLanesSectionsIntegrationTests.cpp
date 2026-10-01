// AutomationLanesSectionsIntegrationTests.cpp -- what a sections lane does around the band: it is not a lane
// row while it backs a modulator (and is one again when the cable goes), Remove modulator and Move to track take
// it along in one undo step, a saved project reopens with it, and the engine plays it (the LFO's level is 0
// outside the sections and 1 inside).

#include "AutomationLanesSectionsFixture.h"

using namespace sections_test;

namespace {
juce::String foldTitle(SectionsScene& s) {
    s.panel().setTrackAutomationExpanded(s.track, false);
    const auto title = s.panel().getTrackHeaderAt(0)->getFoldArrow().getTitle();
    s.panel().setTrackAutomationExpanded(s.track, true);
    return title;
}
} // namespace

TEST_F(TimelinePanelIntegrationTest, TheSectionsLaneIsNotALaneRowWhileItBacksAModulator) {
    SectionsScene s;
    const int extraBefore = s.panel().getClipLaneArea().getRowLayout().trackExtraHeight(0);
    const auto titleBefore = foldTitle(s);
    EXPECT_TRUE(titleBefore.contains("(1 lane)"));

    s.drawBars(9, 16);

    const auto* level = s.levelLane();
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(s.panel().laneHeaderForTest(level->id), nullptr) << "no header";
    EXPECT_EQ(s.panel().laneEditorForTest(level->id), nullptr) << "no curve editor";
    EXPECT_TRUE(s.panel().laneRowBoundsForTest(level->id).isEmpty());
    EXPECT_EQ(s.panel().getClipLaneArea().getRowLayout().trackExtraHeight(0), extraBefore) << "no extra row height";
    EXPECT_EQ(foldTitle(s), titleBefore) << "the fold arrow still counts one lane";
    EXPECT_NE(s.panel().laneHeaderForTest(s.lane), nullptr) << "the lane it modulates is untouched";
    EXPECT_NE(s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, TheSectionsLaneIsALaneRowAgainWhenTheCableIsRemovedByHand) {
    SectionsScene s;
    s.drawBars(9, 16);
    const auto levelId = s.levelLane()->id;
    ASSERT_EQ(s.panel().laneHeaderForTest(levelId), nullptr);
    const int extraWithSections = s.panel().getClipLaneArea().getRowLayout().trackExtraHeight(0);

    // The cable a person deletes on the canvas: the routing goes, the LFO and its lane stay.
    auto* atten = s.byUuid(s.row()->getInfo().attenuverterUuid);
    ASSERT_NE(atten, nullptr);
    s.mc.getAudioEngine().removeModRouting(atten->nodeID);
    s.mc.getGraphEditor().updateComponents();
    s.panel().refreshModulators();

    EXPECT_EQ(s.row(), nullptr);
    EXPECT_NE(s.levelLane(), nullptr) << "nothing is lost";
    EXPECT_NE(s.panel().laneHeaderForTest(levelId), nullptr) << "an ordinary lane row again";
    EXPECT_NE(s.panel().laneEditorForTest(levelId), nullptr);
    EXPECT_FALSE(s.panel().laneRowBoundsForTest(levelId).isEmpty());
    EXPECT_NE(s.panel().getClipLaneArea().getRowLayout().trackExtraHeight(0), extraWithSections)
        << "the modulator row gave way to a lane row";
    EXPECT_TRUE(foldTitle(s).contains("(2 lanes)"));
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorAlsoRemovesTheSectionsLaneInOneUndoStep) {
    SectionsScene s;
    s.drawBars(9, 16);
    const auto expected = pointsOf(s.levelLane());

    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
    EXPECT_EQ(synth::ui::sectionsLaneFor(s.doc(), s.lfoUuid), nullptr);
    EXPECT_EQ(s.doc().getTracks().front().lanes.size(), 1u) << "only the cutoff lane is left";

    ASSERT_TRUE(s.undo().undo());
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_EQ(pointsOf(s.levelLane()), expected) << "the LFO, its cable and its sections come back together";
    EXPECT_NE(s.row(), nullptr);
    ASSERT_TRUE(s.undo().redo());
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty()) << "and redo removes both again";
    EXPECT_EQ(synth::ui::sectionsLaneFor(s.doc(), s.lfoUuid), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorKeepsTheSectionsOfAnLfoThatStillDrivesAnotherJack) {
    SectionsScene s;
    s.drawBars(9, 16);
    auto* lfo = s.nodesOf<LFOModule>().front();
    auto other = addNodeWithUuid(s.mc, "Filter");
    auto* otherFilter = dynamic_cast<ModuleBase*>(other->getProcessor());
    const int otherRaw = s.mc.getGraphEditor().modulationChannelFor(other->nodeID, "resonance");
    ASSERT_GE(otherRaw, 0);
    s.mc.getGraphEditor().connectPorts(lfo->nodeID, 0, other->nodeID,
                                       otherFilter->mapInputChannel(otherRaw).visibleJackIndex, false, false);
    s.mc.getGraphEditor().updateComponents();

    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_NE(s.levelLane(), nullptr) << "the LFO still plays: its sections stay";
}

TEST_F(TimelinePanelIntegrationTest, MovingTheLaneToAnotherTrackMovesItsSectionsWithIt) {
    SectionsScene s;
    s.mc.simulateAddMidiTrackClick();
    ASSERT_EQ(s.doc().getTracks().size(), 2u);
    const auto second = s.doc().getTracks().back().id;
    s.drawBars(9, 16);
    ASSERT_EQ(s.doc().getTrackForLane(s.levelLane()->id)->id, s.track);
    const auto expected = pointsOf(s.levelLane());

    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    header->buildMenu(); // captures the move targets, as the menu does
    header->applyMenuChoice(synth::ui::AutomationLaneHeaderComponent::kMoveToTrackMenuIdBase);

    EXPECT_EQ(s.doc().getTrackForLane(s.lane)->id, second);
    ASSERT_NE(s.levelLane(), nullptr);
    EXPECT_EQ(s.doc().getTrackForLane(s.levelLane()->id)->id, second) << "the sections travel with their lane";
    EXPECT_EQ(pointsOf(s.levelLane()), expected);

    s.panel().setTrackAutomationExpanded(second, true);
    EXPECT_EQ(s.panel().laneHeaderForTest(s.levelLane()->id), nullptr) << "still shown as the modulator's band";
    ASSERT_NE(s.band(), nullptr);
    EXPECT_EQ(s.band()->currentBlocks(), (SectionBlocks{{32.0, 64.0}}));

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.doc().getTrackForLane(s.lane)->id, s.track);
    EXPECT_EQ(s.doc().getTrackForLane(s.levelLane()->id)->id, s.track) << "one undo puts both back";
}

TEST_F(TimelinePanelIntegrationTest, ASavedProjectReopensWithItsSections) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-sections-load-" + juce::Uuid().toString());
    ASSERT_TRUE(scratch.createDirectory());
    const juce::File bundle = scratch.getChildFile("Project.agsproj");
    {
        SectionsScene s;
        s.drawBars(9, 16);
        s.drawBars(25, 32);
        ASSERT_TRUE(s.mc.saveProjectForTest(bundle));
        ASSERT_TRUE(s.mc.openProjectForTest(bundle));

        const auto* cutoff = s.doc().getLaneForParam(s.targetUuid, "cutoff");
        ASSERT_NE(cutoff, nullptr);
        s.panel().setTrackAutomationExpanded(s.doc().getTrackForLane(cutoff->id)->id, true);
        const auto* level = synth::ui::sectionsLaneFor(s.doc(), s.lfoUuid);
        ASSERT_NE(level, nullptr);
        EXPECT_EQ(pointsOf(level), holdPoints({{0.0, 0.0}, {32.0, 1.0}, {64.0, 0.0}, {96.0, 1.0}, {128.0, 0.0}}));
        auto* band = s.panel().modulatorBandForTest(cutoff->id, 0);
        ASSERT_NE(band, nullptr);
        EXPECT_EQ(band->currentBlocks(), (SectionBlocks{{32.0, 64.0}, {96.0, 128.0}}));
        EXPECT_EQ(s.panel().laneHeaderForTest(level->id), nullptr) << "and is still the band, not a lane row";
    }
    scratch.deleteRecursively();
}
