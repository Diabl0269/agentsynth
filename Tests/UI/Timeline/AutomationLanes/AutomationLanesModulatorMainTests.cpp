// AutomationLanesModulatorMainTests.cpp -- a lane's modulators against a real MainComponent: "Add LFO
// modulator" from the lane menu (a real LFO card beside the module, cabled through the normal CV path at half
// depth, one undo step, inside the module's macro), a parameter with no CV jack, a cable patched by hand
// showing up as a row, and "Remove modulator". The row's controls are in AutomationLanesModulatorEditTests.cpp.

#include "AutomationLanesModulatorFixture.h"

using namespace modulator_test;
using namespace automation_lanes_test;

TEST_F(TimelinePanelIntegrationTest, AddLfoModulatorCreatesAnLfoBesideTheModuleCabledAtHalfDepthAsOneUndoStep) {
    Scene s;
    const int raw = s.channelFor("cutoff");
    ASSERT_GE(raw, 0) << "Filter cutoff has a CV jack";
    ASSERT_TRUE(s.nodesOf<LFOModule>().empty());

    s.addLfoFromLaneMenu();

    const auto lfos = s.nodesOf<LFOModule>();
    ASSERT_EQ(lfos.size(), 1u);
    const auto lfoUuid = lfos.front()->properties["uuid"].toString();
    EXPECT_TRUE(lfoUuid.isNotEmpty());
    const auto chains = s.chainsInto(lfoUuid, raw);
    ASSERT_EQ(chains.size(), 1u) << "LFO -> attenuverter -> cutoff CV";
    EXPECT_FLOAT_EQ(s.depthOf(chains.front()), 0.5f);
    EXPECT_TRUE(s.graph().getNodeForId(chains.front().attenuverterNodeID) != nullptr);
    EXPECT_TRUE(s.graph().getNodeForId(chains.front().attenuverterNodeID)->properties["uuid"].toString().isNotEmpty());

    // Defaults: Sine, synced at 1/4, bipolar, full level.
    EXPECT_FLOAT_EQ(s.parameter(lfoUuid, "shape"), 0.0f);
    EXPECT_FLOAT_EQ(s.parameter(lfoUuid, "mode"), 1.0f);
    EXPECT_FLOAT_EQ(s.parameter(lfoUuid, "rateSync"), 5.0f);
    EXPECT_FLOAT_EQ(s.parameter(lfoUuid, "bipolar"), 1.0f);
    EXPECT_FLOAT_EQ(s.parameter(lfoUuid, "level"), 1.0f);

    // Its card sits clear of every other card.
    auto* card = s.cardFor(lfos.front()->nodeID);
    ASSERT_NE(card, nullptr);
    for (auto* other : s.mc.getGraphEditor().getModuleComponents())
        if (other != nullptr && other != card && other->isVisible())
            EXPECT_FALSE(card->getBounds().intersects(other->getBounds())) << other->getName();

    // The lane shows it.
    auto* row = s.row();
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->getInfo().isLfo);
    EXPECT_EQ(row->getInfo().sourceUuid, lfoUuid);

    // One undo takes the LFO, the routing and the attenuverter; one redo brings all of it back.
    ASSERT_TRUE(s.undo().undo());
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
    EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), s.baseAttenuverters);
    EXPECT_NE(s.byUuid(s.targetUuid), nullptr) << "the module itself stays";
    EXPECT_EQ(s.row(), nullptr) << "the row follows the graph";
    ASSERT_TRUE(s.undo().redo());
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    const auto again = s.chainsInto(lfoUuid, raw);
    ASSERT_EQ(again.size(), 1u);
    EXPECT_FLOAT_EQ(s.depthOf(again.front()), 0.5f);
    EXPECT_NE(s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, AddLfoModulatorOnAMacroMemberPutsTheLfoInThatMacro) {
    Scene s;
    synth::Macro macro;
    macro.name = "Tone";
    macro.collapsed = false;
    macro.bounds = {300, 300, 400, 300};
    macro.members.push_back(s.targetUuid);
    const auto macroId = s.mc.getGraphEditor().getMacros().add(macro);
    s.mc.getGraphEditor().updateComponents();

    s.addLfoFromLaneMenu();

    const auto lfos = s.nodesOf<LFOModule>();
    ASSERT_EQ(lfos.size(), 1u);
    const auto lfoUuid = lfos.front()->properties["uuid"].toString();
    auto& macros = s.mc.getGraphEditor().getMacros();
    const auto* owner = macros.findByMember(lfoUuid);
    ASSERT_NE(owner, nullptr) << "the LFO joined a macro";
    EXPECT_EQ(owner->id, macroId);
    EXPECT_TRUE(owner->ports.empty()) << "the cable is inside the macro: no port for it";
    EXPECT_EQ(s.chainsInto(lfoUuid, s.channelFor("cutoff")).size(), 1u);

    ASSERT_TRUE(s.undo().undo());
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
    const auto* restored = macros.find(macroId);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->members, std::vector<juce::String>{s.targetUuid}) << "membership undone in the same step";
}

TEST_F(TimelinePanelIntegrationTest, AParameterWithNoCvJackCannotBeModulated) {
    Scene s("ADSR", "attackCurve");
    EXPECT_LT(s.channelFor("attackCurve"), 0);
    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    const auto menu = header->buildMenu();
    juce::PopupMenu::MenuItemIterator it(menu, true);
    const juce::PopupMenu::Item* add = nullptr;
    while (it.next())
        if (it.getItem().itemID == synth::ui::AutomationLaneHeaderComponent::kAddModulatorMenuId)
            add = &it.getItem();
    ASSERT_NE(add, nullptr);
    EXPECT_FALSE(add->isEnabled);

    s.addLfoFromLaneMenu(); // even if a stale menu sent it
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
}

TEST_F(TimelinePanelIntegrationTest, AnLfoCablePatchedByHandShowsUpAsAModulatorRow) {
    Scene s;
    auto lfo = addNodeWithUuid(s.mc, "LFO");
    ASSERT_NE(lfo, nullptr);
    s.mc.getGraphEditor().updateComponents();
    ASSERT_EQ(s.row(), nullptr);

    // The cable a drag onto the cutoff jack makes, recorded like one.
    const int raw = s.channelFor("cutoff");
    auto* filter = dynamic_cast<ModuleBase*>(s.target->getProcessor());
    s.mc.getGraphEditor().connectPorts(lfo->nodeID, 0, s.target->nodeID, filter->mapInputChannel(raw).visibleJackIndex,
                                       false, true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the undo broadcast is async

    auto* row = s.row();
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->getInfo().isLfo);
    EXPECT_EQ(row->getInfo().sourceUuid, lfo->properties["uuid"].toString());
    EXPECT_TRUE(row->getInfo().attenuverterUuid.isNotEmpty()) << "a hand-drawn CV cable has its depth too";
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorTakesTheRoutingAndTheLoneLfoInOneUndoStep) {
    Scene s;
    s.addLfoFromLaneMenu();
    const auto lfoUuid = s.nodesOf<LFOModule>().front()->properties["uuid"].toString();
    auto* row = s.row();
    ASSERT_NE(row, nullptr);

    row->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId); // destroys the row

    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
    EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), s.baseAttenuverters);
    EXPECT_EQ(s.row(), nullptr);
    ASSERT_TRUE(s.undo().undo());
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_EQ(s.chainsInto(lfoUuid, s.channelFor("cutoff")).size(), 1u);
    EXPECT_NE(s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorKeepsAnLfoThatStillDrivesSomethingElse) {
    Scene s;
    s.addLfoFromLaneMenu();
    auto* lfo = s.nodesOf<LFOModule>().front();
    const auto lfoUuid = lfo->properties["uuid"].toString();
    auto other = addNodeWithUuid(s.mc, "Filter");
    auto* otherFilter = dynamic_cast<ModuleBase*>(other->getProcessor());
    const int otherRaw = s.mc.getGraphEditor().modulationChannelFor(other->nodeID, "resonance");
    ASSERT_GE(otherRaw, 0);
    s.mc.getGraphEditor().connectPorts(lfo->nodeID, 0, other->nodeID,
                                       otherFilter->mapInputChannel(otherRaw).visibleJackIndex, false, false);
    s.mc.getGraphEditor().updateComponents();

    s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "the LFO still drives the other filter";
    EXPECT_TRUE(s.chainsInto(lfoUuid, s.channelFor("cutoff")).empty());
    EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), s.baseAttenuverters + 1) << "only the other routing is left";
    EXPECT_EQ(s.row(), nullptr);
}
