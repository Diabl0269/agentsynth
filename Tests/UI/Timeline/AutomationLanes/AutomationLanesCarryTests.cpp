// AutomationLanesCarryTests.cpp -- a lane's modulators following it: "Change parameter..." moves every modulator
// routing (and its amount lane, points untouched) onto the new parameter's CV jack, "Duplicate" gives the copy its own
// routings with copies of the amount lanes while the original keeps its own, each in ONE graph + timeline undo step; a
// parameter with no CV jack leaves the modulators where they are; and a pick in the real picker reaches the host call.

#include "AutomationLanesAmountFixture.h"
#include "AutomationLanesMenuFixture.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

using namespace amount_test;
using Header = synth::ui::AutomationLaneHeaderComponent;

namespace {

// A Cutoff lane carrying an LFO and an ADSR, each with an amount lane holding drawn points.
struct CarryScene : AmountScene {
    juce::String adsrUuid;
    juce::String adsrAttenUuid;
    std::vector<synth::AutomationLane::Breakpoint> lfoPoints{{0.0, 0.25, 0.0f, kHold}, {4.0, -0.5, 0.0f, kHold}};
    std::vector<synth::AutomationLane::Breakpoint> adsrPoints{{0.0, 0.75, 0.0f, kHold}, {8.0, 0.1, 0.0f, kHold}};
    float lfoDepth = 0.0f;
    float adsrDepth = 0.4f;

    CarryScene() {
        // The track must play the Filter (Track In -> Oscillator -> Filter) for its pickers to offer the Filter's
        // other parameters.
        auto osc = addNodeWithUuid(mc, "Oscillator");
        for (auto* node : graph().getNodes())
            if (node->properties["uuid"].toString() == doc().getTracks().front().bindingUuid) {
                EXPECT_TRUE(graph().addConnection({{node->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                                   {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
                break;
            }
        EXPECT_TRUE(graph().addConnection({{osc->nodeID, 0}, {Scene::target->nodeID, 0}}));
        auto adsr = addNodeWithUuid(mc, "ADSR");
        adsrUuid = adsr->properties["uuid"].toString();
        mc.getGraphEditor().updateComponents();
        const auto cutoff = channelFor("cutoff");
        const auto atten = mc.getGraphEditor().connectModulationSource(adsr->nodeID, 0, Scene::target->nodeID, cutoff,
                                                                       adsrDepth, /*recordUndo=*/false);
        adsrAttenUuid = uuidOfNode(atten);
        lfoDepth = depthOf(chainsInto(lfoUuid, cutoff).front());
        undo().recordTimelineChange(doc(), [&] {
            synth::ui::writeAmountLane(doc(), track, attenUuid, lfoPoints);
            synth::ui::writeAmountLane(doc(), track, adsrAttenUuid, adsrPoints);
        });
        undo().clearUndoHistory();
    }

    juce::String uuidOfNode(juce::AudioProcessorGraph::NodeID id) {
        return synth::AIStateMapper::ensureNodeUuid(graph().getNodeForId(id));
    }
    // The Attenuverter uuid of the one routing from `source` into the target's `paramId` jack ("" when not exactly
    // one).
    juce::String attenInto(const juce::String& source, const juce::String& paramId) {
        const auto chains = chainsInto(source, channelFor(paramId));
        return chains.size() == 1 ? uuidOfNode(chains.front().attenuverterNodeID) : juce::String();
    }
    size_t routingsInto(const juce::String& paramId) {
        return chainsInto(lfoUuid, channelFor(paramId)).size() + chainsInto(adsrUuid, channelFor(paramId)).size();
    }
    // The picker of the lane header's menu item, a row chosen by its text.
    bool pick(int menuId, const juce::String& rowText) {
        lane_menu_test::PickerCapture capture;
        panel().laneHeaderForTest(lane)->applyMenuChoice(menuId);
        if (capture.picker == nullptr)
            return false;
        const auto texts = capture.picker->getVisibleItemTextsForTest();
        for (int i = 0; i < (int)texts.size(); ++i)
            if (texts[(size_t)i] == rowText) {
                capture.picker->chooseVisibleItemForTest(i);
                return true;
            }
        ADD_FAILURE() << "rows: " << juce::StringArray(texts.data(), (int)texts.size()).joinIntoString(", ");
        return false;
    }
    void expectPoints(const synth::AutomationLane* amount, const std::vector<synth::AutomationLane::Breakpoint>& want) {
        ASSERT_NE(amount, nullptr);
        ASSERT_EQ(amount->points.size(), want.size());
        for (size_t i = 0; i < want.size(); ++i) {
            EXPECT_DOUBLE_EQ(amount->points[i].beat, want[i].beat);
            EXPECT_DOUBLE_EQ(amount->points[i].value, want[i].value);
        }
    }
};

bool hasRow(const juce::StringArray& texts, const juce::String& text) { return texts.contains(text); }
} // namespace

TEST_F(TimelinePanelIntegrationTest, ChangeParameterMovesBothModulatorsWithTheirAmountLanesInOneUndoStep) {
    CarryScene s;
    const auto cutoff = s.channelFor("cutoff");
    const auto resonance = s.channelFor("resonance");
    const auto lfoLane = s.amountLane()->id;
    const auto adsrLane = synth::ui::amountLaneFor(s.doc(), s.adsrAttenUuid)->id;
    const auto oldLfoAtten = s.attenUuid;
    ASSERT_EQ(s.routingsInto("cutoff"), 2u);

    ASSERT_TRUE(s.pick(Header::kChangeParameterMenuId, "Resonance"));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_EQ(s.doc().getLane(s.lane)->paramId, "resonance");
    EXPECT_EQ(s.chainsInto(s.lfoUuid, resonance).size(), 1u);
    EXPECT_EQ(s.chainsInto(s.adsrUuid, resonance).size(), 1u);
    EXPECT_EQ(s.routingsInto("cutoff"), 0u) << "none stays on the old parameter";
    EXPECT_FLOAT_EQ(s.depthOf(s.chainsInto(s.lfoUuid, resonance).front()), s.lfoDepth);
    EXPECT_FLOAT_EQ(s.depthOf(s.chainsInto(s.adsrUuid, resonance).front()), s.adsrDepth);

    // Each amount lane is the same lane (id, track, points), keyed to the new Attenuverter.
    const auto newLfoAtten = s.attenInto(s.lfoUuid, "resonance");
    const auto newAdsrAtten = s.attenInto(s.adsrUuid, "resonance");
    ASSERT_TRUE(newLfoAtten.isNotEmpty() && newAdsrAtten.isNotEmpty());
    const auto* lfoAmount = synth::ui::amountLaneFor(s.doc(), newLfoAtten);
    const auto* adsrAmount = synth::ui::amountLaneFor(s.doc(), newAdsrAtten);
    ASSERT_NE(lfoAmount, nullptr);
    ASSERT_NE(adsrAmount, nullptr);
    EXPECT_EQ(lfoAmount->id, lfoLane);
    EXPECT_EQ(adsrAmount->id, adsrLane);
    s.expectPoints(lfoAmount, s.lfoPoints);
    s.expectPoints(adsrAmount, s.adsrPoints);
    EXPECT_EQ(synth::ui::amountLaneFor(s.doc(), oldLfoAtten), nullptr);

    // ONE undo restores the routings, the lane's target and the keys; one redo re-applies them.
    ASSERT_TRUE(s.undo().undo());
    EXPECT_FALSE(s.undo().canUndo()) << "nothing else was recorded";
    EXPECT_EQ(s.doc().getLane(s.lane)->paramId, "cutoff");
    EXPECT_EQ(s.chainsInto(s.lfoUuid, cutoff).size(), 1u);
    EXPECT_EQ(s.chainsInto(s.adsrUuid, cutoff).size(), 1u);
    EXPECT_EQ(s.routingsInto("resonance"), 0u);
    s.expectPoints(synth::ui::amountLaneFor(s.doc(), s.attenInto(s.lfoUuid, "cutoff")), s.lfoPoints);
    s.expectPoints(synth::ui::amountLaneFor(s.doc(), s.attenInto(s.adsrUuid, "cutoff")), s.adsrPoints);
    EXPECT_EQ(synth::ui::amountLaneFor(s.doc(), newLfoAtten), nullptr);

    ASSERT_TRUE(s.undo().redo());
    EXPECT_EQ(s.doc().getLane(s.lane)->paramId, "resonance");
    EXPECT_EQ(s.chainsInto(s.lfoUuid, resonance).size(), 1u);
    EXPECT_EQ(s.chainsInto(s.adsrUuid, resonance).size(), 1u);
    EXPECT_EQ(s.routingsInto("cutoff"), 0u);
    s.expectPoints(synth::ui::amountLaneFor(s.doc(), s.attenInto(s.lfoUuid, "resonance")), s.lfoPoints);
}

TEST_F(TimelinePanelIntegrationTest, DuplicateGivesTheCopyItsOwnModulatorsAndLeavesTheOriginalAlone) {
    CarryScene s;
    const auto cutoff = s.channelFor("cutoff");
    const auto drive = s.channelFor("drive");
    const auto lfoAtten = s.attenInto(s.lfoUuid, "cutoff");
    const auto adsrAtten = s.attenInto(s.adsrUuid, "cutoff");
    const auto lanesBefore = s.doc().getTrack(s.track)->lanes.size();
    const auto nodesBefore = s.graph().getNodes().size();

    ASSERT_TRUE(s.pick(Header::kDuplicateMenuId, "Drive"));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    const auto* copy = s.doc().getLaneForParam(s.targetUuid, "drive");
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->id, s.lane);
    EXPECT_EQ(s.chainsInto(s.lfoUuid, drive).size(), 1u);
    EXPECT_EQ(s.chainsInto(s.adsrUuid, drive).size(), 1u);
    EXPECT_FLOAT_EQ(s.depthOf(s.chainsInto(s.lfoUuid, drive).front()), s.lfoDepth);
    EXPECT_FLOAT_EQ(s.depthOf(s.chainsInto(s.adsrUuid, drive).front()), s.adsrDepth);

    // The copy's amount lanes are new lanes with the same points; the original's are untouched.
    const auto* copyLfo = synth::ui::amountLaneFor(s.doc(), s.attenInto(s.lfoUuid, "drive"));
    const auto* copyAdsr = synth::ui::amountLaneFor(s.doc(), s.attenInto(s.adsrUuid, "drive"));
    s.expectPoints(copyLfo, s.lfoPoints);
    s.expectPoints(copyAdsr, s.adsrPoints);
    EXPECT_EQ(s.attenInto(s.lfoUuid, "cutoff"), lfoAtten);
    EXPECT_EQ(s.attenInto(s.adsrUuid, "cutoff"), adsrAtten);
    EXPECT_EQ(s.chainsInto(s.lfoUuid, cutoff).size(), 1u);
    EXPECT_EQ(s.chainsInto(s.adsrUuid, cutoff).size(), 1u);
    s.expectPoints(synth::ui::amountLaneFor(s.doc(), lfoAtten), s.lfoPoints);
    s.expectPoints(synth::ui::amountLaneFor(s.doc(), adsrAtten), s.adsrPoints);
    EXPECT_EQ(s.doc().getLane(s.lane)->paramId, "cutoff");

    // ONE undo removes the copy, its routings and its amount lanes.
    ASSERT_TRUE(s.undo().undo());
    EXPECT_FALSE(s.undo().canUndo());
    EXPECT_EQ(s.doc().getLaneForParam(s.targetUuid, "drive"), nullptr);
    EXPECT_EQ(s.routingsInto("drive"), 0u);
    EXPECT_EQ(s.doc().getTrack(s.track)->lanes.size(), lanesBefore);
    EXPECT_EQ(s.graph().getNodes().size(), nodesBefore);
    EXPECT_EQ(s.routingsInto("cutoff"), 2u);
    s.expectPoints(synth::ui::amountLaneFor(s.doc(), s.attenInto(s.lfoUuid, "cutoff")), s.lfoPoints);
}

TEST_F(TimelinePanelIntegrationTest, AParameterWithNoCvJackTakesTheLaneButLeavesTheModulatorsOnTheOldParameter) {
    CarryScene s;
    const auto lfoAtten = s.attenInto(s.lfoUuid, "cutoff");

    // The envelope's Attack Curve has no CV jack (checked below once the lane is on it).
    ASSERT_TRUE(s.pick(Header::kChangeParameterMenuId, "Attack Curve"));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    const auto* moved = s.doc().getLane(s.lane);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->nodeUuid, s.adsrUuid);
    EXPECT_LT(s.mc.getGraphEditor().modulationChannelFor(s.byUuid(s.adsrUuid)->nodeID, moved->paramId), 0)
        << "the premise: the new parameter has no CV jack";
    EXPECT_EQ(s.routingsInto("cutoff"), 2u);
    EXPECT_EQ(s.attenInto(s.lfoUuid, "cutoff"), lfoAtten);
    s.expectPoints(synth::ui::amountLaneFor(s.doc(), lfoAtten), s.lfoPoints);
    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.doc().getLane(s.lane)->paramId, "cutoff");
    EXPECT_EQ(s.routingsInto("cutoff"), 2u);
}

TEST_F(TimelinePanelIntegrationTest, TheDriveRowIsOfferedByTheRealPickerForALaneWithModulators) {
    CarryScene s;
    lane_menu_test::PickerCapture capture;
    s.panel().laneHeaderForTest(s.lane)->applyMenuChoice(Header::kDuplicateMenuId);
    ASSERT_NE(capture.picker, nullptr);
    const auto texts = capture.picker->getVisibleItemTextsForTest();
    juce::StringArray names;
    for (const auto& t : texts)
        names.add(t);
    EXPECT_TRUE(hasRow(names, "Drive"));
    EXPECT_TRUE(hasRow(names, "Resonance"));
    EXPECT_FALSE(hasRow(names, "Cutoff")) << "this lane's own parameter is never offered";
}

namespace {
// A host that answers both lane calls itself, to see that a pick in the real picker reaches them.
struct CarryHost : lane_menu_test::PickHost {
    int retargets = 0;
    int duplicates = 0;
    synth::ui::LaneTarget lastTarget;
    synth::LaneId lastLane;
    bool retargetLaneWithAttachments(synth::LaneId lane, const synth::ui::LaneTarget& target) override {
        ++retargets;
        lastLane = lane;
        lastTarget = target;
        return true;
    }
    synth::LaneId duplicateLaneWithAttachments(synth::LaneId lane, const synth::ui::LaneTarget& target) override {
        ++duplicates;
        lastLane = lane;
        lastTarget = target;
        return lane;
    }
};
} // namespace

TEST(AutomationLanesCarryTest, APickInTheRealPickerReachesTheHostCallAndTheDocIsLeftToIt) {
    lane_menu_test::MenuPanel f;
    CarryHost host;
    host.offered = f.host.offered;
    host.ranges = f.host.ranges;
    f.panel.setTrackHeaderHost(&host);
    const auto lanesBefore = f.doc.getTrack(f.track)->lanes.size();
    {
        lane_menu_test::PickerCapture capture;
        f.header(f.lane)->applyMenuChoice(Header::kChangeParameterMenuId);
        ASSERT_NE(capture.picker, nullptr);
        capture.picker->chooseVisibleItemForTest(0); // Resonance (Cutoff is this lane's own)
    }
    EXPECT_EQ(host.retargets, 1);
    EXPECT_EQ(host.lastLane, f.lane);
    EXPECT_EQ(host.lastTarget.paramId, "res");
    EXPECT_EQ(f.laneOf(f.lane).paramId, "cutoff") << "the host owns the edit, so the doc-only fallback did not run";
    {
        lane_menu_test::PickerCapture capture;
        f.header(f.lane)->applyMenuChoice(Header::kDuplicateMenuId);
        ASSERT_NE(capture.picker, nullptr);
        capture.picker->chooseVisibleItemForTest(1); // Detune
    }
    EXPECT_EQ(host.duplicates, 1);
    EXPECT_EQ(host.lastTarget.paramId, "detune");
    EXPECT_EQ(f.doc.getTrack(f.track)->lanes.size(), lanesBefore);
    f.panel.setTrackHeaderHost(&f.host);
}
