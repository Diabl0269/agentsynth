// HostedPluginTests.cpp
//
// TL7-2: synth::HostedPluginBackend + synth::HostedPluginModule — third-party VST3/AU plugins as
// graph modules.
//
// Everything here runs against Tests/StubPluginInstance.h rather than a real plugin: there is no
// third-party binary we can check into this repo and load in CI, and the backend seam exists
// precisely so that is not a reason to leave hosting untested. The stub matches the real formats'
// async, message-thread callback contract, so these tests pump the message loop exactly as they
// would have to with a real VST3.
//
// Groups:
//   1. Pass-through until ready — the dry path, and bypass sharing it.
//   2. Publication — an async load renders, and the visible ports follow the instance.
//   3. Refusal — an instance wider than kMaxPluginChannels, and an identity that does not resolve.
//   4. State — the trusted round-trip through graphToJSON/applyJSONToGraph, and the assertion that
//      no path is ever serialized.
//   5. Trust — the untrusted path cannot author the type at all (TL7-4's mechanism).
//   6. Instance lifetime — the audio thread never frees.
//   7. Stream format — prepareToPlay propagates to the live instance.
//   8. Registration — the internal-only checklist Track In / Rec Tap / Track Audio established.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Plugin/Hosting/HostedPluginModule.h"
#include "../Source/UI/CableColour.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "StubPluginInstance.h"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using synth::HostedPluginBackend;
using synth::HostedPluginModule;
using synth::PluginIdentity;
using synth::test::StubBackend;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

/** Wider than HostedPluginModule::kMaxPluginChannels — the whole point of the refusal test. */
constexpr int kTooManyChannels = 32;
static_assert(kTooManyChannels > HostedPluginModule::kMaxPluginChannels,
              "the refusal test must have something to refuse");

/** Pumps the JUCE message loop until `predicate` holds or the timeout expires — the bounded-poll
 *  idiom from AccountServiceTests, needed here because the backend callback is posted, never
 *  fired re-entrantly. */
template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

/** A description the StubBackend will happily build an instance for. */
juce::PluginDescription stubDescription(const juce::String& name = "Stub Plugin", int uid = 0x5754424) {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = "VST3";
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    // A real description always carries one; nothing downstream may serialize it.
    description.fileOrIdentifier = "/nonexistent/test/path/StubPlugin.vst3";
    return description;
}

/** Fills every channel with a per-channel constant, so a pass-through, a gain marker and a cleared
 *  channel are all trivially distinguishable. */
void fillRamp(juce::AudioBuffer<float>& buffer) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(channel, sample, 0.1f * (float)(channel + 1));
}

} // namespace

// ============================================================================
// 1. Pass-through until ready
// ============================================================================

TEST(HostedPluginTest, PassThroughUntilReady) {
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    ASSERT_FALSE(module.hasInstance());

    juce::AudioBuffer<float> buffer(HostedPluginModule::kMaxPluginChannels, kBlockSize);
    fillRamp(buffer);
    juce::AudioBuffer<float> expected;
    expected.makeCopyOf(buffer);
    juce::MidiBuffer midi;

    module.processBlock(buffer, midi);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            ASSERT_FLOAT_EQ(buffer.getSample(channel, sample), expected.getSample(channel, sample))
                << "no instance must pass audio through untouched (channel " << channel << ")";

    // Bypass takes the same branch — a dry path exists, so it must not clear.
    module.setBypassed(true);
    fillRamp(buffer);
    module.processBlock(buffer, midi);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        ASSERT_FLOAT_EQ(buffer.getSample(channel, 0), expected.getSample(channel, 0))
            << "bypass must pass the dry signal through, not clear it (channel " << channel << ")";

    // Mute is the other branch of the contract and DOES clear.
    module.setBypassed(false);
    module.setMuted(true);
    fillRamp(buffer);
    module.processBlock(buffer, midi);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        ASSERT_FLOAT_EQ(buffer.getSample(channel, 0), 0.0f) << "mute must clear (channel " << channel << ")";
}

TEST(HostedPluginTest, BareModuleShowsOneJackASide) {
    HostedPluginModule module;
    EXPECT_EQ(module.getTotalNumInputChannels(), HostedPluginModule::kMaxPluginChannels);
    EXPECT_EQ(module.getTotalNumOutputChannels(), HostedPluginModule::kMaxPluginChannels);
    EXPECT_EQ(module.getVisibleInputPortCount(), 1);
    EXPECT_EQ(module.getVisibleOutputPortCount(), 1);
}

// ============================================================================
// 2. Publication
// ============================================================================

TEST(HostedPluginTest, AsyncLoadPublishesAndProcesses) {
    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    module.loadPlugin(stubDescription(), backend);
    EXPECT_FALSE(module.hasInstance()) << "the backend callback must not fire re-entrantly";
    EXPECT_TRUE(module.isLoading());

    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); })) << "the load never published";
    EXPECT_FALSE(module.isLoading());
    EXPECT_TRUE(module.getStatusMessage().isEmpty());

    // The visible ports are the instance's REAL counts, not the node's 16.
    EXPECT_EQ(module.getVisibleInputPortCount(), 2);
    EXPECT_EQ(module.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(module.getTotalNumOutputChannels(), HostedPluginModule::kMaxPluginChannels)
        << "the node's channel count is fixed at construction and must NOT follow the instance";

    // The instance was prepared to OUR stream format, not to whatever it defaulted to.
    EXPECT_DOUBLE_EQ(backend.lastSampleRate, kSampleRate);
    EXPECT_EQ(backend.lastBlockSize, kBlockSize);

    juce::AudioBuffer<float> buffer(HostedPluginModule::kMaxPluginChannels, kBlockSize);
    fillRamp(buffer);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    // Channels the stub renders carry its marker gain...
    EXPECT_FLOAT_EQ(buffer.getSample(0, 0), 0.1f * StubPluginInstance::kGainMarker);
    EXPECT_FLOAT_EQ(buffer.getSample(1, 0), 0.2f * StubPluginInstance::kGainMarker);

    // ...and every hidden channel is silenced every block (the Macro rule). Without this, whatever
    // an upstream node put on channel 9 would sail past a stereo plugin untouched.
    for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
        EXPECT_FLOAT_EQ(buffer.getSample(channel, 0), 0.0f) << "hidden channel " << channel << " must be silent";
}

TEST(HostedPluginTest, InstrumentWithNoInputsGetsItsOutputChannelsCleared) {
    // A 0-in / 2-out instrument. Channels below the instance's output count are output-only and must
    // arrive cleared, or our upstream audio leaks straight through as if the plugin were bypassed.
    StubBackend backend([] { return std::make_unique<StubPluginInstance>(0, 2, "Stub Synth"); });
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    module.loadPlugin(stubDescription("Stub Synth"), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); }));

    EXPECT_EQ(module.getVisibleInputPortCount(), 0) << "an instrument really does have no input jacks";
    EXPECT_EQ(module.getVisibleOutputPortCount(), 2);

    juce::AudioBuffer<float> buffer(HostedPluginModule::kMaxPluginChannels, kBlockSize);
    fillRamp(buffer);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    // Cleared, then gained by the stub: 0 either way — but distinguishable from 0.1 * 0.5.
    EXPECT_FLOAT_EQ(buffer.getSample(0, 0), 0.0f);
    EXPECT_FLOAT_EQ(buffer.getSample(1, 0), 0.0f);
}

// ============================================================================
// 3. Refusal
// ============================================================================

TEST(HostedPluginTest, OverMaxRefusedWithMessage) {
    StubBackend backend(
        [] { return std::make_unique<StubPluginInstance>(kTooManyChannels, kTooManyChannels, "Wide Plugin"); });
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    module.loadPlugin(stubDescription("Wide Plugin"), backend);
    ASSERT_TRUE(pumpUntil([&] { return !module.isLoading(); })) << "the load never completed";

    EXPECT_FALSE(module.hasInstance()) << "an instance wider than kMaxPluginChannels must be refused";
    EXPECT_TRUE(module.getStatusMessage().isNotEmpty()) << "a refusal the user cannot see is a silent failure";
    EXPECT_TRUE(module.getStatusMessage().contains(juce::String(kTooManyChannels)))
        << "the message must say how wide the plugin was: " << module.getStatusMessage();
    EXPECT_TRUE(module.getStatusMessage().contains(juce::String(HostedPluginModule::kMaxPluginChannels)))
        << "...and what the limit is: " << module.getStatusMessage();

    // Refused, not broken: the module keeps passing audio through.
    EXPECT_EQ(module.getVisibleInputPortCount(), 1);
    juce::AudioBuffer<float> buffer(HostedPluginModule::kMaxPluginChannels, kBlockSize);
    fillRamp(buffer);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);
    EXPECT_FLOAT_EQ(buffer.getSample(0, 0), 0.1f) << "a refused load must leave the dry path intact";
    EXPECT_FLOAT_EQ(buffer.getSample(5, 0), 0.6f);
}

TEST(HostedPluginTest, UnresolvedIdentityStaysAPlaceholderThatRemembersItsPlugin) {
    // The "plugin not installed on this machine" case — the state TL7-3's placeholder card renders.
    StubBackend backend;
    backend.resolves = false;

    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "Absent Plugin";
    identity.uid = 4242;
    module.loadPlugin(identity, backend);

    ASSERT_TRUE(pumpUntil([&] { return !module.isLoading(); }));
    EXPECT_FALSE(module.hasInstance());
    EXPECT_EQ(module.getIdentity(), identity) << "the identity must survive a failed resolve — a placeholder that "
                                                 "forgot its plugin would be destroyed by the next save";
    EXPECT_TRUE(module.getStatusMessage().contains("Absent Plugin")) << module.getStatusMessage();

    // And it still serializes, so re-saving on a machine without the plugin does not drop it.
    const juce::var state = module.getExtraState();
    ASSERT_NE(state.getDynamicObject(), nullptr);
    EXPECT_EQ(state.getDynamicObject()->getProperty("pluginName").toString(), "Absent Plugin");
    EXPECT_EQ((int)state.getDynamicObject()->getProperty("pluginUid"), 4242);
}

// ============================================================================
// 4. State — the trusted round-trip
// ============================================================================

TEST(HostedPluginTest, StateRoundTrip) {
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);

    constexpr int kUid = 0x1234abc;
    const juce::String payload = "stub-payload-round-trip";

    // --- Author a graph with a loaded hosted plugin ------------------------------------------
    juce::AudioProcessorGraph source;
    auto module = std::make_unique<HostedPluginModule>();
    auto* modulePtr = module.get();
    source.addNode(std::move(module));

    modulePtr->prepareToPlay(kSampleRate, kBlockSize);
    modulePtr->loadPlugin(stubDescription("Round Trip Plugin", kUid), backend);
    ASSERT_TRUE(pumpUntil([&] { return modulePtr->hasInstance(); }));

    // Give the instance some state of its own to carry.
    const juce::var beforeState = modulePtr->getExtraState();
    ASSERT_NE(beforeState.getDynamicObject(), nullptr);
    {
        // Reach the instance the only way the app does: through setExtraState with a blob.
        auto* object = beforeState.getDynamicObject();
        juce::MemoryBlock blob;
        blob.append(payload.toRawUTF8(), payload.getNumBytesAsUTF8());
        object->setProperty("pluginState", blob.toBase64Encoding());
        modulePtr->setExtraState(beforeState);
        ASSERT_TRUE(pumpUntil([&] { return modulePtr->hasInstance() && !modulePtr->isLoading(); }));
    }

    // --- Serialize ---------------------------------------------------------------------------
    const juce::var json = synth::AIStateMapper::graphToJSON(source);
    const juce::String jsonText = juce::JSON::toString(json);

    // The identity is format + uid + name, and NOTHING resembling a path — not the plugin binary's,
    // not anything else. The stub deliberately reports a fileOrIdentifier so this is a real test.
    EXPECT_FALSE(jsonText.contains("StubPlugin.vst3")) << "a plugin path must never reach the patch: " << jsonText;
    EXPECT_FALSE(jsonText.contains("/nonexistent")) << "a plugin path must never reach the patch: " << jsonText;
    EXPECT_TRUE(jsonText.contains("Round Trip Plugin"));
    EXPECT_TRUE(jsonText.contains("pluginUid"));
    EXPECT_TRUE(jsonText.contains("pluginFormat"));

    // --- Restore into a fresh graph on the TRUSTED path ---------------------------------------
    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, /*clearExisting=*/true, /*trusted=*/true));

    HostedPluginModule* restoredModule = nullptr;
    for (auto* node : restored.getNodes())
        if (auto* candidate = dynamic_cast<HostedPluginModule*>(node->getProcessor()))
            restoredModule = candidate;
    ASSERT_NE(restoredModule, nullptr) << "the hosted plugin node must round-trip through the factory";

    // The identity came back before the instance did — the placeholder is a valid intermediate.
    EXPECT_EQ(restoredModule->getIdentity().name, "Round Trip Plugin");
    EXPECT_EQ(restoredModule->getIdentity().uid, kUid);
    EXPECT_EQ(restoredModule->getIdentity().format, "VST3");

    // setExtraState reached for the DEFAULT backend (the ScopedDefault above), which is the seam
    // that makes restore work at all — applyJSONToGraph has no backend to pass down.
    ASSERT_TRUE(pumpUntil([&] { return restoredModule->hasInstance(); })) << "the restore never resolved an instance";

    // ...and the plugin's own opaque blob came with it.
    const juce::var afterState = restoredModule->getExtraState();
    ASSERT_NE(afterState.getDynamicObject(), nullptr);
    juce::MemoryBlock afterBlob;
    afterBlob.fromBase64Encoding(afterState.getDynamicObject()->getProperty("pluginState").toString());
    EXPECT_EQ(juce::String::fromUTF8((const char*)afterBlob.getData(), (int)afterBlob.getSize()), payload);
}

// ============================================================================
// 5. Trust — TL7-4's mechanism
// ============================================================================

TEST(HostedPluginTest, UntrustedCannotAuthorIt) {
    juce::AudioProcessorGraph graph;

    // Never offered to a model...
    EXPECT_FALSE(synth::AIStateMapper::authorableModuleTypes().contains("Hosted Plugin"));

    // ...and refused outright by validatePatch, which is the part that matters: the schema enum is
    // only a hint, but a hand-edited file or a local model never saw it.
    const juce::var plain = juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Hosted Plugin"}],"connections":[]})");
    auto result = synth::AIStateMapper::validatePatch(plain, graph, /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, synth::PatchValidationError::InternalModuleNotAllowed);
    EXPECT_TRUE(result.message.contains("Hosted Plugin"));

    // The same with a state blob attached — the payload a hostile patch would actually carry, since
    // "state" is fed verbatim to third-party setStateInformation. Rejected at the TYPE, before the
    // blob is ever looked at.
    const juce::var withState = juce::JSON::parse(
        R"({"nodes":[{"id":1,"type":"Hosted Plugin","state":{"pluginFormat":"VST3","pluginName":"Evil","pluginUid":1,"pluginState":"QUJD"}}],"connections":[]})");
    result = synth::AIStateMapper::validatePatch(withState, graph, /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, synth::PatchValidationError::InternalModuleNotAllowed);

    // ...and apply refuses the whole patch rather than partially building it.
    juce::AudioProcessorGraph target;
    EXPECT_FALSE(synth::AIStateMapper::applyJSONToGraph(withState, target, /*clearExisting=*/true, /*trusted=*/false));
    EXPECT_EQ(target.getNumNodes(), 0);
}

TEST(HostedPluginTest, UntrustedApplyNeverReachesSetExtraStateOnAnExistingNode) {
    // The other direction: a merge patch aimed at a hosted plugin node that ALREADY exists. The type
    // rejection has to fire there too, or an untrusted patch could hand a blob to a live instance.
    StubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);

    juce::AudioProcessorGraph graph;
    auto module = std::make_unique<HostedPluginModule>();
    auto* modulePtr = module.get();
    graph.addNode(std::move(module));
    modulePtr->prepareToPlay(kSampleRate, kBlockSize);
    modulePtr->loadPlugin(stubDescription("Victim"), backend);
    ASSERT_TRUE(pumpUntil([&] { return modulePtr->hasInstance(); }));

    const juce::var before = modulePtr->getExtraState();

    const juce::var merge = juce::JSON::parse(
        R"({"nodes":[{"id":1,"type":"Hosted Plugin","state":{"pluginFormat":"VST3","pluginName":"Evil","pluginUid":99,"pluginState":"QUJD"}}],"connections":[]})");
    EXPECT_FALSE(synth::AIStateMapper::applyJSONToGraph(merge, graph, /*clearExisting=*/false, /*trusted=*/false));

    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(modulePtr->getIdentity().name, "Victim") << "an untrusted patch must not repoint a live hosted plugin";
    EXPECT_EQ(juce::JSON::toString(modulePtr->getExtraState()), juce::JSON::toString(before));
}

// ============================================================================
// 6. Instance lifetime — the audio thread never frees
// ============================================================================

TEST(HostedPluginTest, AudioThreadNeverFrees) {
    StubPluginInstance::clearDestructionRecord();

    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    module.loadPlugin(stubDescription("First"), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); }));

    // A stand-in audio thread: renders continuously while the message thread swaps instances under
    // it. This is the race the retained-instance discipline exists for.
    std::atomic<bool> stop{false};
    std::atomic<int> blocksRendered{0};
    const auto messageThreadId = std::this_thread::get_id();
    std::atomic<bool> renderThreadFreedSomething{false};

    std::thread renderThread([&] {
        juce::AudioBuffer<float> buffer(HostedPluginModule::kMaxPluginChannels, kBlockSize);
        juce::MidiBuffer midi;
        while (!stop.load(std::memory_order_acquire)) {
            fillRamp(buffer);
            module.processBlock(buffer, midi);
            blocksRendered.fetch_add(1);
            if (StubPluginInstance::lastDestructionThread().load() == std::this_thread::get_id())
                renderThreadFreedSomething.store(true);
        }
    });

    // Two swaps and an unload, each pumped to completion, while the render loop runs.
    module.loadPlugin(stubDescription("Second"), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.getIdentity().name == "Second" && module.hasInstance(); }));

    module.loadPlugin(stubDescription("Third"), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.getIdentity().name == "Third" && module.hasInstance(); }));

    module.unloadPlugin();
    // Reaping needs the audio thread to visibly move on, then a message-thread pass to do the work.
    const int atUnload = blocksRendered.load();
    ASSERT_TRUE(pumpUntil([&] { return blocksRendered.load() > atUnload + 4; }));
    ASSERT_TRUE(pumpUntil([&] { return StubPluginInstance::destructionCount().load() >= 2; }, 3000))
        << "retired instances were never reaped (freed " << StubPluginInstance::destructionCount().load() << ")";

    stop.store(true, std::memory_order_release);
    renderThread.join();

    EXPECT_GT(blocksRendered.load(), 0) << "the render loop never ran, so the test proved nothing";
    EXPECT_FALSE(renderThreadFreedSomething.load()) << "an instance was destroyed on the render thread";
    EXPECT_EQ(StubPluginInstance::lastDestructionThread().load(), messageThreadId)
        << "every instance must be freed on the message thread";
}

// ============================================================================
// 7. Stream format
// ============================================================================

TEST(HostedPluginTest, PrepareToPlayPropagates) {
    std::atomic<StubPluginInstance*> lastInstance{nullptr};
    StubBackend backend([&lastInstance] {
        auto instance = std::make_unique<StubPluginInstance>(2, 2);
        lastInstance.store(instance.get());
        return instance;
    });

    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    module.loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); }));

    auto* instance = lastInstance.load();
    ASSERT_NE(instance, nullptr);
    EXPECT_DOUBLE_EQ(instance->preparedSampleRate, kSampleRate) << "the instance must be prepared to OUR rate";
    EXPECT_EQ(instance->preparedBlockSize, kBlockSize);
    const int preparesAtPublish = instance->prepareCount;

    // A device / host stream-format change re-prepares the LIVE instance in place.
    module.prepareToPlay(96000.0, 256);
    EXPECT_DOUBLE_EQ(instance->preparedSampleRate, 96000.0);
    EXPECT_EQ(instance->preparedBlockSize, 256);
    EXPECT_GT(instance->prepareCount, preparesAtPublish);
    EXPECT_TRUE(module.hasInstance()) << "re-preparing must not retire the instance";

    // And a plugin loaded AFTER the change gets the new format, not the constructor default.
    module.loadPlugin(stubDescription("Second"), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.getIdentity().name == "Second" && module.hasInstance(); }));
    EXPECT_DOUBLE_EQ(backend.lastSampleRate, 96000.0);
    EXPECT_EQ(backend.lastBlockSize, 256);
}

TEST(HostedPluginTest, LatencyIsPublishedToTheGraph) {
    StubBackend backend([] {
        auto instance = std::make_unique<StubPluginInstance>(2, 2);
        instance->setLatencySamples(128);
        return instance;
    });

    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_EQ(module.getLatencySamples(), 0);

    module.loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); }));
    EXPECT_EQ(module.getLatencySamples(), 128) << "the host module must report the plugin's latency (TL7-7 builds on "
                                                  "this)";

    module.unloadPlugin();
    EXPECT_EQ(module.getLatencySamples(), 0);
}

// ============================================================================
// 8. Registration — the internal-only checklist
// ============================================================================

TEST(HostedPluginTest, RegisteredButInternalOnly) {
    auto processor = synth::AIStateMapper::createModule("Hosted Plugin");
    ASSERT_NE(processor, nullptr) << "Hosted Plugin must be in the factory so a saved patch round-trips it";
    auto* module = dynamic_cast<HostedPluginModule*>(processor.get());
    ASSERT_NE(module, nullptr);

    EXPECT_EQ(module->getModuleType(), ModuleType::HostedPlugin);
    // THE invariant: 16 in / 16 out at construction, whatever it ends up hosting.
    EXPECT_EQ(module->getTotalNumInputChannels(), HostedPluginModule::kMaxPluginChannels);
    EXPECT_EQ(module->getTotalNumOutputChannels(), HostedPluginModule::kMaxPluginChannels);
    EXPECT_EQ(HostedPluginModule::kMaxPluginChannels, 16);
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(module), "Hosted Plugin");

    // Never offered to a model. (The full golden lives in AIStateMapperTests.)
    EXPECT_FALSE(synth::AIStateMapper::authorableModuleTypes().contains("Hosted Plugin"));

    // Neutral bucket: what it hosts is unknowable from the type alone.
    EXPECT_EQ(synth::ui::categoryFor(ModuleType::HostedPlugin), synth::ui::ModuleCategory::Utility);

    // A bare module has nothing to persist, so it adds no JSON noise.
    EXPECT_TRUE(module->getExtraState().isVoid());
}

TEST(HostedPluginTest, AbsentFromTheLibraryWithAPinnedSizeEstimate) {
    ModuleLibraryComponent library;
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Hosted Plugin"))
        << "Hosted Plugin is internal-only until TL7-3 ships the scan list and the load UX";

    auto processor = synth::AIStateMapper::createModule("Hosted Plugin");
    ASSERT_NE(processor, nullptr);

    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

    const auto estimate = GraphEditor::estimateModuleSize("Hosted Plugin");
    EXPECT_EQ(estimate.x, comp.getWidth());
    EXPECT_EQ(estimate.y, comp.getHeight());
}

TEST(HostedPluginTest, IdentitySerializationCarriesNoPath) {
    // The unit-level twin of the assertion StateRoundTrip makes over a whole patch.
    const auto description = stubDescription("Path Carrier", 7);
    const auto identity = PluginIdentity::fromDescription(description);

    EXPECT_EQ(identity.format, "VST3");
    EXPECT_EQ(identity.name, "Path Carrier");
    EXPECT_EQ(identity.uid, 7);

    const juce::String text = juce::JSON::toString(identity.toVar());
    EXPECT_FALSE(text.contains("nonexistent"));
    EXPECT_FALSE(text.contains(".vst3"));

    EXPECT_EQ(PluginIdentity::fromVar(identity.toVar()), identity);

    // uid-first matching, name only as the uid-less fallback.
    EXPECT_TRUE(identity.matches(description));
    auto renamed = description;
    renamed.name = "Renamed By The User";
    EXPECT_TRUE(identity.matches(renamed)) << "a uid match must survive a rename";
    auto otherFormat = description;
    otherFormat.pluginFormatName = "AudioUnit";
    EXPECT_FALSE(identity.matches(otherFormat)) << "format is part of the identity";
}
