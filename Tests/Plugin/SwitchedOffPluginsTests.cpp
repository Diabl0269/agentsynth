// Which hosted plugins feeding a channel strip are switched off after a crash (Plugin/Hosting/SwitchedOffPlugins.h):
// found by walking the cables backward, stopping at another strip, gone again once the plugin is turned back on, and
// signalled by the revision counter the badges poll.

#include "../StubPluginInstance.h"
#include "../TestWait.h"
#include "Modules/ChannelStripModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/SwitchedOffPlugins.h"
#include <gtest/gtest.h>

using synth::HostedPluginModule;
using synth::test::StubBackend;
using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

synth::PluginIdentity identityNamed(const juce::String& name, int uid) {
    synth::PluginIdentity identity;
    identity.format = "VST3";
    identity.name = name;
    identity.uid = uid;
    return identity;
}

struct Rig {
    StubBackend backend;
    juce::AudioProcessorGraph graph;

    NodeID addStrip() { return graph.addNode(std::make_unique<ChannelStripModule>())->nodeID; }

    /** A hosted plugin opened "without it" (switched off) or loaded normally, wired into `target`. */
    HostedPluginModule* addPlugin(const juce::String& name, int uid, bool switchedOff, NodeID target) {
        if (switchedOff)
            backend.skipPluginForNextLoad(name, "/Library/Audio/Plug-Ins/VST3/" + name + ".vst3");
        auto* module = new HostedPluginModule();
        auto node = graph.addNode(std::unique_ptr<juce::AudioProcessor>(module));
        node->properties.set("uuid", "uuid-" + name);
        module->prepareToPlay(48000.0, 64);
        module->loadPlugin(identityNamed(name, uid), backend);
        EXPECT_TRUE(synth::test::waitUntil([module] { return !module->isLoading(); }));
        EXPECT_TRUE(graph.addConnection({{node->nodeID, 0}, {target, 0}}));
        return module;
    }
};

} // namespace

TEST(SwitchedOffPluginsTest, ListsOnlyTheSwitchedOffPluginsThatFeedTheStrip) {
    Rig rig;
    const auto strip = rig.addStrip();
    rig.addPlugin("Beta Synth", 2, true, strip);
    rig.addPlugin("Alpha Synth", 1, true, strip);
    rig.addPlugin("Fine Synth", 3, false, strip);

    const auto found = synth::switchedOffPluginsFeeding(rig.graph, strip);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].title, "Alpha Synth") << "sorted by title";
    EXPECT_EQ(found[1].title, "Beta Synth");
    EXPECT_EQ(found[0].nodeUuid, "uuid-Alpha Synth");
    EXPECT_EQ(synth::titlesOf(found).joinIntoString("|"), "Alpha Synth|Beta Synth");
}

TEST(SwitchedOffPluginsTest, AnEffectBehindAnotherModuleIsFoundAndAnotherStripsPluginIsNot) {
    Rig rig;
    const auto stripA = rig.addStrip();
    const auto stripB = rig.addStrip();
    const auto relay = rig.graph.addNode(std::make_unique<HostedPluginModule>())->nodeID; // a plain module in between
    ASSERT_TRUE(rig.graph.addConnection({{relay, 0}, {stripA, 0}}));
    rig.addPlugin("Deep Synth", 4, true, relay);
    rig.addPlugin("Other Synth", 5, true, stripB);
    ASSERT_TRUE(rig.graph.addConnection({{stripB, 0}, {stripA, 0}})); // B plays into A, as a bus source would

    const auto found = synth::titlesOf(synth::switchedOffPluginsFeeding(rig.graph, stripA));
    EXPECT_EQ(found.joinIntoString("|"), "Deep Synth") << "the walk stops at the other strip";
    EXPECT_EQ(synth::titlesOf(synth::switchedOffPluginsFeeding(rig.graph, stripB)).joinIntoString("|"), "Other Synth");
}

TEST(SwitchedOffPluginsTest, TurningThePluginBackOnClearsItAndMovesTheRevisionBothWays) {
    Rig rig;
    const auto strip = rig.addStrip();
    const auto before = HostedPluginModule::switchedOffRevision();
    auto* module = rig.addPlugin("Crashy Synth", 6, true, strip);
    EXPECT_NE(HostedPluginModule::switchedOffRevision(), before) << "switching off moves the revision";
    ASSERT_EQ(synth::switchedOffPluginsFeeding(rig.graph, strip).size(), 1u);

    const auto switchedOff = HostedPluginModule::switchedOffRevision();
    module->turnBackOn(rig.backend);
    EXPECT_NE(HostedPluginModule::switchedOffRevision(), switchedOff) << "turning on moves it again";
    ASSERT_TRUE(synth::test::waitUntil([module] { return module->hasInstance(); }));
    EXPECT_TRUE(synth::switchedOffPluginsFeeding(rig.graph, strip).empty());

    const auto steady = HostedPluginModule::switchedOffRevision();
    module->unloadPlugin();
    EXPECT_EQ(HostedPluginModule::switchedOffRevision(), steady) << "no change of state, no new revision";
}

TEST(SwitchedOffPluginsTest, AStripNotInTheGraphHasNone) {
    Rig rig;
    EXPECT_TRUE(synth::switchedOffPluginsFeeding(rig.graph, NodeID(9999)).empty());
}
