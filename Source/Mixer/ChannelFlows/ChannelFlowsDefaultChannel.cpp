// Concern: the factory-default mixer channel builders -- buildDefaultAudioChannel, the poly
// voice-mixer/envelope helpers it composes with, and the isProcessorPoly/setProcessorPoly pair
// they share.
#include "ChannelFlows.h"

#include "ChannelFlowsInternal.h"
#include "Modules/VCAModule.h"

namespace synth {

namespace {

// Sets a named juce::AudioParameterFloat's real-world value. Hand-rolled for the same reason
// isProcessorPoly is above: only needs to write one parameter by paramID, not AIStateMapper's full
// (private) param-matching idiom. No-op if the processor has no such float param.
void setFloatParam(juce::AudioProcessor& processor, const juce::String& paramID, float value) {
    for (auto* param : processor.getParameters())
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == paramID) {
                *floatParam = value;
                return;
            }
}

// Sets a named juce::AudioParameterBool. Same reasoning as setFloatParam above.
void setBoolParam(juce::AudioProcessor& processor, const juce::String& paramID, bool value) {
    for (auto* param : processor.getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == paramID) {
                *boolParam = value;
                return;
            }
}
} // namespace

// True when `processor` declares a "poly" AudioParameterBool and it's currently on. Hand-rolled
// rather than reusing AIStateMapper::applyParamsToProcessor's param-matching idiom: that method is
// private to AIStateMapper.cpp, and this only needs to READ one parameter, not set arbitrary ones.
// Exposed (ChannelFlows.h) so a caller can pick the right builder before calling any of them.
bool isProcessorPoly(juce::AudioProcessor* processor) {
    if (processor == nullptr)
        return false;
    for (auto* param : processor->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                return boolParam->get();
    return false;
}

void setProcessorPoly(juce::AudioProcessor* processor, bool poly) {
    if (processor != nullptr)
        setBoolParam(*processor, "poly", poly);
}

DefaultChannel buildDefaultAudioChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node& source,
                                        const DefaultChannelLayout& layout, int sourceRightChannel) {
    return buildChannelChain(graph, {{source.nodeID, 0}}, {{source.nodeID, sourceRightChannel}}, layout);
}

juce::AudioProcessorGraph::Node* addVoiceMixerForPolyInstrument(juce::AudioProcessorGraph& graph,
                                                                juce::AudioProcessorGraph::Node& instrument,
                                                                juce::Point<int> position, juce::String& uuidOut) {
    if (!isProcessorPoly(instrument.getProcessor()))
        return nullptr;

    auto* voiceMixer = addChainNode(graph, "Voice Mixer", position, uuidOut);
    if (voiceMixer == nullptr)
        return nullptr;

    // The instrument's L-octet (raw ch0-7, poly ON) is up to 8 simultaneous voices; Voice Mixer sums
    // them to a mono value duplicated onto its own ch0(L)/ch1(R) (docs/mixer.md §5.4/§5.8). The
    // instrument's R-octet is deliberately NOT summed in — same precedent, same known limitation.
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{instrument.nodeID, voice}, {voiceMixer->nodeID, voice}});

    return voiceMixer;
}

EnvelopeAndVCA addEnvelopeAndVCAForRawInstrument(juce::AudioProcessorGraph& graph,
                                                 juce::AudioProcessorGraph::Node& trackIn,
                                                 juce::AudioProcessorGraph::Node& chainSource,
                                                 int chainSourceRightChannel, juce::Point<int> adsrPosition,
                                                 juce::Point<int> vcaPosition) {
    EnvelopeAndVCA result;

    juce::String adsrUuid;
    auto* adsr = addChainNode(graph, "ADSR", adsrPosition, adsrUuid);
    if (adsr == nullptr)
        return result;
    // Forced non-poly regardless of the instrument's own poly flag — see this function's header
    // comment for why a poly ADSR fed only Track In's MIDI would never fire.
    setBoolParam(*adsr->getProcessor(), "poly", false);
    // Explicit sustain override so this auto-wired chain's level doesn't depend on ADSR's own
    // stock default (1.0 as of FRO110) — see the header comment.
    setFloatParam(*adsr->getProcessor(), "sustain", 0.7f);

    juce::String vcaUuid;
    auto* vca = addChainNode(graph, "VCA", vcaPosition, vcaUuid);
    if (vca == nullptr) {
        result.adsrUuid = adsrUuid;
        return result;
    }
    setBoolParam(*vca->getProcessor(), "poly", false);
    // Overrides VCA's stock gain default (0.5) so the envelope alone governs level — see the
    // header comment.
    setFloatParam(*vca->getProcessor(), "gain", 1.0f);

    // Track In's MIDI, fanned alongside its existing wire to the instrument, drives the ADSR's gate.
    graph.addConnection({{trackIn.nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {adsr->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    // chainSource L/R -> VCA Audio L/R (VCAModule::kRightBase — never ch1, that's the Gain CV).
    graph.addConnection({{chainSource.nodeID, 0}, {vca->nodeID, 0}});
    graph.addConnection({{chainSource.nodeID, chainSourceRightChannel}, {vca->nodeID, VCAModule::kRightBase}});

    // ADSR Env (ch0) -> VCA's mono Gain CV (ch1).
    graph.addConnection({{adsr->nodeID, 0}, {vca->nodeID, 1}});

    result.adsrUuid = adsrUuid;
    result.vcaUuid = vcaUuid;
    result.vca = vca;
    return result;
}

PolyEnvelopeAndVCA addPolyEnvelopeAndVCAForInstrument(juce::AudioProcessorGraph& graph,
                                                      juce::AudioProcessorGraph::Node& trackIn,
                                                      juce::AudioProcessorGraph::Node& instrument,
                                                      juce::Point<int> polyMidiPosition, juce::Point<int> adsrPosition,
                                                      juce::Point<int> vcaPosition) {
    PolyEnvelopeAndVCA result;

    juce::String polyMidiUuid;
    auto* polyMidi = addChainNode(graph, "Poly MIDI", polyMidiPosition, polyMidiUuid);
    if (polyMidi == nullptr)
        return result;

    juce::String adsrUuid;
    auto* adsr = addChainNode(graph, "ADSR", adsrPosition, adsrUuid);
    if (adsr == nullptr) {
        result.polyMidiUuid = polyMidiUuid;
        return result;
    }
    // Poly, unlike addEnvelopeAndVCAForRawInstrument's forced-mono ADSR — its gate comes from Poly
    // MIDI's per-voice CV below, not raw MIDI, so the poly branch actually fires. See this
    // function's header comment for why the non-poly path can't do this.
    setBoolParam(*adsr->getProcessor(), "poly", true);
    // Same override as the non-poly path, same reason: a held note should sustain.
    setFloatParam(*adsr->getProcessor(), "sustain", 0.7f);

    juce::String vcaUuid;
    auto* vca = addChainNode(graph, "VCA", vcaPosition, vcaUuid);
    if (vca == nullptr) {
        result.polyMidiUuid = polyMidiUuid;
        result.adsrUuid = adsrUuid;
        return result;
    }
    setBoolParam(*vca->getProcessor(), "poly", true);
    // Same override as the non-poly path, same reason: the envelope alone should govern level.
    setFloatParam(*vca->getProcessor(), "gain", 1.0f);

    // Track In's MIDI drives Poly MIDI, not a mono ADSR gate fallback.
    graph.addConnection({{trackIn.nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {polyMidi->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    // Poly MIDI's Pitch fan (ch0-7) -> instrument's poly Pitch CV in (ch0-7).
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{polyMidi->nodeID, voice}, {instrument.nodeID, voice}});

    // Poly MIDI's Gate fan (ch8-15) -> ADSR's poly Gate CV in (ch0-7).
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{polyMidi->nodeID, 8 + voice}, {adsr->nodeID, voice}});

    // ADSR's poly Env out (ch0-7) -> VCA's poly Gain CV in (ch8-15, VCAModule::kPolyCVBase).
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{adsr->nodeID, voice}, {vca->nodeID, VCAModule::kPolyCVBase + voice}});

    // instrument's poly Audio L (ch0-7) -> VCA's poly Audio L in (ch0-7). The R-octet is
    // deliberately not wired — see this function's header comment ("known limitation").
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{instrument.nodeID, voice}, {vca->nodeID, voice}});

    result.polyMidiUuid = polyMidiUuid;
    result.adsrUuid = adsrUuid;
    result.vcaUuid = vcaUuid;
    result.vca = vca;
    return result;
}

// FRO15 (P9-9): an empty bus channel. The shared chain builder with no feeds at all, plus the
// display-only isBus flag -- see ChannelFlows.h for why this is not a separate node type.
DefaultChannel buildBusChannel(juce::AudioProcessorGraph& graph, const DefaultChannelLayout& layout) {
    auto channel = buildChannelChain(graph, {}, {}, layout);
    if (channel.strip != nullptr)
        if (auto* strip = dynamic_cast<ChannelStripModule*>(channel.strip->getProcessor()))
            strip->setIsBus(true);
    return channel;
}

} // namespace synth
