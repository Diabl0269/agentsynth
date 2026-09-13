#include "ChannelFlows.h"

#include "../AI/AIStateMapper.h"
#include "../Modules/AttenuverterModule.h"
#include "../Modules/ChannelStripModule.h"
#include "../Modules/MasterModule.h"
#include "../Modules/ModuleBase.h"
#include "../Modules/RecordTapModule.h"
#include "../Modules/VCAModule.h"
#include "MasterSplice.h"
#include <algorithm>

namespace synth {

namespace {

// Creates one node through the factory (so it round-trips through graphToJSON/applyJSONToGraph,
// exactly like every other node-creation call site), assigns it a fresh uuid mirrored into the
// processor (ModuleBase::setNodeUuid), and records its canvas position. Returns nullptr on any
// factory/addNode failure, leaving `uuidOut` untouched.
juce::AudioProcessorGraph::Node* addChainNode(juce::AudioProcessorGraph& graph, const juce::String& moduleType,
                                              juce::Point<int> position, juce::String& uuidOut) {
    auto processor = AIStateMapper::createModule(moduleType);
    if (processor == nullptr)
        return nullptr;
    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return nullptr;

    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);

    uuidOut = uuid;
    return node.get();
}

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

// The shared builder behind both buildDefaultAudioChannel (one fixed stereo-pair source) and
// buildChannelForFeeds (T184: an arbitrary set of left/right feeds gathered from
// findUnchanneledOutputFeeds). Builds EQ(bypassed) -> Compressor(bypassed) -> Channel Strip
// (Stereo) -> Master (Mix) and wires every entry in `leftFeeds`/`rightFeeds` into the EQ's ch0/ch1
// respectively (AudioProcessorGraph sums multiple sources landing on the same input channel, so
// more than one feed a side is fine). Same ordering as buildDefaultAudioChannel's own contract:
// chain wired first, THEN spliceMasterNode, THEN Strip->Master as plain edges.
DefaultChannel buildChannelChain(juce::AudioProcessorGraph& graph,
                                 const std::vector<juce::AudioProcessorGraph::NodeAndChannel>& leftFeeds,
                                 const std::vector<juce::AudioProcessorGraph::NodeAndChannel>& rightFeeds,
                                 const DefaultChannelLayout& layout) {
    DefaultChannel result;

    juce::String eqUuid;
    auto* eq = addChainNode(graph, "Parametric EQ", layout.eq, eqUuid);
    if (eq == nullptr)
        return result;
    // Factory default: present but bypassed until the user opts in (docs/mixer.md §5.7/D3).
    if (auto* module = dynamic_cast<ModuleBase*>(eq->getProcessor()))
        module->setBypassed(true);

    juce::String compressorUuid;
    auto* compressor = addChainNode(graph, "Compressor", layout.compressor, compressorUuid);
    if (compressor == nullptr) {
        result.eqUuid = eqUuid;
        return result;
    }
    if (auto* module = dynamic_cast<ModuleBase*>(compressor->getProcessor()))
        module->setBypassed(true);

    // ChannelStripModule::setShape() must run BEFORE graph.addNode(): adding to a live graph can
    // prepareToPlay and lock the shape (see that method's own contract).
    auto stripProcessor = AIStateMapper::createModule("Channel Strip");
    if (stripProcessor == nullptr) {
        result.eqUuid = eqUuid;
        result.compressorUuid = compressorUuid;
        return result;
    }
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    auto stripNode = graph.addNode(std::move(stripProcessor));
    if (stripNode == nullptr) {
        result.eqUuid = eqUuid;
        result.compressorUuid = compressorUuid;
        return result;
    }
    auto* strip = stripNode.get();
    const juce::String stripUuid = juce::Uuid().toDashedString();
    strip->properties.set("uuid", stripUuid);
    if (auto* module = dynamic_cast<ModuleBase*>(strip->getProcessor()))
        module->setNodeUuid(stripUuid);
    strip->properties.set("x", layout.strip.x);
    strip->properties.set("y", layout.strip.y);

    // feeds -> EQ. Stereo on raw ch0/ch1 throughout, except the strip's right leg, which is
    // ChannelStripModule::kRightBase — NEVER ch1 (Source/Modules/CLAUDE.md), see ChannelFlows.h's
    // own comment for why that's the one place the number jumps.
    for (const auto& feed : leftFeeds)
        graph.addConnection({feed, {eq->nodeID, 0}});
    for (const auto& feed : rightFeeds)
        graph.addConnection({feed, {eq->nodeID, 1}});
    graph.addConnection({{eq->nodeID, 0}, {compressor->nodeID, 0}});
    graph.addConnection({{eq->nodeID, 1}, {compressor->nodeID, 1}});
    graph.addConnection({{compressor->nodeID, 0}, {strip->nodeID, 0}});
    graph.addConnection({{compressor->nodeID, 1}, {strip->nodeID, ChannelStripModule::kRightBase}});

    // Master AFTER the chain above is wired, BEFORE the Strip->Master edges below: spliceMasterNode
    // re-routes whatever already feeds the audio output, and nothing of this channel's own should be
    // among that yet (see ChannelFlows.h's own comment on the ordering).
    auto* master = spliceMasterNode(graph, layout.master);
    if (master != nullptr) {
        // A PLAIN graph edge, never a macro port — see ChannelFlows.h's own comment for why.
        graph.addConnection({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}});
        graph.addConnection(
            {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}});
    }

    result.eqUuid = eqUuid;
    result.compressorUuid = compressorUuid;
    result.stripUuid = stripUuid;
    result.strip = strip;
    result.master = master;
    return result;
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
    // Overrides ADSR's stock sustain default (0.0) so a held note sustains instead of
    // plucking-and-dying after ~0.25s — see the header comment.
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

std::vector<juce::AudioProcessorGraph::Connection> findUnchanneledOutputFeeds(juce::AudioProcessorGraph& graph,
                                                                              juce::AudioProcessorGraph::NodeID start) {
    std::vector<juce::AudioProcessorGraph::Connection> exits;

    std::vector<juce::AudioProcessorGraph::NodeID> visited{start};
    std::vector<juce::AudioProcessorGraph::NodeID> queue{start};

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        auto* node = graph.getNodeForId(nodeId);
        if (node == nullptr)
            continue;
        auto* processor = node->getProcessor();
        if (processor == nullptr)
            continue;

        // Never expand PAST a terminal, an already-channeled branch, or a hidden modulation hop —
        // see the header comment for why each of these stops traversal here.
        if (dynamic_cast<RecordTapModule*>(processor) != nullptr || dynamic_cast<MasterModule*>(processor) != nullptr ||
            dynamic_cast<ChannelStripModule*>(processor) != nullptr ||
            dynamic_cast<AttenuverterModule*>(processor) != nullptr || processor->getName() == "Audio Output")
            continue;

        for (const auto& conn : graph.getConnections()) {
            if (conn.source.nodeID != nodeId)
                continue;

            auto* destNode = graph.getNodeForId(conn.destination.nodeID);
            if (destNode == nullptr)
                continue;
            auto* destProcessor = destNode->getProcessor();
            if (destProcessor == nullptr)
                continue;

            // A hidden modulation hop: never traversed, never an exit (see header comment).
            if (dynamic_cast<AttenuverterModule*>(destProcessor) != nullptr)
                continue;

            const int channel = conn.destination.channelIndex;

            if (destProcessor->getName() == "Audio Output") {
                if (channel == 0 || channel == 1)
                    exits.push_back(conn);
                continue; // terminal — never expand past Audio Output
            }
            if (dynamic_cast<RecordTapModule*>(destProcessor) != nullptr) {
                if (channel == 0 || channel == 1)
                    exits.push_back(conn);
                continue; // terminal — never expand past Rec Tap
            }
            if (dynamic_cast<MasterModule*>(destProcessor) != nullptr) {
                // kMixLeft/kMixRight are NOT an exit — only a strip's own output can land there,
                // and this BFS never reaches one (it stops at a ChannelStripModule below).
                if (channel == MasterModule::kDirectLeft || channel == MasterModule::kDirectRight)
                    exits.push_back(conn);
                continue; // terminal — never expand past Master
            }
            if (dynamic_cast<ChannelStripModule*>(destProcessor) != nullptr)
                continue; // already channeled — do not expand past it, and not an exit itself

            if (std::find(visited.begin(), visited.end(), conn.destination.nodeID) == visited.end()) {
                visited.push_back(conn.destination.nodeID);
                queue.push_back(conn.destination.nodeID);
            }
        }
    }

    return exits;
}

DefaultChannel buildChannelForFeeds(juce::AudioProcessorGraph& graph,
                                    const std::vector<juce::AudioProcessorGraph::Connection>& exits,
                                    const DefaultChannelLayout& layout) {
    if (exits.empty())
        return {};

    // Classify by DESTINATION channel before anything is removed — ch0/kDirectLeft -> Left,
    // ch1/kDirectRight -> Right (the only two channel numbers findUnchanneledOutputFeeds ever
    // returns an exit for).
    std::vector<juce::AudioProcessorGraph::NodeAndChannel> leftFeeds, rightFeeds;
    for (const auto& exit : exits) {
        const int channel = exit.destination.channelIndex;
        const bool isRight = (channel == 1) || (channel == MasterModule::kDirectRight);
        (isRight ? rightFeeds : leftFeeds).push_back(exit.source);
    }

    // REMOVE FIRST, then build — same "collect, then mutate" reasoning spliceMasterNode's own
    // splice uses (removeConnection while iterating the list it came from would invalidate it).
    for (const auto& exit : exits)
        graph.removeConnection(exit);

    return buildChannelChain(graph, leftFeeds, rightFeeds, layout);
}

} // namespace synth
