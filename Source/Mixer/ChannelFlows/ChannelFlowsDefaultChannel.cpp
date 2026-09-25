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

// Builds the factory default mixer channel (docs/mixer/mixer.md#the-factory-default-chain, T173a, FRO226) that
// "+ Track -> Audio Track" wires after a freshly-created Track Audio node's stereo output:
//
//     source 0/1 -> Gate 0/1 -> Parametric EQ 0/1 -> Compressor 0/1 -> Channel Strip (Stereo) -> Master (Mix)
//
// All three inserts are created BYPASSED (docs/mixer/mixer.md's factory-default chain: present but inert
// until the user opts in). The Channel Strip is Stereo, shape set BEFORE the node is added to the live
// graph (ChannelStripModule::setShape()'s own contract — adding to a live graph can prepareToPlay
// and lock the shape).
//
// Channel numbering, verified against the module headers rather than assumed:
//   - source 0/1 -> Gate 0/1 (GateModule's audio pair sits on raw ch0/ch1);
//   - Gate 0/1 -> EQ 0/1 (Parametric EQ's audio pair sits on raw ch0/ch1);
//   - EQ 0/1 -> Compressor 0/1 (same, both stereo pairs on raw ch0/ch1);
//   - Compressor 0/1 -> Strip 0 / ChannelStripModule::kRightBase (=4) — the strip's right leg is
//     NEVER ch1 (Source/Modules/CLAUDE.md), so this is the one place the raw channel number jumps;
//   - Strip 0 / kRightBase -> Master MasterModule::kMixLeft / kMixRight.
//
// Master is spliced via spliceMasterNode() — reusing the existing singleton when this isn't the
// first channel — AFTER the source->EQ->Compressor->Strip chain is wired but BEFORE the
// Strip->Master edges are added: spliceMasterNode() re-routes whatever already feeds the audio
// output, and nothing of this channel's own should be among that yet.
//
// The Strip->Master edge is a PLAIN graph edge, never boxed behind a macro port, on purpose:
// spliceMasterNode()/ensureMasterNode() (Source/Mixer/MasterSplice.h) classify a re-routed feed as
// Mix vs Direct by checking whether the connection's SOURCE NODE is itself a ChannelStripModule. A
// MacroOutlet sitting between the strip and Master would make the source node a MacroOutlet instead,
// defeating that check — which the later "Create channels" subtask depends on. (MainComponent::
// addAudioTrack keeps Master OUTSIDE the macro it boxes this channel's other nodes into, for exactly
// this reason.)
//
// NO UNDO's own reasoning: today's only caller is MainComponent::addAudioTrack, via
// AppUndoManager::recordGraphTimelineAndMacroChange. Core cannot depend on AppUndoManager or
// GraphEditor (AppUI-only), so this function touches only the graph.
//
// Positions the EQ/Compressor/Strip (and, when newly spliced, Master) cards exactly where `layout`
// says. Core cannot size UI cards itself — no dependency on Source/UI/LayoutUtil or
// GraphEditor::estimateModuleSize (both AppUI-only) — so the caller must have already worked out
// non-overlapping positions from the real card widths (MainComponent::addAudioTrack is today's only
// caller).
DefaultChannel buildDefaultAudioChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node& source,
                                        const DefaultChannelLayout& layout, int sourceRightChannel) {
    return buildChannelChain(graph, {{source.nodeID, 0}}, {{source.nodeID, sourceRightChannel}}, layout);
}

// T183 (P9-3b): when `instrument` is currently in poly mode (it has a "poly" AudioParameterBool and
// it's on), its L-octet (raw ch0-7) can carry up to 8 simultaneous voices, which
// buildDefaultAudioChannel() cannot accept directly — it wants one stereo pair. Creates a Voice
// Mixer (docs/mixer/mixer.md#an-instrument-track: any chain ending poly gets one ahead of the strip), wires
// `instrument`'s raw ch0-7 into it, and returns it so the caller can pass ITS ch0/ch1 as `source` to
// buildDefaultAudioChannel() instead of `instrument` directly.
//
// A factory-created instrument defaults to poly OFF (Oscillator/Wavetable's own `poly` parameter
// default), so this returns nullptr on the golden "+ Track -> Instrument" path today; it exists so
// poly instruments are handled correctly wherever they arise (a caller that explicitly turns poly on
// before building the chain, or a future direct-poly picker), without the strip ever seeing more
// than one stereo pair.
//
// FRO46 (P9-3j): for a poly Oscillator/Wavetable specifically, the caller no longer calls this —
// addPolyEnvelopeAndVCAForInstrument() below replaces it entirely (its poly VCA does its own
// 8-voice summing, so a separate Voice Mixer stage is redundant). This function is still the right
// one for every other poly instrument (e.g. a poly Sampler, which has no auto-wired envelope).
juce::AudioProcessorGraph::Node* addVoiceMixerForPolyInstrument(juce::AudioProcessorGraph& graph,
                                                                juce::AudioProcessorGraph::Node& instrument,
                                                                juce::Point<int> position, juce::String& uuidOut) {
    if (!isProcessorPoly(instrument.getProcessor()))
        return nullptr;

    auto* voiceMixer = addChainNode(graph, "Voice Mixer", position, uuidOut);
    if (voiceMixer == nullptr)
        return nullptr;

    // The instrument's L-octet (raw ch0-7, poly ON) is up to 8 simultaneous voices; Voice Mixer sums
    // them to a mono value duplicated onto its own ch0(L)/ch1(R) (docs/mixer/mixer.md#an-instrument-track). The
    // instrument's R-octet is deliberately NOT summed in — same precedent, same known limitation.
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{instrument.nodeID, voice}, {voiceMixer->nodeID, voice}});

    return voiceMixer;
}

// P9-3i (FRO43): Oscillator/Wavetable have no envelope of their own — a held or even released note
// drones forever. Inserts an ADSR gated by the track's own MIDI (fanned alongside the existing
// Track In -> instrument wire) driving a VCA's gain, ahead of the rest of the default chain:
//
//     Track In --MIDI--> ADSR --Env(ch0)--> VCA's Gain CV (ch1)
//     chainSource L/R -----------------------> VCA Audio L/R (ch0 / VCAModule::kRightBase)
//
// Both nodes are forced non-poly, regardless of the instrument's own "poly" parameter: ADSRModule's
// poly branch is CV-gate-only (it never reads the MIDI note-on/off fallback tracked via the
// `heldNotes` bitset, which exists solely in its non-poly branch), so a poly ADSR fed only Track
// In's MIDI would output a permanent zero envelope -> total silence, not degraded polyphony.
// Non-poly composes correctly whether or not `chainSource` is already a Voice Mixer's poly-voice
// sum (addVoiceMixerForPolyInstrument above) — this is deliberately called AFTER that stage, never
// before it, so a poly instrument's per-voice audio is summed to one stereo pair before the
// (necessarily-mono) VCA gates it.
//
// ADSR's sustain (stock factory default 1.0 as of FRO110; this override predates that and is kept
// for clarity/explicitness) is set to 0.7 so a held note settles at a musical level instead of the
// full peak; the release stage (stock default, unchanged) is what fixes the drone-after-note-off
// bug. VCA's gain (stock factory default 0.5) is overridden to 1.0 so the envelope alone governs
// perceived level, not an extra silent 50% attenuation stacked under it.
//
// FRO46 (P9-3j) superseded this as the poly instrument's ONLY option: when the instrument is poly,
// the caller now builds addPolyEnvelopeAndVCAForInstrument() below instead of this one (true
// per-voice envelopes, no Voice Mixer). This function remains exactly as before for the non-poly
// case — see addPolyEnvelopeAndVCAForInstrument's own comment below for why a poly ADSR fed only raw
// MIDI can't work, and how the poly path solves it (a Poly MIDI node supplying per-voice CV
// instead).
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
    // Forced non-poly regardless of the instrument's own poly flag — see above for why a poly ADSR
    // fed only Track In's MIDI would never fire.
    setBoolParam(*adsr->getProcessor(), "poly", false);
    // Explicit sustain override so this auto-wired chain's level doesn't depend on ADSR's own
    // stock default (1.0 as of FRO110) — see above.
    setFloatParam(*adsr->getProcessor(), "sustain", 0.7f);

    juce::String vcaUuid;
    auto* vca = addChainNode(graph, "VCA", vcaPosition, vcaUuid);
    if (vca == nullptr) {
        result.adsrUuid = adsrUuid;
        return result;
    }
    setBoolParam(*vca->getProcessor(), "poly", false);
    // Overrides VCA's stock gain default (0.5) so the envelope alone governs level — see above.
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

// FRO46 (P9-3j): addEnvelopeAndVCAForRawInstrument's ADSR+VCA are forced non-poly because
// ADSRModule's poly branch is CV-gate-only — it never reads the MIDI note-on/off fallback that
// drives its mono branch (ADSRModule.h's `heldNotes` bitset), so a poly ADSR fed only Track In's
// raw MIDI would output a permanent zero envelope. This is the poly counterpart: instead of MIDI
// driving a mono ADSR, a Poly MIDI node (the codebase's existing per-voice MIDI-to-CV converter —
// docs/modules/modules.md#poly-midi-module) turns Track In's MIDI into per-voice pitch/gate CV, which
// drives a genuinely poly ADSR and VCA:
//
//     Track In --MIDI--> Poly MIDI --Pitch(ch0-7)--> instrument's poly Pitch CV in (ch0-7)
//                         Poly MIDI --Gate(ch8-15)--> ADSR's poly Gate CV in (ch0-7)
//     ADSR poly Env (ch0-7) --> VCA's poly Gain CV in (ch8-15, VCAModule::kPolyCVBase)
//     instrument's poly Audio L (ch0-7) --> VCA's poly Audio L in (ch0-7)
//
// VCA's own poly branch already sums all 8 gated voices to a stereo-shaped pair (ch0 = left sum,
// ch1 = the module's legacy duplicate of it — VCAModule.h), so unlike the non-poly path, no
// separate Voice Mixer is inserted; this REPLACES addVoiceMixerForPolyInstrument entirely for the
// Oscillator/Wavetable case, it does not compose after it. The instrument's R-octet is deliberately
// NOT wired into VCA's own Audio R poly block (ch16-23) — same known limitation
// addVoiceMixerForPolyInstrument's own comment documents (a poly instrument's stereo image isn't
// preserved; downstream reads the mono ch0/ch1 duplicate).
//
// The existing Track In -> instrument MIDI connection (wired unconditionally at instrument-track
// creation) is left as-is: it's harmless in poly mode. OscillatorModule/WavetableOscillatorModule
// only ever consult raw MIDI as a last-resort fallback for voice 0's pitch when no CV is present on
// ch0, and Poly MIDI supplies real per-voice Hz once a note sounds — the fallback simply never
// triggers once this is wired.
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
    // MIDI's per-voice CV below, not raw MIDI, so the poly branch actually fires. See
    // addEnvelopeAndVCAForRawInstrument's own comment above for why the non-poly path can't do this.
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
    // deliberately not wired — see this function's own comment above ("known limitation").
    for (int voice = 0; voice < 8; ++voice)
        graph.addConnection({{instrument.nodeID, voice}, {vca->nodeID, voice}});

    result.polyMidiUuid = polyMidiUuid;
    result.adsrUuid = adsrUuid;
    result.vcaUuid = vcaUuid;
    result.vca = vca;
    return result;
}

// FRO15 (P9-9, docs/mixer/sends-and-buses.md): an EMPTY group/send bus — the same bypassed Gate -> bypassed
// EQ -> bypassed Compressor -> Channel Strip (Stereo) -> Master (Mix) chain every other channel gets, with
// nothing feeding the Gate yet, and the strip marked isBus() so the mixer gives its column the BUS badge. This
// lives here rather than in MixerSends because it IS that shared chain builder with an empty feed
// list — a bus is an ordinary channel whose inputs happen to be other strips' outputs (D1), so there
// is deliberately no separate bus node type and no second chain builder.
DefaultChannel buildBusChannel(juce::AudioProcessorGraph& graph, const DefaultChannelLayout& layout) {
    auto channel = buildChannelChain(graph, {}, {}, layout);
    if (channel.strip != nullptr)
        if (auto* strip = dynamic_cast<ChannelStripModule*>(channel.strip->getProcessor()))
            strip->setIsBus(true);
    return channel;
}

} // namespace synth
