// ChannelFlowInstrumentTests.cpp
//
// T183 (P9-3b): "+ Track -> Instrument -> {Oscillator/Wavetable/Sampler}" — the MIDI-track
// mirror of the Audio Track flow in ChannelFlowTests.cpp. Shared ChannelFlowTest fixture and
// helpers live in ChannelFlowTestFixture.h.

#include "../../StubPluginInstance.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine.h"
#include "Branding.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "Timeline/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <thread>

// ---------------------------------------------------------------------------------------------
// T183 (P9-3b): "+ Track -> Instrument -> {Oscillator/Wavetable/Sampler}" builds
//
//     Track In -> instrument -> Parametric EQ (bypassed) -> Compressor (bypassed)
//              -> Channel Strip (Stereo) -> Master (Mix)
//
// as ONE undo step, with {Track In, instrument, EQ, Compressor, Strip} boxed into one collapsed
// macro named after the track — the MIDI-track mirror of the Audio Track tests above. See
// MainComponent::addInstrumentTrack's own comment for why this stays a TrackKind::Midi track
// rather than a new TrackKind, and Source/Mixer/ChannelFlows.h for the poly/Voice Mixer contract.
// ---------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, InstrumentTrackSamplerBuildsDefaultChannelDirectlyOnAContiguousPair) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Sampler");

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::TimelineMidiSource), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Sampler), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "Sampler is a contiguous stereo pair — no Voice Mixer needed";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);

    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    auto* sampler = findNodeOfTypeCFT(graph, ModuleType::Sampler);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(sampler, nullptr);
    ASSERT_NE(eq, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {sampler->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));

    auto* samplerModule = dynamic_cast<ModuleBase*>(sampler->getProcessor());
    ASSERT_NE(samplerModule, nullptr);
    ASSERT_EQ(samplerModule->rightAudioLegChannel(), 1) << "Sampler's legs ARE the contiguous ch0/ch1 pair";
    EXPECT_TRUE(graph.isConnected({{sampler->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{sampler->nodeID, 1}, {eq->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, InstrumentTrackOscillatorWiresSplitBlockRightLegNeverCh1) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator");

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "a freshly created Oscillator defaults to poly OFF — no Voice Mixer needed";

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    auto* oscillator = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    // The factory default preset every fresh MainComponent loads already has its own VCA node —
    // disambiguate via macro membership, not "last one seen" (see findMacroMemberOfTypeCFT).
    auto* vca = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    ASSERT_NE(oscillator, nullptr);
    ASSERT_NE(vca, nullptr) << "P9-3i: Oscillator has no envelope of its own, so the default chain must insert a VCA";

    auto* oscModule = dynamic_cast<ModuleBase*>(oscillator->getProcessor());
    ASSERT_NE(oscModule, nullptr);
    const int rightLeg = oscModule->rightAudioLegChannel();
    ASSERT_GT(rightLeg, 1) << "Oscillator's right leg is a dedicated kRightBase block, never ch1";

    // Oscillator -> VCA (never ch1 for the right leg — same split-block contract, one hop earlier).
    EXPECT_TRUE(graph.isConnected({{oscillator->nodeID, 0}, {vca->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{oscillator->nodeID, rightLeg}, {vca->nodeID, VCAModule::kRightBase}}));
    EXPECT_FALSE(graph.isConnected({{oscillator->nodeID, 1}, {vca->nodeID, 1}}))
        << "must never assume ch1 for a split-block source, and ch1 on the VCA is its Gain CV";

    // VCA -> EQ, now that the VCA sits between the instrument and the rest of the chain.
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(eq, nullptr);
    EXPECT_TRUE(graph.isConnected({{vca->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{vca->nodeID, VCAModule::kRightBase}, {eq->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{oscillator->nodeID, 0}, {eq->nodeID, 0}}))
        << "Oscillator must no longer feed EQ directly — it goes through the VCA";
}

// P9-3i (FRO43): the ADSR gating the VCA above is driven by the same Track In MIDI as the
// instrument, and its Env output lands on the VCA's mono Gain CV (ch1) — never the audio legs.
TEST_F(ChannelFlowTest, InstrumentTrackOscillatorEnvelopeGatesTheVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes (Amp Env, Filter Env) and VCA node — disambiguate via macro membership.
    auto* adsr = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    auto* vca = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(adsr, nullptr);
    ASSERT_NE(vca, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {adsr->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}))
        << "the ADSR must be gated by the same Track In MIDI as the instrument";
    EXPECT_TRUE(graph.isConnected({{adsr->nodeID, 0}, {vca->nodeID, 1}}))
        << "ADSR's Env output must land on the VCA's mono Gain CV (ch1)";

    auto* adsrModule = dynamic_cast<ModuleBase*>(adsr->getProcessor());
    ASSERT_NE(adsrModule, nullptr);
    for (auto* param : adsr->getProcessor()->getParameters()) {
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_FALSE(boolParam->get())
                    << "the auto-wired ADSR must be non-poly — its poly branch ignores MIDI entirely";
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == "sustain")
                EXPECT_FLOAT_EQ(floatParam->get(), 0.7f)
                    << "sustain must be overridden so a held note doesn't decay to silence";
    }
    for (auto* param : vca->getProcessor()->getParameters()) {
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_FALSE(boolParam->get()) << "the auto-wired VCA must be non-poly";
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == "gain")
                EXPECT_FLOAT_EQ(floatParam->get(), 1.0f)
                    << "gain must be overridden so the envelope alone governs level";
    }
}

// The ticket's own bug: a held-then-released note must not drone forever. Renders the ADSR and VCA
// nodes directly (Track In is a timeline MIDI source and won't forward an injected MidiBuffer, so a
// full-graph render can't inject a note) — this is the render-level check topology assertions above
// cannot give: it proves the envelope actually gates audio, not just that the wires exist.
TEST_F(ChannelFlowTest, InstrumentTrackOscillatorEnvelopeActuallySilencesAfterNoteOff) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes (Amp Env, Filter Env) — disambiguate via macro membership.
    auto* adsrNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    ASSERT_NE(adsrNode, nullptr);
    auto* adsr = adsrNode->getProcessor();

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;
    adsr->prepareToPlay(sampleRate, blockSize);

    // ch0 is the mono "Gate" CV input, unused by this test — leave it at 0 (below the default
    // 0.5 threshold) so ONLY the injected MIDI drives the gate. Filling it with any value above
    // threshold would latch the Schmitt trigger permanently high, masking the MIDI path entirely.
    auto renderAdsrBlock = [&](juce::MidiBuffer midi) {
        juce::AudioBuffer<float> buf(9, blockSize);
        buf.clear();
        adsr->processBlock(buf, midi);
        return buf.getSample(0, blockSize - 1);
    };

    // No note yet: the envelope must be at rest.
    EXPECT_NEAR(renderAdsrBlock({}), 0.0f, 1e-4f) << "envelope must start silent with no note held";

    // Note on: after enough blocks to clear attack+decay, the envelope must have risen and settled
    // at (roughly) the overridden sustain level, not fallen back to 0 — this is the actual bug fix.
    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    float lastEnv = renderAdsrBlock(noteOn);
    for (int i = 0; i < 50; ++i)
        lastEnv = renderAdsrBlock({});
    EXPECT_GT(lastEnv, 0.5f) << "a held note must sustain, not decay to silence while still held";

    // Note off: after enough blocks for the release stage, the envelope must return to silence —
    // this is the drone this ticket exists to fix.
    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    lastEnv = renderAdsrBlock(noteOff);
    for (int i = 0; i < 50; ++i)
        lastEnv = renderAdsrBlock({});
    EXPECT_NEAR(lastEnv, 0.0f, 1e-3f) << "the envelope must return to silence after note-off + release";
}

TEST_F(ChannelFlowTest, InstrumentTrackDefaultInsertsAreBypassedAndStripIsStereo) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Sampler");

    auto* eqModule = dynamic_cast<ModuleBase*>(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)->getProcessor());
    auto* compModule = dynamic_cast<ModuleBase*>(findNodeOfTypeCFT(graph, ModuleType::Compressor)->getProcessor());
    auto* stripModule =
        dynamic_cast<ChannelStripModule*>(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip)->getProcessor());
    ASSERT_NE(eqModule, nullptr);
    ASSERT_NE(compModule, nullptr);
    ASSERT_NE(stripModule, nullptr);

    EXPECT_TRUE(eqModule->isBypassed());
    EXPECT_TRUE(compModule->isBypassed());
    EXPECT_FALSE(stripModule->isBypassed());
    EXPECT_EQ(stripModule->getShape(), ChannelStripModule::Shape::Stereo);
}

// P9-3i (FRO43) is scoped to Oscillator/Wavetable only — Sampler already has its own one-shot
// playback envelope and must be completely unaffected: no ADSR/VCA nodes, no change to its wiring
// or macro membership.
TEST_F(ChannelFlowTest, InstrumentTrackSamplerGetsNoEnvelopeOrVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Sampler");

    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes and a VCA node, so a raw graph-wide count can't tell "none created" from "the preset's
    // own" — check the track's own macro membership instead (freshly built, contains only this
    // track's own nodes).
    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR), nullptr);
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA), nullptr);

    auto* sampler = findNodeOfTypeCFT(graph, ModuleType::Sampler);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(sampler, nullptr);
    ASSERT_NE(eq, nullptr);
    EXPECT_TRUE(graph.isConnected({{sampler->nodeID, 0}, {eq->nodeID, 0}}))
        << "Sampler must still feed EQ directly, unchanged by P9-3i";
}

TEST_F(ChannelFlowTest, InstrumentTrackIsOneCollapsedMacroNamedAfterTrackAndStaysMidiKind) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Sampler");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    const auto& track = doc.getTracks().back();

    EXPECT_EQ(macro.name, track.name);
    EXPECT_TRUE(macro.collapsed);
    // T183's scope decision: an instrument track is TrackKind::Midi (a Track In feeding exactly
    // one instrument), not a new TrackKind — see MainComponent::addInstrumentTrack's own comment.
    EXPECT_EQ(track.kind, synth::TrackKind::Midi);
    EXPECT_EQ(track.bindingUuid, nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource)));

    std::vector<juce::String> expected = {nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Sampler)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip))};
    auto actual = macro.members;
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);

    EXPECT_FALSE(macro.hasMember(nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Master))))
        << "Master must stay outside the macro";
}

// P9-3i (FRO43): an Oscillator track's macro must include the new ADSR+VCA members too —
// InstrumentTrackIsOneCollapsedMacroNamedAfterTrackAndStaysMidiKind above uses Sampler, which never
// exercises this membership change.
TEST_F(ChannelFlowTest, InstrumentTrackOscillatorMacroIncludesEnvelopeAndVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator");

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();

    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes and a VCA node — disambiguate via macro membership, not "last one seen".
    std::vector<juce::String> expected = {nodeUuid(findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Oscillator)),
                                          nodeUuid(findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR)),
                                          nodeUuid(findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ParametricEQ)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::Compressor)),
                                          nodeUuid(findNodeOfTypeCFT(graph, ModuleType::ChannelStrip))};
    auto actual = macro.members;
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);
}

TEST_F(ChannelFlowTest, InstrumentTrackOneUndoStepRevertsEverythingAndRedoRestores) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(macros.toVar());

    addInstrumentTrack(mc, "Oscillator");
    ASSERT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1) << "the channel must have been built";
    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(macros.toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the whole channel was ONE undo step";

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosAfter);
}

// The Wavetable card and the Parametric EQ card are BOTH double-width — the exact overlap shape
// P9-3a's own bug (see this file's header comment) reproduced for, now two nodes to the left of
// where it was (P9-3i inserted ADSR+VCA between them). Reuses
// ChannelCardsDoNotOverlapAndMasterIsRightOfStrip's real-ModuleComponent approach rather than
// inferring bounds from positions.
TEST_F(ChannelFlowTest, InstrumentTrackWavetableCardsDoNotOverlap) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(2600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Wavetable");

    ASSERT_EQ(macros.size(), 1);
    const auto macroId = macros.getAll().front().id;
    mc.getGraphEditor().setMacroCollapsed(macroId, false);
    ASSERT_FALSE(macros.find(macroId)->collapsed);

    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes and a VCA node — disambiguate via macro membership, not "last one seen".
    const auto& macro = *macros.find(macroId);
    auto* trackInNode = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    auto* wavetableNode = findNodeOfTypeCFT(graph, ModuleType::Wavetable);
    auto* adsrNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    auto* vcaNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    auto* eqNode = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* compNode = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* stripNode = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    auto* masterNode = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(trackInNode, nullptr);
    ASSERT_NE(wavetableNode, nullptr);
    ASSERT_NE(adsrNode, nullptr) << "P9-3i: Wavetable also has no envelope of its own";
    ASSERT_NE(vcaNode, nullptr);
    ASSERT_NE(eqNode, nullptr);
    ASSERT_NE(compNode, nullptr);
    ASSERT_NE(stripNode, nullptr);
    ASSERT_NE(masterNode, nullptr);

    auto findComp = [&mc](juce::AudioProcessorGraph::Node* node) -> ModuleComponent* {
        for (auto* comp : mc.getGraphEditor().getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                return comp;
        return nullptr;
    };

    const std::array<ModuleComponent*, 8> cards = {findComp(trackInNode), findComp(wavetableNode), findComp(adsrNode),
                                                   findComp(vcaNode),     findComp(eqNode),        findComp(compNode),
                                                   findComp(stripNode),   findComp(masterNode)};
    for (auto* card : cards)
        ASSERT_NE(card, nullptr) << "every macro member must have a real ModuleComponent once expanded";

    for (size_t i = 0; i < cards.size(); ++i)
        for (size_t j = i + 1; j < cards.size(); ++j)
            EXPECT_FALSE(cards[i]->getBounds().intersects(cards[j]->getBounds()))
                << "card " << i << " " << cards[i]->getBounds().toString().toStdString() << " overlaps card " << j
                << " " << cards[j]->getBounds().toString().toStdString();

    for (size_t i = 0; i + 1 < cards.size(); ++i)
        EXPECT_LT(cards[i]->getX(), cards[i + 1]->getX());
}

TEST_F(ChannelFlowTest, InstrumentTrackRefusedAtMaxTracksCreatesNothing) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& doc = mc.getTimelineDoc();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    while ((int)doc.getTracks().size() < synth::TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(synth::TrackKind::Midi, "Filler").isValid());
    ASSERT_FALSE(mc.getUndoManager().canUndo());
    const int nodesBefore = graph.getNumNodes();

    addInstrumentTrack(mc, "Sampler");

    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a refused instrument track must leave no orphan node";
    EXPECT_EQ(macros.size(), 0) << "a refused instrument track must leave no macro";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "nothing changed in any domain: no undo step";
}
