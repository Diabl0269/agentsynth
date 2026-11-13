// The running sentinel, the previous-run lookup, the strike count and the skip-for-this-load path, all on temp folders
// and the stub backend.

#include "../StubPluginInstance.h"
#include "../TestWait.h"
#include "../UniqueTempDir.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CrashReportFixtures.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCrashRecovery.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include <gtest/gtest.h>

using synth::HostedPluginBackend;
using synth::HostedPluginModule;
using synth::PluginIdentity;
using synth::PreviousRunKind;
using synth::RunSentinel;
using synth::test::StubBackend;

namespace {

const RunSentinel::IsProcessAlive kNobodyAlive = [](int) { return false; };

struct Folders {
    juce::File state = synth::test::makeUniqueTempDir("crash_state");
    juce::File reports = synth::test::makeUniqueTempDir("crash_reports");
    ~Folders() {
        state.deleteRecursively();
        reports.deleteRecursively();
    }

    /** A crashed run: a sentinel left behind, and a report written after it. */
    void leaveCrashedRun(const juce::String& reportText) {
        RunSentinel sentinel(state, 4242);
        state.getChildFile("running-4242")
            .setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::minutes(5));
        if (reportText.isNotEmpty())
            reports.getChildFile("Agent Synth-2026-10-28-170226.ips").replaceWithText(reportText);
    }
};

PluginIdentity examplePluginIdentity() {
    PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "Example Synth";
    identity.uid = 77;
    return identity;
}

void skipExample(HostedPluginBackend& backend) {
    backend.skipPluginForNextLoad("Example Synth", "/Library/Audio/Plug-Ins/VST3/Example Synth.vst3");
}

} // namespace

TEST(RunSentinelTest, ACleanQuitLeavesNothingBehind) {
    Folders folders;
    RunSentinel sentinel(folders.state, 4242);
    EXPECT_TRUE(folders.state.getChildFile("running-4242").existsAsFile());
    sentinel.markCleanExit();
    EXPECT_FALSE(RunSentinel::takeStaleRun(folders.state, kNobodyAlive).has_value());
}

TEST(RunSentinelTest, ASentinelWhoseProcessIsGoneIsReportedOnce) {
    Folders folders;
    RunSentinel sentinel(folders.state, 4242);
    EXPECT_TRUE(RunSentinel::takeStaleRun(folders.state, kNobodyAlive).has_value());
    EXPECT_FALSE(RunSentinel::takeStaleRun(folders.state, kNobodyAlive).has_value()) << "each crash is reported once";
}

TEST(RunSentinelTest, AnotherRunningInstanceIsNotACrash) {
    Folders folders;
    RunSentinel other(folders.state, 4242);
    const auto result = RunSentinel::takeStaleRun(folders.state, [](int pid) { return pid == 4242; });
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(folders.state.getChildFile("running-4242").existsAsFile()) << "a live instance keeps its sentinel";
}

TEST(RunSentinelTest, TheRealLivenessCheckKnowsThisProcess) {
    EXPECT_TRUE(RunSentinel::processIsAlive(RunSentinel::currentProcessId()));
}

TEST(PreviousRunTest, AReportNamingAPluginIsReportedWithAStrike) {
    Folders folders;
    folders.leaveCrashedRun(synth::test::kPluginCrashReport);

    const auto outcome = synth::inspectPreviousRun(folders.state, folders.reports, kNobodyAlive);
    EXPECT_EQ(outcome.kind, PreviousRunKind::InsidePlugin);
    EXPECT_EQ(outcome.plugin.name, "Example Synth");
    EXPECT_EQ(outcome.strikes, 1);
    EXPECT_EQ(synth::pluginCrashStrikes(folders.state, "Example Synth"), 1);

    folders.leaveCrashedRun(synth::test::kPluginCrashReport);
    EXPECT_EQ(synth::inspectPreviousRun(folders.state, folders.reports, kNobodyAlive).strikes, 2);
}

TEST(PreviousRunTest, AReportOlderThanTheRunIsIgnored) {
    Folders folders;
    folders.reports.getChildFile("Agent Synth-2026-10-01-100000.ips").replaceWithText(synth::test::kPluginCrashReport);
    folders.reports.getChildFile("Agent Synth-2026-10-01-100000.ips")
        .setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::days(3));
    RunSentinel sentinel(folders.state, 4242);

    const auto outcome = synth::inspectPreviousRun(folders.state, folders.reports, kNobodyAlive);
    EXPECT_EQ(outcome.kind, PreviousRunKind::CleanOrUnknown);
}

TEST(PreviousRunTest, ACrashOutsideAnyPluginSaysNothing) {
    Folders folders;
    folders.leaveCrashedRun(R"({}
{"faultingThread":0,"threads":[{"frames":[{"imageIndex":0}]}],"usedImages":[{"path":"/Applications/Agent Synth.app/Contents/MacOS/Agent Synth"}]})");
    EXPECT_EQ(synth::inspectPreviousRun(folders.state, folders.reports, kNobodyAlive).kind,
              PreviousRunKind::CleanOrUnknown);
}

TEST(PreviousRunTest, WithoutCrashReportsAStaleSentinelIsAnUnnamedUnexpectedQuit) {
    Folders folders;
    RunSentinel sentinel(folders.state, 4242);
    const auto outcome = synth::inspectPreviousRun(folders.state, juce::File(), kNobodyAlive);
    EXPECT_EQ(outcome.kind, PreviousRunKind::Unexpected);
    EXPECT_TRUE(outcome.plugin.name.isEmpty()) << "no plugin name without a report";
}

TEST(PreviousRunTest, ACleanPreviousRunSaysNothing) {
    Folders folders;
    EXPECT_EQ(synth::inspectPreviousRun(folders.state, folders.reports, kNobodyAlive).kind,
              PreviousRunKind::CleanOrUnknown);
    EXPECT_EQ(synth::inspectPreviousRun(folders.state, juce::File(), kNobodyAlive).kind,
              PreviousRunKind::CleanOrUnknown);
}

TEST(SkipPluginForNextLoadTest, ASkippedIdentityStaysAPlaceholderThatKeepsItsIdentity) {
    StubBackend backend;
    skipExample(backend);

    HostedPluginModule module;
    module.loadPlugin(examplePluginIdentity(), backend);
    ASSERT_TRUE(synth::test::waitUntil([&] { return !module.isLoading(); }));

    EXPECT_FALSE(module.hasInstance());
    EXPECT_EQ(backend.createCount, 0) << "the plugin's code must never be loaded";
    EXPECT_EQ(module.getIdentity(), examplePluginIdentity()) << "the project keeps naming the plugin";
    EXPECT_TRUE(module.getStatusMessage().contains("switched off"));
}

TEST(SkipPluginForNextLoadTest, OtherPluginsStillLoad) {
    StubBackend backend;
    skipExample(backend);

    auto other = examplePluginIdentity();
    other.name = "Another Synth";
    other.uid = 78;
    HostedPluginModule module;
    module.loadPlugin(other, backend);
    ASSERT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); }));
}

TEST(SkipPluginForNextLoadTest, TheSkipEndsWithTheLoadAndALoadByDescriptionIsNeverBlocked) {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault install(&backend);
    skipExample(backend);
    {
        HostedPluginBackend::ScopedClearSkips endOfLoad;
    }
    EXPECT_FALSE(backend.hasSkippedPlugins());

    HostedPluginModule module;
    module.loadPlugin(examplePluginIdentity(), backend);
    ASSERT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); })) << "re-enabling loads it again";

    // A skip that is still set does not stop a load the user asks for from a card (by description).
    skipExample(backend);
    juce::PluginDescription description;
    description.name = "Example Synth";
    description.pluginFormatName = "VST3";
    description.uniqueId = 77;
    HostedPluginModule card;
    card.loadPlugin(description, backend);
    ASSERT_TRUE(synth::test::waitUntil([&] { return card.hasInstance(); }));
}

TEST(SkipPluginForNextLoadTest, MatchesABundleByPathOrByName) {
    StubBackend backend;
    skipExample(backend);
    juce::PluginDescription byPath;
    byPath.name = "Renamed By Its Maker";
    byPath.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/Example Synth.vst3";
    EXPECT_TRUE(backend.isSkippedForNextLoad(byPath));
    juce::PluginDescription byName;
    byName.name = "example synth";
    EXPECT_TRUE(backend.isSkippedForNextLoad(byName));
    juce::PluginDescription unrelated;
    unrelated.name = "Other";
    unrelated.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/Other.vst3";
    EXPECT_FALSE(backend.isSkippedForNextLoad(unrelated));
}

TEST(SkipPluginForNextLoadTest, AProjectLoadSkipsTheNamedPluginThenTheSkipEnds) {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);

    // A patch with one hosted plugin node, built the way a saved project has it.
    juce::var json;
    {
        juce::AudioProcessorGraph source;
        auto module = std::make_unique<HostedPluginModule>();
        auto* modulePtr = module.get();
        source.addNode(std::move(module));
        modulePtr->loadPlugin(examplePluginIdentity(), backend);
        ASSERT_TRUE(synth::test::waitUntil([&] { return modulePtr->hasInstance(); }));
        json = synth::AIStateMapper::graphToJSON(source);
    }
    backend.createCount = 0;
    skipExample(backend);

    auto findModule = [](juce::AudioProcessorGraph& graph) {
        for (auto* node : graph.getNodes())
            if (auto* hosted = dynamic_cast<HostedPluginModule*>(node->getProcessor()))
                return hosted;
        return static_cast<HostedPluginModule*>(nullptr);
    };

    // The real load path, inside the scope MainComponent's project loads hold.
    juce::AudioProcessorGraph skipped;
    {
        HostedPluginBackend::ScopedClearSkips endOfLoad;
        ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, skipped, /*clearExisting=*/true, /*trusted=*/true));
    }
    auto* placeholder = findModule(skipped);
    ASSERT_NE(placeholder, nullptr);
    ASSERT_TRUE(synth::test::waitUntil([&] { return !placeholder->isLoading(); }));
    EXPECT_FALSE(placeholder->hasInstance());
    EXPECT_EQ(backend.createCount, 0) << "the plugin must not be created at all";
    EXPECT_EQ(placeholder->getIdentity(), examplePluginIdentity()) << "saving the project keeps the plugin named";
    EXPECT_FALSE(backend.hasSkippedPlugins()) << "the skip covers that one load";

    // The next load of the same project brings it back.
    juce::AudioProcessorGraph normal;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, normal, /*clearExisting=*/true, /*trusted=*/true));
    auto* loaded = findModule(normal);
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(synth::test::waitUntil([&] { return loaded->hasInstance(); }));
}

TEST(SkipPluginForNextLoadTest, ASkippedModuleKnowsItWasSwitchedOffAndTurnBackOnKeepsEverythingTheProjectHolds) {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);
    backend.setFactory([] {
        return std::make_unique<synth::test::StubPluginInstance>(
            2, 2, "Example Synth", 77, "VST3",
            std::vector<synth::test::StubParamSpec>{{"cutoff", "Cutoff", 0.2f}, {"reso", "Resonance", 0.5f}});
    });

    // A saved plugin: its state, a card layout override and a mod slot.
    juce::var saved;
    {
        HostedPluginModule source;
        source.prepareToPlay(48000.0, 64);
        source.loadPlugin(examplePluginIdentity(), backend);
        ASSERT_TRUE(synth::test::waitUntil([&] { return source.hasInstance(); }));
        ASSERT_GE(source.bindModSlot("cutoff"), 0);
        saved = source.getExtraState();
        auto* object = saved.getDynamicObject();
        juce::MemoryBlock blob;
        blob.append("saved-patch", 11);
        object->setProperty("pluginState", blob.toBase64Encoding());
        auto layout = juce::JSON::parse(R"({"version":1,"slots":[{"paramId":"reso"}]})");
        object->setProperty("cardLayout", layout);
    }

    // Opened "without it": the load names the plugin but never creates it.
    backend.createCount = 0;
    skipExample(backend);
    HostedPluginModule module;
    module.prepareToPlay(48000.0, 64);
    {
        HostedPluginBackend::ScopedClearSkips endOfLoad;
        module.setExtraState(saved);
    }
    ASSERT_TRUE(synth::test::waitUntil([&] { return !module.isLoading(); }));
    EXPECT_FALSE(module.hasInstance());
    EXPECT_TRUE(module.isSwitchedOffAfterCrash());
    EXPECT_EQ(backend.createCount, 0);
    EXPECT_FALSE(backend.hasSkippedPlugins()) << "the skip ended with the project load";
    const auto layoutBefore = module.getCardLayoutOverride();
    EXPECT_FALSE(layoutBefore.isVoid());

    // Turn on, with the skip even still set: an explicit turn-on is never blocked, and only this plugin's skip goes.
    skipExample(backend);
    backend.skipPluginForNextLoad("Other Synth", "/Library/Audio/Plug-Ins/VST3/Other Synth.vst3");
    module.turnBackOn(backend);
    EXPECT_TRUE(module.isLoading());
    ASSERT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); }));
    EXPECT_FALSE(module.isSwitchedOffAfterCrash());
    EXPECT_TRUE(backend.hasSkippedPlugins()) << "another plugin's skip is untouched";

    auto* instance = dynamic_cast<synth::test::StubPluginInstance*>(module.getActiveInstanceForEditor());
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->payload, "saved-patch") << "the saved plugin state reaches the instance";
    EXPECT_EQ(juce::JSON::toString(module.getCardLayoutOverride()), juce::JSON::toString(layoutBefore));
    EXPECT_EQ(module.modChannelForParam("cutoff"), HostedPluginModule::kModChannelBase) << "the mod slot is kept";
}

TEST(SkipPluginForNextLoadTest, ALoadThatFailsForAnotherReasonIsNotSwitchedOffAfterACrash) {
    StubBackend backend;
    backend.failWith = "Example Synth could not be opened.";
    HostedPluginModule module;
    module.loadPlugin(examplePluginIdentity(), backend);
    ASSERT_TRUE(synth::test::waitUntil([&] { return !module.isLoading(); }));
    EXPECT_FALSE(module.hasInstance());
    EXPECT_FALSE(module.isSwitchedOffAfterCrash());
}
