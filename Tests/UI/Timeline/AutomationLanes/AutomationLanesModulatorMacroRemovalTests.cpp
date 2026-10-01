// AutomationLanesModulatorMacroRemovalTests.cpp -- "Remove modulator" from a lane row when the LFO reaches
// the parameter through macro ports (an inlet one or two macros deep, the attenuverter before or after the
// port, an LFO inside a macro feeding out through an outlet), when the LFO's own input comes from a macro,
// and when the LFO is shared. Remove follows the cable through the ports, takes the ports left with nothing
// on one side and the lone LFO, and one Undo brings the lot back, macro membership included.

#include "AutomationLanesModulatorFixture.h"

#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"
#include <map>

using namespace modulator_test;
using namespace automation_lanes_test;

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

// A lane over Filter cutoff plus the pieces a macro-port chain is built from by hand. Wiring the graph
// directly (rather than through a cable drag) pins each chain shape exactly.
struct PortRig {
    Scene s;
    NodeID lfo;
    juce::String lfoUuid;
    size_t baseConnections = 0;
    int cutoffRaw = -1;

    PortRig() {
        cutoffRaw = s.channelFor("cutoff");
        baseConnections = s.graph().getConnections().size();
        auto node = addNodeWithUuid(s.mc, "LFO");
        lfo = node->nodeID;
        lfoUuid = node->properties["uuid"].toString();
    }

    GraphEditor& editor() { return s.mc.getGraphEditor(); }
    NodeID target() { return s.byUuid(s.targetUuid)->nodeID; }

    juce::String macroAround(const juce::String& name, const juce::String& memberUuid) {
        synth::Macro macro;
        macro.name = name;
        macro.collapsed = false;
        macro.bounds = {300, 300, 400, 300};
        macro.members.push_back(memberUuid);
        return editor().getMacros().add(macro);
    }

    // A parent macro (holding a spare module so it stays valid) around `childId`.
    juce::String parentAround(const juce::String& childId) {
        const auto spare = addNodeWithUuid(s.mc, "Filter")->properties["uuid"].toString();
        const auto parentId = macroAround("Outer", spare);
        EXPECT_TRUE(editor().getMacros().setParent(childId, parentId));
        return parentId;
    }

    NodeID port(const juce::String& macroId, bool isInput) {
        const auto uuid = editor().getMacroController().addMacroPort(macroId, isInput, synth::MacroPortKind::AudioCV,
                                                                     MacroPortShape::Mono, 1, "P");
        return s.byUuid(uuid)->nodeID;
    }

    void connect(NodeID from, NodeID to, int toChannel = 0) { s.graph().addConnection({{from, 0}, {to, toChannel}}); }

    // source -> hidden attenuverter -> dest, as the routing a modulator row stands for.
    void attenuate(NodeID from, NodeID to, int toChannel) {
        const auto atten = s.mc.getAudioEngine().addModRouting(from, 0, to, toChannel);
        synth::AIStateMapper::ensureNodeUuid(s.graph().getNodeForId(atten));
    }

    void settle() { editor().updateComponents(); }

    struct Snapshot {
        size_t connections = 0, lfos = 0, attens = 0, inlets = 0, outlets = 0;
        std::map<juce::String, std::pair<size_t, size_t>> macros; // name -> (members, ports)
    };

    Snapshot snapshot() {
        Snapshot snap;
        snap.connections = s.graph().getConnections().size();
        snap.lfos = s.nodesOf<LFOModule>().size();
        snap.attens = s.nodesOf<AttenuverterModule>().size();
        snap.inlets = s.nodesOf<MacroInletModule>().size();
        snap.outlets = s.nodesOf<MacroOutletModule>().size();
        for (const auto& m : editor().getMacros().getAll())
            snap.macros[m.name] = {m.members.size(), m.ports.size()};
        return snap;
    }

    static bool menuOffersRemove(synth::ui::ModulatorRow& row) {
        const auto menu = row.buildMenu();
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            if (it.getItem().itemID == synth::ui::ModulatorRow::kRemoveMenuId)
                return true;
        return false;
    }

    // The lane's one row, checked to name the LFO (not a port) and to offer Remove.
    void expectLfoRowWithRemove() {
        auto* row = s.row();
        ASSERT_NE(row, nullptr) << "the lane shows the modulator";
        EXPECT_TRUE(row->getInfo().isLfo);
        EXPECT_EQ(row->getInfo().sourceUuid, lfoUuid);
        EXPECT_TRUE(menuOffersRemove(*row));
    }

    // Remove leaves nothing of the chain; one Undo restores `before` and the row.
    void removeThenUndo(const Snapshot& before) {
        s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
        EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
        EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), s.baseAttenuverters);
        EXPECT_TRUE(s.nodesOf<MacroInletModule>().empty());
        EXPECT_TRUE(s.nodesOf<MacroOutletModule>().empty());
        EXPECT_EQ(s.graph().getConnections().size(), baseConnections) << "no cable left behind";
        EXPECT_EQ(s.row(), nullptr);

        ASSERT_TRUE(s.undo().undo());
        const auto after = snapshot();
        EXPECT_EQ(after.connections, before.connections);
        EXPECT_EQ(after.lfos, before.lfos);
        EXPECT_EQ(after.attens, before.attens);
        EXPECT_EQ(after.inlets, before.inlets);
        EXPECT_EQ(after.outlets, before.outlets);
        EXPECT_EQ(after.macros, before.macros) << "macro membership and ports are restored";
        EXPECT_NE(s.row(), nullptr);
    }
};
} // namespace

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorThroughOneInletWithTheAttenuverterBeforeThePort) {
    PortRig r;
    const auto macroId = r.macroAround("Tone", r.s.targetUuid);
    const auto inlet = r.port(macroId, true);
    r.attenuate(r.lfo, inlet, 0);
    r.connect(inlet, r.target(), r.cutoffRaw);
    r.settle();
    r.expectLfoRowWithRemove();
    r.removeThenUndo(r.snapshot());
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorThroughOneInletWithTheAttenuverterAfterThePort) {
    PortRig r;
    const auto macroId = r.macroAround("Tone", r.s.targetUuid);
    const auto inlet = r.port(macroId, true);
    r.connect(r.lfo, inlet);
    r.attenuate(inlet, r.target(), r.cutoffRaw);
    r.settle();
    r.expectLfoRowWithRemove();
    r.removeThenUndo(r.snapshot());
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorThroughTwoNestedInletsWithTheAttenuverterBeforeThePorts) {
    PortRig r;
    const auto childId = r.macroAround("Inner", r.s.targetUuid);
    const auto parentId = r.parentAround(childId);
    const auto outerIn = r.port(parentId, true);
    const auto innerIn = r.port(childId, true);
    r.attenuate(r.lfo, outerIn, 0);
    r.connect(outerIn, innerIn);
    r.connect(innerIn, r.target(), r.cutoffRaw);
    r.settle();
    r.expectLfoRowWithRemove();
    r.removeThenUndo(r.snapshot());
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorThroughTwoNestedInletsWithTheAttenuverterAfterThePorts) {
    PortRig r;
    const auto childId = r.macroAround("Inner", r.s.targetUuid);
    const auto parentId = r.parentAround(childId);
    const auto outerIn = r.port(parentId, true);
    const auto innerIn = r.port(childId, true);
    r.connect(r.lfo, outerIn);
    r.connect(outerIn, innerIn);
    r.attenuate(innerIn, r.target(), r.cutoffRaw);
    r.settle();
    r.expectLfoRowWithRemove();
    r.removeThenUndo(r.snapshot());
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorOfAnLfoInsideAMacroThatFeedsOutThroughAnOutlet) {
    PortRig r;
    const auto macroId = r.macroAround("Src", r.lfoUuid);
    const auto outlet = r.port(macroId, false);
    r.connect(r.lfo, outlet);
    r.attenuate(outlet, r.target(), r.cutoffRaw);
    r.settle();
    r.expectLfoRowWithRemove();
    // The macro held the LFO and its outlet; with both gone it dissolves, and Undo brings it back.
    r.s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);
    EXPECT_TRUE(r.s.nodesOf<LFOModule>().empty());
    EXPECT_TRUE(r.s.nodesOf<MacroOutletModule>().empty());
    EXPECT_EQ(r.s.nodesOf<AttenuverterModule>().size(), r.s.baseAttenuverters);
    EXPECT_EQ(r.s.graph().getConnections().size(), r.baseConnections);
    EXPECT_TRUE(r.editor().getMacros().empty());
    ASSERT_TRUE(r.s.undo().undo());
    ASSERT_EQ(r.s.nodesOf<LFOModule>().size(), 1u);
    ASSERT_NE(r.editor().getMacros().findByMember(r.lfoUuid), nullptr);
    EXPECT_EQ(r.editor().getMacros().findByMember(r.lfoUuid)->ports.size(), 1u);
    EXPECT_NE(r.s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorCleansPortsEvenWhenAutoDeleteOfPortsIsOff) {
    PortRig r;
    r.editor().setAutoDeleteMacroPortsOnLastCableEnabled(false);
    const auto childId = r.macroAround("Inner", r.s.targetUuid);
    const auto parentId = r.parentAround(childId);
    const auto outerIn = r.port(parentId, true);
    const auto innerIn = r.port(childId, true);
    r.attenuate(r.lfo, outerIn, 0);
    r.connect(outerIn, innerIn);
    r.connect(innerIn, r.target(), r.cutoffRaw);
    r.settle();
    r.expectLfoRowWithRemove();
    r.removeThenUndo(r.snapshot());
    EXPECT_FALSE(r.editor().getAutoDeleteMacroPortsOnLastCableEnabled()) << "the preference itself is untouched";
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorKeepsAnLfoAndItsOtherChainThroughPorts) {
    PortRig r;
    const auto macroId = r.macroAround("Tone", r.s.targetUuid);
    const int resonanceRaw = r.s.channelFor("resonance");
    ASSERT_GE(resonanceRaw, 0);
    const auto inletA = r.port(macroId, true);
    const auto inletB = r.port(macroId, true);
    r.attenuate(r.lfo, inletA, 0);
    r.connect(inletA, r.target(), r.cutoffRaw);
    r.attenuate(r.lfo, inletB, 0);
    r.connect(inletB, r.target(), resonanceRaw);
    r.settle();
    r.expectLfoRowWithRemove();
    const auto before = r.snapshot();

    r.s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_EQ(r.s.nodesOf<LFOModule>().size(), 1u) << "the LFO still drives resonance";
    EXPECT_EQ(r.s.nodesOf<AttenuverterModule>().size(), r.s.baseAttenuverters + 1);
    EXPECT_EQ(r.s.nodesOf<MacroInletModule>().size(), 1u) << "only the cutoff chain's port went";
    EXPECT_EQ(r.s.graph().getConnections().size(), r.baseConnections + 3) << "lfo->atten->port->resonance is intact";
    EXPECT_EQ(r.s.row(), nullptr);
    ASSERT_TRUE(r.s.undo().undo());
    EXPECT_EQ(r.snapshot().connections, before.connections);
    EXPECT_EQ(r.snapshot().macros, before.macros);
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorKeepsTheOtherChainWhenOnePortFansOutToTwoAttenuverters) {
    PortRig r;
    const auto macroId = r.macroAround("Tone", r.s.targetUuid);
    const int resonanceRaw = r.s.channelFor("resonance");
    ASSERT_GE(resonanceRaw, 0);
    const auto inlet = r.port(macroId, true);
    r.connect(r.lfo, inlet);
    r.attenuate(inlet, r.target(), r.cutoffRaw);
    r.attenuate(inlet, r.target(), resonanceRaw);
    r.settle();
    r.expectLfoRowWithRemove();

    r.s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    EXPECT_EQ(r.s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_EQ(r.s.nodesOf<MacroInletModule>().size(), 1u) << "the other chain still needs the port";
    EXPECT_EQ(r.s.nodesOf<AttenuverterModule>().size(), r.s.baseAttenuverters + 1);
    EXPECT_EQ(r.s.graph().getConnections().size(), r.baseConnections + 3) << "lfo->port->atten->resonance is intact";
    EXPECT_EQ(r.s.row(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, RemoveModulatorOfAnLfoWhoseRateIsFedFromAMacroPort) {
    PortRig r;
    // The LFO and its target share a macro; a second LFO outside drives the first one's rate through the
    // macro's inlet.
    const auto macroId = r.macroAround("Tone", r.s.targetUuid);
    r.editor().getMacroController().addSelectionToMacro(macroId, {r.lfoUuid}, /*recordUndo=*/false);
    r.attenuate(r.lfo, r.target(), r.cutoffRaw);
    const auto outer = addNodeWithUuid(r.s.mc, "LFO");
    const auto inlet = r.port(macroId, true);
    const int rateRaw = r.editor().modulationChannelFor(r.lfo, "rateHz");
    ASSERT_GE(rateRaw, 0) << "an LFO's rate has a CV jack";
    r.attenuate(outer->nodeID, inlet, 0);
    r.connect(inlet, r.lfo, rateRaw);
    r.settle();
    r.expectLfoRowWithRemove();
    const auto before = r.snapshot();

    r.s.row()->applyMenuChoice(synth::ui::ModulatorRow::kRemoveMenuId);

    // As deleting that LFO's card does, the outside LFO's own chain into the inlet is left (the inlet still has a
    // cable on it).
    EXPECT_EQ(r.s.byUuid(r.lfoUuid), nullptr) << "the LFO is gone";
    EXPECT_EQ(r.s.row(), nullptr);
    ASSERT_TRUE(r.s.undo().undo());
    EXPECT_NE(r.s.byUuid(r.lfoUuid), nullptr);
    EXPECT_EQ(r.snapshot().connections, before.connections) << "every cable is back";
    EXPECT_EQ(r.snapshot().macros, before.macros);
    EXPECT_NE(r.s.row(), nullptr);
}
