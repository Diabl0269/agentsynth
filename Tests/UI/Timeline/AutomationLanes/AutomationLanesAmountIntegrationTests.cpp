// AutomationLanesAmountIntegrationTests.cpp -- what an amount lane does around the band: it is not a lane row
// while its routing exists (and is one again, orphaned, when the cable goes), Remove modulator and Move to track
// take it along in one undo step, a saved project reopens with it, and a project saved with the retired LFO
// sections is migrated to amount lanes once, on load.

#include "AutomationLanesAmountFixture.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"

using namespace amount_test;

namespace {
juce::String foldTitle(AmountScene& s) {
    s.panel().setTrackAutomationExpanded(s.track, false);
    const auto title = s.panel().getTrackHeaderAt(0)->getFoldArrow().getTitle();
    s.panel().setTrackAutomationExpanded(s.track, true);
    return title;
}

struct Pt {
    double beat;
    double value;
    int curve;
    bool operator==(const Pt& o) const {
        return beat == o.beat && std::abs(value - o.value) < 1.0e-6 && curve == o.curve;
    }
};
std::vector<Pt> pointsOf(const synth::AutomationLane* lane) {
    std::vector<Pt> out;
    if (lane != nullptr)
        for (const auto& p : lane->points)
            out.push_back({p.beat, p.value, p.curve});
    return out;
}
std::ostream& operator<<(std::ostream& os, const Pt& p) { return os << "(" << p.beat << ", " << p.value << ")"; }

juce::File scratchBundle(const juce::String& name) {
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("agentsynth-" + name + "-" + juce::Uuid().toString());
    dir.createDirectory();
    return dir.getChildFile("Project.agsproj");
}
} // namespace

TEST_F(TimelinePanelIntegrationTest, TheAmountLaneIsNotALaneRowWhileItsRoutingExists) {
    AmountScene s;
    const int extraBefore = s.panel().getClipLaneArea().getRowLayout().trackExtraHeight(0);
    const auto titleBefore = foldTitle(s);
    EXPECT_TRUE(titleBefore.contains("(1 lane)"));

    s.doubleClick({s.x(16.0), s.yFor(0.25)});

    const auto* amount = s.amountLane();
    ASSERT_NE(amount, nullptr);
    EXPECT_EQ(s.panel().laneHeaderForTest(amount->id), nullptr) << "no header";
    EXPECT_EQ(s.panel().laneEditorForTest(amount->id), nullptr) << "no curve editor of its own";
    EXPECT_TRUE(s.panel().laneRowBoundsForTest(amount->id).isEmpty());
    EXPECT_EQ(s.panel().getClipLaneArea().getRowLayout().trackExtraHeight(0), extraBefore) << "no extra row height";
    EXPECT_EQ(foldTitle(s), titleBefore) << "the fold arrow still counts one lane";
    EXPECT_NE(s.panel().laneHeaderForTest(s.lane), nullptr) << "the lane it modulates is untouched";
}

// What deleting the cable (or the LFO) on the canvas does: the Attenuverter goes, so the amount lane's node no
// longer resolves. Like any lane whose node is gone it is kept, orphaned, never deleted, and with no routing to
// draw it as a band it shows as an ordinary lane row; an undo of the canvas delete hides it again.
TEST_F(TimelinePanelIntegrationTest, TheAmountLaneIsAnOrphanedLaneRowWhenTheCableIsDeletedByHand) {
    AmountScene s;
    s.doubleClick({s.x(16.0), s.yFor(0.25)});
    const auto amountId = s.amountLane()->id;
    ASSERT_EQ(s.panel().laneHeaderForTest(amountId), nullptr);

    auto* atten = s.byUuid(s.attenUuid);
    ASSERT_NE(atten, nullptr);
    s.mc.getAudioEngine().removeModRouting(atten->nodeID);
    s.mc.getGraphEditor().updateComponents();
    s.panel().refreshModulators();

    EXPECT_EQ(s.row(), nullptr);
    ASSERT_NE(s.amountLane(), nullptr) << "nothing is lost";
    EXPECT_NE(s.panel().laneHeaderForTest(amountId), nullptr) << "an ordinary lane row again";
    EXPECT_TRUE(foldTitle(s).contains("(2 lanes)"));
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorAlsoRemovesTheAmountLaneInOneUndoStep) {
    AmountScene s;
    s.doubleClick({s.x(16.0), s.yFor(0.25)});
    const auto expected = pointsOf(s.amountLane());
    ASSERT_FALSE(expected.empty());

    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
    EXPECT_EQ(s.amountLane(), nullptr);
    EXPECT_EQ(s.doc().getTracks().front().lanes.size(), 1u) << "only the cutoff lane is left";

    ASSERT_TRUE(s.undo().undo());
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_EQ(pointsOf(s.amountLane()), expected) << "the LFO, its cable and its amount lane come back together";
    EXPECT_NE(s.row(), nullptr);
    ASSERT_TRUE(s.undo().redo());
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty()) << "and redo removes both again";
    EXPECT_EQ(s.amountLane(), nullptr);
}

// The amount lane belongs to the routing, not to the LFO: it goes with its cable even when the LFO stays.
TEST_F(TimelinePanelIntegrationTest, RemoveModulatorTakesTheAmountLaneEvenWhenTheLfoStillDrivesAnotherJack) {
    AmountScene s;
    s.doubleClick({s.x(16.0), s.yFor(0.25)});
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
    EXPECT_EQ(s.amountLane(), nullptr);
    ASSERT_TRUE(s.undo().undo());
    EXPECT_NE(s.amountLane(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, MovingTheLaneToAnotherTrackMovesItsAmountLaneWithIt) {
    AmountScene s;
    s.mc.simulateAddMidiTrackClick();
    ASSERT_EQ(s.doc().getTracks().size(), 2u);
    const auto second = s.doc().getTracks().back().id;
    s.doubleClick({s.x(16.0), s.yFor(0.25)});
    ASSERT_EQ(s.doc().getTrackForLane(s.amountLane()->id)->id, s.track);
    const auto expected = pointsOf(s.amountLane());

    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    header->buildMenu(); // captures the move targets, as the menu does
    header->applyMenuChoice(synth::ui::AutomationLaneHeaderComponent::kMoveToTrackMenuIdBase);

    EXPECT_EQ(s.doc().getTrackForLane(s.lane)->id, second);
    ASSERT_NE(s.amountLane(), nullptr);
    EXPECT_EQ(s.doc().getTrackForLane(s.amountLane()->id)->id, second) << "the amount lane travels with its lane";
    EXPECT_EQ(pointsOf(s.amountLane()), expected);

    s.panel().setTrackAutomationExpanded(second, true);
    EXPECT_EQ(s.panel().laneHeaderForTest(s.amountLane()->id), nullptr) << "still shown as the modulator's band";
    ASSERT_NE(s.band(), nullptr);
    EXPECT_EQ(s.band()->amountLane(), s.amountLane());

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.doc().getTrackForLane(s.lane)->id, s.track);
    EXPECT_EQ(s.doc().getTrackForLane(s.amountLane()->id)->id, s.track) << "one undo puts both back";
}

TEST_F(TimelinePanelIntegrationTest, ASavedProjectReopensWithItsAmountLane) {
    const auto bundle = scratchBundle("amount-load");
    {
        AmountScene s;
        s.doubleClick({s.x(16.0), s.yFor(-0.5)});
        const auto expected = pointsOf(s.amountLane());
        ASSERT_TRUE(s.mc.saveProjectForTest(bundle));
        ASSERT_TRUE(s.mc.openProjectForTest(bundle));

        const auto* cutoff = s.doc().getLaneForParam(s.targetUuid, "cutoff");
        ASSERT_NE(cutoff, nullptr);
        s.panel().setTrackAutomationExpanded(s.doc().getTrackForLane(cutoff->id)->id, true);
        const auto* amount = synth::ui::amountLaneFor(s.doc(), s.attenUuid);
        ASSERT_NE(amount, nullptr);
        EXPECT_EQ(pointsOf(amount), expected);
        auto* band = s.panel().modulatorBandForTest(cutoff->id, 0);
        ASSERT_NE(band, nullptr);
        EXPECT_EQ(band->amountLane(), amount);
        EXPECT_EQ(s.panel().laneHeaderForTest(amount->id), nullptr) << "and is still the band, not a lane row";
    }
    bundle.getParentDirectory().deleteRecursively();
}

// A project saved before amount lanes: the LFO (inside a macro, with its Filter) had sections, i.e. a `level`
// lane of Hold 0/1 blocks. Opening it migrates that once: the routing's amount lane holds the routing's amount
// inside the old blocks and 0 outside, the level lane is gone, the LFO's level is 1, and nothing on the canvas
// moved.
TEST_F(TimelinePanelIntegrationTest, AProjectSavedWithLfoSectionsInsideAMacroOpensWithAnAmountLaneInstead) {
    const auto bundle = scratchBundle("sections-migration");
    {
        Scene s;
        synth::Macro macro;
        macro.name = "Tone";
        macro.collapsed = false;
        macro.bounds = {300, 300, 400, 300};
        macro.members.push_back(s.targetUuid);
        s.mc.getGraphEditor().getMacros().add(macro);
        s.mc.getGraphEditor().updateComponents();
        s.addLfoFromLaneMenu();
        const auto lfoUuid = s.nodesOf<LFOModule>().front()->properties["uuid"].toString();
        ASSERT_NE(s.mc.getGraphEditor().getMacros().findByMember(lfoUuid), nullptr) << "the LFO is in the macro";
        const auto attenUuid = s.row()->getInfo().attenuverterUuid;
        auto* amountParam = findParameterByID(s.byUuid(attenUuid)->getProcessor(), "amount");
        amountParam->setValueNotifyingHost(amountParam->convertTo0to1(0.6f));

        // The sections lane exactly as the retired band wrote it: bars 9-16 and 25 onward.
        const auto level = s.doc().addLane(s.track, lfoUuid, "level", {0.0f, 1.0f, 1.0f});
        ASSERT_TRUE(s.doc().editBreakpoints(
            level, {},
            {{0.0, 0.0, 0.0f, kHold}, {32.0, 1.0, 0.0f, kHold}, {64.0, 0.0, 0.0f, kHold}, {96.0, 1.0, 0.0f, kHold}}));
        auto* lfoLevel = findParameterByID(s.byUuid(lfoUuid)->getProcessor(), "level");
        lfoLevel->setValueNotifyingHost(lfoLevel->convertTo0to1(0.8f));
        ASSERT_TRUE(s.mc.saveProjectForTest(bundle));

        std::map<juce::String, juce::Point<int>> positions;
        for (auto* node : s.graph().getNodes())
            positions[node->properties["uuid"].toString()] = {(int)node->properties["x"], (int)node->properties["y"]};
        const auto macroBounds = s.mc.getGraphEditor().getMacros().findByMember(lfoUuid)->bounds;

        ASSERT_TRUE(s.mc.openProjectForTest(bundle));

        EXPECT_EQ(synth::ui::sectionsLaneFor(s.doc(), lfoUuid), nullptr) << "the level lane is gone";
        const auto* amount = synth::ui::amountLaneFor(s.doc(), attenUuid);
        ASSERT_NE(amount, nullptr) << "the routing has an amount lane";
        EXPECT_EQ(pointsOf(amount),
                  (std::vector<Pt>{{0.0, 0.0, kHold}, {32.0, 0.6, kHold}, {64.0, 0.0, kHold}, {96.0, 0.6, kHold}}));
        EXPECT_EQ(s.doc().getTrackForLane(amount->id)->id,
                  s.doc().getTrackForLane(s.doc().getLaneForParam(s.targetUuid, "cutoff")->id)->id);
        EXPECT_FLOAT_EQ(s.parameter(lfoUuid, "level"), 1.0f) << "the LFO is no longer silenced on its own";
        EXPECT_NEAR(s.parameter(attenUuid, "amount"), 0.6f, 1.0e-5f) << "the knob keeps its value";

        for (auto* node : s.graph().getNodes()) {
            const auto uuid = node->properties["uuid"].toString();
            if (positions.count(uuid) > 0)
                EXPECT_EQ(juce::Point<int>((int)node->properties["x"], (int)node->properties["y"]), positions[uuid])
                    << node->getProcessor()->getName() << " moved";
        }
        const auto* reopened = s.mc.getGraphEditor().getMacros().findByMember(lfoUuid);
        ASSERT_NE(reopened, nullptr);
        EXPECT_EQ(reopened->bounds, macroBounds);

        // The migrated lane is the band, and the document opens clean: the migration was not an edit.
        const auto* cutoff = s.doc().getLaneForParam(s.targetUuid, "cutoff");
        s.panel().setTrackAutomationExpanded(s.doc().getTrackForLane(cutoff->id)->id, true);
        EXPECT_EQ(s.panel().laneHeaderForTest(amount->id), nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the undo broadcast is async
        EXPECT_FALSE(s.mc.isProjectDirty());
    }
    bundle.getParentDirectory().deleteRecursively();
}
