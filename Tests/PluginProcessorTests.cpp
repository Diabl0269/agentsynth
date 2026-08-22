// Tests for VST3/AU plugin support: AudioEngine's Hosted mode, AgentSynthAudioProcessor, and the
// MainComponent engine-ownership split introduced alongside it.
//
// Headless/deterministic constraints (see docs/testing.md and house style in
// MainComponentTests.cpp / StateRoundTripTests.cpp): no real audio device, no network, no sleeps.
// HostMode::Hosted engines never touch the device manager, so they are used wherever a real,
// initialised AudioEngine is required. HostMode::Standalone engines are only ever default- or
// value-constructed and never have initialise() called on them here, to avoid opening real
// hardware (see AudioRenderingTests.cpp / StatusBarTests.cpp for the same convention).

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/MainComponent.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Plugin/Hosting/HostedPluginModule.h"
#include "../Source/Plugin/PluginEditor.h"
#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/UI/Theme/AppLookAndFeel.h"
#include "../Source/UI/Theme/ThemeManager.h"
#include "../Source/UserSettings.h"
#include "StubPluginInstance.h"
#include <atomic>
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <set>
#include <thread>

namespace {

// A provider that never touches the network — used wherever a MainComponent/AI service needs
// *some* provider so construction doesn't fall through to the registry's real Ollama provider.
class NullAIProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "TestNull"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        if (callback)
            callback({}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{false, {}, {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    int requestTimeoutMs = 240000;
};

// Structural fingerprint of a graph node: its ModuleBase::getModuleType() for module nodes, or
// its fixed processor name for non-module nodes (the audio I/O nodes). Deliberately ID-free and
// name-free-for-modules, since ModuleBase::getName() gets renumbered ("Oscillator 1", "Oscillator
// 2", ...) by AudioEngine::updateModuleNames() and two independently-built graphs are not
// guaranteed to renumber identically.
juce::String nodeTypeKey(juce::AudioProcessorGraph::Node* node) {
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        return "Module:" + juce::String(static_cast<int>(module->getModuleType()));
    return "IO:" + node->getProcessor()->getName();
}

std::multiset<juce::String> collectNodeTypeKeys(juce::AudioProcessorGraph& graph) {
    std::multiset<juce::String> keys;
    for (auto* node : graph.getNodes())
        keys.insert(nodeTypeKey(node));
    return keys;
}

bool allFinite(const juce::AudioBuffer<float>& buffer) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(data[i]))
                return false;
    }
    return true;
}

bool allZero(const juce::AudioBuffer<float>& buffer) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (data[i] != 0.0f)
                return false;
    }
    return true;
}

/** Spins until `predicate` holds or the budget runs out; returns what it ended on. The only way to
 *  wait on another thread here without a sleep, and bounded so a broken build fails instead of
 *  hanging the suite. */
template <typename Predicate>
bool spinUntil(Predicate predicate, int timeoutMs = 5000) {
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
    while (!predicate()) {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::yield();
    }
    return true;
}

/** Pumps the message loop until `predicate` holds — the bounded-poll idiom from PluginScanTests.
 *  Needed wherever a plugin instance arrives: every backend (real or stub) posts its creation
 *  callback rather than calling it, so spinUntil's yield-only loop would never see it. */
template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 4000) {
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (juce::Time::getMillisecondCounter() < deadline);
    return predicate();
}

/** Parks the audio thread INSIDE a render pass on demand. Nothing else can hold a pass open from a
 *  test, and holding one open is the only way to observe whether a setter waited for it. */
class RenderPassProbeModule : public ModuleBase {
public:
    RenderPassProbeModule()
        : ModuleBase("Render Probe", 2, 2) {}

    void prepareToPlay(double, int) override {}

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {
        insidePass.store(true, std::memory_order_release);
        // Bounded, so a test that never releases fails rather than wedging the audio thread.
        const auto deadline = juce::Time::getMillisecondCounter() + 5000u;
        while (!release.load(std::memory_order_acquire) && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::yield();
        passFinished.store(true, std::memory_order_release);
    }

    ModuleType getModuleType() const override { return ModuleType::VCA; } // unused by these tests

    std::atomic<bool> insidePass{false};
    std::atomic<bool> release{false};
    std::atomic<bool> passFinished{false};
};

} // namespace

// ============================================================================
// AudioEngine — HostMode::Hosted
// ============================================================================

TEST(AudioEngineHostedModeTest, InitialiseOpensNoDeviceButBuildsNonEmptyGraph) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    // The single most important guarantee: a plugin that grabs the audio hardware fights its host.
    EXPECT_EQ(engine.getDeviceManager().getCurrentAudioDevice(), nullptr);
    EXPECT_GT(engine.getGraph().getNumNodes(), 0);

    engine.shutdown();
}

TEST(AudioEngineHostedModeTest, DefaultPatchWiresIntoAudioOutputBeforeHostPreparesToPlay) {
    // Regression: a default-constructed AudioProcessorGraph reports 0 output channels until
    // something sets its channel layout, and the graph's "Audio Output" IO node snapshots that
    // count once, the moment it's added. In hosted mode the real channel count wasn't known until
    // prepareForHost() (called from the host's first prepareToPlay()), which runs strictly after
    // initialise() builds the default patch — so every connection into Audio Output (the shipped
    // Default preset's Reverb -> Output wires included) was silently rejected as out-of-range.
    // Deliberately do NOT call prepareForHost() here: the bug only reproduces before it runs.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    juce::AudioProcessorGraph::Node* outputNode = nullptr;
    for (auto* node : engine.getGraph().getNodes())
        if (node->getProcessor()->getName() == "Audio Output")
            outputNode = node;
    ASSERT_NE(outputNode, nullptr) << "default patch must contain an Audio Output node";

    int connectionsIntoOutput = 0;
    for (const auto& connection : engine.getGraph().getConnections())
        if (connection.destination.nodeID == outputNode->nodeID)
            ++connectionsIntoOutput;

    EXPECT_GE(connectionsIntoOutput, 2)
        << "default patch's audio chain must reach both output channels on load, before the host "
           "ever calls prepareToPlay()";

    engine.shutdown();
}

TEST(AudioEngineHostedModeTest, IsHostedReflectsConstructorArgument) {
    AudioEngine defaultEngine; // no arg — must default to Standalone
    EXPECT_FALSE(defaultEngine.isHosted());
    EXPECT_EQ(defaultEngine.getHostMode(), AudioEngine::HostMode::Standalone);

    AudioEngine standaloneEngine(AudioEngine::HostMode::Standalone);
    EXPECT_FALSE(standaloneEngine.isHosted());

    AudioEngine hostedEngine(AudioEngine::HostMode::Hosted);
    EXPECT_TRUE(hostedEngine.isHosted());
    EXPECT_EQ(hostedEngine.getHostMode(), AudioEngine::HostMode::Hosted);
}

TEST(AudioEngineHostedModeTest, ProcessHostBlockProducesFiniteOutput) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);

    juce::MidiBuffer midi;
    for (int block = 0; block < 4; ++block) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear(); // mirrors AgentSynthAudioProcessor::processBlock zeroing before the graph runs
        EXPECT_NO_THROW(engine.processHostBlock(buffer, midi));
        EXPECT_TRUE(allFinite(buffer)) << "block " << block << " produced non-finite output";
    }

    engine.releaseFromHost();
    engine.shutdown();
}

TEST(AudioEngineHostedModeTest, MasterMuteForcesSilenceInHostedBlock) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    juce::MidiBuffer midi;

    engine.setMasterMute(true);
    juce::AudioBuffer<float> mutedBuffer(2, 512);
    mutedBuffer.clear();
    engine.processHostBlock(mutedBuffer, midi);
    EXPECT_TRUE(allZero(mutedBuffer)) << "master mute must produce exact silence in hosted mode";

    // Unmuted: only assert finiteness. The default patch's audio content (whether it's
    // instantaneously silent at t=0) is not something this test should pin down.
    engine.setMasterMute(false);
    juce::AudioBuffer<float> unmutedBuffer(2, 512);
    unmutedBuffer.clear();
    engine.processHostBlock(unmutedBuffer, midi);
    EXPECT_TRUE(allFinite(unmutedBuffer)) << "unmuted hosted output must remain finite";

    engine.releaseFromHost();
    engine.shutdown();
}

TEST(AudioEngineHostedModeTest, EnsureMidiDeviceOpenIsNoOpWhenHosted) {
    // NOTE: midiInputs is private, so this cannot directly assert "no input was opened". It can
    // only prove the call is safe (no throw/crash) when routed through the isHosted() early
    // return. A bogus device name is used deliberately so the assertion doesn't depend on which
    // real MIDI devices (if any) happen to be present on the machine running the test.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    EXPECT_NO_THROW(engine.ensureMidiDeviceOpen("Definitely-Not-A-Real-MIDI-Device-92348"));
    EXPECT_NO_THROW(engine.ensureMidiDeviceOpen({}));
    EXPECT_EQ(engine.getDeviceManager().getCurrentAudioDevice(), nullptr);

    engine.shutdown();
}

// ============================================================================
// AudioEngine — the borrowed-pointer teardown handshake
// ============================================================================

TEST(AudioEngineHostedModeTest, BorrowedPointerSetterWaitsForAnInFlightRenderPass) {
    // The plugin path's teardown race: the engine belongs to the processor and KEEPS RENDERING
    // after the editor's MainComponent — which owns the MIDI capture sink and the automation
    // recorder — is destroyed. A setter that only stores the null can return while a block already
    // inside renderNextBlock is still dereferencing the old pointer, so both setters drain first.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    auto probeNode = engine.getGraph().addNode(std::make_unique<RenderPassProbeModule>());
    ASSERT_NE(probeNode, nullptr);
    auto* probe = dynamic_cast<RenderPassProbeModule*>(probeNode->getProcessor());
    ASSERT_NE(probe, nullptr);

    engine.prepareForHost(44100.0, 256, 0, 2);

    std::thread audioThread([&] {
        juce::AudioBuffer<float> buffer(2, 256);
        buffer.clear();
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
    });

    ASSERT_TRUE(spinUntil([&] { return probe->insidePass.load(std::memory_order_acquire); }))
        << "the probe never rendered, so this test would prove nothing";

    // Releases the parked pass 50 ms from now: a setter that does NOT wait returns while the pass
    // is provably still inside the graph, which is exactly the window the fix closes.
    std::thread releaser([&] {
        juce::Thread::sleep(50);
        probe->release.store(true, std::memory_order_release);
    });

    engine.setMidiCaptureSink(nullptr);
    EXPECT_TRUE(probe->passFinished.load(std::memory_order_acquire))
        << "setMidiCaptureSink returned while a render pass was still in flight — the owner may now "
           "free a recorder the audio thread is still using";

    releaser.join();
    audioThread.join();
    engine.shutdown();
}

TEST(AudioEngineHostedModeTest, AutomationRecorderSetterDrainsToo) {
    // The sibling of the test above: both borrowed pointers are read inside the same pass, so both
    // setters have to make the same promise.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    auto probeNode = engine.getGraph().addNode(std::make_unique<RenderPassProbeModule>());
    ASSERT_NE(probeNode, nullptr);
    auto* probe = dynamic_cast<RenderPassProbeModule*>(probeNode->getProcessor());
    ASSERT_NE(probe, nullptr);

    engine.prepareForHost(44100.0, 256, 0, 2);

    std::thread audioThread([&] {
        juce::AudioBuffer<float> buffer(2, 256);
        buffer.clear();
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
    });

    ASSERT_TRUE(spinUntil([&] { return probe->insidePass.load(std::memory_order_acquire); }));

    std::thread releaser([&] {
        juce::Thread::sleep(50);
        probe->release.store(true, std::memory_order_release);
    });

    engine.setAutomationRecorder(nullptr);
    EXPECT_TRUE(probe->passFinished.load(std::memory_order_acquire))
        << "setAutomationRecorder returned while a render pass was still in flight";

    releaser.join();
    audioThread.join();
    engine.shutdown();
}

// ============================================================================
// AgentSynthAudioProcessor — basic contract & buses
// ============================================================================

TEST(AgentSynthAudioProcessorTest, BasicContract) {
    synth::AgentSynthAudioProcessor processor;
    EXPECT_TRUE(processor.acceptsMidi());
    EXPECT_FALSE(processor.producesMidi());
    EXPECT_FALSE(processor.isMidiEffect());
    EXPECT_TRUE(processor.hasEditor());
    EXPECT_FALSE(processor.getName().isEmpty());
}

TEST(AgentSynthAudioProcessorTest, BusesLayoutRejectsAnyInput) {
    synth::AgentSynthAudioProcessor processor;
    using Layout = juce::AudioProcessor::BusesLayout;

    Layout monoInStereoOut;
    monoInStereoOut.inputBuses.add(juce::AudioChannelSet::mono());
    monoInStereoOut.outputBuses.add(juce::AudioChannelSet::stereo());
    EXPECT_FALSE(processor.isBusesLayoutSupported(monoInStereoOut));

    Layout stereoInStereoOut;
    stereoInStereoOut.inputBuses.add(juce::AudioChannelSet::stereo());
    stereoInStereoOut.outputBuses.add(juce::AudioChannelSet::stereo());
    EXPECT_FALSE(processor.isBusesLayoutSupported(stereoInStereoOut));
}

TEST(AgentSynthAudioProcessorTest, BusesLayoutAcceptsMonoOrStereoOutputOnly) {
    synth::AgentSynthAudioProcessor processor;
    using Layout = juce::AudioProcessor::BusesLayout;

    Layout monoOut;
    monoOut.outputBuses.add(juce::AudioChannelSet::mono());
    EXPECT_TRUE(processor.isBusesLayoutSupported(monoOut));

    Layout stereoOut;
    stereoOut.outputBuses.add(juce::AudioChannelSet::stereo());
    EXPECT_TRUE(processor.isBusesLayoutSupported(stereoOut));

    Layout quadOut;
    quadOut.outputBuses.add(juce::AudioChannelSet::discreteChannels(4));
    EXPECT_FALSE(processor.isBusesLayoutSupported(quadOut)) << "only mono/stereo output is an instrument bus";
}

// ============================================================================
// AgentSynthAudioProcessor — processBlock
// ============================================================================

TEST(AgentSynthAudioProcessorTest, ProcessBlockProducesFiniteOutputAndClearsMidi) {
    synth::AgentSynthAudioProcessor processor;
    processor.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), 512);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    ASSERT_FALSE(midi.isEmpty());

    EXPECT_NO_THROW(processor.processBlock(buffer, midi));

    EXPECT_TRUE(allFinite(buffer));
    EXPECT_TRUE(midi.isEmpty()) << "producesMidi() is false — nothing the graph left in the buffer may escape";

    processor.releaseResources();
}

// ============================================================================
// AgentSynthAudioProcessor — state save/restore
// ============================================================================

TEST(AgentSynthAudioProcessorStateTest, GraphSurvivesStateRoundTripAcrossInstances) {
    synth::AgentSynthAudioProcessor first;
    auto& graph1 = first.getAudioEngine().getGraph();
    // Distinguish this graph from a vanilla default patch so a setStateInformation() that
    // silently no-ops can't pass this test by coincidence.
    graph1.addNode(std::make_unique<OscillatorModule>());
    const int nodeCount1 = graph1.getNumNodes();

    juce::MemoryBlock state;
    first.getStateInformation(state);
    ASSERT_GT(state.getSize(), 0u);

    synth::AgentSynthAudioProcessor second;
    second.setStateInformation(state.getData(), (int)state.getSize());
    auto& graph2 = second.getAudioEngine().getGraph();

    EXPECT_EQ(graph2.getNumNodes(), nodeCount1);
    EXPECT_EQ(collectNodeTypeKeys(graph2), collectNodeTypeKeys(graph1));
}

TEST(AgentSynthAudioProcessorStateTest, ParameterValuesSurviveStateRoundTripExactly) {
    // Regression: session state used to be APPLIED untrusted, so the untrusted path's [0,1]
    // rescale heuristic (meant for sloppy model output) corrupted exact app-authored values —
    // an LFO rate of 0.5 Hz on the 0.01–20 Hz range reloaded as ~10 Hz. setStateInformation now
    // validates untrusted, then applies trusted (the docs/layout.md §12.5 pairing), so values
    // inside [0,1] on wider ranges must survive bit-for-bit.
    synth::AgentSynthAudioProcessor first;
    auto& graph1 = first.getAudioEngine().getGraph();
    auto lfoNode = graph1.addNode(std::make_unique<LFOModule>());
    auto* rate1 = findParameterByID(lfoNode->getProcessor(), "rateHz");
    ASSERT_NE(rate1, nullptr);
    rate1->setValueNotifyingHost(rate1->convertTo0to1(0.5f)); // 0.5 Hz — the corruption case

    juce::MemoryBlock state;
    first.getStateInformation(state);
    ASSERT_GT(state.getSize(), 0u);

    synth::AgentSynthAudioProcessor second;
    second.setStateInformation(state.getData(), (int)state.getSize());

    juce::RangedAudioParameter* rate2 = nullptr;
    for (auto* node : second.getAudioEngine().getGraph().getNodes())
        if (dynamic_cast<LFOModule*>(node->getProcessor()) != nullptr)
            rate2 = findParameterByID(node->getProcessor(), "rateHz");
    ASSERT_NE(rate2, nullptr) << "LFO node lost in state round-trip";
    EXPECT_NEAR(0.5f, rate2->convertFrom0to1(rate2->getValue()), 1e-3f)
        << "untrusted rescale heuristic corrupted an exact session value";
}

TEST(AgentSynthAudioProcessorStateTest, EditorSizeSurvivesStateRoundTrip) {
    synth::AgentSynthAudioProcessor first;
    first.setSavedEditorSize({1234, 777});

    juce::MemoryBlock state;
    first.getStateInformation(state);

    synth::AgentSynthAudioProcessor second;
    second.setStateInformation(state.getData(), (int)state.getSize());

    EXPECT_EQ(second.getSavedEditorSize().x, 1234);
    EXPECT_EQ(second.getSavedEditorSize().y, 777);
}

class AgentSynthAudioProcessorJunkStateTest : public ::testing::Test {
protected:
    void SetUp() override { processor = std::make_unique<synth::AgentSynthAudioProcessor>(); }
    std::unique_ptr<synth::AgentSynthAudioProcessor> processor;
};

TEST_F(AgentSynthAudioProcessorJunkStateTest, IgnoresNullData) {
    const int nodesBefore = processor->getAudioEngine().getGraph().getNumNodes();
    EXPECT_NO_THROW(processor->setStateInformation(nullptr, 0));
    EXPECT_EQ(processor->getAudioEngine().getGraph().getNumNodes(), nodesBefore);
}

TEST_F(AgentSynthAudioProcessorJunkStateTest, IgnoresEmptyData) {
    const int nodesBefore = processor->getAudioEngine().getGraph().getNumNodes();
    const char empty[] = "";
    EXPECT_NO_THROW(processor->setStateInformation(empty, 0));
    EXPECT_EQ(processor->getAudioEngine().getGraph().getNumNodes(), nodesBefore);
}

TEST_F(AgentSynthAudioProcessorJunkStateTest, IgnoresNonJSONBytes) {
    const int nodesBefore = processor->getAudioEngine().getGraph().getNumNodes();
    const unsigned char garbage[] = {0x00, 0x01, 0xFF, 0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_NO_THROW(processor->setStateInformation(garbage, (int)sizeof(garbage)));
    EXPECT_EQ(processor->getAudioEngine().getGraph().getNumNodes(), nodesBefore);
}

TEST_F(AgentSynthAudioProcessorJunkStateTest, IgnoresValidJSONThatIsNotAnObject) {
    // NOTE: juce::var::isObject() is true for both DynamicObject-backed JSON objects AND
    // JSON arrays (both are internally reference-counted "object" var types — see
    // juce_Variant.cpp's VariantType(ArrayTag)), so a top-level array slips past
    // setStateInformation's `!parsed.isObject()` guard and AIStateMapper::applyJSONToGraph's
    // `!json.isObject()` guard. It is still safely rejected one line later in
    // applyJSONToGraph, whose `json.getDynamicObject() == nullptr` check catches arrays before
    // validatePatch (and therefore before any graph mutation) ever runs. Net effect for this
    // test either way: no crash, no partial apply.
    const int nodesBefore = processor->getAudioEngine().getGraph().getNumNodes();
    const juce::String jsonArray = "[1, 2, 3]";
    EXPECT_NO_THROW(processor->setStateInformation(jsonArray.toRawUTF8(), (int)jsonArray.getNumBytesAsUTF8()));
    EXPECT_EQ(processor->getAudioEngine().getGraph().getNumNodes(), nodesBefore);
}

TEST_F(AgentSynthAudioProcessorJunkStateTest, RejectsPatchExceedingValidationLimitsWithoutPartiallyApplying) {
    const int nodesBefore = processor->getAudioEngine().getGraph().getNumNodes();

    // One node entry over AIStateMapper::kMaxNodes — trusted=false on the processor's
    // setStateInformation path means this must be rejected wholesale by validatePatch(), never
    // partially applied. Mirrors the TooManyNodes case in AIPatchValidationTests.cpp.
    juce::DynamicObject::Ptr n = new juce::DynamicObject();
    n->setProperty("id", 1);
    n->setProperty("type", "Oscillator");
    juce::Array<juce::var> nodes;
    for (int i = 0; i < synth::AIStateMapper::kMaxNodes + 1; ++i)
        nodes.add(juce::var(n.get()));
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", nodes);
    const juce::String json = juce::JSON::toString(juce::var(root.get()));

    EXPECT_NO_THROW(processor->setStateInformation(json.toRawUTF8(), (int)json.getNumBytesAsUTF8()));
    EXPECT_EQ(processor->getAudioEngine().getGraph().getNumNodes(), nodesBefore)
        << "an over-limit patch must be rejected outright, not partially applied";
}

// ============================================================================
// MainComponent — engine ownership split (the key regression guard for the plugin refactor)
// ============================================================================

TEST(MainComponentEngineOwnershipTest, ExternalEngineSurvivesMainComponentDestruction) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;

    // Mirrors AgentSynthAudioProcessor's own ctor: Hosted mode, initialise() called once, up
    // front, by whoever owns the engine — never by MainComponent.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    ASSERT_GT(engine.getGraph().getNumNodes(), 0);
    const int nodeCountBefore = engine.getGraph().getNumNodes();

    {
        MainComponent mc(tm, lf, engine, std::make_unique<NullAIProvider>());
        // mc goes out of scope here. Its destructor must take the "external engine" branch and
        // leave `engine` completely alone.
    }

    EXPECT_EQ(engine.getGraph().getNumNodes(), nodeCountBefore)
        << "MainComponent must not shut down an AudioEngine it does not own";

    engine.shutdown();
}

TEST(MainComponentEngineOwnershipTest, DestructionWhileTheHostKeepsRenderingIsSafe) {
    // The end-to-end shape of the race the drain closes, and the ASan/TSan probe for it: the host
    // never stops calling processBlock, and the editor (with the recorder and the capture sink it
    // owns) is destroyed underneath it.
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;

    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 256, 0, 2);

    auto mc = std::make_unique<MainComponent>(tm, lf, engine, std::make_unique<NullAIProvider>());

    std::atomic<bool> stop{false};
    std::atomic<int> blocksRendered{0};
    std::thread renderThread([&] {
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        while (!stop.load(std::memory_order_acquire)) {
            buffer.clear();
            engine.processHostBlock(buffer, midi);
            blocksRendered.fetch_add(1, std::memory_order_relaxed);
        }
    });

    ASSERT_TRUE(spinUntil([&] { return blocksRendered.load(std::memory_order_relaxed) > 4; }))
        << "the render loop never ran, so this test would prove nothing";

    // The destructor unregisters both borrowed pointers (each draining) and only then lets the
    // recorder and the capture sink be destroyed.
    mc.reset();

    const int atTeardown = blocksRendered.load(std::memory_order_relaxed);
    EXPECT_TRUE(spinUntil([&] { return blocksRendered.load(std::memory_order_relaxed) > atTeardown + 4; }))
        << "the engine must keep rendering after its editor is gone";

    stop.store(true, std::memory_order_release);
    renderThread.join();
    engine.shutdown();
}

// NOTE on the contrast case ("the owning ctors DO tear their engine down"):
// Skipped. It cannot be checked without either (a) opening a real audio device — MainComponent's
// owning constructors hardcode HostMode::Standalone, so exercising that path calls
// AudioEngine::initialise() -> deviceManager.initialiseWithDefaultDevices(), which is exactly the
// real-hardware dependency this test file is required to avoid — or (b) inspecting the owned
// AudioEngine after MainComponent's destructor has run, which is a use-after-free: the owned
// engine is a member (via ownedAudioEngine) destroyed as part of the same destructor call, so
// there is no safe point at which a black-box test can observe "the graph was cleared" without
// reading freed memory. AudioEngine::shutdown() is not virtual/instrumentable, so no test double
// can intercept the call either. See the report for how this was reasoned through.

// ============================================================================
// Hosted-plugin identity resolution with NO editor — the DAW-restores-a-session path
// ============================================================================

namespace {

constexpr const char* kAlphaFile = "/plugins/Alpha.vst3";
constexpr int kAlphaUid = 0xA1FA;

/** The real backend's resolveIdentity (so the processor's scan service is genuinely in the loop)
 *  with stub instance creation (CI has no third-party binary to load). Same split as
 *  PluginScanTests' ScanningStubBackend. */
class ScanListStubBackend : public synth::DefaultHostedPluginBackend {
public:
    using synth::DefaultHostedPluginBackend::createInstanceAsync;

    void createInstanceAsync(const juce::PluginDescription& description, double, int,
                             InstanceCallback callback) override {
        if (callback == nullptr)
            return;
        lastDescription = description;
        auto sharedCallback = std::make_shared<InstanceCallback>(std::move(callback));
        juce::MessageManager::callAsync([sharedCallback] {
            (*sharedCallback)(std::make_unique<synth::test::StubPluginInstance>(2, 2), juce::String());
        });
    }

    juce::PluginDescription lastDescription;
};

/** What a completed scan leaves in the user setting: juce::KnownPluginList's own XML. */
juce::String scanListXml() {
    juce::PluginDescription description;
    description.name = "Alpha";
    description.pluginFormatName = "VST3";
    description.uniqueId = kAlphaUid;
    description.deprecatedUid = kAlphaUid;
    description.fileOrIdentifier = kAlphaFile;
    description.manufacturerName = "Test Labs";
    description.version = "1.0.0";

    juce::KnownPluginList list;
    list.addType(description);
    auto xml = list.createXml();
    return xml != nullptr ? xml->toString() : juce::String();
}

synth::HostedPluginModule* findHostedModule(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* module = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()))
            return module;
    return nullptr;
}

synth::PluginIdentity alphaIdentity() {
    synth::PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "Alpha";
    identity.uid = kAlphaUid;
    return identity;
}

// A session whose patch names the scanned plugin. Hand-written rather than round-tripped so the
// test does not depend on the very resolution it is checking to author its own input.
const char* kSessionPatchJson =
    R"({"nodes":[{"id":1,"type":"Hosted Plugin","state":{"pluginFormat":"VST3","pluginName":"Alpha","pluginUid":41466}}],)"
    R"("connections":[]})";

} // namespace

/** Seeds the persisted scan list the way a completed standalone scan would, and puts the setting
 *  back afterwards — this is the real user settings file (same convention as MainComponentTests). */
class ProcessorScanListTest : public ::testing::Test {
protected:
    void SetUp() override {
        properties.setStorageParameters(synth::userSettingsOptions());
        previousValue = settings().getValue(synth::kPluginScanListSettingKey);
        settings().setValue(synth::kPluginScanListSettingKey, scanListXml());
        settings().saveIfNeeded();
    }

    void TearDown() override {
        if (previousValue.isEmpty())
            settings().removeValue(synth::kPluginScanListSettingKey);
        else
            settings().setValue(synth::kPluginScanListSettingKey, previousValue);
        settings().saveIfNeeded();
    }

    juce::PropertiesFile& settings() { return *properties.getUserSettings(); }

    juce::ApplicationProperties properties;
    juce::String previousValue;
};

TEST_F(ProcessorScanListTest, RestoresAHostedPluginWithNoEditorEverOpened) {
    ScanListStubBackend backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    synth::AgentSynthAudioProcessor processor;
    ASSERT_EQ(processor.getActiveEditor(), nullptr) << "the whole point: no editor, ever";

    // The processor — not MainComponent — restored the list and installed it as the resolver.
    EXPECT_EQ(backend.getScanService(), &processor.getPluginScanService());
    EXPECT_EQ(processor.getPluginScanService().getNumKnownPlugins(), 1);

    processor.prepareToPlay(48000.0, 64);
    processor.setStateInformation(kSessionPatchJson, (int)juce::String(kSessionPatchJson).getNumBytesAsUTF8());

    auto* module = findHostedModule(processor.getAudioEngine().getGraph());
    ASSERT_NE(module, nullptr) << "the Hosted Plugin node must come back from the session state";
    EXPECT_EQ(module->getIdentity(), alphaIdentity());
    ASSERT_TRUE(pumpUntil([&] { return module->hasInstance(); }))
        << "the identity never resolved: " << module->getStatusMessage();
    EXPECT_EQ(backend.lastDescription.fileOrIdentifier, kAlphaFile)
        << "resolution must go through the persisted scan list, path and all";
}

TEST_F(ProcessorScanListTest, AnEditorAdoptsTheProcessorsResolverInsteadOfReplacingIt) {
    ScanListStubBackend backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    synth::AgentSynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);

    {
        // Opening the plugin window: the editor's MainComponent runs on the processor's engine.
        MainComponent editor(processor.getThemeManager(), processor.getLookAndFeel(), processor.getAudioEngine(),
                             std::make_unique<NullAIProvider>());
        EXPECT_EQ(&editor.getPluginScanService(), &processor.getPluginScanService())
            << "an editor on an external engine must adopt the installed service";
        EXPECT_EQ(backend.getScanService(), &processor.getPluginScanService());
        EXPECT_EQ(editor.getModuleLibrary().getPluginCount(), 1) << "...and show its plugins in the sidebar";
    }

    // Closing the window must not take the session's resolver with it.
    EXPECT_EQ(backend.getScanService(), &processor.getPluginScanService());

    processor.setStateInformation(kSessionPatchJson, (int)juce::String(kSessionPatchJson).getNumBytesAsUTF8());
    auto* module = findHostedModule(processor.getAudioEngine().getGraph());
    ASSERT_NE(module, nullptr);
    EXPECT_TRUE(pumpUntil([&] { return module->hasInstance(); }))
        << "resolution must survive an editor being opened and closed: " << module->getStatusMessage();
}
