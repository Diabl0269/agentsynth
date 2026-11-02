// AutomationLanesAddModulatorTests.cpp -- a lane's "Add modulator..." picker and "Remove modulator" against a
// real MainComponent: the picker's rows (the New group first, "New LFO" and "New envelope", then every LFO and
// envelope with where it lives and what it moves, one that already moves this parameter greyed), picking a new
// envelope or an existing LFO or envelope (one undo step, through macro ports across macros), the same source types
// as the Change source picker (the sibling-gap guard), and the confirm before a removal that would also delete the LFO.

#include "AutomationLanesModulatorFixture.h"

#include "Modules/ADSRModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

using namespace modulator_test;
using namespace automation_lanes_test;

namespace {
using synth::ui::ModMatrixPicker;

// A spare filter whose cutoff an LFO already drives, inside a macro named "Pads" (the LFO joins it).
void lfoInsideMacroDrivingAnotherFilter(Scene& s, const juce::String& macroName) {
    auto other = addNodeWithUuid(s.mc, "Filter");
    const auto otherUuid = other->properties["uuid"].toString();
    synth::Macro macro;
    macro.name = macroName;
    macro.collapsed = false;
    macro.bounds = {900, 700, 400, 300};
    macro.members.push_back(otherUuid);
    s.mc.getGraphEditor().getMacros().add(macro);
    s.mc.getGraphEditor().updateComponents();
    const auto id = s.mc.getGraphEditor().addLfoModulator(other->nodeID, "cutoff");
    ASSERT_NE(s.graph().getNodeForId(id), nullptr);
}

void press(ModMatrixPicker& picker, int keyCode) { picker.sendKeyForTest(juce::KeyPress(keyCode)); }
} // namespace

TEST_F(TimelinePanelIntegrationTest, AddModulatorPickerListsTheNewGroupThenEveryLfoWithWhereItLivesAndWhatItMoves) {
    Scene s;
    lfoInsideMacroDrivingAnotherFilter(s, "Pads");
    addNodeWithUuid(s.mc, "LFO");
    s.mc.getGraphEditor().updateComponents();

    auto picker = s.openAddModulatorPicker();
    ASSERT_NE(picker, nullptr);
    const auto texts = picker->getVisibleItemTextsForTest();
    const auto details = picker->getVisibleItemDetailsForTest();
    ASSERT_EQ(texts.size(), 4u + s.nodesOf<ADSRModule>().size())
        << "New LFO, New envelope, the macro's LFO, the spare one, then the default project's envelopes";
    EXPECT_EQ(texts[0], "New LFO");
    EXPECT_EQ(texts[1], "New envelope");
    EXPECT_TRUE(texts[2].contains("inside macro Pads")) << texts[2];
    EXPECT_FALSE(texts[3].contains("inside macro")) << texts[3];
    EXPECT_TRUE(details[2].startsWith("moves ")) << details[2];
    EXPECT_TRUE(details[2].containsIgnoreCase("cutoff")) << details[2];
    EXPECT_EQ(details[3], "not connected yet");
    for (int i = 0; i < (int)texts.size(); ++i)
        EXPECT_TRUE(picker->isVisibleItemPickableForTest(i));
}

TEST_F(TimelinePanelIntegrationTest, AddModulatorPickerSearchFindsAnLfoByItsMacroAndByWhatItMoves) {
    Scene s;
    lfoInsideMacroDrivingAnotherFilter(s, "Pads");
    addNodeWithUuid(s.mc, "LFO");
    s.mc.getGraphEditor().updateComponents();

    auto picker = s.openAddModulatorPicker();
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest("pads");
    auto texts = picker->getVisibleItemTextsForTest();
    ASSERT_EQ(texts.size(), 3u) << "the New rows stay (first) whatever is typed, then the macro's LFO";
    EXPECT_EQ(texts[0], "New LFO");
    EXPECT_EQ(texts[1], "New envelope");
    EXPECT_TRUE(texts[2].contains("Pads"));

    picker->setSearchTextForTest("cutoff");
    texts = picker->getVisibleItemTextsForTest();
    EXPECT_TRUE(std::any_of(texts.begin(), texts.end(), [](const auto& t) { return t.contains("Pads"); }))
        << "the LFO that moves a cutoff (the default envelopes may also move one)";

    // "env" and "adsr" find the existing envelopes and keep the New envelope row; the LFO drops out.
    for (const char* word : {"env", "adsr"}) {
        picker->setSearchTextForTest(word);
        texts = picker->getVisibleItemTextsForTest();
        ASSERT_GE(texts.size(), 2u) << word;
        EXPECT_EQ(texts[1], "New envelope") << word;
        EXPECT_FALSE(std::any_of(texts.begin(), texts.end(), [](const auto& t) { return t.contains("Pads"); }))
            << word << ": the LFO is not an envelope";
    }
}

TEST_F(TimelinePanelIntegrationTest, AnLfoThatAlreadyMovesThisParameterIsShownDisabledAndCannotBePicked) {
    Scene s;
    s.addLfoFromLaneMenu(); // an LFO on this cutoff
    auto lfos = s.nodesOf<LFOModule>();
    ASSERT_EQ(lfos.size(), 1u);
    addNodeWithUuid(s.mc, "LFO"); // a second one, free
    s.mc.getGraphEditor().updateComponents();

    auto picker = s.openAddModulatorPicker();
    ASSERT_NE(picker, nullptr);
    const auto details = picker->getVisibleItemDetailsForTest();
    ASSERT_GE(details.size(), 4u) << "New LFO, New envelope, the two LFOs, then the envelopes";
    EXPECT_TRUE(details[2].startsWith("already moves ")) << details[2];
    EXPECT_FALSE(picker->isVisibleItemPickableForTest(2));
    EXPECT_TRUE(picker->isVisibleItemPickableForTest(3));

    // Chosen by a click on its row, or by Return: nothing happens.
    picker->chooseVisibleItemForTest(2);
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 2u);
    EXPECT_EQ(s.row(1), nullptr);

    // Down from "New LFO" lands on "New envelope", then on the next pickable row, past the disabled one; Return picks
    // it.
    press(*picker, juce::KeyPress::downKey);
    EXPECT_EQ(picker->getHighlightedItemIndexForTest(), 1);
    press(*picker, juce::KeyPress::downKey);
    EXPECT_EQ(picker->getHighlightedItemIndexForTest(), 3);
    press(*picker, juce::KeyPress::returnKey);
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 2u) << "no new card";
    EXPECT_NE(s.row(1), nullptr) << "the second LFO's row";
}

TEST_F(TimelinePanelIntegrationTest, PickingNewLfoWithTheKeyboardAddsOneBesideTheModule) {
    Scene s;
    auto picker = s.openAddModulatorPicker();
    ASSERT_NE(picker, nullptr);
    EXPECT_EQ(picker->getHighlightedItemIndexForTest(), 0);
    press(*picker, juce::KeyPress::returnKey);
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_NE(s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, PickingAnExistingLfoWiresItInAsOneUndoStepWithoutANewCard) {
    Scene s;
    const int raw = s.channelFor("cutoff");
    auto spare = addNodeWithUuid(s.mc, "LFO");
    const auto lfoUuid = spare->properties["uuid"].toString();
    s.mc.getGraphEditor().updateComponents();
    const auto attenuvertersBefore = s.nodesOf<AttenuverterModule>().size();

    auto picker = s.openAddModulatorPicker();
    ASSERT_NE(picker, nullptr);
    picker->chooseVisibleItemForTest(2);

    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "no new LFO card";
    const auto chains = s.chainsInto(lfoUuid, raw);
    ASSERT_EQ(chains.size(), 1u);
    EXPECT_FLOAT_EQ(s.depthOf(chains.front()), 0.5f);
    EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), attenuvertersBefore + 1);
    ASSERT_NE(s.row(), nullptr);
    EXPECT_EQ(s.row()->getInfo().sourceUuid, lfoUuid);

    ASSERT_TRUE(s.undo().undo());
    EXPECT_TRUE(s.chainsInto(lfoUuid, raw).empty()) << "one Undo takes the routing";
    EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), attenuvertersBefore);
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "the LFO itself was never part of the step";
    EXPECT_EQ(s.row(), nullptr);

    ASSERT_TRUE(s.undo().redo());
    EXPECT_EQ(s.chainsInto(lfoUuid, raw).size(), 1u);
}

TEST_F(TimelinePanelIntegrationTest, PickingNewEnvelopeAddsAnAdsrCardCableAndRowAsOneUndoStep) {
    Scene s;
    const int raw = s.channelFor("cutoff");
    const auto adsrBefore = s.nodesOf<ADSRModule>().size();
    const auto attenuvertersBefore = s.nodesOf<AttenuverterModule>().size();
    const auto connectionsBefore = s.graph().getConnections().size();

    auto picker = s.openAddModulatorPicker();
    ASSERT_NE(picker, nullptr);
    ASSERT_EQ(picker->getVisibleItemTextsForTest()[1], "New envelope");
    picker->chooseVisibleItemForTest(1);

    ASSERT_EQ(s.nodesOf<ADSRModule>().size(), adsrBefore + 1) << "the envelope card";
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty()) << "no LFO was made";
    const auto adsrUuid = s.nodesOf<ADSRModule>().back()->properties["uuid"].toString();
    const auto chains = s.chainsInto(adsrUuid, raw);
    ASSERT_EQ(chains.size(), 1u) << "cabled to the lane's parameter";
    EXPECT_FLOAT_EQ(s.depthOf(chains.front()), 0.5f);
    EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), attenuvertersBefore + 1);
    ASSERT_NE(s.row(), nullptr) << "the mod row under the lane";
    EXPECT_EQ(s.row()->getInfo().sourceUuid, adsrUuid);

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.nodesOf<ADSRModule>().size(), adsrBefore) << "one Undo takes the card";
    EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), attenuvertersBefore);
    EXPECT_EQ(s.graph().getConnections().size(), connectionsBefore) << "and the cable";
    EXPECT_EQ(s.row(), nullptr) << "and the row";

    ASSERT_TRUE(s.undo().redo());
    EXPECT_EQ(s.nodesOf<ADSRModule>().size(), adsrBefore + 1);
    EXPECT_EQ(s.chainsInto(s.nodesOf<ADSRModule>().back()->properties["uuid"].toString(), raw).size(), 1u);
    EXPECT_NE(s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, PickingAnExistingEnvelopeWiresItInWithoutANewCard) {
    Scene s;
    const int raw = s.channelFor("cutoff");
    const auto adsrBefore = s.nodesOf<ADSRModule>().size();
    ASSERT_GE(adsrBefore, 1u) << "the default project's envelopes";

    auto picker = s.openAddModulatorPicker();
    ASSERT_NE(picker, nullptr);
    const auto texts = picker->getVisibleItemTextsForTest();
    ASSERT_EQ(texts.size(), 2u + adsrBefore) << "the New group, then no LFO, then the envelopes";
    picker->chooseVisibleItemForTest(2);

    EXPECT_EQ(s.nodesOf<ADSRModule>().size(), adsrBefore) << "no new card";
    ASSERT_NE(s.row(), nullptr);
    const auto adsrUuid = s.row()->getInfo().sourceUuid;
    ASSERT_NE(dynamic_cast<ADSRModule*>(s.byUuid(adsrUuid)->getProcessor()), nullptr);
    EXPECT_EQ(s.chainsInto(adsrUuid, raw).size(), 1u);

    ASSERT_TRUE(s.undo().undo());
    EXPECT_TRUE(s.chainsInto(adsrUuid, raw).empty());
    EXPECT_EQ(s.nodesOf<ADSRModule>().size(), adsrBefore) << "the envelope itself was never part of the step";
    EXPECT_EQ(s.row(), nullptr);
}

// Sibling-gap guard: the lane's "Add modulator..." and a row's "Change source..." list the same source
// types. Both pickers are opened the way a person opens them; every LFO and envelope output the source catalogue
// knows is a row in both, both hold the same rows, and both offer a New row for each creatable LFO and envelope type.
// A modulator type added to one picker and not the other fails here.
TEST_F(TimelinePanelIntegrationTest, AddModulatorOffersEverySourceTypeChangeSourceOffers) {
    Scene s;
    addNodeWithUuid(s.mc, "ADSR");
    s.addLfoFromLaneMenu(); // a routed LFO, so a row exists to open Change source from
    ASSERT_NE(s.row(), nullptr);
    s.mc.getGraphEditor().updateComponents();

    auto add = s.openAddModulatorPicker();
    ASSERT_NE(add, nullptr);
    std::unique_ptr<ModMatrixPicker> change;
    synth::ui::test_hooks::changeSourcePickerHookForTest() = [&change](auto p) { change = std::move(p); };
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kChangeSourceMenuId);
    synth::ui::test_hooks::changeSourcePickerHookForTest() = nullptr;
    ASSERT_NE(change, nullptr);

    auto sorted = [](ModMatrixPicker& picker) {
        juce::StringArray texts;
        for (const auto& text : picker.getVisibleItemTextsForTest())
            texts.add(text);
        texts.sort(false);
        return texts.joinIntoString("\n");
    };
    EXPECT_EQ(sorted(*add), sorted(*change));

    juce::StringArray texts;
    for (const auto& text : add->getVisibleItemTextsForTest())
        texts.add(text);
    int catalogued = 0;
    for (const auto& item : synth::ui::enumerateModSources(s.graph())) {
        if (item.group != synth::ui::ModSourceGroup::Lfos && item.group != synth::ui::ModSourceGroup::Envelopes)
            continue;
        ++catalogued;
        EXPECT_TRUE(texts.contains(item.label())) << item.label();
    }
    EXPECT_GE(catalogued, 2);
    for (const auto& type : synth::ui::newModuleSources())
        if (type.typeName == "LFO")
            EXPECT_TRUE(texts.contains("New LFO"));
        else if (type.typeName == "ADSR")
            EXPECT_TRUE(texts.contains("New envelope"));
}

TEST_F(TimelinePanelIntegrationTest, PickingAnLfoFromAnotherMacroCablesItThroughMacroPorts) {
    Scene s;
    auto spare = addNodeWithUuid(s.mc, "LFO");
    const auto lfoUuid = spare->properties["uuid"].toString();
    auto& editor = s.mc.getGraphEditor();
    synth::Macro src;
    src.name = "Src";
    src.collapsed = false;
    src.bounds = {100, 700, 400, 300};
    src.members.push_back(lfoUuid);
    editor.getMacros().add(src);
    synth::Macro tone;
    tone.name = "Tone";
    tone.collapsed = false;
    tone.bounds = {700, 100, 400, 300};
    tone.members.push_back(s.targetUuid);
    editor.getMacros().add(tone);
    editor.updateComponents();
    ASSERT_TRUE(editor.getAutoCreateMacroPortsOnDragEnabled());
    const auto connectionsBefore = s.graph().getConnections().size();
    const auto inletsBefore = s.nodesOf<MacroInletModule>().size();
    const auto outletsBefore = s.nodesOf<MacroOutletModule>().size();

    auto picker = s.openAddModulatorPicker();
    ASSERT_NE(picker, nullptr);
    const auto texts = picker->getVisibleItemTextsForTest();
    ASSERT_EQ(texts.size(), 3u + s.nodesOf<ADSRModule>().size());
    EXPECT_TRUE(texts[2].contains("inside macro Src")) << texts[2];
    picker->chooseVisibleItemForTest(2);

    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_GT(s.nodesOf<MacroOutletModule>().size(), outletsBefore) << "an outlet leaves Src";
    EXPECT_GT(s.nodesOf<MacroInletModule>().size(), inletsBefore) << "an inlet enters Tone";
    ASSERT_NE(s.row(), nullptr) << "the lane shows the LFO, ports looked through";
    EXPECT_EQ(s.row()->getInfo().sourceUuid, lfoUuid);
    EXPECT_TRUE(s.row()->getInfo().isLfo);

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.row(), nullptr);
    EXPECT_EQ(s.nodesOf<MacroInletModule>().size(), inletsBefore) << "the ports go with the step";
    EXPECT_EQ(s.nodesOf<MacroOutletModule>().size(), outletsBefore);
    EXPECT_EQ(s.graph().getConnections().size(), connectionsBefore);
}

TEST_F(TimelinePanelIntegrationTest, RemovingASharedLfosRoutingNeverAsksAndKeepsTheLfo) {
    Scene s;
    s.addLfoFromLaneMenu();
    const auto lfoUuid = s.nodesOf<LFOModule>().front()->properties["uuid"].toString();
    auto other = addNodeWithUuid(s.mc, "Filter");
    s.mc.getGraphEditor().updateComponents();
    ASSERT_TRUE(s.mc.getGraphEditor().connectExistingModulator(s.byUuid(lfoUuid)->nodeID, 0, other->nodeID, "cutoff"));
    ASSERT_NE(s.row(), nullptr);

    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_EQ(s.confirmAsked, 0);
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "it still moves the other filter";
    EXPECT_EQ(s.row(), nullptr);
    EXPECT_TRUE(s.chainsInto(lfoUuid, s.channelFor("cutoff")).empty());
}

TEST_F(TimelinePanelIntegrationTest, RemovingAnLfosLastDestinationAsksAndCancelChangesNothing) {
    Scene s;
    s.addLfoFromLaneMenu();
    const auto lfoNode = s.nodesOf<LFOModule>().front();
    const auto lfoUuid = lfoNode->properties["uuid"].toString();
    const auto connections = s.graph().getConnections().size();
    s.confirmAnswer = false;

    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_EQ(s.confirmAsked, 1);
    EXPECT_TRUE(s.lastConfirm.title.startsWith("Remove LFO")) << s.lastConfirm.title;
    EXPECT_TRUE(s.lastConfirm.title.endsWith("?"));
    EXPECT_TRUE(s.lastConfirm.message.startsWith("Cutoff is the last thing ")) << s.lastConfirm.message;
    EXPECT_TRUE(s.lastConfirm.message.contains("also deletes")) << s.lastConfirm.message;
    EXPECT_TRUE(s.lastConfirm.message.contains("Z brings it back")) << s.lastConfirm.message;
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_EQ(s.graph().getConnections().size(), connections);
    ASSERT_NE(s.row(), nullptr);
    EXPECT_EQ(s.row()->getInfo().sourceUuid, lfoUuid);
    EXPECT_TRUE(s.mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("timelineAskBeforeRemovingLfo", true))
        << "cancel leaves the preference alone";
}

TEST_F(TimelinePanelIntegrationTest, ConfirmingRemovesTheLfoAsOneUndoStep) {
    Scene s;
    s.addLfoFromLaneMenu();
    const auto lfoUuid = s.nodesOf<LFOModule>().front()->properties["uuid"].toString();

    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_EQ(s.confirmAsked, 1);
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
    EXPECT_EQ(s.row(), nullptr);

    ASSERT_TRUE(s.undo().undo());
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "one Undo brings the LFO back";
    EXPECT_EQ(s.nodesOf<LFOModule>().front()->properties["uuid"].toString(), lfoUuid);
    ASSERT_NE(s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, DontAskAgainTurnsThePreferenceOffAndTheNextRemovalDoesNotAsk) {
    Scene s;
    auto* settings = s.mc.getAppPropertiesForTest().getUserSettings();
    ASSERT_TRUE(settings->getBoolValue("timelineAskBeforeRemovingLfo", true)) << "default ON";
    s.addLfoFromLaneMenu();
    s.confirmDontAsk = true;
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_EQ(s.confirmAsked, 1);
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
    EXPECT_FALSE(settings->getBoolValue("timelineAskBeforeRemovingLfo", true));

    s.addLfoFromLaneMenu();
    ASSERT_NE(s.row(), nullptr);
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_EQ(s.confirmAsked, 1) << "the second removal goes straight through";
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
}

TEST_F(TimelinePanelIntegrationTest, CancelWithDontAskAgainTickedDoesNotTurnThePreferenceOff) {
    Scene s;
    s.addLfoFromLaneMenu();
    s.confirmAnswer = false;
    s.confirmDontAsk = true;
    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_TRUE(s.mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("timelineAskBeforeRemovingLfo", true));
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u);
}
