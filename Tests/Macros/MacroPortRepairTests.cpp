// MacroPortRepairTests.cpp -- a port module no macro lists goes back to the one macro its cables meet, as a project
// opens (MacroPortRepair.h); any other stray is left alone.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "MacroPortRepair.h"
#include "MacroSet.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

namespace {

struct RepairGraph {
    juce::AudioProcessorGraph graph;
    synth::MacroSet macros;

    juce::AudioProcessorGraph::NodeID add(std::unique_ptr<juce::AudioProcessor> processor, juce::String& uuid) {
        auto node = graph.addNode(std::move(processor));
        uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        return node->nodeID;
    }

    juce::String macroOf(const juce::String& member, const char* name) {
        synth::Macro macro;
        macro.name = name;
        macro.members.push_back(member);
        return macros.add(macro);
    }
};

} // namespace

TEST(MacroPortRepair, AStrayInletCabledIntoOneMacroBecomesItsPortAgain) {
    RepairGraph g;
    juce::String oscUuid, strayUuid, outsideUuid;
    const auto osc = g.add(std::make_unique<OscillatorModule>(), oscUuid);
    const auto stray = g.add(std::make_unique<MacroInletModule>(), strayUuid);
    const auto outside = g.add(std::make_unique<OscillatorModule>(), outsideUuid);
    const auto macroId = g.macroOf(oscUuid, "Inside");
    g.graph.addConnection({{outside, 0}, {stray, 0}});
    g.graph.addConnection({{stray, 0}, {osc, 2}});

    EXPECT_TRUE(synth::isStrayMacroPort(g.macros, strayUuid, g.graph.getNodeForId(stray)->getProcessor()));
    EXPECT_EQ(synth::adoptStrayMacroPorts(g.graph, g.macros), 1);
    const auto* macro = g.macros.find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(strayUuid));
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_TRUE(macro->ports.front().isInput);
    EXPECT_FALSE(synth::isStrayMacroPort(g.macros, strayUuid, g.graph.getNodeForId(stray)->getProcessor()));
    EXPECT_EQ(synth::adoptStrayMacroPorts(g.graph, g.macros), 0) << "a second pass adopts nothing";
}

TEST(MacroPortRepair, AStrayOutletCabledOutOfOneMacroBecomesAnOutputPort) {
    RepairGraph g;
    juce::String oscUuid, strayUuid, outsideUuid;
    const auto osc = g.add(std::make_unique<OscillatorModule>(), oscUuid);
    const auto stray = g.add(std::make_unique<MacroOutletModule>(), strayUuid);
    const auto outside = g.add(std::make_unique<OscillatorModule>(), outsideUuid);
    const auto macroId = g.macroOf(oscUuid, "Inside");
    g.graph.addConnection({{osc, 0}, {stray, 0}});
    g.graph.addConnection({{stray, 0}, {outside, 2}});
    EXPECT_EQ(synth::adoptStrayMacroPorts(g.graph, g.macros), 1);
    const auto* macro = g.macros.find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports.front().isInput);
}

TEST(MacroPortRepair, AStrayThatMeetsNoMacroOrSeveralIsLeftAlone) {
    RepairGraph g;
    juce::String aUuid, bUuid, noneUuid, bothUuid, outsideUuid;
    const auto a = g.add(std::make_unique<OscillatorModule>(), aUuid);
    const auto b = g.add(std::make_unique<OscillatorModule>(), bUuid);
    const auto outside = g.add(std::make_unique<OscillatorModule>(), outsideUuid);
    const auto none = g.add(std::make_unique<MacroInletModule>(), noneUuid);
    const auto both = g.add(std::make_unique<MacroInletModule>(), bothUuid);
    g.macroOf(aUuid, "A");
    g.macroOf(bUuid, "B");
    g.graph.addConnection({{outside, 0}, {none, 0}});
    g.graph.addConnection({{both, 0}, {a, 2}});
    g.graph.addConnection({{both, 0}, {b, 2}});
    EXPECT_EQ(synth::adoptStrayMacroPorts(g.graph, g.macros), 0);
    EXPECT_TRUE(synth::isStrayMacroPort(g.macros, noneUuid, g.graph.getNodeForId(none)->getProcessor()));
    EXPECT_TRUE(synth::isStrayMacroPort(g.macros, bothUuid, g.graph.getNodeForId(both)->getProcessor()));
}

TEST(MacroPortRepair, AnOrdinaryModuleIsNeverAStrayPort) {
    RepairGraph g;
    juce::String uuid;
    const auto osc = g.add(std::make_unique<OscillatorModule>(), uuid);
    EXPECT_FALSE(synth::isStrayMacroPort(g.macros, uuid, g.graph.getNodeForId(osc)->getProcessor()));
}
