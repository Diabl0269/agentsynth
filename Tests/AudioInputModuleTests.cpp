// TL6-2: "Audio Input" as a real module — the device's input reaching the patch through the
// playhead, the jack count following the device, and every compatibility promise the swap made.
//
// Two layers, deliberately:
//   * MODULE layer — an AudioInputModule driven directly, with a bare synth::TransportService
//     installed as its playhead. That is exactly what AudioEngine does per block, minus the engine,
//     so these tests pin the module's own contract (copy, clear, clamp, bypass) in any build.
//   * ENGINE layer — the whole path, standalone and hosted. These need the playhead the ENGINE
//     installs, which is compiled out of a SYNTH_ENABLE_TIMELINE=OFF build (AudioEngine's ctor
//     gates setPlayHead; AudioEngineTransportTest.FlagOffTransportNeverAdvances pins that it must
//     stay gated). So they are gated too, and the OFF build's Audio Input node renders silence —
//     acceptable because OFF is a revertibility check, not a shipping configuration. See the note
//     in docs/architecture.md.
//
// Headless house rules as everywhere else: no real device (Tests/FakeAudioIODevice.h), no sleeps.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/AudioInputModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/SnippetManager.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include "FakeAudioIODevice.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

using synth::test::FakeAudioIODevice;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate;
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

/** A ramp / a sine, so a passthrough assertion fails loudly on an off-by-one or a swapped channel. */
std::vector<float> makeRamp(int numSamples, float scale = 1.0f) {
    std::vector<float> data((std::size_t)numSamples);
    for (int i = 0; i < numSamples; ++i)
        data[(std::size_t)i] = scale * (float)i / (float)numSamples;
    return data;
}

std::vector<float> makeSine(int numSamples) {
    std::vector<float> data((std::size_t)numSamples);
    for (int i = 0; i < numSamples; ++i)
        data[(std::size_t)i] = std::sin(juce::MathConstants<float>::twoPi * 3.0f * (float)i / (float)numSamples);
    return data;
}

constexpr float kSentinel = -9999.0f;

bool channelIsSilent(const juce::AudioBuffer<float>& buffer, int channel) {
    const float* data = buffer.getReadPointer(channel);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (std::abs(data[i]) > 1.0e-7f)
            return false;
    return true;
}

/** The module layer's harness: a module with a transport on its playhead, whose device-input
 *  context the test sets by hand exactly as AudioEngine::renderPass would. */
struct ModuleRig {
    AudioInputModule module;
    synth::TransportService transport;
    juce::AudioBuffer<float> buffer{AudioInputModule::kMaxChannels, kBlockSize};
    juce::MidiBuffer midi;

    ModuleRig() {
        module.setPlayHead(&transport);
        module.prepareToPlay(kSampleRate, kBlockSize);
        // TL6-7: this harness exists to pin the module's own copy/clear/clamp/bypass contract, which
        // is orthogonal to the monitoring gate — enable monitoring unconditionally so every existing
        // assertion here keeps meaning what it always meant. The gate itself is Tests/FeedbackGuardTests.cpp's job.
        transport.setInputMonitoringEnabledForBlock(true);
    }

    /** Publishes `channels` of device input for one block and renders it. Channel c carries a ramp
     *  scaled by (c + 1), so a swapped channel is visible in the value itself. */
    void renderWithDeviceChannels(int channels) {
        std::vector<std::vector<float>> input;
        std::vector<const float*> pointers;
        for (int c = 0; c < channels; ++c)
            input.push_back(makeRamp(kBlockSize, (float)(c + 1)));
        for (auto& channel : input)
            pointers.push_back(channel.data());

        transport.setDeviceInputForBlock(channels > 0 ? pointers.data() : nullptr, channels, kBlockSize);

        for (int c = 0; c < buffer.getNumChannels(); ++c)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(c), kSentinel, kBlockSize);

        module.processBlock(buffer, midi);
        transport.setDeviceInputForBlock(nullptr, 0, 0);
    }
};

juce::AudioProcessorGraph::Node* findNodeNamed(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node;
    return nullptr;
}

bool hasConnection(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID src, int srcChannel) {
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == src && c.source.channelIndex == srcChannel)
            return true;
    return false;
}

} // namespace

// ============================================================================
// Module layer — the port model
// ============================================================================

TEST(AudioInputModuleTest, VisiblePortsFollowDevice) {
    ModuleRig rig;

    EXPECT_EQ(rig.module.getVisibleOutputPortCount(), 1) << "a module that has never seen a block shows one jack";
    EXPECT_EQ(rig.module.getTotalNumOutputChannels(), AudioInputModule::kMaxChannels)
        << "the CHANNEL count is fixed for the node's lifetime — only the visible jack count varies";
    EXPECT_EQ(rig.module.getVisibleInputPortCount(), 0);

    rig.renderWithDeviceChannels(2);
    EXPECT_EQ(rig.module.getVisibleOutputPortCount(), 2);

    rig.renderWithDeviceChannels(0);
    EXPECT_EQ(rig.module.getVisibleOutputPortCount(), 1)
        << "no input device must still leave one jack — a patch has to show where input would arrive";

    // More channels than the module can carry: the transport clamps on publish and the module
    // clamps again on read, so the jack count can never exceed the channels the node actually has.
    rig.renderWithDeviceChannels(AudioInputModule::kMaxChannels + 4);
    EXPECT_EQ(rig.module.getVisibleOutputPortCount(), AudioInputModule::kMaxChannels);

    rig.renderWithDeviceChannels(2);
    EXPECT_EQ(rig.module.getVisibleOutputPortCount(), 2) << "shrinking back must shrink the jack count back";
}

TEST(AudioInputModuleTest, HiddenChannelsCleared) {
    ModuleRig rig;
    rig.renderWithDeviceChannels(2);

    ASSERT_EQ(rig.module.getVisibleOutputPortCount(), 2);

    for (int i = 0; i < kBlockSize; ++i) {
        ASSERT_NEAR(rig.buffer.getSample(0, i), (float)i / (float)kBlockSize, 1.0e-6f);
        ASSERT_NEAR(rig.buffer.getSample(1, i), 2.0f * (float)i / (float)kBlockSize, 1.0e-6f);
    }

    for (int channel = 2; channel < AudioInputModule::kMaxChannels; ++channel)
        EXPECT_TRUE(channelIsSilent(rig.buffer, channel))
            << "channel " << channel
            << " is hidden with a 2-input device and must be cleared every block — a hidden jack that still "
               "carried signal would be a routing nobody can unplug";
}

TEST(AudioInputModuleTest, BypassClears) {
    ModuleRig rig;

    auto* bypassed = findParameterByID(&rig.module, "bypassed");
    ASSERT_NE(bypassed, nullptr);
    bypassed->setValueNotifyingHost(1.0f);

    rig.renderWithDeviceChannels(2);

    for (int channel = 0; channel < AudioInputModule::kMaxChannels; ++channel)
        EXPECT_TRUE(channelIsSilent(rig.buffer, channel))
            << "channel " << channel
            << ": a pure source has no dry path, so bypass clears (docs/architecture.md's bypass/mute contract)";

    EXPECT_EQ(findParameterByID(&rig.module, "muted"), nullptr)
        << "no mute parameter by design — bypass already silences everything there is to silence";
}

TEST(AudioInputModuleTest, NoTransportRendersSilence) {
    // A foreign host's playhead, no playhead at all, or a SYNTH_ENABLE_TIMELINE=OFF build: there is
    // no context to read, so the module goes quiet rather than replaying whatever was in the buffer.
    AudioInputModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(AudioInputModule::kMaxChannels, kBlockSize);
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(c), kSentinel, kBlockSize);

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        EXPECT_TRUE(channelIsSilent(buffer, channel)) << "channel " << channel;
    EXPECT_EQ(module.getVisibleOutputPortCount(), 1);
}

// ============================================================================
// Engine layer — the whole path (see the file header for the flag gate)
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

TEST(AudioInputModuleTest, DeviceInputFlowsThroughModule) {
    // Standalone, never initialise()d: the device callback is called by hand, so no real device is
    // opened. The wiring is CROSSED (input ch1 -> output ch0) on purpose: the callback copies the
    // device's input into the render buffer channel-for-channel, so a straight-through assertion
    // could pass on that copy alone. Only the module can put the RIGHT input on the LEFT speaker.
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

    auto in = graph.addNode(std::make_unique<AudioInputModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    graph.addConnection({{in->nodeID, 1}, {out->nodeID, 0}});
    graph.addConnection({{in->nodeID, 0}, {out->nodeID, 1}});

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    // TL6-7: this test is about the copy path (crossed wiring, snapshot vs. render buffer), which is
    // orthogonal to the monitoring gate — enable it so the module's output isn't gated here too. The
    // gate itself is Tests/FeedbackGuardTests.cpp's job.
    engine.setInputMonitoringEnabled(true);

    auto inLeft = makeRamp(kBlockSize);
    auto inRight = makeSine(kBlockSize);
    std::vector<float> outLeft((std::size_t)kBlockSize, kSentinel), outRight((std::size_t)kBlockSize, kSentinel);

    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    for (int i = 0; i < kBlockSize; ++i) {
        ASSERT_NEAR(outLeft[(std::size_t)i], inRight[(std::size_t)i], 1.0e-6f)
            << "device input channel 1 must reach the module and come out of the jack it was patched "
               "to (sample "
            << i << ")";
        ASSERT_NEAR(outRight[(std::size_t)i], inLeft[(std::size_t)i], 1.0e-6f);
    }

    auto* module = dynamic_cast<AudioInputModule*>(graph.getNodeForId(in->nodeID)->getProcessor());
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2) << "one rendered block is enough to learn the device's width";

    engine.audioDeviceStopped();
}

TEST(AudioInputModuleTest, HostedInputFlows) {
    // Hosted: the host hands us ONE buffer that is both its input and its output, and the graph
    // renders over it in place. The module must still emit the ORIGINAL input — that is the whole
    // reason the engine keeps a private input snapshot instead of pointing at the render buffer.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

    auto in = graph.addNode(std::make_unique<AudioInputModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    graph.addConnection({{in->nodeID, 1}, {out->nodeID, 0}});
    graph.addConnection({{in->nodeID, 0}, {out->nodeID, 1}});

    engine.prepareForHost(kSampleRate, kBlockSize, 2, 2);
    EXPECT_EQ(engine.getDeviceInputChannelCount(), 2);
    // TL6-7: orthogonal to what this test pins (the snapshot survives the host's in-place render) —
    // see the comment on DeviceInputFlowsThroughModule above.
    engine.setInputMonitoringEnabled(true);

    auto hostLeft = makeRamp(kBlockSize);
    auto hostRight = makeSine(kBlockSize);
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    std::copy(hostLeft.begin(), hostLeft.end(), buffer.getWritePointer(0));
    std::copy(hostRight.begin(), hostRight.end(), buffer.getWritePointer(1));

    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);

    for (int i = 0; i < kBlockSize; ++i) {
        ASSERT_NEAR(buffer.getSample(0, i), hostRight[(std::size_t)i], 1.0e-6f)
            << "the host's input channel 1 must survive the graph rendering over the same buffer (sample " << i << ")";
        ASSERT_NEAR(buffer.getSample(1, i), hostLeft[(std::size_t)i], 1.0e-6f);
    }

    engine.releaseFromHost();
}

TEST(AudioInputModuleTest, DefaultPatchUsesTheModule) {
    // Hosted so initialise() touches no hardware. Whatever the default patch comes from (the bundled
    // preset or createDefaultPatch's fallback), its "Audio Input" node must be the module.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    auto* node = findNodeNamed(engine.getGraph(), "Audio Input");
    ASSERT_NE(node, nullptr) << "the default patch must still have an Audio Input node";
    EXPECT_NE(dynamic_cast<AudioInputModule*>(node->getProcessor()), nullptr)
        << "the default patch must build the module, not the graph's raw audioInputNode";
    EXPECT_EQ(dynamic_cast<IOProcessor*>(node->getProcessor()), nullptr);

    engine.shutdown();
}

#endif // SYNTH_ENABLE_TIMELINE

// ============================================================================
// Compatibility: the factory key, the on-disk type name, the singleton rule
// ============================================================================

TEST(AudioInputModuleTest, FactoryBuildsTheModule) {
    auto processor = synth::AIStateMapper::createModule("Audio Input");
    ASSERT_NE(processor, nullptr);
    EXPECT_NE(dynamic_cast<AudioInputModule*>(processor.get()), nullptr);
    EXPECT_EQ(processor->getName(), "Audio Input");
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(processor.get()), "Audio Input")
        << "the type string on disk must not change, or every saved patch loses its input node";

    // The numbered-suffix fallback every other module gets.
    EXPECT_NE(dynamic_cast<AudioInputModule*>(synth::AIStateMapper::createModule("Audio Input 1").get()), nullptr);
}

TEST(AudioInputModuleTest, LegacyPatchLoads) {
    // Shaped exactly like a patch saved BEFORE TL6-2, when "Audio Input" was the graph's raw
    // audioInputNode: same type string, same channel indices on the connection. It must load onto
    // the module with the wire intact — that is the entire compatibility story.
    const juce::String legacy = R"({
      "nodes": [
        {"id": 1, "type": "Audio Input", "position": {"x": 10, "y": 10}},
        {"id": 2, "type": "Audio Output", "position": {"x": 500, "y": 10}}
      ],
      "connections": [
        {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
        {"src": 1, "srcPort": 1, "dst": 2, "dstPort": 1}
      ]
    })";

    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

    auto json = juce::JSON::parse(legacy);
    ASSERT_TRUE(json.isObject());
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));

    auto* node = findNodeNamed(graph, "Audio Input");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(dynamic_cast<AudioInputModule*>(node->getProcessor()), nullptr);

    EXPECT_TRUE(hasConnection(graph, node->nodeID, 0)) << "channel 0's wire must survive the swap";
    EXPECT_TRUE(hasConnection(graph, node->nodeID, 1)) << "channel 1's wire must survive the swap";

    // ... and saving it again emits the very same type string, so the round trip is closed.
    auto saved = synth::AIStateMapper::graphToJSON(graph);
    auto* nodes = saved.getDynamicObject()->getProperty("nodes").getArray();
    ASSERT_NE(nodes, nullptr);
    bool sawAudioInput = false;
    for (const auto& entry : *nodes)
        if (entry.getDynamicObject()->getProperty("type").toString() == "Audio Input")
            sawAudioInput = true;
    EXPECT_TRUE(sawAudioInput);
}

TEST(AudioInputModuleTest, SingletonRuleStillHolds) {
    EXPECT_TRUE(GraphEditor::isSingletonIOModule("Audio Input"));

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.addNode(std::make_unique<AudioInputModule>());
    editor.updateComponents();

    // graphHasModuleNamed matches on the processor's display name, which is how the singleton guard
    // recognises an existing node — the module has to answer to the same name the IO node did.
    EXPECT_TRUE(GraphEditor::graphHasModuleNamed(graph, "Audio Input"));

    const int before = graph.getNodes().size();

    juce::Component dragSource;
    juce::var description("Audio Input");
    juce::DragAndDropTarget::SourceDetails details(description, &dragSource, juce::Point<int>(200, 200));
    editor.itemDropped(details);

    EXPECT_EQ(graph.getNodes().size(), before) << "a second Audio Input drop must stay a no-op";

    // A snippet must not smuggle one in either: it is a singleton wherever it is inserted, and the
    // "is it an IO processor?" test that used to exclude it no longer matches a ModuleBase.
    auto* inputNode = findNodeNamed(graph, "Audio Input");
    ASSERT_NE(inputNode, nullptr);
    auto snippet = synth::SnippetManager::extractSnippet(graph, {inputNode->nodeID}, "io");
    ASSERT_TRUE(snippet.isObject());
    auto* snippetNodes = snippet.getDynamicObject()->getProperty("nodes").getArray();
    ASSERT_NE(snippetNodes, nullptr);
    EXPECT_EQ(snippetNodes->size(), 0) << "Audio Input is not snippet-eligible — it is a singleton";

    engine.shutdown();
}

// ============================================================================
// Owner drop: a device that shrinks unplugs the jacks it took away
// ============================================================================

TEST(AudioInputModuleTest, DeviceShrinkDropsHiddenRoutings) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(8, 2, kSampleRate, kBlockSize);

    auto in = graph.addNode(std::make_unique<AudioInputModule>());
    auto a = graph.addNode(std::make_unique<FilterModule>());
    auto b = graph.addNode(std::make_unique<FilterModule>());
    auto c = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_TRUE(graph.addConnection({{in->nodeID, 0}, {a->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{in->nodeID, 1}, {b->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{in->nodeID, 3}, {c->nodeID, 0}}));

    GraphEditor editor(engine);
    editor.setSize(1200, 800);
    editor.updateComponents();

    // An 8-in interface: every jack is visible, so nothing is dropped and the card is taller.
    FakeAudioIODevice wide(8, 2);
    engine.audioDeviceAboutToStart(&wide);
    ASSERT_EQ(engine.getDeviceInputChannelCount(), 8);
    editor.refreshIoModulesAfterDeviceChange();

    auto* module = dynamic_cast<AudioInputModule*>(graph.getNodeForId(in->nodeID)->getProcessor());
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 8);
    EXPECT_TRUE(hasConnection(graph, in->nodeID, 3)) << "channel 3 is a visible jack on an 8-in device";

    int tallHeight = 0;
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == in->nodeID)
            tallHeight = comp->getHeight();
    EXPECT_EQ(tallHeight, 217) << "eight jacks measure 217px — the port gutter alone sets the card height, since the "
                                  "module has no controls (see GraphEditor::estimateModuleSize's note)";

    // The user switches to a 2-in device. Channel 3's jack no longer exists, so its cable cannot be
    // unplugged by hand — the owner has to drop it.
    engine.audioDeviceStopped();
    FakeAudioIODevice narrow(2, 2);
    engine.audioDeviceAboutToStart(&narrow);
    ASSERT_EQ(engine.getDeviceInputChannelCount(), 2);
    editor.refreshIoModulesAfterDeviceChange();

    EXPECT_EQ(module->getVisibleOutputPortCount(), 2);
    EXPECT_TRUE(hasConnection(graph, in->nodeID, 0)) << "channel 0 is still visible and must keep its cable";
    EXPECT_TRUE(hasConnection(graph, in->nodeID, 1)) << "channel 1 is still visible and must keep its cable";
    EXPECT_FALSE(hasConnection(graph, in->nodeID, 3))
        << "channel 3's jack disappeared with the device — its routing must go with it";

    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == in->nodeID)
            EXPECT_LT(comp->getHeight(), tallHeight) << "fewer jacks must give a shorter card";

    engine.audioDeviceStopped();
}
