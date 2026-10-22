// AutomationLanesRemoveLastSourceTests.cpp -- removing the LAST modulator an LFO drives, against a real
// MainComponent: the question offers Keep module next to Remove LFO, "Remember my choice" stores the answer, and a
// double-click on the cable asks exactly what the timeline row's Remove modulator asks.

#include "AutomationLanesModulatorFixture.h"

#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

using namespace modulator_test;

namespace {

constexpr const char* kAsk = "timelineAskBeforeRemovingLfo";
constexpr const char* kChoice = "timelineRemoveLastSourceChoice";

// A double-click on the canvas at the middle of the LFO's modulation cable, through GraphEditor::mouseDoubleClick.
void doubleClickTheCable(Scene& s) {
    auto& editor = s.mc.getGraphEditor();
    editor.timerCallback(); // the cable list follows the routing cache
    const auto lfoId = s.nodesOf<LFOModule>().front()->nodeID.uid;
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain && cable.id.srcUid == lfoId) {
            const auto mid = ((cable.p1 + cable.p2) / 2.0f).roundToInt();
            const auto at = editor.getLocalPoint(editor.getChildComponent(0), mid).toFloat();
            const juce::ModifierKeys left(juce::ModifierKeys::leftButtonModifier);
            editor.mouseDoubleClick(juce::MouseEvent(
                juce::Desktop::getInstance().getMainMouseSource(), at, left, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &editor,
                &editor, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 2, false));
            return;
        }
    FAIL() << "no modulation cable on the canvas";
}

juce::PropertiesFile& settingsOf(Scene& s) { return *s.mc.getAppPropertiesForTest().getUserSettings(); }

} // namespace

TEST_F(TimelinePanelIntegrationTest, KeepModuleRemovesOnlyTheCableAndOneUndoBringsItBack) {
    Scene s;
    s.addLfoFromLaneMenu();
    const auto lfoUuid = s.nodesOf<LFOModule>().front()->properties["uuid"].toString();
    s.confirmKeep = true;

    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_EQ(s.confirmAsked, 1);
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "Keep module leaves the LFO on the canvas";
    EXPECT_TRUE(s.chainsInto(lfoUuid, s.channelFor("cutoff")).empty()) << "the cable is gone";
    EXPECT_EQ(s.row(), nullptr);
    EXPECT_TRUE(settingsOf(s).getBoolValue(kAsk, true)) << "without the box ticked the question stays on";
    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.chainsInto(lfoUuid, s.channelFor("cutoff")).size(), 1u) << "one undo restores the cable";
}

TEST_F(TimelinePanelIntegrationTest, RememberMyChoiceKeepStoresItAndTheNextRemovalDoesNotAsk) {
    Scene s;
    s.addLfoFromLaneMenu();
    s.confirmKeep = true;
    s.confirmDontAsk = true;
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_FALSE(settingsOf(s).getBoolValue(kAsk, true));
    EXPECT_EQ(settingsOf(s).getValue(kChoice), "keep");

    s.addLfoFromLaneMenu();
    ASSERT_NE(s.row(), nullptr);
    const auto asked = s.confirmAsked;
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_EQ(s.confirmAsked, asked) << "the remembered choice stands";
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 2u) << "both LFOs are still there: the answer was keep";
}

TEST_F(TimelinePanelIntegrationTest, RememberMyChoiceRemoveStillDeletesTheLfoWithoutAsking) {
    Scene s;
    s.addLfoFromLaneMenu();
    s.confirmDontAsk = true;
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_EQ(settingsOf(s).getValue(kChoice), "remove");
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());

    s.addLfoFromLaneMenu();
    const auto asked = s.confirmAsked;
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_EQ(s.confirmAsked, asked);
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
}

TEST_F(TimelinePanelIntegrationTest, ACancelNeverStoresTheRememberedChoice) {
    Scene s;
    s.addLfoFromLaneMenu();
    s.confirmAnswer = false;
    s.confirmDontAsk = true;
    s.confirmKeep = true;
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_FALSE(settingsOf(s).containsKey(kChoice));
    EXPECT_TRUE(settingsOf(s).getBoolValue(kAsk, true));
}

// Regression test for FRO792: a double-click on the cable cut the chain without the question the timeline row asks,
// and had no way to keep the module.
TEST_F(TimelinePanelIntegrationTest, DoubleClickingTheLastCableAsksTheSameQuestionAsTheRow) {
    Scene s;
    s.addLfoFromLaneMenu();
    s.confirmAnswer = false;
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    const auto fromRow = s.lastConfirm;
    ASSERT_EQ(s.confirmAsked, 1);

    doubleClickTheCable(s);

    ASSERT_EQ(s.confirmAsked, 2) << "the cable double-click asks too";
    EXPECT_EQ(s.lastConfirm.title, fromRow.title);
    EXPECT_EQ(s.lastConfirm.message, fromRow.message);
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "Cancel changes nothing";
    EXPECT_NE(s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, DoubleClickingTheLastCableCanKeepOrRemoveTheModule) {
    {
        Scene s;
        s.addLfoFromLaneMenu();
        s.confirmKeep = true;
        doubleClickTheCable(s);
        EXPECT_EQ(s.confirmAsked, 1);
        EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u);
        EXPECT_EQ(s.row(), nullptr) << "the cable is gone";
    }
    {
        Scene s;
        s.addLfoFromLaneMenu();
        doubleClickTheCable(s);
        EXPECT_EQ(s.confirmAsked, 1);
        EXPECT_TRUE(s.nodesOf<LFOModule>().empty()) << "Remove LFO deletes it as the row does";
        ASSERT_TRUE(s.undo().undo());
        EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "one undo step";
    }
}

TEST_F(TimelinePanelIntegrationTest, ACableDoubleClickWithAnotherDestinationLeftNeverAsks) {
    Scene s;
    s.addLfoFromLaneMenu();
    const auto lfoUuid = s.nodesOf<LFOModule>().front()->properties["uuid"].toString();
    auto other = addNodeWithUuid(s.mc, "Filter");
    s.mc.getGraphEditor().updateComponents();
    ASSERT_TRUE(s.mc.getGraphEditor().connectExistingLfoModulator(s.byUuid(lfoUuid)->nodeID, other->nodeID, "cutoff"));

    doubleClickTheCable(s);

    EXPECT_EQ(s.confirmAsked, 0);
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u);
}
