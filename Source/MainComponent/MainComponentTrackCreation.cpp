// MainComponentTrackCreation.cpp — add/delete track, MIDI/audio/instrument/plugin track
// creation and the channel-strip build-out (buildInstrumentTrackAndChain, makeChannelForNode,
// duplicateIntoChannel). MainComponent is declared in MainComponent.h; the rest of its
// implementation lives in the sibling MainComponent*.cpp units next to this one, including the
// TrackHeaderHost query/binding surface in MainComponentTrackHeaderHost.cpp.
#include "MainComponent.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Branding.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/VCAModule.h" // VCAModule::kRightBase for the P9-3i envelope+VCA insertion below
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Timeline/TrackColour.h"
#include <algorithm>

namespace {

// Horizontal gap (T173a) between each of the expanded channel's cards (Track Audio -> EQ ->
// Compressor -> Strip -> Master), on top of the real card widths (GraphEditor::estimateModuleSize)
// — purely cosmetic. addAudioTrack lays every card out left-to-right along this stride so none
// overlap regardless of how wide an individual card is (e.g. Parametric EQ's double-width card).
constexpr int kChannelCardGapX = 40;

} // namespace

void MainComponent::deleteTrack(synth::TrackId track) {
    const auto* existing = timelineDoc.getTrack(track);
    if (existing == nullptr)
        return;
    const juce::String uuid = existing->bindingUuid;

    // ONE undo step covering both domains: the track and the node that fed it disappear together,
    // and come back together.
    undoManager.recordCombinedChange(audioEngine.getGraph(), timelineDoc, [this, track, uuid] {
        if (auto* node = findNodeByUuid(uuid)) {
            // Mirrors GraphEditor::requestDeleteModule: drop the mod-matrix rows before the node
            // (and the connections into it) go, then reconcile the canvas.
            graphEditor.getModMatrix().clearRows();
            audioEngine.getGraph().removeNode(node->nodeID);
            graphEditor.updateComponents();
        }
        timelineDoc.removeTrack(track);
    });
    reconcileTimelineAfterGraphChange();
}

void MainComponent::performTrackEdit(const std::function<void()>& mutation) {
    if (!mutation)
        return;
    // recordTimelineChange pushes nothing when the mutation turns out to be a no-op, so a header
    // that writes the value already in the doc costs no undo step.
    undoManager.recordTimelineChange(timelineDoc, mutation);
}

void MainComponent::addMidiTrack() {
    const int index = (int)timelineDoc.getTracks().size();

    // ONE compound undo step: the Track In node, its auto-wire, the track, its binding and its
    // colour are a single gesture, so a single Cmd+Z removes all of it.
    const bool pushed = undoManager.recordCombinedChange(audioEngine.getGraph(), timelineDoc, [this, index] {
        // Doc side FIRST. addTrack refuses at kMaxTracks, and a node created before that refusal
        // stays in the graph with no track to feed — recordCombinedChange RECORDS the mutation, it
        // does not undo it. A track with no binding yet is never flagged orphaned, so the order
        // costs nothing.
        const auto trackId = timelineDoc.addTrack(synth::TrackKind::Midi, "Track " + juce::String(index + 1));
        if (!trackId.isValid())
            return; // at kMaxTracks: nothing added, and no node created
        const juce::String uuid = createTrackInNode();
        if (uuid.isNotEmpty())
            timelineDoc.setTrackBinding(trackId, uuid);
        timelineDoc.setTrackColour(trackId, synth::ui::trackPaletteColour(index).getARGB());
    });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed ? "Added Track " + juce::String(index + 1) : "Could not add a track");
}

void MainComponent::addAudioTrack() {
    const int index = (int)timelineDoc.getTracks().size();
    juce::String trackName; // set inside the mutation; read afterwards for the status message

    // T173a: "+ Track -> Audio Track" now creates a WHOLE mixer channel in ONE undo step — Track
    // Audio -> Parametric EQ (bypassed) -> Compressor (bypassed) -> Channel Strip (Stereo) -> Master
    // (Mix), with {Track Audio, EQ, Compressor, Strip} boxed into one collapsed macro named after the
    // track, plus the track/binding/colour exactly like addMidiTrack()'s single compound step. A
    // single Cmd+Z removes every bit of it.
    //
    // Master stays OUTSIDE the macro, and the Strip -> Master cable is left a PLAIN graph edge, never
    // a macro port: synth::spliceMasterNode/synth::ensureMasterNode (Source/Mixer/MasterSplice.h)
    // classify a re-routed feed as Mix vs Direct by checking whether the connection's SOURCE NODE is
    // itself a ChannelStripModule. A MacroOutlet sitting between the strip and Master would make the
    // source node a MacroOutlet instead, defeating that check — which the later "Create channels"
    // subtask depends on.
    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [this, index, &trackName] {
            // Doc side FIRST, for the reason addMidiTrack() spells out: a node created before the
            // kMaxTracks refusal would be left orphaned in the graph.
            trackName = "Audio " + juce::String(index + 1);
            const auto trackId = timelineDoc.addTrack(synth::TrackKind::Audio, trackName);
            if (!trackId.isValid())
                return; // at kMaxTracks: nothing added, no node created, no macro

            // Unwired (T173a): buildDefaultAudioChannel below wires it into the chain instead of
            // straight to the master bus.
            const juce::String trackAudioUuid = createTrackAudioNode(/*wireDirectlyToMasterBus=*/false);
            if (trackAudioUuid.isEmpty())
                return;
            timelineDoc.setTrackBinding(trackId, trackAudioUuid);
            timelineDoc.setTrackColour(trackId, synth::ui::trackPaletteColour(index).getARGB());

            auto* trackAudioNode = findNodeByUuid(trackAudioUuid);
            if (trackAudioNode == nullptr)
                return;
            const auto trackAudioPosition =
                juce::Point<int>(static_cast<int>(trackAudioNode->properties.getWithDefault("x", 0)),
                                 static_cast<int>(trackAudioNode->properties.getWithDefault("y", 0)));

            // Lay every card of the expanded chain out left-to-right from the real card widths
            // (GraphEditor::estimateModuleSize) rather than a fixed stride — Parametric EQ is
            // double-width, so a fixed stride overlaps it with the Compressor (the bug this fixes).
            // {Track Audio, EQ, Compressor, Strip} end up boxed into one collapsed macro below, so
            // only their expanded-state positions matter for not overlapping each other; Master's
            // position is only used the first time (it is a singleton afterwards) and sits right of
            // where the collapsed macro card will be.
            const int eqX = trackAudioPosition.x + GraphEditor::estimateModuleSize("Track Audio").x + kChannelCardGapX;
            const int compressorX = eqX + GraphEditor::estimateModuleSize("Parametric EQ").x + kChannelCardGapX;
            const int stripX = compressorX + GraphEditor::estimateModuleSize("Compressor").x + kChannelCardGapX;
            const int masterX = stripX + GraphEditor::estimateModuleSize("Channel Strip").x + kChannelCardGapX;
            const synth::DefaultChannelLayout layout{
                /*eq=*/{eqX, trackAudioPosition.y},
                /*compressor=*/{compressorX, trackAudioPosition.y},
                /*strip=*/{stripX, trackAudioPosition.y},
                /*master=*/{masterX, trackAudioPosition.y},
            };

            // T187 layout follow-up: know BEFORE building the chain whether this call is the one
            // that splices Master for the first time — that's the only time a bare "Audio Output"
            // (the newPatch seed, or wherever the user first dropped one) is worth relocating below.
            const bool masterExistedBefore = synth::findMasterNode(audioEngine.getGraph()) != nullptr;

            const auto channel = synth::buildDefaultAudioChannel(audioEngine.getGraph(), *trackAudioNode, layout);
            if (channel.stripUuid.isEmpty())
                return; // a factory/addNode failure partway — see buildDefaultAudioChannel's contract

            // On the very first channel, Master lands right of this chain (masterX above) but a bare
            // Audio Output still sits wherever it started — the newPatch seed's canvas origin, or
            // wherever the user first dropped one — so the finished chain would cable back across the
            // whole canvas to reach it. Move it to terminate the row instead: Track Audio -> EQ ->
            // Compressor -> Strip -> Master -> Audio Output, left to right. Only done once; after
            // Master exists, the canvas is already arranged and nothing here should reshuffle it.
            if (!masterExistedBefore && channel.master != nullptr) {
                const int outputX = masterX + GraphEditor::estimateModuleSize("Master").x + kChannelCardGapX;
                for (auto* node : audioEngine.getGraph().getNodes()) {
                    if (node != nullptr && node->getProcessor() != nullptr &&
                        node->getProcessor()->getName() == "Audio Output") {
                        node->properties.set("x", outputX);
                        node->properties.set("y", trackAudioPosition.y);
                    }
                }
            }

            // Box {Track Audio, EQ, Compressor, Strip} into ONE collapsed macro named after the
            // track. Master is deliberately NOT a member — see this method's own comment above.
            graphEditor.addMacroForMembers({trackAudioUuid, channel.eqUuid, channel.compressorUuid, channel.stripUuid},
                                           trackName, trackAudioPosition);

            // Inside the mutation, not after: MacroSet::retainOnly() (run by updateComponents())
            // must see every node above still alive to keep the macro's membership.
            graphEditor.updateComponents();
        });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed ? "Added " + trackName : "Could not add a track");
}

void MainComponent::addInstrumentTrack(const juce::String& instrumentModuleType, bool poly) {
    // T183 (P9-3b): "+ Track -> Instrument -> {Oscillator/Wavetable/Sampler}" builds Track In ->
    // instrument -> default chain (Parametric EQ bypassed -> Compressor bypassed -> Channel Strip
    // Stereo -> Master Mix) in ONE undo step, the MIDI-track mirror of addAudioTrack()'s T173a step.
    // {Track In, instrument, EQ, Compressor, Strip} are boxed into one collapsed macro named after
    // the track; Master stays outside it for the same spliceMasterNode reason addAudioTrack's own
    // comment explains. A single Cmd+Z removes every bit of it. See buildInstrumentTrackAndChain's
    // own comment for the shared tail this delegates to (also used by addInstrumentPluginTrack).
    //
    // This is a TrackKind::Midi track — a Track In feeding exactly one instrument is already what
    // addMidiTrack() produces once a cable is drawn by hand; this flow just draws that cable and
    // builds the channel automatically. docs/mixer.md §5.2 once called this "(new track kind)" but
    // T183's own scope never asked for a new TrackKind, and adding one is a format/serialization
    // change nothing here requires — see that doc's update alongside this change.
    buildInstrumentTrackAndChain(synth::AIStateMapper::createModule(instrumentModuleType), instrumentModuleType, poly);
}

// T183/FRO42: the shared tail of addInstrumentTrack() and addInstrumentPluginTrack() — Track In
// -> `instrumentProcessor` -> [poly/envelope branches, gated exactly as addInstrumentTrack's own
// comment describes] -> default chain -> Master, boxed into one collapsed macro, as ONE undo
// step. `instrumentProcessor` must be non-null and NOT yet added to any graph (a factory
// failure, or a plugin whose load failed/refused, is the CALLER's job to catch and report before
// this ever runs — see addInstrumentPluginTrack's own comment for why: nothing here may open a
// transaction for an instrument that doesn't exist). Every branch this method takes (Oscillator/
// Wavetable's ADSR+VCA, a poly instrument's Voice Mixer) is keyed off the PROCESSOR's own
// getName()/"poly" parameter, never a caller-supplied type string — a hosted plugin's getName()
// is "Hosted Plugin" and it declares no "poly" parameter, so every one of those branches falls
// out to the plain path automatically, with no separate plugin-shaped copy of this logic. `poly`
// mirrors addInstrumentTrack's own parameter (always false from the plugin path, which has no
// poly concept). `trackNamePrefix` becomes "<prefix> <N>", same numbering as before.
void MainComponent::buildInstrumentTrackAndChain(std::unique_ptr<juce::AudioProcessor> instrumentProcessor,
                                                 const juce::String& trackNamePrefix, bool poly) {
    if (instrumentProcessor == nullptr) {
        // A factory failure (addInstrumentTrack) or a caller that already checked and still handed
        // us nothing — either way, nothing to build with, and nothing below may run: no track, no
        // node, no undo entry.
        statusBar.showMessage("Could not add a track");
        return;
    }

    const int index = (int)timelineDoc.getTracks().size();
    juce::String trackName; // set inside the mutation; read afterwards for the status message
    // shared_ptr so the mutate lambda (which recordGraphTimelineAndMacroChange takes by const-ref
    // and must remain copyable as std::function) can carry a move-only juce::AudioProcessor —
    // the same idiom GraphEditor::addModuleAtCanvasPosition's own recordStructuralChange call uses.
    auto stagedInstrument = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(instrumentProcessor));

    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(),
        [this, index, &trackName, trackNamePrefix, poly, stagedInstrument] {
            trackName = trackNamePrefix + " " + juce::String(index + 1);
            const auto trackId = timelineDoc.addTrack(synth::TrackKind::Midi, trackName);
            if (!trackId.isValid())
                return; // at kMaxTracks: nothing added, no node created, no macro

            auto& graph = audioEngine.getGraph();

            // Track In, through the factory like createTrackInNode() — inlined rather than reused
            // because that method auto-wires to "the sole existing instrument" and calls
            // updateComponents() itself, neither of which fits this compound build (this wires the
            // instrument THIS call creates, and updateComponents() runs once at the end).
            auto trackInProcessor = synth::AIStateMapper::createModule("Track In");
            if (trackInProcessor == nullptr)
                return;
            auto trackInNodePtr = graph.addNode(std::move(trackInProcessor));
            if (trackInNodePtr == nullptr)
                return;
            auto* trackInNode = trackInNodePtr.get();
            const juce::String trackInUuid = juce::Uuid().toDashedString();
            trackInNode->properties.set("uuid", trackInUuid);
            if (auto* module = dynamic_cast<ModuleBase*>(trackInNode->getProcessor()))
                module->setNodeUuid(trackInUuid);
            const auto trackInSize = GraphEditor::estimateModuleSize("Track In");
            const auto trackInPosition = graphEditor.findLeftEdgeSlotBelowModules(trackInSize.x, trackInSize.y);
            trackInNode->properties.set("x", trackInPosition.x);
            trackInNode->properties.set("y", trackInPosition.y);

            // The instrument, right of Track In — already created (and, for a hosted plugin,
            // already loaded) by the caller; adopted into the live graph HERE, inside this undo
            // transaction, so Cmd+Z removes it along with everything else.
            if (!*stagedInstrument)
                return; // moved exactly once above; defensive, mirrors addModuleAtCanvasPosition's own guard
            const int instrumentX = trackInPosition.x + trackInSize.x + kChannelCardGapX;
            auto instrumentNodePtr = graph.addNode(std::move(*stagedInstrument));
            if (instrumentNodePtr == nullptr)
                return;
            auto* instrumentNode = instrumentNodePtr.get();
            // The processor's OWN name, not the caller's `trackNamePrefix` — for a hosted plugin the
            // latter is the plugin's display name (e.g. "Serum"), never "Hosted Plugin", so every
            // branch below that keys off the factory type name (isOscOrWavetable, estimateModuleSize)
            // must read it from the node it actually got.
            const juce::String instrumentModuleType = instrumentNode->getProcessor()->getName();
            const juce::String instrumentUuid = juce::Uuid().toDashedString();
            instrumentNode->properties.set("uuid", instrumentUuid);
            if (auto* module = dynamic_cast<ModuleBase*>(instrumentNode->getProcessor()))
                module->setNodeUuid(instrumentUuid);
            instrumentNode->properties.set("x", instrumentX);
            instrumentNode->properties.set("y", trackInPosition.y);

            if (poly)
                synth::setProcessorPoly(instrumentNode->getProcessor(), true);

            timelineDoc.setTrackBinding(trackId, trackInUuid);
            timelineDoc.setTrackColour(trackId, synth::ui::trackPaletteColour(index).getARGB());

            // Track In -> instrument, MIDI. Unambiguous by construction: this instrument was just
            // created for this track alone.
            graph.addConnection({{trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                 {instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

            const bool isOscOrWavetable = instrumentModuleType == "Oscillator" || instrumentModuleType == "Wavetable";
            const bool instrumentIsPoly = synth::isProcessorPoly(instrumentNode->getProcessor());

            auto* chainSource = instrumentNode;
            int sourceRightChannel = 1;
            if (auto* instrumentModule = dynamic_cast<ModuleBase*>(instrumentNode->getProcessor()))
                sourceRightChannel = instrumentModule->rightAudioLegChannel();
            juce::String chainSourceType = instrumentModuleType;
            auto chainSourcePosition =
                juce::Point<int>(static_cast<int>(chainSource->properties.getWithDefault("x", 0)),
                                 static_cast<int>(chainSource->properties.getWithDefault("y", 0)));

            juce::String voiceMixerUuid, polyMidiUuid, adsrUuid, vcaUuid;

            if (isOscOrWavetable && instrumentIsPoly) {
                // FRO46 (P9-3j): a poly Oscillator/Wavetable gets a TRUE per-voice envelope — Poly
                // MIDI + poly ADSR + poly VCA, replacing both the Voice Mixer stage below and
                // addEnvelopeAndVCAForRawInstrument's forced-mono ADSR/VCA (see
                // addPolyEnvelopeAndVCAForInstrument's own comment for why the non-poly path can't
                // just be made poly in place). No Voice Mixer is inserted: the poly VCA does its own
                // 8-voice summing.
                const int polyMidiX =
                    instrumentX + GraphEditor::estimateModuleSize(instrumentModuleType).x + kChannelCardGapX;
                const int adsrX = polyMidiX + GraphEditor::estimateModuleSize("Poly MIDI").x + kChannelCardGapX;
                const int vcaX = adsrX + GraphEditor::estimateModuleSize("ADSR").x + kChannelCardGapX;
                const auto polyEnv = synth::addPolyEnvelopeAndVCAForInstrument(
                    graph, *trackInNode, *instrumentNode, {polyMidiX, trackInPosition.y}, {adsrX, trackInPosition.y},
                    {vcaX, trackInPosition.y});
                if (polyEnv.vca != nullptr) {
                    polyMidiUuid = polyEnv.polyMidiUuid;
                    adsrUuid = polyEnv.adsrUuid;
                    vcaUuid = polyEnv.vcaUuid;
                    chainSource = polyEnv.vca;
                    // The legacy ch0/ch1 duplicate (VCAModule.h) — the instrument's R-octet is
                    // deliberately not wired into the VCA's real Audio R poly block, same known
                    // limitation addVoiceMixerForPolyInstrument's own comment documents.
                    sourceRightChannel = 1;
                    chainSourceType = "VCA";
                    chainSourcePosition = {vcaX, trackInPosition.y};
                }
            } else {
                // A poly instrument's L-octet needs summing before the strip (see
                // addVoiceMixerForPolyInstrument's own comment) — a no-op for today's factory-default
                // instruments (poly defaults off) and for the poly Oscillator/Wavetable case handled
                // above, kept general for a poly Sampler or any other poly instrument.
                const int voiceMixerX =
                    instrumentX + GraphEditor::estimateModuleSize(instrumentModuleType).x + kChannelCardGapX;
                auto* voiceMixerNode = synth::addVoiceMixerForPolyInstrument(
                    graph, *instrumentNode, {voiceMixerX, trackInPosition.y}, voiceMixerUuid);

                if (voiceMixerNode != nullptr) {
                    chainSource = voiceMixerNode;
                    sourceRightChannel = 1;
                    chainSourceType = "Voice Mixer";
                    chainSourcePosition =
                        juce::Point<int>(static_cast<int>(chainSource->properties.getWithDefault("x", 0)),
                                         static_cast<int>(chainSource->properties.getWithDefault("y", 0)));
                }

                // P9-3i (FRO43): Oscillator/Wavetable have no envelope of their own, so a held (or
                // even released) note drones forever. Insert an ADSR (gated by the same Track In
                // MIDI as the instrument) driving a VCA, ahead of the rest of the chain — AFTER any
                // Voice Mixer stage above, never before it (see addEnvelopeAndVCAForRawInstrument's
                // own comment for why). Sampler already has its own one-shot playback envelope and
                // is out of scope. Never reached for a poly Oscillator/Wavetable (handled above).
                if (isOscOrWavetable) {
                    const int adsrX =
                        chainSourcePosition.x + GraphEditor::estimateModuleSize(chainSourceType).x + kChannelCardGapX;
                    const int vcaX = adsrX + GraphEditor::estimateModuleSize("ADSR").x + kChannelCardGapX;
                    const auto envAndVca = synth::addEnvelopeAndVCAForRawInstrument(
                        graph, *trackInNode, *chainSource, sourceRightChannel, {adsrX, chainSourcePosition.y},
                        {vcaX, chainSourcePosition.y});
                    if (envAndVca.vca != nullptr) {
                        adsrUuid = envAndVca.adsrUuid;
                        vcaUuid = envAndVca.vcaUuid;
                        chainSource = envAndVca.vca;
                        sourceRightChannel = VCAModule::kRightBase;
                        chainSourceType = "VCA";
                        chainSourcePosition = {vcaX, chainSourcePosition.y};
                    }
                }
            }

            // Lay every card of the expanded chain out left-to-right from the real card widths, the
            // same reason addAudioTrack's own comment gives (Parametric EQ is double-width).
            const int eqX =
                chainSourcePosition.x + GraphEditor::estimateModuleSize(chainSourceType).x + kChannelCardGapX;
            const int compressorX = eqX + GraphEditor::estimateModuleSize("Parametric EQ").x + kChannelCardGapX;
            const int stripX = compressorX + GraphEditor::estimateModuleSize("Compressor").x + kChannelCardGapX;
            const int masterX = stripX + GraphEditor::estimateModuleSize("Channel Strip").x + kChannelCardGapX;
            const synth::DefaultChannelLayout layout{
                /*eq=*/{eqX, chainSourcePosition.y},
                /*compressor=*/{compressorX, chainSourcePosition.y},
                /*strip=*/{stripX, chainSourcePosition.y},
                /*master=*/{masterX, chainSourcePosition.y},
            };

            const auto channel = synth::buildDefaultAudioChannel(graph, *chainSource, layout, sourceRightChannel);
            if (channel.stripUuid.isEmpty())
                return; // a factory/addNode failure partway — see buildDefaultAudioChannel's contract

            // Box {Track In, instrument, [Voice Mixer if poly], [Poly MIDI if poly Oscillator/
            // Wavetable], [ADSR+VCA if Oscillator/Wavetable], EQ, Compressor, Strip} into ONE
            // collapsed macro named after the track. Master is deliberately NOT a member — same
            // spliceMasterNode reason addAudioTrack's own comment explains.
            std::vector<juce::String> macroMembers{trackInUuid, instrumentUuid};
            if (!voiceMixerUuid.isEmpty())
                macroMembers.push_back(voiceMixerUuid);
            if (!polyMidiUuid.isEmpty())
                macroMembers.push_back(polyMidiUuid);
            if (!adsrUuid.isEmpty())
                macroMembers.push_back(adsrUuid);
            if (!vcaUuid.isEmpty())
                macroMembers.push_back(vcaUuid);
            macroMembers.push_back(channel.eqUuid);
            macroMembers.push_back(channel.compressorUuid);
            macroMembers.push_back(channel.stripUuid);
            graphEditor.addMacroForMembers(macroMembers, trackName, trackInPosition);

            // Inside the mutation, not after: MacroSet::retainOnly() (run by updateComponents())
            // must see every node above still alive to keep the macro's membership.
            graphEditor.updateComponents();
        });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed ? "Added " + trackName : "Could not add a track");
}

// FRO26 (P9-3e, docs/mixer.md §5.13): every track whose bound node's chain still reaches the
// output without passing through a ChannelStripModule — the same synth::findUnchanneledOutputFeeds
// query T184's connect-triggered auto-channel already runs, just from each track's own binding
// instead of a just-completed cable drag. An unbound or orphaned track (empty/unresolved
// bindingUuid) contributes nothing — there is no chain to channel.
bool MainComponent::hasTracksNeedingChannels() const {
    auto& graph = audioEngine.getGraph();
    for (const auto& track : timelineDoc.getTracks()) {
        if (track.bindingUuid.isEmpty())
            continue;
        auto* node = findNodeByUuid(track.bindingUuid);
        if (node == nullptr)
            continue;
        if (!synth::findUnchanneledOutputFeeds(graph, node->nodeID).empty())
            return true;
    }
    return false;
}

// FRO26 (P9-3e, docs/mixer.md §5.13): the "+ Track" menu's "Create Channels" action for existing
// projects — docs/mixer.md §5.13 says the project itself opens unchanged (no automatic migration on
// load; this method is never called from anywhere but the explicit menu choice below). Gathers
// every track's own bound node first (outside the undo transaction — this is a pure read), then
// wraps GraphEditor::createChannelsForUnchanneledTracks() in ONE
// recordGraphTimelineAndMacroChange transaction, the same shape addAudioTrack/addInstrumentTrack
// use, so N channel-less tracks becoming N new channels is a SINGLE Cmd+Z. A track already reaching
// a ChannelStripModule is silently skipped inside that call (see its own comment) — nothing here
// needs to pre-filter beyond "has a live binding at all".
void MainComponent::createChannelsForExistingTracks() {
    std::vector<juce::AudioProcessorGraph::NodeID> sourceNodeIds;
    for (const auto& track : timelineDoc.getTracks()) {
        if (track.bindingUuid.isEmpty())
            continue;
        auto* node = findNodeByUuid(track.bindingUuid);
        if (node != nullptr)
            sourceNodeIds.push_back(node->nodeID);
    }

    // No-op, no undo step, when nothing needs a channel (also the "+ Track" menu's own disabled
    // condition — see hasTracksNeedingChannels() above) — checked again here rather than trusting
    // the menu's enabled state, since this is also the seam tests drive directly.
    if (!hasTracksNeedingChannels()) {
        statusBar.showMessage("No tracks need a channel");
        return;
    }

    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [this, &sourceNodeIds] {
            graphEditor.createChannelsForUnchanneledTracks(sourceNodeIds);
            graphEditor.updateComponents();
        });

    reconcileTimelineAfterGraphChange();
    // Reaching here with pushed == false means hasTracksNeedingChannels() was true but the sweep
    // still built nothing (a factory/addNode failure partway through, same contract as
    // buildDefaultAudioChannel/buildChannelForFeeds) — an internal failure, not "nothing needed a
    // channel" (that case already returned above), so it gets the same wording addAudioTrack's own
    // failure branch uses rather than a misleadingly cheerful no-op message.
    statusBar.showMessage(pushed ? "Created channels" : "Could not create channels");
}

// FRO25 (P9-3d, docs/mixer.md §5.8): "Make channel" on one track's bound node — the header menu's
// enabled state is the same synth::planMakeChannel query the action itself runs.
bool MainComponent::canMakeChannelForTrack(synth::TrackId trackId) const {
    const auto* track = timelineDoc.getTrack(trackId);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return false;
    auto* node = findNodeByUuid(track->bindingUuid);
    return node != nullptr && graphEditor.nodeNeedsChannel(node->nodeID);
}

void MainComponent::makeChannelForTrack(synth::TrackId trackId) {
    const auto* track = timelineDoc.getTrack(trackId);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return;
    if (auto* node = findNodeByUuid(track->bindingUuid))
        makeChannelForNode(node->nodeID);
}

// Every "Make channel" entry point lands here: the refusal/no-op checks run BEFORE the transaction
// (so neither pushes an undo step), then ONE recordGraphTimelineAndMacroChange covers the whole
// rebuild — own channel, any bus channels, their macros and ports — with updateComponents() inside
// the mutation (MacroSet::retainOnly must see every new node alive), then the reconcile pass. The
// macro is named after the track the chain belongs to (the one bound to `source`), falling back to
// the source module's own name for a trackless chain picked on the canvas.
// FRO25 (P9-3d): the ONE undo transaction (graph + timeline + macros) + reconcile pass behind
// every "Make channel" entry point (header menu, canvas/module menu via
// GraphEditor::onMakeChannelRequested), and behind "Duplicate into Channel"
// (GraphEditor::onDuplicateIntoChannelRequested).
void MainComponent::makeChannelForNode(juce::AudioProcessorGraph::NodeID source) {
    auto& graph = audioEngine.getGraph();
    auto* sourceNode = graph.getNodeForId(source);
    if (sourceNode == nullptr)
        return;
    const auto plan = synth::planMakeChannel(graph, source, graphEditor.getMacros());
    if (!plan.needsChannel) {
        statusBar.showMessage("This chain already has a channel");
        return;
    }
    if (plan.refusal.isNotEmpty()) {
        statusBar.showMessage(plan.refusal);
        return;
    }

    juce::String name = sourceNode->getProcessor() != nullptr ? sourceNode->getProcessor()->getName() : "Channel";
    const juce::String sourceUuid = sourceNode->properties["uuid"].toString();
    if (sourceUuid.isNotEmpty())
        for (const auto& track : timelineDoc.getTracks())
            if (track.bindingUuid == sourceUuid) {
                name = track.name;
                break;
            }

    bool built = false;
    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        graph, timelineDoc, graphEditor.getMacros(), [this, source, name, &built] {
            built = graphEditor.makeChannelFromNode(source, name);
            graphEditor.updateComponents();
        });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(built && pushed ? "Made channel: " + name : juce::String("Could not make a channel"));
}

// FRO25 (P9-3d): "Duplicate into Channel" — same one-transaction + reconcile shape as above.
void MainComponent::duplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId) {
    const auto targets = graphEditor.duplicateIntoChannelTargets(nodeId);
    if (std::find(targets.begin(), targets.end(), macroId) == targets.end())
        return;

    bool built = false;
    undoManager.recordGraphTimelineAndMacroChange(audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(),
                                                  [this, nodeId, macroId, &built] {
                                                      built = graphEditor.duplicateIntoChannel(nodeId, macroId);
                                                      graphEditor.updateComponents();
                                                  });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(built ? "Duplicated into the channel" : "Could not duplicate into the channel");
}

// FRO42 (P9-3h): instrument-capable hosted plugins for the "+ Track -> Instrument -> Plugin"
// submenu — see TrackHeaderHost::getInstrumentPluginOptions's own comment. getKnownPlugins() (full
// juce::PluginDescription, which carries isInstrument) is read here rather than
// getKnownPluginIdentities() (PluginIdentity alone, no isInstrument) precisely because the filter
// needs that field.
std::vector<synth::PluginIdentity> MainComponent::getInstrumentPluginOptions() const {
    std::vector<synth::PluginIdentity> options;
    for (const auto& description : getPluginScanService().getKnownPlugins()) {
        if (!description.isInstrument)
            continue;
        // FRO42 review fix: never offer to host this app's OWN VST3/AU build as an instrument —
        // matched against synth::branding's product identity (the single source of truth CMake's
        // PRODUCT_NAME/COMPANY_NAME args are mirrored into for C++ code, see Branding.h) rather than
        // a literal re-typed here, and against BOTH name and manufacturer so a same-named third-
        // party plugin from a different vendor is not caught by accident. The library sidebar goes
        // through getKnownPluginIdentities() (MainComponent::refreshPluginLibrary(), never this
        // method), so it is deliberately untouched by this filter.
        if (description.name.equalsIgnoreCase(synth::branding::kProductName) &&
            description.manufacturerName.equalsIgnoreCase(synth::branding::kCompanyName))
            continue;
        options.push_back(synth::PluginIdentity::fromDescription(description));
    }

    // Same ordering as PluginScanService::getKnownPluginIdentities(), so the submenu lists plugins
    // in the same order the library sidebar's Plugins section does.
    std::sort(options.begin(), options.end(), [](const synth::PluginIdentity& a, const synth::PluginIdentity& b) {
        const int byName = a.name.compareIgnoreCase(b.name);
        return byName != 0 ? byName < 0 : a.format < b.format;
    });
    return options;
}

void MainComponent::addInstrumentPluginTrack(const synth::PluginIdentity& identity) {
    if (!identity.isValid())
        return;

    const auto description = getPluginScanService().resolve(identity);
    if (!description.has_value()) {
        statusBar.showMessage((identity.name.isNotEmpty() ? identity.name : juce::String("That plugin")) +
                              " is no longer available - try rescanning.");
        return; // graph and undo stack both untouched
    }

    auto instrumentProcessor = synth::AIStateMapper::createModule("Hosted Plugin");
    auto* hosted = dynamic_cast<synth::HostedPluginModule*>(instrumentProcessor.get());
    if (hosted == nullptr) {
        statusBar.showMessage("Could not add a track");
        return;
    }

    // Owned here (an independent, external owner) for the whole in-flight load — see
    // pendingInstrumentPluginLoads_'s own comment for why NOT a shared_ptr captured by the module's
    // own onLoadCompleted below, which would make the object keep itself alive through its own
    // member. Nothing here has touched the graph or the undo stack yet, and nothing below does
    // either until the load actually succeeds.
    pendingInstrumentPluginLoads_.push_back(std::move(instrumentProcessor));

    const juce::String pluginName = description->name;
    // FRO42 review fix: captured NOW, against the document this load was started for. New Patch/
    // Open/Load preset (all via guardUnsavedChanges) bump documentGeneration_ before replacing the
    // document, so a completion that lands after that has a stale value here — see
    // documentGeneration_'s own comment.
    const int loadGeneration = documentGeneration_;
    juce::Component::SafePointer<MainComponent> safeThis(this);
    hosted->onLoadCompleted = [safeThis, hosted, pluginName, loadGeneration](bool success) {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return; // this MainComponent (and its pending-loads list) is already gone

        if (self->documentGeneration_ != loadGeneration) {
            // The document this load was for has already been replaced (New Patch/Open/Load preset
            // ran while the load was still in flight). Building the track now would land it in the
            // WRONG (freshly loaded/created) document. Drop it the same deferred way a failed load
            // is dropped — never destroy `hosted` from inside its own currently-executing callback —
            // and touch neither the graph nor the undo stack of the document that is live now.
            juce::Component::SafePointer<MainComponent> deferredSelf = safeThis;
            juce::MessageManager::callAsync([deferredSelf, hosted] {
                if (auto* mc = deferredSelf.getComponent())
                    mc->dropPendingInstrumentPluginLoad(hosted);
            });
            return;
        }

        if (success) {
            // Extract ownership WITHOUT destroying anything — it transfers straight into
            // buildInstrumentTrackAndChain (ultimately, the graph node), never freed here.
            auto& pending = self->pendingInstrumentPluginLoads_;
            const auto it =
                std::find_if(pending.begin(), pending.end(),
                             [hosted](const std::unique_ptr<juce::AudioProcessor>& p) { return p.get() == hosted; });
            if (it == pending.end())
                return; // should not happen — defensive
            auto instrumentProcessor = std::move(*it);
            pending.erase(it);
            self->buildInstrumentTrackAndChain(std::move(instrumentProcessor), pluginName, /*poly=*/false);
            return;
        }

        // Failed or refused (including the over-max refusal inside publishInstance(), which
        // hasInstance() reads identically to an outright backend failure — see
        // HostedPluginModule::onLoadCompleted's own comment). Graph and undo stack both untouched:
        // `hosted` is still owned only by pendingInstrumentPluginLoads_ and never joined the graph.
        self->statusBar.showMessage(hosted->getStatusMessage().isNotEmpty() ? hosted->getStatusMessage()
                                                                            : "Could not load " + pluginName);

        // Deferred to the next message-loop turn: dropping the entry here would destroy `hosted`
        // — and with it the onLoadCompleted std::function member THIS VERY LAMBDA is the target of
        // — from inside its own currently-executing invocation. callAsync runs on a fresh call
        // stack once this call has fully returned, where that is simply a normal member teardown.
        juce::Component::SafePointer<MainComponent> deferredSelf = safeThis;
        juce::MessageManager::callAsync([deferredSelf, hosted] {
            if (auto* mc = deferredSelf.getComponent())
                mc->dropPendingInstrumentPluginLoad(hosted);
        });
    };
    hosted->loadPlugin(*description);
}

void MainComponent::dropPendingInstrumentPluginLoad(juce::AudioProcessor* processor) {
    auto& pending = pendingInstrumentPluginLoads_;
    pending.erase(
        std::remove_if(pending.begin(), pending.end(),
                       [processor](const std::unique_ptr<juce::AudioProcessor>& p) { return p.get() == processor; }),
        pending.end());
}
