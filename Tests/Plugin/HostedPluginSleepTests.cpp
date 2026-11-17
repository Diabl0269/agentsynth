// The Mac sleeping and waking under hosted plugins: every in-process instance is saved and freed before sleep and
// loaded again as it was on wake, an AUv3 (out of process) is left alone, and the guard that decides it reopens the
// editors that were open. All against the stub backend; no real sleep is involved.

#include "../StubPluginInstance.h"
#include "../TestWait.h"
#include "MainComponent/MainComponent.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/HostedPluginSleepGuard.h"
#include <gtest/gtest.h>
#include <map>

using synth::HostedPluginBackend;
using synth::HostedPluginModule;
using synth::HostedPluginSleepGuard;
using synth::PluginIdentity;
using synth::test::StubBackend;
using synth::test::StubPluginInstance;

namespace {

PluginIdentity exampleIdentity() {
    PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "Example Synth";
    identity.uid = 77;
    return identity;
}

StubBackend::Factory auxFactory() {
    return [] {
        return std::make_unique<StubPluginInstance>(
            2, 2, "Example Synth", 77, "VST3",
            std::vector<synth::test::StubParamSpec>{{"cutoff", "Cutoff", 0.2f}, {"reso", "Resonance", 0.5f}}, false, 0,
            false, synth::test::StubAuxOutputs{2, 2, true});
    };
}

/** A module with a loaded instance, a card layout, a mod slot, a saved plugin state and a shown aux output. */
struct LoadedModule {
    StubBackend backend{auxFactory()};
    HostedPluginModule module;

    LoadedModule() {
        module.loadPlugin(exampleIdentity(), backend);
        EXPECT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); }));
        dynamic_cast<StubPluginInstance*>(module.getActiveInstanceForEditor())->payload = "tweaked-state";
        EXPECT_TRUE(module.setOutputShown(1, true));
        EXPECT_GE(module.bindModSlot("cutoff"), 0);
        module.setCardLayoutOverride(juce::JSON::parse(R"({"version":1,"slots":[{"paramId":"reso"}]})"));
    }
};

juce::String payloadOf(HostedPluginModule& module) {
    auto* instance = dynamic_cast<StubPluginInstance*>(module.getActiveInstanceForEditor());
    return instance != nullptr ? instance->payload : juce::String("<none>");
}

class MinimalProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "HostedPluginSleepMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"mock-model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{true, "Mock response.", {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "mock-model";
    int requestTimeoutMs = 240000;
};

} // namespace

TEST(HostedPluginSleepTest, SuspendFreesTheInstanceAndResumeReloadsItAsItWas) {
    LoadedModule loaded;
    auto& module = loaded.module;
    const auto layout = juce::JSON::toString(module.getCardLayoutOverride());
    const auto outputsBefore = juce::JSON::toString(module.getExtraState().getProperty("outputs", {}));
    ASSERT_TRUE(module.canSuspendForSleep());
    StubPluginInstance::clearDestructionRecord();

    ASSERT_TRUE(module.suspendForSleep());
    EXPECT_FALSE(module.hasInstance());
    EXPECT_TRUE(module.isSuspendedForSleep());
    EXPECT_EQ(StubPluginInstance::destructionCount().load(), 1) << "the plugin is gone before the Mac sleeps";
    EXPECT_FALSE(module.canSuspendForSleep()) << "already asleep";
    EXPECT_EQ(module.getIdentity(), exampleIdentity());

    // A save made while asleep still carries everything.
    const auto asleepState = module.getExtraState();
    EXPECT_TRUE(asleepState.hasProperty("pluginState"));
    EXPECT_EQ(juce::JSON::toString(asleepState.getProperty("outputs", {})), outputsBefore);

    loaded.backend.createCount = 0;
    module.resumeAfterSleep(loaded.backend);
    EXPECT_FALSE(module.isSuspendedForSleep());
    ASSERT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); }));
    EXPECT_EQ(loaded.backend.createCount, 1);
    EXPECT_EQ(payloadOf(module), "tweaked-state");
    EXPECT_EQ(juce::JSON::toString(module.getCardLayoutOverride()), layout);
    EXPECT_EQ(module.modChannelForParam("cutoff"), HostedPluginModule::kModChannelBase);
    ASSERT_GE(module.getOutputBuses().size(), 2u);
    EXPECT_TRUE(module.getOutputBuses()[1].shown) << "the shown output comes back shown";
    EXPECT_EQ(juce::JSON::toString(module.getExtraState().getProperty("outputs", {})), outputsBefore);
}

TEST(HostedPluginSleepTest, ASleepDuringALoadDropsTheLateInstanceAndResumeLoadsIt) {
    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(48000.0, 64);
    module.loadPlugin(exampleIdentity(), backend);
    ASSERT_TRUE(module.isLoading());
    ASSERT_TRUE(module.canSuspendForSleep());

    ASSERT_TRUE(module.suspendForSleep());
    EXPECT_FALSE(module.isLoading());
    // The superseded callback runs and publishes nothing.
    synth::test::flushMessageQueue();
    EXPECT_FALSE(module.hasInstance());

    module.resumeAfterSleep(backend);
    ASSERT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); }));
    EXPECT_EQ(backend.createCount, 2);
}

TEST(HostedPluginSleepTest, AnOutOfProcessInstanceIsLeftAlone) {
    LoadedModule loaded;
    auto& module = loaded.module;
    HostedPluginModule::setOutOfProcessProbeForTest([](juce::AudioPluginInstance&) { return true; });
    auto* before = module.getActiveInstanceForEditor();
    EXPECT_FALSE(module.canSuspendForSleep());
    EXPECT_FALSE(module.suspendForSleep());
    HostedPluginModule::setOutOfProcessProbeForTest({});

    EXPECT_FALSE(module.isSuspendedForSleep());
    EXPECT_TRUE(module.hasInstance());
    EXPECT_EQ(module.getActiveInstanceForEditor(), before);
}

TEST(HostedPluginSleepTest, APlaceholderIsNotSuspendedAndStillSavesItsState) {
    StubBackend backend;
    backend.failWith = "not installed";
    juce::MemoryBlock blob;
    blob.append("kept", 4);
    auto state = exampleIdentity().toVar();
    state.getDynamicObject()->setProperty("pluginState", blob.toBase64Encoding());

    HostedPluginBackend::ScopedDefault install(&backend);
    HostedPluginModule module;
    module.setExtraState(state);
    ASSERT_TRUE(synth::test::waitUntil([&] { return !module.isLoading(); }));
    ASSERT_FALSE(module.hasInstance());

    EXPECT_FALSE(module.canSuspendForSleep());
    EXPECT_FALSE(module.suspendForSleep());
    EXPECT_FALSE(module.isSuspendedForSleep());
    EXPECT_EQ(module.getExtraState().getProperty("pluginState", {}).toString(), blob.toBase64Encoding());
}

namespace {

/** The guard over fake hooks: a list of modules, an editor set, and a record of reopen calls. */
struct GuardRig {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed{&backend};
    std::vector<std::unique_ptr<HostedPluginModule>> modules;
    std::map<int, bool> editorOpen;
    std::vector<int> closed;
    std::vector<int> opened;
    bool enabled = true;
    std::unique_ptr<HostedPluginSleepGuard> guard;

    ~GuardRig() { guard.reset(); }

    HostedPluginModule& add() {
        modules.push_back(std::make_unique<HostedPluginModule>());
        auto& module = *modules.back();
        module.prepareToPlay(48000.0, 64);
        module.loadPlugin(exampleIdentity(), backend);
        EXPECT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); }));
        return module;
    }

    void build() {
        HostedPluginSleepGuard::Hooks hooks;
        hooks.hostedModules = [this] {
            std::vector<std::pair<juce::AudioProcessorGraph::NodeID, HostedPluginModule*>> list;
            for (size_t i = 0; i < modules.size(); ++i)
                if (modules[i] != nullptr)
                    list.emplace_back(juce::AudioProcessorGraph::NodeID(static_cast<juce::uint32>(i + 1)),
                                      modules[i].get());
            return list;
        };
        hooks.isEditorOpen = [this](juce::AudioProcessorGraph::NodeID id) { return editorOpen[(int)id.uid]; };
        hooks.closeEditor = [this](juce::AudioProcessorGraph::NodeID id) {
            closed.push_back((int)id.uid);
            editorOpen[(int)id.uid] = false;
        };
        hooks.openEditor = [this](HostedPluginModule* module, juce::AudioProcessorGraph::NodeID id) {
            EXPECT_TRUE(module->hasInstance()) << "reopened only once the instance is live";
            opened.push_back((int)id.uid);
            editorOpen[(int)id.uid] = true;
        };
        hooks.enabled = [this] { return enabled; };
        guard = std::make_unique<HostedPluginSleepGuard>(std::move(hooks));
        guard->setWakeDelayMs(0);
    }
};

} // namespace

TEST(HostedPluginSleepGuardTest, AnOpenEditorClosesBeforeSuspendAndReopensOnceTheReloadedInstanceIsLive) {
    GuardRig rig;
    auto& module = rig.add();
    rig.build();
    rig.editorOpen[1] = true;

    EXPECT_EQ(rig.guard->systemWillSleep(), 1);
    EXPECT_EQ(rig.closed, std::vector<int>{1});
    EXPECT_FALSE(module.hasInstance());
    EXPECT_EQ(rig.guard->getAsleepCount(), 1);
    EXPECT_TRUE(rig.opened.empty());

    rig.guard->systemDidWake();
    EXPECT_TRUE(rig.opened.empty()) << "not before the instance is live";
    ASSERT_TRUE(synth::test::waitUntil([&] { return !rig.opened.empty(); }));
    EXPECT_EQ(rig.opened, std::vector<int>{1});
    EXPECT_EQ(rig.guard->getAsleepCount(), 0);
}

TEST(HostedPluginSleepGuardTest, AFailedReloadReopensNothing) {
    GuardRig rig;
    rig.add();
    rig.build();
    rig.editorOpen[1] = true;
    rig.guard->systemWillSleep();
    rig.backend.failWith = "could not open";
    rig.guard->systemDidWake();
    ASSERT_TRUE(synth::test::waitUntil([&] { return !rig.modules[0]->isLoading(); }));
    synth::test::flushMessageQueue();
    EXPECT_FALSE(rig.modules[0]->hasInstance());
    EXPECT_TRUE(rig.opened.empty());
}

TEST(HostedPluginSleepGuardTest, WithTheSettingOffNothingIsSuspended) {
    GuardRig rig;
    auto& module = rig.add();
    rig.build();
    rig.enabled = false;
    EXPECT_EQ(rig.guard->systemWillSleep(), 0);
    EXPECT_TRUE(module.hasInstance());
    EXPECT_FALSE(module.isSuspendedForSleep());
}

TEST(HostedPluginSleepGuardTest, WakeStillReloadsWhatSleptEvenIfTheSettingWasTurnedOffMeanwhile) {
    GuardRig rig;
    auto& module = rig.add();
    rig.build();
    ASSERT_EQ(rig.guard->systemWillSleep(), 1);
    rig.enabled = false;
    rig.guard->systemDidWake();
    ASSERT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); }));
}

TEST(HostedPluginSleepGuardTest, ANodeDeletedDuringSleepIsSkipped) {
    GuardRig rig;
    rig.add();
    auto& second = rig.add();
    rig.build();
    ASSERT_EQ(rig.guard->systemWillSleep(), 2);
    rig.modules[0].reset(); // the node is deleted while the Mac sleeps
    rig.guard->systemDidWake();
    ASSERT_TRUE(synth::test::waitUntil([&] { return second.hasInstance(); }));
    EXPECT_EQ(rig.guard->getAsleepCount(), 0);
}

TEST(HostedPluginSleepGuardTest, ASecondSleepBeforeADelayedWakeCancelsThatWake) {
    GuardRig rig;
    auto& module = rig.add();
    rig.build();
    rig.guard->setWakeDelayMs(150);
    ASSERT_EQ(rig.guard->systemWillSleep(), 1);
    rig.guard->systemDidWake();
    EXPECT_FALSE(module.hasInstance());

    rig.guard->systemWillSleep(); // the Mac went back to sleep before the delay ran out
    // Past the 150 ms delay the cancelled wake would have fired in.
    const auto until = juce::Time::getMillisecondCounter() + 400;
    synth::test::waitUntil([&] { return module.hasInstance() || juce::Time::getMillisecondCounter() > until; });
    EXPECT_FALSE(module.hasInstance()) << "the cancelled wake must not reload";
    EXPECT_TRUE(module.isSuspendedForSleep());

    rig.guard->setWakeDelayMs(0);
    rig.guard->systemDidWake();
    ASSERT_TRUE(synth::test::waitUntil([&] { return module.hasInstance(); }));
}

TEST(HostedPluginSleepGuardTest, ATestsMainComponentHasNoSystemSleepWatcher) {
    MainComponent main(std::make_unique<MinimalProvider>());
    EXPECT_FALSE(main.hasSystemSleepWatcherForTest()) << "only the real app's Main.cpp starts the watcher";
    EXPECT_EQ(main.getPluginSleepGuard(), nullptr);
}
