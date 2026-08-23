// ModuleMidiFlagsTests.cpp
//
// Part of the MIDI-flag audit: ModuleBase defaults acceptsMidi()/producesMidi() to true (see its
// own header comment), so before this audit almost every module advertised a MIDI jack that its
// processBlock never actually read from or wrote to. Two things are pinned here:
//
//   1. A table-driven sweep over every module the factory can construct (createModule(), the same
//      entry point AIStateMapper/GraphEditor/undo-redo all use), asserting acceptsMidi()/
//      producesMidi() against an EXPLICIT expected table keyed by ModuleType. The table is built
//      from an exhaustive switch with no default case (mirrors
//      AIStateMapper::getFactoryTypeName's own convention for the same enum) — a module type added
//      without a matching case fails to COMPILE rather than silently inheriting whatever
//      ModuleBase's default happens to be.
//   2. The trusted-load compatibility question the flag audit raises: a previously-saved patch can
//      contain a MIDI connection into a module that used to (silently, incorrectly) accept it and
//      no longer does. See TrustedLoadSkipsAStaleMidiConnectionIntoANowNonMidiModule below for what
//      actually happens (nothing dramatic — the wire is just absent).
//   3. A real, end-to-end regression for the bug the audit was fixing: MainComponent's MIDI
//      destinations picker used to only offer the hardcoded isMidiInstrumentType allowlist, which
//      excluded ADSR even though it genuinely consumes MIDI. See
//      MidiDestinationOptionsNowIncludeAnADSRNode below.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/MainComponent.h"
#include "../Source/Modules/ModuleBase.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <set>

namespace {

struct ExpectedMidiFlags {
    bool acceptsMidi = false;
    bool producesMidi = false;
};

// The audit's conclusion, one line per ModuleType. Exhaustive switch, deliberately with NO
// default: adding a ModuleType without adding a case here is a compile error (-Wswitch), not a
// module that silently inherits ModuleBase's true/true default.
ExpectedMidiFlags expectedFlagsFor(ModuleType type) {
    switch (type) {
    // ---- Real MIDI consumers: processBlock reads note-on/off for something ----
    case ModuleType::Oscillator: // mono-mode MIDI pitch fallback
    case ModuleType::Wavetable:  // same fallback as Oscillator
    case ModuleType::Sampler:    // note-on retriggers + transposes playback
    case ModuleType::ADSR:       // note-on/off drives the gate fallback
    case ModuleType::PolyMidi:   // converts note-on/off into poly Pitch/Gate CV
    case ModuleType::LFO:        // note-on retriggers the phase
        return {true, false};

    // ---- Real MIDI emitters: processBlock writes note/CC events, never reads incoming MIDI ----
    case ModuleType::Sequencer:          // self-contained step sequencer
    case ModuleType::PolySequencer:      // same, poly
    case ModuleType::MidiKeyboard:       // on-screen keys — a source, never a destination
    case ModuleType::ExternalMidi:       // device input merged in internally, not from the graph
    case ModuleType::TimelineMidiSource: // the timeline's "Track In" node
        return {false, true};

    // ---- Everything else: pure audio/CV, processBlock never touches the MIDI buffer ----
    case ModuleType::Filter:
    case ModuleType::VCA:
    case ModuleType::Attenuverter:
    case ModuleType::Delay:
    case ModuleType::Distortion:
    case ModuleType::Reverb:
    case ModuleType::Chorus:
    case ModuleType::Phaser:
    case ModuleType::Compressor:
    case ModuleType::Flanger:
    case ModuleType::Limiter:
    case ModuleType::ParametricEQ:
    case ModuleType::VoiceMixer:
    case ModuleType::Bitcrusher:
    case ModuleType::PitchShifter:
    case ModuleType::RingModulator:
    case ModuleType::Noise:
    case ModuleType::Math:
    case ModuleType::MacroControl:
    case ModuleType::SampleHold:
    case ModuleType::EnvelopeFollower:
    case ModuleType::Comparator:
    case ModuleType::AudioInput:
    case ModuleType::RecordTap:
    case ModuleType::TimelineAudioSource:
        return {false, false};

    // HostedPluginModule does not yet override acceptsMidi()/producesMidi() to delegate to the
    // wrapped juce::AudioPluginInstance — it is outside Source/Modules/** (Source/Plugin/Hosting/)
    // and therefore outside this audit's scope. It still inherits ModuleBase's true/true default
    // today; pinning that HERE (rather than leaving it untested) means a future fix that makes it
    // delegate to the real instance will fail this test as a deliberate prompt to update the
    // expectation, instead of the gap staying invisible.
    case ModuleType::HostedPlugin:
        return {true, true};
    }
    return {}; // unreachable; silences -Wreturn-type on compilers that don't trust the switch above
}

} // namespace

TEST(ModuleMidiFlagsTest, EveryFactoryModuleMatchesItsExpectedMidiFlags) {
    std::set<ModuleType> seenTypes;

    for (const auto& typeName : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto processor = synth::AIStateMapper::createModule(typeName);
        ASSERT_NE(processor, nullptr) << "factory key '" << typeName.toStdString() << "' failed to construct";

        // "Audio Output" / "Midi Input" are juce::AudioProcessorGraph::AudioGraphIOProcessor, not
        // a ModuleBase — nothing to check here, they are not part of this audit.
        auto* module = dynamic_cast<ModuleBase*>(processor.get());
        if (module == nullptr)
            continue;

        const auto type = module->getModuleType();
        seenTypes.insert(type);
        const auto expected = expectedFlagsFor(type);
        EXPECT_EQ(module->acceptsMidi(), expected.acceptsMidi)
            << "factory key '" << typeName.toStdString() << "' (ModuleType " << static_cast<int>(type) << ")";
        EXPECT_EQ(module->producesMidi(), expected.producesMidi)
            << "factory key '" << typeName.toStdString() << "' (ModuleType " << static_cast<int>(type) << ")";
    }

    // Every module type this audit covers must actually have been reachable through the factory —
    // otherwise the sweep above silently tests nothing for it.
    std::set<ModuleType> requiredTypes = {
        ModuleType::Oscillator,
        ModuleType::Filter,
        ModuleType::VCA,
        ModuleType::ADSR,
        ModuleType::LFO,
        ModuleType::Sequencer,
        ModuleType::PolySequencer,
        ModuleType::MidiKeyboard,
        ModuleType::PolyMidi,
        ModuleType::ExternalMidi,
        ModuleType::Attenuverter,
        ModuleType::Delay,
        ModuleType::Distortion,
        ModuleType::Reverb,
        ModuleType::Chorus,
        ModuleType::Phaser,
        ModuleType::Compressor,
        ModuleType::Flanger,
        ModuleType::Limiter,
        ModuleType::ParametricEQ,
        ModuleType::VoiceMixer,
        ModuleType::Bitcrusher,
        ModuleType::PitchShifter,
        ModuleType::RingModulator,
        ModuleType::Noise,
        ModuleType::Math,
        ModuleType::Sampler,
        ModuleType::Wavetable,
        ModuleType::MacroControl,
        ModuleType::SampleHold,
        ModuleType::EnvelopeFollower,
        ModuleType::Comparator,
        ModuleType::AudioInput,
        ModuleType::HostedPlugin,
    };
    requiredTypes.insert(ModuleType::TimelineMidiSource);
    requiredTypes.insert(ModuleType::RecordTap);
    requiredTypes.insert(ModuleType::TimelineAudioSource);

    for (auto type : requiredTypes)
        EXPECT_TRUE(seenTypes.count(type) > 0)
            << "ModuleType " << static_cast<int>(type) << " was never reached via the module factory";
}

// ---- Trusted-load compatibility: a stale MIDI wire into a now-non-MIDI module -----------------
//
// Before this audit, ModuleBase's true/true default meant a Filter (an audio/CV processor with no
// real MIDI path) reported acceptsMidi()==true, so a MIDI wire into one was legal and a patch could
// genuinely contain one. Filter's acceptsMidi() is now false. The question this test answers: does
// loading such a patch on the TRUSTED path (presets, undo/redo) still work?
//
// It does, with no code change needed:
//   - AIStateMapper::validatePatch(trusted=true) only runs the purely structural checks (are
//     "nodes"/"connections"/etc. arrays) and returns success immediately — it never validates an
//     individual connection's legality on the trusted path.
//   - applyJSONToGraph's connection loop calls juce::AudioProcessorGraph::addConnection() for every
//     connection and does not check the return value; addConnection() returns false (never throws,
//     never asserts) for a connection that fails Connections::isConnectionLegal — here, that the
//     destination's acceptsMidi() is false — and the loop simply moves on to the next connection.
//
// So the stale wire is silently dropped and every other node and connection in the patch loads
// intact — proven below rather than assumed.
TEST(ModuleMidiFlagsTest, TrustedLoadSkipsAStaleMidiConnectionIntoANowNonMidiModule) {
    juce::AudioProcessorGraph graph;
    // The audio sink is a VCA rather than Audio Output: the graph's IO node is a
    // juce::AudioGraphIOProcessor whose channel count is 0 until the graph gets a play config, so
    // a bare test graph would drop that wire for an unrelated reason and mask what this test pins.
    juce::var json = juce::JSON::parse(R"({
        "nodes":[
            {"id":1,"type":"Sequencer","params":{}},
            {"id":2,"type":"Oscillator","params":{}},
            {"id":3,"type":"Filter","params":{}},
            {"id":4,"type":"VCA","params":{}}
        ],
        "connections":[
            {"src":1,"dst":3,"srcPort":-1,"dstPort":-1},
            {"src":2,"dst":3,"srcPort":0,"dstPort":0},
            {"src":3,"dst":4,"srcPort":0,"dstPort":0}
        ]
    })");

    const bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true);
    ASSERT_TRUE(success) << "one un-addable connection must not fail the whole trusted-path load";
    ASSERT_EQ(graph.getNumNodes(), 4);

    // trusted + clearExisting preserves the JSON's own "id" as the live NodeID (see
    // applyJSONToGraph's adoptUuidIfTrusted/preservedId handling), so nodes are addressable by the
    // ids used above.
    using NodeID = juce::AudioProcessorGraph::NodeID;
    auto* seqNode = graph.getNodeForId(NodeID(1));
    auto* oscNode = graph.getNodeForId(NodeID(2));
    auto* filterNode = graph.getNodeForId(NodeID(3));
    auto* vcaNode = graph.getNodeForId(NodeID(4));
    ASSERT_NE(seqNode, nullptr);
    ASSERT_NE(oscNode, nullptr);
    ASSERT_NE(filterNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);

    ASSERT_FALSE(filterNode->getProcessor()->acceptsMidi());

    // The stale wire is simply absent.
    EXPECT_FALSE(graph.isConnected({{seqNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                    {filterNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));

    // The rest of the patch — every node, and both valid audio connections — loaded intact.
    EXPECT_TRUE(graph.isConnected({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}}));
}

// ---- MainComponent::getMidiDestinationOptions now enumerates by ground truth -------------------

namespace {

// Minimal stub provider — same shape as TimelinePanelTests.cpp's MockProviderTL. MainComponent's
// constructor requires one; nothing in this test ever sends it a prompt.
class MockProviderForMidiFlagsTest : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMidiFlags"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace

// End-to-end, through the exact production path the real "MIDI destinations..." menu entry uses
// (TimelineTrackHeaderComponent::createMidiDestinationPickerForTest(), wired to the real
// MainComponent as its TrackHeaderHost) rather than calling
// MainComponent::getMidiDestinationOptions directly: MainComponent inherits TrackHeaderHost
// PRIVATELY (see MainComponent.h's class declaration), so nothing outside the class — a test
// included — can name that base or call the override on it directly. This is also exactly what
// "extend the existing host/component-level tests" means here: it drives MainComponent's real
// getMidiDestinationOptions() through the real component, not a stub.
TEST(ModuleMidiFlagsTest, MidiDestinationOptionsNowIncludeAnADSRNode) {
    MainComponent mc(std::make_unique<MockProviderForMidiFlagsTest>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getGraphEditor().newPatch();
    mc.getGraphEditor().addModuleAtCanvasPosition("ADSR", {200, 200}, {});
    mc.getUndoManager().clearUndoHistory();

    mc.simulateAddMidiTrackClick();
    ASSERT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 1);

    auto* header = mc.getTimelinePanel().getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    auto picker = header->createMidiDestinationPickerForTest();
    ASSERT_NE(picker, nullptr);

    const auto names = picker->getVisibleRowNamesForTest();
    const bool hasADSR = std::find(names.begin(), names.end(), juce::String("ADSR")) != names.end();
    EXPECT_TRUE(hasADSR) << "the old hardcoded isMidiInstrumentType allowlist excluded ADSR even "
                            "though it genuinely consumes MIDI (see ADSRModule::acceptsMidi())";
}
