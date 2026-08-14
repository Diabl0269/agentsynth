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
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Plugin/PluginEditor.h"
#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/UI/Theme/AppLookAndFeel.h"
#include "../Source/UI/Theme/ThemeManager.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <set>

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
