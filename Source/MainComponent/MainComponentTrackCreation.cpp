// MainComponentTrackCreation.cpp — add/delete track, MIDI/audio/instrument/plugin track
// creation and the channel-strip build-out (buildInstrumentTrackAndChain, makeChannelForNode,
// duplicateIntoChannel). MainComponent is declared in MainComponent.h; the rest of its
// implementation lives in the sibling MainComponent*.cpp units next to this one, including the
// TrackHeaderHost query/binding surface in MainComponentTrackHeaderHost.cpp.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Branding.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/VCAModule.h" // VCAModule::kRightBase for the envelope+VCA insertion below
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Timeline/UniqueTrackName.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Timeline/TrackColour.h"
#include <algorithm>
#include <set>
#include <vector>

namespace {

// Horizontal gap between each of the expanded channel's cards (Track Audio -> Gate -> EQ ->
// Compressor -> Strip -> Master), on top of the real card widths (GraphEditor::estimateModuleSize)
// — purely cosmetic. addAudioTrack lays every card out left-to-right along this stride so none
// overlap regardless of how wide an individual card is (e.g. Parametric EQ's double-width card).
constexpr int kChannelCardGapX = 40;

// The per-type default track preset settings keys — duplicated from PreferencesSettingsTabInternal.h's own copy for
// the same "one-line string not worth a header dependency" reason every other cross-file settings key in this
// codebase is (see docs/mixer/track-presets.md#saving-and-setting-a-default).
constexpr const char* kMixerDefaultTrackPresetAudioKey = "mixerDefaultTrackPresetAudio";
constexpr const char* kMixerDefaultTrackPresetInstrumentKey = "mixerDefaultTrackPresetInstrument";

// `base` made unique against the document's current track names ("Diva", else "Diva 2", "Diva 3"...).
juce::String uniqueTrackName(const synth::TimelineDoc& doc, const juce::String& base) {
    std::vector<juce::String> names;
    for (const auto& track : doc.getTracks())
        names.push_back(track.name);
    return synth::uniqueNameAmong(base, names);
}

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

// Cmd+Backspace on a focused row. Asks first unless the person switched the question off; the deletion itself is
// deleteTrack, so the menu's one undo step is unchanged. "Don't ask again" counts only when they confirm.
void MainComponent::deleteTrackAfterConfirm(synth::TrackId track) {
    const auto* existing = timelineDoc.getTrack(track);
    if (existing == nullptr)
        return;
    const auto* settings = appProperties.getUserSettings();
    if (settings != nullptr && !settings->getBoolValue(synth::ui::kAskBeforeDeletingTrackKey, true)) {
        deleteTrack(track);
        return;
    }
    juce::Component::SafePointer<MainComponent> safeThis(this);
    synth::ui::confirmDeleteTrack(synth::ui::deleteTrackConfirmText(existing->name),
                                  [safeThis, track](bool confirmed, bool dontAskAgain) {
                                      auto* self = safeThis.getComponent();
                                      if (self == nullptr || !confirmed)
                                          return;
                                      if (dontAskAgain)
                                          if (auto* userSettings = self->appProperties.getUserSettings()) {
                                              userSettings->setValue(synth::ui::kAskBeforeDeletingTrackKey, "0");
                                              userSettings->saveIfNeeded();
                                          }
                                      self->deleteTrack(track);
                                  });
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

std::function<void(juce::AudioProcessor&, const juce::String&)> MainComponent::newModuleHook() {
    return [this](juce::AudioProcessor& processor, const juce::String& moduleType) {
        graphEditor.applyDualIODefaultTo(processor, moduleType);
    };
}

void MainComponent::addAudioTrack() {
    const int index = (int)timelineDoc.getTracks().size();
    juce::String trackName; // set inside the mutation; read afterwards for the status message

    // "+ Track -> Audio Track" creates a WHOLE mixer channel in ONE undo step —
    // Track Audio -> Gate (bypassed) -> Parametric EQ (bypassed) -> Compressor (bypassed) -> Channel
    // Strip (Stereo) -> Master (Mix), with {Track Audio, Gate, EQ, Compressor, Strip} boxed into one
    // collapsed macro named after the track, plus the track/binding/colour exactly like
    // addMidiTrack()'s single compound step. A single Cmd+Z removes every bit of it.
    //
    // Master stays OUTSIDE the macro. Core (buildDefaultAudioChannel) builds the Strip -> Master Mix cables as two
    // PLAIN edges, because it cannot create macro ports; routeChannelOutputThroughMacroPort then moves them behind
    // the macro's output port (one stereo jack, or two with the Split Left/Right jacks preference), so the track's
    // sound visibly leaves through its own card. Master's Mix/Direct classification looks backward through macro
    // ports (synth::resolveSourceThroughPorts), which is what keeps a strip behind an outlet on Mix.
    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [this, index, &trackName] {
            // Consult the per-type default BEFORE any node is created. Defaults only steer this plain "+ Track ->
            // Audio" gesture — a saved preset is always reachable regardless via the grouped list / file insert
            // (TimelinePanelTrackHeaders.cpp), independent of what's set here
            // (see docs/mixer/track-presets.md#saving-and-setting-a-default).
            const juce::String defaultPresetName =
                appProperties.getUserSettings()->getValue(kMixerDefaultTrackPresetAudioKey, {});
            if (defaultPresetName.isNotEmpty()) {
                auto preset = synth::TrackPresetManager::loadTrackPreset(
                    synth::TrackPresetManager::getDefaultTrackPresetsDirectory(), defaultPresetName);
                if (preset.isObject()) {
                    trackName = insertTrackFromPresetVar(preset, synth::TrackPresetKind::Audio, "Audio");
                    if (trackName.isNotEmpty())
                        return; // inserted from the default preset - skip the factory chain below
                }
                // else: the name resolves to nothing (deleted file) -> fall through to the
                // unchanged factory path below, SILENTLY (docs/mixer/mixer.md gives no "tell the user
                // their default vanished" requirement for v1).
            }

            // Doc side FIRST, for the reason addMidiTrack() spells out: a node created before the
            // kMaxTracks refusal would be left orphaned in the graph.
            trackName = "Audio " + juce::String(index + 1);
            const auto trackId = timelineDoc.addTrack(synth::TrackKind::Audio, trackName);
            if (!trackId.isValid())
                return; // at kMaxTracks: nothing added, no node created, no macro

            // Unwired: buildDefaultAudioChannel below wires it into the chain instead of
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
            // {Track Audio, Gate, EQ, Compressor, Strip} end up boxed into one collapsed macro below,
            // so only their expanded-state positions matter for not overlapping each other; Master's
            // position is only used the first time (it is a singleton afterwards) and sits right of
            // where the collapsed macro card will be.
            const int gateX =
                trackAudioPosition.x + GraphEditor::estimateModuleSize("Track Audio").x + kChannelCardGapX;
            const int eqX = gateX + GraphEditor::estimateModuleSize("Gate").x + kChannelCardGapX;
            const int compressorX = eqX + GraphEditor::estimateModuleSize("Parametric EQ").x + kChannelCardGapX;
            const int stripX = compressorX + GraphEditor::estimateModuleSize("Compressor").x + kChannelCardGapX;
            // Master clears the channel macro's open hull (right strip reaches kMacroHullSideOutset past the Strip).
            const int masterX = stripX + GraphEditor::estimateModuleSize("Channel Strip").x +
                                synth::LayoutUtil::kMacroHullSideOutset + kChannelCardGapX;
            const synth::DefaultChannelLayout layout{
                /*gate=*/{gateX, trackAudioPosition.y},
                /*eq=*/{eqX, trackAudioPosition.y},
                /*compressor=*/{compressorX, trackAudioPosition.y},
                /*strip=*/{stripX, trackAudioPosition.y},
                /*master=*/{masterX, trackAudioPosition.y},
                /*onNewModule=*/newModuleHook(),
            };

            const auto channel = synth::buildDefaultAudioChannel(audioEngine.getGraph(), *trackAudioNode, layout);
            if (channel.stripUuid.isEmpty())
                return; // a factory/addNode failure partway — see buildDefaultAudioChannel's contract

            // Master (and Audio Output) sit in the output dock, derived to the right of everything by
            // GraphEditor::reflowOutputDock -- run by the updateComponents() / addMacroForMembers below.

            // Box {Track Audio, Gate, EQ, Compressor, Strip} into ONE collapsed macro named after
            // the track. Master is deliberately NOT a member — see this method's own comment above — and the
            // strip's Master-bound edges move behind the macro's output port right after.
            graphEditor.getMacroController().addMacroForMembers(
                {trackAudioUuid, channel.gateUuid, channel.eqUuid, channel.compressorUuid, channel.stripUuid},
                trackName, trackAudioPosition);
            graphEditor.routeChannelOutputThroughMacroPort(channel.stripUuid);

            // Inside the mutation, not after: MacroSet::retainOnly() (run by updateComponents())
            // must see every node above still alive to keep the macro's membership.
            graphEditor.updateComponents();
        });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed ? "Added " + trackName : "Could not add a track");
}

void MainComponent::addInstrumentTrack(const juce::String& instrumentModuleType, bool poly) {
    // "+ Track -> Instrument -> {Oscillator/Wavetable/Sampler}" builds Track In
    // -> instrument -> default chain (Gate bypassed -> Parametric EQ bypassed -> Compressor bypassed
    // -> Channel Strip Stereo -> Master Mix) in ONE undo step, the MIDI-track mirror of
    // addAudioTrack()'s step. {Track In, instrument, Gate, EQ, Compressor, Strip} are boxed
    // into one collapsed macro named after the track; Master stays outside it (a shared
    // singleton) and the strip's Master edges move behind the macro's output port, as in addAudioTrack(). A single
    // Cmd+Z removes every bit of it. See buildInstrumentTrackAndChain's own comment for the shared tail this delegates
    // to (also used by addInstrumentPluginTrack).
    //
    // This is a TrackKind::Midi track — a Track In feeding exactly one instrument is already what
    // addMidiTrack() produces once a cable is drawn by hand; this flow just draws that cable and
    // builds the channel automatically; no new TrackKind is needed
    // (docs/mixer/mixer.md#channels-follow-audio-not-tracks), and adding one would be a format/serialization change
    // nothing here requires.
    buildInstrumentTrackAndChain(synth::AIStateMapper::createModule(instrumentModuleType), instrumentModuleType, poly);
}

// The shared tail of addInstrumentTrack() and addInstrumentPluginTrack() — Track In
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
// poly concept). `trackNamePrefix` becomes "<prefix> <N>", same numbering as before, except a hosted plugin's track is
// named after the plugin and numbered only when that name is taken ("Diva", "Diva 2"). The build itself is
// buildInstrumentTrackBody, shared with the timelineOps host.
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
    // A hosted plugin's track is named after the plugin ("Diva", then "Diva 2"); every other instrument keeps
    // "<prefix> <N>".
    const bool namedAfterPlugin = dynamic_cast<synth::HostedPluginModule*>(instrumentProcessor.get()) != nullptr;
    juce::String trackName; // set inside the mutation; read afterwards for the status message
    // shared_ptr so the mutate lambda (which recordGraphTimelineAndMacroChange takes by const-ref
    // and must remain copyable as std::function) can carry a move-only juce::AudioProcessor —
    // the same idiom GraphEditor::addModuleAtCanvasPosition's own recordStructuralChange call uses.
    auto stagedInstrument = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(instrumentProcessor));

    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(),
        [this, index, &trackName, trackNamePrefix, poly, stagedInstrument, namedAfterPlugin] {
            // Same default-consulting branch as addAudioTrack's own, before any node is created
            // (including `stagedInstrument`, which is simply discarded unused when a default wins — never
            // added to the graph) (see docs/mixer/track-presets.md#saving-and-setting-a-default).
            const juce::String defaultPresetName =
                appProperties.getUserSettings()->getValue(kMixerDefaultTrackPresetInstrumentKey, {});
            if (defaultPresetName.isNotEmpty()) {
                auto preset = synth::TrackPresetManager::loadTrackPreset(
                    synth::TrackPresetManager::getDefaultTrackPresetsDirectory(), defaultPresetName);
                if (preset.isObject()) {
                    trackName = insertTrackFromPresetVar(preset, synth::TrackPresetKind::Instrument, trackNamePrefix);
                    if (trackName.isNotEmpty())
                        return; // inserted from the default preset - skip the whole build below
                }
            }

            trackName = namedAfterPlugin ? uniqueTrackName(timelineDoc, trackNamePrefix)
                                         : trackNamePrefix + " " + juce::String(index + 1);
            buildInstrumentTrackBody(stagedInstrument, trackName, poly, /*inserts=*/{}, /*envelopeParams=*/{});
        });

    if (namedAfterPlugin)
        audioEngine.updateModuleNames(); // the new module's "Diva 2" card title, immediately
    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed ? "Added " + trackName : "Could not add a track");
}

// The transaction body of an instrument-track build, shared by the "+ Track -> Instrument" menu
// (buildInstrumentTrackAndChain, inside its own transaction, after its default-preset lookup) and
// the timelineOps host (inside TimelineOpsHost::recordBatch, never consulting a preset): Track In
// -> instrument -> envelope stage -> `inserts` -> channel -> Master, boxed into one macro named
// `trackName`. Opens no transaction. On a failure partway (kMaxTracks, or a factory/addNode
// failure) it removes every node it added and the doc track before returning nullopt -- the host
// contract, and it keeps the menu flow from leaving a half-built chain behind too. Edges it
// already moved on pre-existing nodes (a reused Master) are not rewound; the enclosing
// transaction's snapshot still covers them for undo. On success it hands back the uuids it minted
// for the Track In, the instrument, each insert (in op order) and the envelope's ADSR (empty for a
// Sampler), for in-response references.
std::optional<synth::InstrumentTrackBuildResult> MainComponent::buildInstrumentTrackBody(
    std::shared_ptr<std::unique_ptr<juce::AudioProcessor>> stagedInstrument, const juce::String& trackName, bool poly,
    const std::vector<synth::InstrumentTrackInsert>& inserts, const juce::var& envelopeParams) {
    auto& graph = audioEngine.getGraph();
    const int index = (int)timelineDoc.getTracks().size();
    std::set<juce::AudioProcessorGraph::NodeID> nodesBefore;
    for (auto* node : graph.getNodes())
        nodesBefore.insert(node->nodeID);

    InstrumentChainBuild build;
    bool built = createTrackInForInstrumentChain(trackName, build) &&
                 adoptInstrumentNodeForChain(stagedInstrument, index, poly, build);
    if (built) {
        buildInstrumentEnvelopeChain(build);
        applyInstrumentEnvelopeParams(envelopeParams, build);
        built = buildInstrumentInserts(inserts, build) && buildInstrumentChannelAndMacro(trackName, build);
    }

    if (!built) {
        std::vector<juce::AudioProcessorGraph::NodeID> added;
        for (auto* node : graph.getNodes())
            if (nodesBefore.count(node->nodeID) == 0)
                added.push_back(node->nodeID);
        for (const auto id : added)
            graph.removeNode(id); // drops its connections too
        if (build.trackId.isValid())
            timelineDoc.removeTrack(build.trackId);
        return std::nullopt;
    }
    // Inside the caller's mutation, not after: MacroSet::retainOnly() (run by updateComponents())
    // must see every node above still alive to keep the macro's membership.
    graphEditor.updateComponents();
    return synth::InstrumentTrackBuildResult{build.trackInUuid, build.instrumentUuid, build.insertUuids,
                                             build.adsrUuid};
}

// buildInstrumentTrackBody step 3a: the op's "envelope" params onto the ADSR step 3 built (poly or
// mono -- both record adsrUuid). Through the patch path's untrusted apply, like an insert's params
// (they were checked by AIStateMapper::validateNodeParams at validation).
void MainComponent::applyInstrumentEnvelopeParams(const juce::var& envelopeParams, const InstrumentChainBuild& build) {
    auto* paramsObj = envelopeParams.getDynamicObject();
    if (paramsObj == nullptr || build.adsrUuid.isEmpty())
        return;
    for (auto* node : audioEngine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == build.adsrUuid) {
            synth::AIStateMapper::applyUntrustedParams(node->getProcessor(), paramsObj);
            return;
        }
}

// buildInstrumentTrackBody step 3b: each effect insert a timelineOps op asked for, in order, after
// the envelope stage and before the channel's Gate. Created through the factory, the jacks
// preference applied like the instrument's, then the op's params through the patch path's own
// untrusted apply (they were checked by AIStateMapper::validateNodeParams at validation). Wires
// the previous stage's L (ch0) and R (`sourceRightChannel`) into the insert's ch0 and its own right
// leg; a mono insert takes L only and feeds both legs onward from ch0. False on a factory/addNode
// failure (the caller cleans up).
bool MainComponent::buildInstrumentInserts(const std::vector<synth::InstrumentTrackInsert>& inserts,
                                           InstrumentChainBuild& build) {
    auto& graph = audioEngine.getGraph();
    for (const auto& insert : inserts) {
        auto processor = synth::AIStateMapper::createModule(insert.type);
        if (processor == nullptr)
            return false;
        newModuleHook()(*processor, insert.type);
        if (auto* paramsObj = insert.params.getDynamicObject())
            synth::AIStateMapper::applyUntrustedParams(processor.get(), paramsObj);

        auto nodePtr = graph.addNode(std::move(processor));
        if (nodePtr == nullptr)
            return false;
        auto* node = nodePtr.get();
        const juce::String uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module != nullptr)
            module->setNodeUuid(uuid);
        const juce::Point<int> position{build.chainSourcePosition.x +
                                            GraphEditor::estimateModuleSize(build.chainSourceType).x + kChannelCardGapX,
                                        build.chainSourcePosition.y};
        node->properties.set("x", position.x);
        node->properties.set("y", position.y);

        const int inputRight = module != nullptr ? module->rightAudioLegChannel() : 1;
        graph.addConnection({{build.chainSource->nodeID, 0}, {node->nodeID, 0}});
        if (inputRight >= 0)
            graph.addConnection({{build.chainSource->nodeID, build.sourceRightChannel}, {node->nodeID, inputRight}});

        build.insertUuids.push_back(uuid);
        build.chainSource = node;
        build.sourceRightChannel = inputRight >= 0 ? inputRight : 0;
        build.chainSourceType = insert.type;
        build.chainSourcePosition = position;
    }
    return true;
}

// buildInstrumentTrackBody step 1/4: the track + its Track In node — through the factory like
// createTrackInNode() — inlined rather than reused because that method auto-wires to "the sole
// existing instrument" and calls updateComponents() itself, neither of which fits this compound
// build (this wires the instrument THIS call creates, and updateComponents() runs once at the
// end). Returns false exactly where the original inline body would have returned (at kMaxTracks,
// or a factory/addNode failure); buildInstrumentTrackBody removes whatever was created.
bool MainComponent::createTrackInForInstrumentChain(const juce::String& trackName, InstrumentChainBuild& build) {
    build.trackId = timelineDoc.addTrack(synth::TrackKind::Midi, trackName);
    if (!build.trackId.isValid())
        return false; // at kMaxTracks: nothing added, no node created, no macro

    auto& graph = audioEngine.getGraph();
    auto trackInProcessor = synth::AIStateMapper::createModule("Track In");
    if (trackInProcessor == nullptr)
        return false;
    auto trackInNodePtr = graph.addNode(std::move(trackInProcessor));
    if (trackInNodePtr == nullptr)
        return false;
    build.trackInNode = trackInNodePtr.get();
    build.trackInUuid = juce::Uuid().toDashedString();
    build.trackInNode->properties.set("uuid", build.trackInUuid);
    if (auto* module = dynamic_cast<ModuleBase*>(build.trackInNode->getProcessor()))
        module->setNodeUuid(build.trackInUuid);
    build.trackInSize = GraphEditor::estimateModuleSize("Track In");
    build.trackInPosition = graphEditor.findLeftEdgeSlotBelowModules(build.trackInSize.x, build.trackInSize.y);
    build.trackInNode->properties.set("x", build.trackInPosition.x);
    build.trackInNode->properties.set("y", build.trackInPosition.y);
    return true;
}

// buildInstrumentTrackBody step 2/4: adopts the caller's already-created (and, for a hosted
// plugin, already-loaded) instrument processor into the live graph HERE, inside this undo
// transaction, so Cmd+Z removes it along with everything else; wires Track In -> instrument MIDI;
// and seeds `chainSource`/`sourceRightChannel`/`chainSourceType`/`chainSourcePosition` at "the
// instrument itself", which step 3 (buildInstrumentEnvelopeChain) may advance past. Returns false
// exactly where the original inline body would have returned (staged instrument already moved, or
// an addNode failure).
bool MainComponent::adoptInstrumentNodeForChain(std::shared_ptr<std::unique_ptr<juce::AudioProcessor>> stagedInstrument,
                                                int index, bool poly, InstrumentChainBuild& build) {
    if (!*stagedInstrument)
        return false; // moved exactly once above; defensive, mirrors addModuleAtCanvasPosition's own guard
    auto& graph = audioEngine.getGraph();
    const int instrumentX = build.trackInPosition.x + build.trackInSize.x + kChannelCardGapX;
    // The instrument is user-facing, so it follows the left/right jacks preference like a library
    // drop does (a hosted plugin has no dualIO parameter, making this a no-op there). Before addNode
    // and before any wiring below reads the module's right-leg channel.
    newModuleHook()(**stagedInstrument, (*stagedInstrument)->getName());
    auto instrumentNodePtr = graph.addNode(std::move(*stagedInstrument));
    if (instrumentNodePtr == nullptr)
        return false;
    build.instrumentNode = instrumentNodePtr.get();
    // The processor's OWN name, not the caller's `trackNamePrefix` — for a hosted plugin the
    // latter is the plugin's display name (e.g. "Serum"), never "Hosted Plugin", so every
    // branch below that keys off the factory type name (isOscOrWavetable, estimateModuleSize)
    // must read it from the node it actually got.
    build.instrumentModuleType = build.instrumentNode->getProcessor()->getName();
    build.instrumentUuid = juce::Uuid().toDashedString();
    build.instrumentNode->properties.set("uuid", build.instrumentUuid);
    if (auto* module = dynamic_cast<ModuleBase*>(build.instrumentNode->getProcessor()))
        module->setNodeUuid(build.instrumentUuid);
    build.instrumentNode->properties.set("x", instrumentX);
    build.instrumentNode->properties.set("y", build.trackInPosition.y);

    if (poly)
        synth::setProcessorPoly(build.instrumentNode->getProcessor(), true);

    timelineDoc.setTrackBinding(build.trackId, build.trackInUuid);
    timelineDoc.setTrackColour(build.trackId, synth::ui::trackPaletteColour(index).getARGB());

    // Track In -> instrument, MIDI. Unambiguous by construction: this instrument was just
    // created for this track alone.
    graph.addConnection({{build.trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {build.instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    build.chainSource = build.instrumentNode;
    build.sourceRightChannel = 1;
    if (auto* instrumentModule = dynamic_cast<ModuleBase*>(build.instrumentNode->getProcessor()))
        build.sourceRightChannel = instrumentModule->rightAudioLegChannel();
    build.chainSourceType = build.instrumentModuleType;
    build.chainSourcePosition =
        juce::Point<int>(static_cast<int>(build.chainSource->properties.getWithDefault("x", 0)),
                         static_cast<int>(build.chainSource->properties.getWithDefault("y", 0)));
    return true;
}

// buildInstrumentTrackBody step 3/4: the optional envelope stage(s) ahead of the channel
// build, advancing `chainSource`/`sourceRightChannel`/`chainSourceType`/`chainSourcePosition` past
// whatever got inserted. Never fails outright — an insertion helper returning null just leaves the
// chain source where step 2 left it.
void MainComponent::buildInstrumentEnvelopeChain(InstrumentChainBuild& build) {
    auto& graph = audioEngine.getGraph();
    const bool isOscOrWavetable =
        build.instrumentModuleType == "Oscillator" || build.instrumentModuleType == "Wavetable";
    const bool instrumentIsPoly = synth::isProcessorPoly(build.instrumentNode->getProcessor());
    const int instrumentX = static_cast<int>(build.instrumentNode->properties.getWithDefault("x", 0));

    if (isOscOrWavetable && instrumentIsPoly) {
        // A poly Oscillator/Wavetable gets a TRUE per-voice envelope — Poly
        // MIDI + poly ADSR + poly VCA, replacing both the Voice Mixer stage below and
        // addEnvelopeAndVCAForRawInstrument's forced-mono ADSR/VCA (see
        // addPolyEnvelopeAndVCAForInstrument's own comment for why the non-poly path can't
        // just be made poly in place). No Voice Mixer is inserted: the poly VCA does its own
        // 8-voice summing.
        const int polyMidiX =
            instrumentX + GraphEditor::estimateModuleSize(build.instrumentModuleType).x + kChannelCardGapX;
        const int adsrX = polyMidiX + GraphEditor::estimateModuleSize("Poly MIDI").x + kChannelCardGapX;
        const int vcaX = adsrX + GraphEditor::estimateModuleSize("ADSR").x + kChannelCardGapX;
        const auto polyEnv = synth::addPolyEnvelopeAndVCAForInstrument(
            graph, *build.trackInNode, *build.instrumentNode, {polyMidiX, build.trackInPosition.y},
            {adsrX, build.trackInPosition.y}, {vcaX, build.trackInPosition.y}, newModuleHook());
        if (polyEnv.vca != nullptr) {
            build.polyMidiUuid = polyEnv.polyMidiUuid;
            build.adsrUuid = polyEnv.adsrUuid;
            build.vcaUuid = polyEnv.vcaUuid;
            build.chainSource = polyEnv.vca;
            // The legacy ch0/ch1 duplicate (VCAModule.h) — the instrument's R-octet is
            // deliberately not wired into the VCA's real Audio R poly block, same known
            // limitation addVoiceMixerForPolyInstrument's own comment documents.
            build.sourceRightChannel = 1;
            build.chainSourceType = "VCA";
            build.chainSourcePosition = {vcaX, build.trackInPosition.y};
        }
        return;
    }

    // A poly instrument's L-octet needs summing before the strip (see
    // addVoiceMixerForPolyInstrument's own comment) — a no-op for today's factory-default
    // instruments (poly defaults off) and for the poly Oscillator/Wavetable case handled
    // above, kept general for a poly Sampler or any other poly instrument.
    const int voiceMixerX =
        instrumentX + GraphEditor::estimateModuleSize(build.instrumentModuleType).x + kChannelCardGapX;
    auto* voiceMixerNode = synth::addVoiceMixerForPolyInstrument(
        graph, *build.instrumentNode, {voiceMixerX, build.trackInPosition.y}, build.voiceMixerUuid);

    if (voiceMixerNode != nullptr) {
        build.chainSource = voiceMixerNode;
        build.sourceRightChannel = 1;
        build.chainSourceType = "Voice Mixer";
        build.chainSourcePosition =
            juce::Point<int>(static_cast<int>(build.chainSource->properties.getWithDefault("x", 0)),
                             static_cast<int>(build.chainSource->properties.getWithDefault("y", 0)));
    }

    // Oscillator/Wavetable have no envelope of their own, so a held (or
    // even released) note drones forever. Insert an ADSR (gated by the same Track In
    // MIDI as the instrument) driving a VCA, ahead of the rest of the chain — AFTER any
    // Voice Mixer stage above, never before it (see addEnvelopeAndVCAForRawInstrument's
    // own comment for why). Sampler already has its own one-shot playback envelope and
    // is out of scope. Never reached for a poly Oscillator/Wavetable (handled above).
    if (isOscOrWavetable) {
        const int adsrX =
            build.chainSourcePosition.x + GraphEditor::estimateModuleSize(build.chainSourceType).x + kChannelCardGapX;
        const int vcaX = adsrX + GraphEditor::estimateModuleSize("ADSR").x + kChannelCardGapX;
        const auto envAndVca = synth::addEnvelopeAndVCAForRawInstrument(
            graph, *build.trackInNode, *build.chainSource, build.sourceRightChannel,
            {adsrX, build.chainSourcePosition.y}, {vcaX, build.chainSourcePosition.y}, newModuleHook());
        if (envAndVca.vca != nullptr) {
            build.adsrUuid = envAndVca.adsrUuid;
            build.vcaUuid = envAndVca.vcaUuid;
            build.chainSource = envAndVca.vca;
            build.sourceRightChannel = VCAModule::kRightBase;
            build.chainSourceType = "VCA";
            build.chainSourcePosition = {vcaX, build.chainSourcePosition.y};
        }
    }
}

// buildInstrumentTrackBody step 4/4: the default Gate/EQ/Compressor/Strip channel off
// `chainSource`, boxed with everything built above into one collapsed macro named after the
// track. Master is deliberately NOT a macro member (a shared singleton); the
// strip's Master-bound edges move behind the macro's output port, as in addAudioTrack(). Returns false exactly where
// the original inline body would have returned (a factory/addNode failure partway through the channel build).
bool MainComponent::buildInstrumentChannelAndMacro(const juce::String& trackName, InstrumentChainBuild& build) {
    // Lay every card of the expanded chain out left-to-right from the real card widths, the
    // same reason addAudioTrack's own comment gives (Parametric EQ is double-width).
    const int gateX =
        build.chainSourcePosition.x + GraphEditor::estimateModuleSize(build.chainSourceType).x + kChannelCardGapX;
    const int eqX = gateX + GraphEditor::estimateModuleSize("Gate").x + kChannelCardGapX;
    const int compressorX = eqX + GraphEditor::estimateModuleSize("Parametric EQ").x + kChannelCardGapX;
    const int stripX = compressorX + GraphEditor::estimateModuleSize("Compressor").x + kChannelCardGapX;
    // Master clears the channel macro's open hull (right strip reaches kMacroHullSideOutset past the Strip).
    const int masterX = stripX + GraphEditor::estimateModuleSize("Channel Strip").x +
                        synth::LayoutUtil::kMacroHullSideOutset + kChannelCardGapX;
    const synth::DefaultChannelLayout layout{
        /*gate=*/{gateX, build.chainSourcePosition.y},
        /*eq=*/{eqX, build.chainSourcePosition.y},
        /*compressor=*/{compressorX, build.chainSourcePosition.y},
        /*strip=*/{stripX, build.chainSourcePosition.y},
        /*master=*/{masterX, build.chainSourcePosition.y},
        /*onNewModule=*/newModuleHook(),
    };

    const auto channel =
        synth::buildDefaultAudioChannel(audioEngine.getGraph(), *build.chainSource, layout, build.sourceRightChannel);
    if (channel.stripUuid.isEmpty())
        return false;

    // Box {Track In, instrument, [Voice Mixer if poly], [Poly MIDI if poly Oscillator/
    // Wavetable], [ADSR+VCA if Oscillator/Wavetable], [timelineOps inserts], Gate, EQ, Compressor, Strip} into ONE
    // collapsed macro named after the track. Master is deliberately NOT a member (a shared
    // singleton); the strip's Master edges then move behind the macro's output port, as in addAudioTrack().
    std::vector<juce::String> macroMembers{build.trackInUuid, build.instrumentUuid};
    if (!build.voiceMixerUuid.isEmpty())
        macroMembers.push_back(build.voiceMixerUuid);
    if (!build.polyMidiUuid.isEmpty())
        macroMembers.push_back(build.polyMidiUuid);
    if (!build.adsrUuid.isEmpty())
        macroMembers.push_back(build.adsrUuid);
    if (!build.vcaUuid.isEmpty())
        macroMembers.push_back(build.vcaUuid);
    macroMembers.insert(macroMembers.end(), build.insertUuids.begin(), build.insertUuids.end());
    macroMembers.push_back(channel.gateUuid);
    macroMembers.push_back(channel.eqUuid);
    macroMembers.push_back(channel.compressorUuid);
    macroMembers.push_back(channel.stripUuid);
    // The chain wires every module's Left AND Right leg, but a module built collapsed (the default
    // preference) has no Right jack. Left as built, the Oscillator -> VCA and VCA -> Gate Right cables
    // are invisible and cannot be unplugged: unplugging the one cable the user can see leaves the sound
    // playing through the hidden one. Same cleanup the Dual I/O toggle runs when a module collapses.
    // Only the voice-chain modules: the channel (Gate/EQ/Compressor/Strip) keeps its own stereo wiring.
    std::vector<juce::String> voiceChain{build.instrumentUuid, build.voiceMixerUuid, build.adsrUuid, build.vcaUuid};
    voiceChain.insert(voiceChain.end(), build.insertUuids.begin(), build.insertUuids.end());
    for (const auto& uuid : voiceChain)
        if (uuid.isNotEmpty())
            if (auto* node = findNodeByUuid(uuid))
                graphEditor.dropHiddenRightLegConnections(node->nodeID);

    graphEditor.getMacroController().addMacroForMembers(macroMembers, trackName, build.trackInPosition);
    graphEditor.routeChannelOutputThroughMacroPort(channel.stripUuid);
    return true;
}

// Every track whose bound node's chain still reaches the output without passing through a ChannelStripModule — the
// same synth::findUnchanneledOutputFeeds query the connect-triggered auto-channel already runs, just from each
// track's own binding instead of a just-completed cable drag. An unbound or orphaned track (empty/unresolved
// bindingUuid) contributes nothing — there is no chain to channel
// (see docs/mixer/mixer.md#creating-channels-in-an-existing-project).
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

// The "+ Track" menu's "Create Channels" action for existing projects —
// docs/mixer/mixer.md#creating-channels-in-an-existing-project says the project itself opens unchanged (no automatic
// migration on load; this method is never called from anywhere but the explicit menu choice below). Gathers every
// track's own bound node first (outside the undo transaction — this is a pure read), then wraps
// GraphEditor::createChannelsForUnchanneledTracks() in ONE recordGraphTimelineAndMacroChange transaction, the same
// shape addAudioTrack/addInstrumentTrack use, so N channel-less tracks becoming N new channels is a SINGLE Cmd+Z. A
// track already reaching a ChannelStripModule is silently skipped inside that call (see its own comment) — nothing
// here needs to pre-filter beyond "has a live binding at all"
// (see docs/mixer/mixer.md#creating-channels-in-an-existing-project).
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

// "Make channel" on one track's bound node — the header menu's enabled state is the same
// synth::planMakeChannel query the action itself runs
// (see docs/mixer/mixer.md#make-channel-and-shared-modules).
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
// The ONE undo transaction (graph + timeline + macros) + reconcile pass behind
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

// "Duplicate into Channel" — same one-transaction + reconcile shape as above.
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

// Instrument-capable hosted plugins for the "+ Track -> Instrument -> Plugin"
// submenu — see TrackHeaderHost::getInstrumentPluginOptions's own comment. getKnownPlugins() (full
// juce::PluginDescription, which carries isInstrument) is read here rather than
// getKnownPluginIdentities() (PluginIdentity alone, no isInstrument) precisely because the filter
// needs that field.
// The Instrument submenu's "Plugin" entries.
std::vector<synth::PluginIdentity> MainComponent::getInstrumentPluginOptions() const {
    std::vector<synth::PluginIdentity> options;
    for (const auto& description : getPluginScanService().getKnownPlugins()) {
        if (!description.isInstrument)
            continue;
        // Never offer to host this app's OWN VST3/AU build as an instrument —
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
    // Captured NOW, against the document this load was started for. New Patch/
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
