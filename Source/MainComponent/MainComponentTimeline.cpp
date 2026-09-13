// MainComponentTimeline.cpp — timeline reconciliation after a graph change, MIDI/audio
// recording commit (including take-file placement), new-patch/clear-timeline, Track In/Track
// Audio node creation, asset relinking, and parameter automation lane creation.
// MainComponent is declared in MainComponent.h; the rest of its implementation lives in the
// sibling MainComponent*.cpp units next to this one.
#include "MainComponent.h"
#include "MainComponentInternal.h"

#include "AI/AIStateMapper.h"
#include "Branding.h"
#include "Modules/TimelineAudioSourceModule.h"
#include "ProjectBundle.h"
#include "Timeline/AssetManager.h"
#include "Timeline/AutomationBinding.h"
#include "Timeline/TakePlacement.h"
#include "Timeline/TimelineReconciler.h"
#include <algorithm>
#include <map>

namespace {

// Upper bound on the take-number search. A folder with 10000 takes in it is a bug report, not a
// session, and an unbounded loop on a stat() call is not something a UI click should be able to do.
constexpr int kMaxTakeNumber = 10000;
// Floor on a committed audio clip's length, so a take stopped the instant it started still produces
// a clip the model accepts (addClip requires a strictly positive length). Same value and same
// reasoning as synth::MidiRecorder::kMinNoteLengthBeats.
constexpr double kMinAudioClipLengthBeats = 1.0 / 32.0;

} // namespace

void MainComponent::timelineChanged(const synth::TimelineDoc&) { publishTimelineAndRebindRecorder(); }

void MainComponent::publishTimelineAndRebindRecorder() {
    audioEngine.publishTimeline(timelineDoc);

    // The recorder's binding table is rebuilt with the SAME resolution AudioEngine::publishTimeline
    // runs for the applier's table — uuid -> node (built once, not per lane), then paramId ->
    // parameter — because a lane the applier can play back is exactly a lane the recorder must be
    // able to capture into. Anything unresolvable is simply left unbound (an orphaned lane is
    // retained in the doc, it just automates nothing).
    automationRecorder.unbindAll();

    std::map<juce::String, juce::AudioProcessorGraph::Node*> nodesByUuid;
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            nodesByUuid.emplace(uuid, node);
    }

    for (const auto& track : timelineDoc.getTracks()) {
        for (const auto& lane : track.lanes) {
            if (lane.nodeUuid.isEmpty() || lane.paramId.isEmpty())
                continue;
            const auto found = nodesByUuid.find(lane.nodeUuid);
            if (found == nodesByUuid.end())
                continue;
            // The shared resolver, exactly like the applier's own binding build — a lane the
            // audio thread can play back is exactly a lane the recorder must be able to capture into.
            const auto resolved =
                synth::resolveLaneParameter(found->second->getProcessor(), lane.paramId, lane.paramIndexHint);
            if (auto* param = resolved.liveParameter())
                automationRecorder.bindLane(lane.id, param, found->second);
        }
    }
}

void MainComponent::reconcileTimelineAfterGraphChange() {
    // reconcileBindings routes through the doc's single mutation choke point when (and only when) a
    // flag actually flips, which fires timelineChanged and therefore publishes. Publishing again
    // here would be a wasted snapshot build, so this only publishes for the "nothing flipped" case
    // — where the graph still changed under us and the recorder's bindings must be re-resolved.
    if (!synth::TimelineReconciler::reconcile(timelineDoc, audioEngine.getGraph()))
        publishTimelineAndRebindRecorder();
}

void MainComponent::reconcileTimelineBindingsOnly() {
    synth::TimelineReconciler::reconcile(timelineDoc, audioEngine.getGraph());
}

void MainComponent::commitMidiRecording() {
    // stopAndCommit()'s own return just says whether a clip was created (an empty take commits
    // nothing) — not something either caller (the transport bar's Record-off click, and the 10 Hz
    // poll's auto-commit-on-stop) needs to react to differently, so it is deliberately ignored here.
    midiRecorder.stopAndCommit(timelineDoc, undoManager);
    if (midiRecorder.hadOverrun())
        statusBar.showMessage("Dropped MIDI events during recording");
    timelinePanel.getTransportBar().setRecordingState(false);
    // Every stop (explicit or auto-committed) ends any in-flight count-in pre-roll —
    // unconditional and idempotent, so a take that was never in a pre-roll to begin with just
    // clears an already-false flag.
    audioEngine.getMetronome().setForcedOn(false);
}

// ---- Audio recording ----

juce::AudioProcessorGraph::Node* MainComponent::ensureMasterRecordTap() {
    auto& graph = audioEngine.getGraph();

    // Already spliced in? THE master tap is a singleton by construction — this function is the only
    // thing that ever creates one — so the first Rec Tap found is it.
    for (auto* node : graph.getNodes())
        if (node != nullptr && dynamic_cast<RecordTapModule*>(node->getProcessor()) != nullptr)
            return node;

    // The node the tap goes in FRONT of. Still a bare juce::AudioGraphIOProcessor (unlike Audio
    // Input), so it is identified by name exactly like every other lookup in the app.
    juce::AudioProcessorGraph::Node* outputNode = nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == "Audio Output")
            outputNode = node;
    if (outputNode == nullptr)
        return nullptr; // nothing to record: there is no master bus

    juce::AudioProcessorGraph::Node* created = nullptr;
    // ONE compound undo step for the node, its position, and the whole re-splice. recordCombinedChange
    // pushes only the domain(s) that actually changed, so this is a single graph SnapshotAction —
    // the timeline is untouched here (the clip is a separate step, committed when the take ends).
    undoManager.recordCombinedChange(graph, timelineDoc, [&] {
        // Through the factory, not constructed ad hoc: that is what makes the node round-trip
        // through graphToJSON/applyJSONToGraph, which is how undo, redo and .agsproj save all
        // reproduce it. Same reasoning as createTrackInNode().
        auto processor = synth::AIStateMapper::createModule("Rec Tap");
        if (processor == nullptr)
            return;
        auto node = graph.addNode(std::move(processor));
        if (node == nullptr)
            return;
        created = node.get();

        // Ensure-uuid, mirrored into the processor in the same breath — the pairing every uuid
        // writer site keeps (see ModuleBase::setNodeUuid).
        const juce::String uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);

        const auto size = GraphEditor::estimateModuleSize("Rec Tap");
        const auto position = graphEditor.findLeftEdgeSlotBelowModules(size.x, size.y);
        node->properties.set("x", position.x);
        node->properties.set("y", position.y);

        // THE SPLICE. Everything that fed the output's audio channels now feeds the tap's matching
        // input, and the tap's outputs feed the output. Collected first and mutated afterwards
        // because removeConnection invalidates the list we would otherwise be iterating.
        //
        // MIDI connections into the output are left alone (a tap carries audio, not MIDI), and so
        // is anything on a channel the tap does not have — re-routing an 8-channel master through a
        // stereo tap would silently drop six channels.
        std::vector<juce::AudioProcessorGraph::Connection> intoOutput;
        for (const auto& connection : graph.getConnections()) {
            if (connection.destination.nodeID != outputNode->nodeID)
                continue;
            const int channel = connection.destination.channelIndex;
            if (channel < 0 || channel >= RecordTapModule::kNumChannels)
                continue;
            intoOutput.push_back(connection);
        }
        for (const auto& connection : intoOutput) {
            graph.removeConnection(connection);
            graph.addConnection({connection.source, {node->nodeID, connection.destination.channelIndex}});
        }
        for (int channel = 0; channel < RecordTapModule::kNumChannels; ++channel)
            graph.addConnection({{node->nodeID, channel}, {outputNode->nodeID, channel}});
    });

    graphEditor.updateComponents();
    return created;
}

RecordTapModule* MainComponent::findMasterRecordTap() const {
    auto& graph = const_cast<MainComponent*>(this)->audioEngine.getGraph();
    if (auto* node = graph.getNodeForId(audioTake_.tapNode))
        if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor()))
            return tap;

    // The id no longer resolves — an undo/redo mid-take rebuilds the graph and renumbers nodes.
    // The tap is a singleton, so a scan still identifies it unambiguously.
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor()))
                return tap;
    return nullptr;
}

bool MainComponent::chooseTakeFiles(AudioTake& take) const {
    juce::File audioDir;
    juce::File peaksDir;
    juce::String refPrefix;

    if (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        audioDir = currentBundleDir_.getChildFile(synth::ProjectBundle::kAudioSubdirName);
        peaksDir = currentBundleDir_.getChildFile(synth::ProjectBundle::kPeaksSubdirName);
        refPrefix = juce::String(synth::ProjectBundle::kAudioSubdirName) + "/";
    } else {
        // An UNSAVED project has no bundle to write into, so takes land in app data and the clip's
        // assetRef carries the reserved "Recordings/" prefix, which resolves against
        // <app data>/<settings folder> rather than a bundle root. saveToFile() runs
        // synth::AssetManager::adoptRecordingsAssets BEFORE ProjectBundle::save, which
        // moves these into the bundle on the first save and rewrites the refs to "Audio/..." — a
        // project.json never carries a "Recordings/" ref. Until saved, the ref is still
        // bundle-RELATIVE in form (isValidAssetRef accepts it), which is what keeps the one path
        // rule — no absolute paths, ever — true for both cases.
        auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile(synth::branding::kSettingsFolderName)
                        .getChildFile(detail::kRecordingsFolderName);
        audioDir = root;
        peaksDir = root;
        refPrefix = juce::String(detail::kRecordingsFolderName) + "/";
    }

    if (!audioDir.exists() && !audioDir.createDirectory().wasOk())
        return false;
    if (!peaksDir.exists() && !peaksDir.createDirectory().wasOk())
        return false;

    // First free take number in whichever folder pair this is. Both files are checked so a take
    // never half-overwrites an earlier one.
    for (int n = 1; n <= kMaxTakeNumber; ++n) {
        const juce::String stem = "take-" + juce::String(n);
        const auto wav = audioDir.getChildFile(stem + ".wav");
        const auto peaks = peaksDir.getChildFile(stem + ".agpk");
        if (wav.exists() || peaks.exists())
            continue;
        take.wavFile = wav;
        take.peaksFile = peaks;
        take.assetRef = refPrefix + stem + ".wav";
        return true;
    }
    return false;
}

void MainComponent::commitAudioRecording() {
    if (!audioTake_.capturing)
        return;

    // Cleared FIRST: every path out of here means the take is over, and leaving the state set would
    // let the 10 Hz poll re-enter this on the next tick.
    const AudioTake take = audioTake_;
    audioTake_ = {};

    RecordTapModule::TakeResult result;
    if (auto* tap = findMasterRecordTap())
        result = tap->stopCapture();

    timelinePanel.getTransportBar().setRecordingState(false);
    // Every stop (explicit or auto-committed) ends any in-flight count-in pre-roll — unconditional
    // and idempotent, exactly like commitMidiRecording's own call.
    audioEngine.getMetronome().setForcedOn(false);

    // Nothing captured: record was disengaged during the count-in, or the tap never armed. No clip
    // and no undo step, mirroring MidiRecorder's "an empty take commits nothing" contract.
    if (!result.ok || result.lengthSamples <= 0)
        return;

    // Where the take lands. Samples -> beats through the transport's tempo. The rate,
    // bpm and round-trip latency used here are the ones FROZEN at record-on (take.captureSampleRate /
    // captureBpm / captureRecordingLatencySamples), not read live off the transport/engine — a
    // device/sample-rate change mid-take forces an early commit (see
    // AudioEngine::handleStreamFormatChange), and by the time that commit reaches here the engine may
    // already be on the NEW rate while every anchor field above (captureStartTimelineSample, the WAV
    // itself) is still in the OLD one. Reading live values would silently convert an OLD-rate sample
    // count with a NEW-rate samples-per-beat, which is wrong by exactly the rate ratio. The frozen
    // fields make this correct unconditionally: for the ordinary take (no rate change), they equal
    // the live values anyway. The whole of the arithmetic lives in synth::computeTakePlacement so it
    // can be asserted to the sample without going through this component. See
    // Source/Timeline/TakePlacement.h.
    synth::TakePlacementInput placementInput;
    placementInput.takeLengthSamples = result.lengthSamples;
    placementInput.captureStartValid = result.captureStartValid;
    placementInput.captureStartTimelineSample = result.captureStartTimelineSample;
    placementInput.captureStartBlockOffset = result.captureStartBlockOffset;
    placementInput.punchInBeat = take.punchInBeat;
    placementInput.recordingLatencySamples = take.captureRecordingLatencySamples;
    placementInput.sampleRate = take.captureSampleRate > 0.0 ? take.captureSampleRate : 44100.0;
    placementInput.bpm = take.captureBpm > 0.0 ? take.captureBpm : 120.0;
    placementInput.minClipLengthBeats = kMinAudioClipLengthBeats;

    const auto placement = synth::computeTakePlacement(placementInput);
    // Everything recorded sits before the punch (record disengaged during the count-in): the file
    // stays, the clip is not created — the same "an empty take commits nothing" rule as above.
    if (!placement.hasContent)
        return;

    // ONE undo step for the clip AND its asset binding: recordTimelineChange snapshots the doc
    // before and after the whole lambda, so the two mutations inside are a single entry.
    undoManager.recordTimelineChange(timelineDoc, [&] {
        const auto clip = timelineDoc.addClip(take.track, placement.clipStartBeat, placement.clipLengthBeats, "Take");
        if (!clip.isValid())
            return; // the track went away, or it is at kMaxClipsPerTrack: a no-op commit
        // The pre-roll (and the latency shift's overhang at timeline 0) is excluded by the WINDOW,
        // not by rewriting the WAV: the file keeps every frame that was captured.
        timelineDoc.setClipAsset(clip, take.assetRef, placement.sourceStartSeconds);
    });

    if (result.overran)
        statusBar.showMessage("Dropped audio during recording");
}

void MainComponent::clearTimelineForNewPatch() {
    if (timelineDoc.isEmpty())
        return; // clear() on an empty doc is a genuine no-op — no undo step for it either
    undoManager.recordTimelineChange(timelineDoc, [this] { timelineDoc.clear(); });
}

// The post-guard half of AppCommands::newPatch — see the command's own comment in perform() for
// why the guard has to run first. Everything below is unchanged from before the guard existed.
void MainComponent::newPatch() {
    ProgrammaticApplyScope guard(*this);
    // Two undo steps, deliberately: GraphEditor::newPatch() owns the graph's own
    // recordStructuralChange, and folding the timeline into it would mean either nesting
    // transactions or duplicating the clear. The timeline is cleared FIRST so the graph's step
    // is the newer one — Cmd+Z brings the canvas back, Cmd+Z again brings the timeline back,
    // and the post-restore reconcile re-derives the bindings after each.
    clearTimelineForNewPatch();
    graphEditor.newPatch();
    // A new document is not the old bundle, so the next take goes to app data rather
    // than into a bundle this patch no longer belongs to.
    currentBundleDir_ = juce::File();
    refreshAssetRoots(); // No bundle any more, so no bundle-relative ref resolves
    reconcileTimelineAfterGraphChange();
    markDocumentClean();
    setCurrentPatchName("Untitled");
    statusBar.showMessage("New patch");
    // T114/P8-10: the welcome screen's "New empty project" button reaches this via
    // AppCommands::newPatch's own guard — this line is what actually hides it, only once the guard
    // has let the action proceed (a Cancel answer never runs newPatch() at all).
    hideWelcomeScreen();
}

juce::AudioProcessorGraph::Node* MainComponent::findNodeByUuid(const juce::String& uuid) const {
    if (uuid.isEmpty())
        return nullptr;
    for (auto* node : audioEngine.getGraph().getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            return node;
    return nullptr;
}

juce::String MainComponent::createTrackInNode() {
    auto& graph = audioEngine.getGraph();

    // Through the factory, not constructed ad hoc: that is what makes the node round-trip through
    // graphToJSON/applyJSONToGraph, which is how undo, redo and .agsproj save all reproduce it.
    auto processor = synth::AIStateMapper::createModule("Track In");
    if (processor == nullptr)
        return {};

    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return {};

    // Ensure-uuid, mirrored into the processor in the same breath. The Track In module strcmps its
    // own uuid against the snapshot's bindingUuid on the AUDIO thread, so the node property and the
    // processor's copy must never diverge — the same pairing AIStateMapper keeps at each of its
    // three uuid writer sites (see ModuleBase::setNodeUuid).
    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);

    const auto size = GraphEditor::estimateModuleSize("Track In");
    const auto position = graphEditor.findLeftEdgeSlotBelowModules(size.x, size.y);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);

    // Auto-wire ONLY when the answer is unambiguous: exactly one MIDI instrument in the patch. With
    // none, or with two, no wire is drawn — a chip that reads "bound" over an unwired node is fine
    // (the cable is the user's to draw), whereas guessing wrong silently plays a track through the
    // wrong instrument.
    juce::AudioProcessorGraph::Node* target = nullptr;
    int candidates = 0;
    for (auto* other : graph.getNodes()) {
        if (other == nullptr || other == node.get() || !detail::isMidiInstrumentNode(other->getProcessor()))
            continue;
        ++candidates;
        target = other;
    }
    if (candidates == 1 && target != nullptr)
        graph.addConnection({{node->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                             {target->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    graphEditor.updateComponents();
    return uuid;
}

juce::String MainComponent::createTrackAudioNode(bool wireDirectlyToMasterBus) {
    auto& graph = audioEngine.getGraph();

    // Through the factory, for the same reason createTrackInNode() is: that is what makes the node
    // round-trip through graphToJSON/applyJSONToGraph, which is how undo, redo and .agsproj save all
    // reproduce it.
    auto processor = synth::AIStateMapper::createModule("Track Audio");
    if (processor == nullptr)
        return {};

    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return {};

    // Ensure-uuid, mirrored into the processor in the same breath. The Track Audio module strcmps
    // its own uuid against the snapshot's bindingUuid on the AUDIO thread, so the node property and
    // the processor's copy must never diverge (see ModuleBase::setNodeUuid).
    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);

    const auto size = GraphEditor::estimateModuleSize("Track Audio");
    const auto position = graphEditor.findLeftEdgeSlotBelowModules(size.x, size.y);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);

    // Auto-wire into the MASTER BUS. Unlike Track In's "exactly one MIDI instrument" rule this is
    // never ambiguous — the master bus is a singleton — but there are two possible sinks and the
    // order matters: if a Rec Tap has already been spliced in front of Audio Output, wiring
    // straight to the output would route this track AROUND the tap and quietly leave it out of every
    // subsequent take. Preferring the tap when one exists makes the two orderings compose: an audio
    // track added before the first take is re-spliced by ensureMasterRecordTap(), and one added
    // after it lands on the tap directly. Skipped when the caller is about to wire this node into a
    // full channel chain instead (see the header comment).
    if (wireDirectlyToMasterBus) {
        juce::AudioProcessorGraph::Node* sink = nullptr;
        for (auto* other : graph.getNodes())
            if (other != nullptr && dynamic_cast<RecordTapModule*>(other->getProcessor()) != nullptr)
                sink = other;
        if (sink == nullptr)
            for (auto* other : graph.getNodes())
                if (other != nullptr && other->getProcessor() != nullptr &&
                    other->getProcessor()->getName() == "Audio Output")
                    sink = other;

        if (sink != nullptr)
            for (int channel = 0; channel < TimelineAudioSourceModule::kNumChannels; ++channel)
                graph.addConnection({{node->nodeID, channel}, {sink->nodeID, channel}});
    }

    graphEditor.updateComponents();
    return uuid;
}

void MainComponent::refreshAssetRoots() {
    // The bundle root is the open .agsproj directory, or invalid when this document has never been
    // saved (in which case only "Recordings/" refs can resolve — see ProjectBundle's asset policy).
    const juce::File bundleRoot =
        (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) ? currentBundleDir_
                                                                                                 : juce::File();
    // The SAME folder chooseTakeFiles() writes unsaved-project takes into — kept in one expression
    // on each side rather than a shared helper so a change to either is visible at the other.
    const juce::File recordingsRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                          .getChildFile(synth::branding::kSettingsFolderName)
                                          .getChildFile(detail::kRecordingsFolderName);

    audioEngine.getAudioClipStreamer().setAssetRoots(bundleRoot, recordingsRoot);
}

void MainComponent::promptRelinkClipAsset(synth::ClipId id) {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Relink Audio", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, id](const juce::FileChooser& fc) {
                                 auto file = fc.getResult();
                                 if (file != juce::File{})
                                     relinkClipAsset(id, file);
                             });
}

void MainComponent::relinkClipAsset(synth::ClipId id, const juce::File& chosenFile) {
    const auto* clip = timelineDoc.getClip(id);
    if (clip == nullptr || clip->assetRef.isEmpty())
        return;
    const juce::String oldRef = clip->assetRef;

    juce::String error;
    juce::String newRef;
    if (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        newRef = synth::AssetManager::importAudioFile(chosenFile, currentBundleDir_, &error);
    } else {
        // No bundle yet (unsaved project) — the SAME app-data Recordings/ convention
        // chooseTakeFiles() uses for a take recorded before the first save.
        const auto recordingsRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                        .getChildFile(synth::branding::kSettingsFolderName)
                                        .getChildFile(detail::kRecordingsFolderName);
        const auto name = synth::AssetManager::importAudioFileToDirectory(chosenFile, recordingsRoot, &error);
        if (name.isNotEmpty())
            newRef = juce::String(detail::kRecordingsFolderName) + "/" + name;
    }

    if (newRef.isEmpty()) {
        statusBar.showMessage("Relink failed: " + error);
        return;
    }

    // Snapshot every clip sharing the OLD ref BEFORE mutating anything — a TimelineDoc reference
    // does not survive a mutation (see TimelineDoc::getClip's own contract), so the ids and each
    // clip's OWN sourceStartSeconds are captured first rather than iterating getTracks() while
    // calling setClipAsset on it.
    struct Target {
        synth::ClipId id;
        double sourceStartSeconds;
    };
    std::vector<Target> targets;
    for (const auto& track : timelineDoc.getTracks())
        for (const auto& c : track.clips)
            if (c.assetRef == oldRef)
                targets.push_back({c.id, c.sourceStartSeconds});

    // Every sharing clip moves to the new ref together, as ONE undo step — the whole point of a
    // relink is that duplicated/copy-pasted clips naming the same asset get fixed as a unit, never
    // half-relinked. The old file is never touched, let alone deleted.
    undoManager.recordTimelineChange(timelineDoc, [&] {
        for (const auto& target : targets)
            timelineDoc.setClipAsset(target.id, newRef, target.sourceStartSeconds);
    });

    statusBar.showMessage("Relinked to " + newRef);
}

double MainComponent::audioFileLengthInBeats(const juce::File& file) const {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return 0.0;

    const double seconds = (double)reader->lengthInSamples / reader->sampleRate;
    const auto snap = audioEngine.getTransport().getPositionSnapshot();
    const double bpm = snap.bpm > 0.0 ? snap.bpm : 120.0;
    return seconds * bpm / 60.0;
}

void MainComponent::importAudioFileToClip(synth::TrackId track, double startBeat, const juce::File& sourceFile) {
    const auto* trackPtr = timelineDoc.getTrack(track);
    if (trackPtr == nullptr || trackPtr->kind != synth::TrackKind::Audio)
        return;

    // The SAME import policy relinkClipAsset() uses — a saved project imports into the bundle's
    // Audio/, an unsaved one into the app-data Recordings/ convention chooseTakeFiles() writes takes
    // into, which saveToFile() sweeps into the bundle via
    // synth::AssetManager::adoptRecordingsAssets before it ever writes project.json.
    juce::String error;
    juce::String newRef;
    if (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        newRef = synth::AssetManager::importAudioFile(sourceFile, currentBundleDir_, &error);
    } else {
        const auto recordingsRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                        .getChildFile(synth::branding::kSettingsFolderName)
                                        .getChildFile(detail::kRecordingsFolderName);
        const auto name = synth::AssetManager::importAudioFileToDirectory(sourceFile, recordingsRoot, &error);
        if (name.isNotEmpty())
            newRef = juce::String(detail::kRecordingsFolderName) + "/" + name;
    }

    if (newRef.isEmpty()) {
        // Reported and abandoned BEFORE any mutation: a failed import leaves no clip behind.
        statusBar.showMessage("Import failed: " + error);
        return;
    }

    // The clip is as long as the file (never as long as some default), floored at the same minimum
    // an audio take uses so a sub-frame file still produces a grabbable clip.
    const double lengthBeats = std::max(kMinAudioClipLengthBeats, audioFileLengthInBeats(sourceFile));

    // ONE undo step for the clip AND its asset binding, exactly like commitAudioRecording().
    undoManager.recordTimelineChange(timelineDoc, [&] {
        const auto clip =
            timelineDoc.addClip(track, std::max(0.0, startBeat), lengthBeats, sourceFile.getFileNameWithoutExtension());
        if (!clip.isValid())
            return; // the track went away, or it is at kMaxClipsPerTrack
        timelineDoc.setClipAsset(clip, newRef, 0.0);
    });

    statusBar.showMessage("Imported " + newRef);
}

int MainComponent::cleanUnusedAssets() {
    if (currentBundleDir_ == juce::File() || !synth::ProjectBundle::isBundle(currentBundleDir_))
        return 0; // nothing to sweep outside a saved bundle
    return synth::AssetManager::cleanUnusedAssets(timelineDoc, currentBundleDir_);
}

void MainComponent::automateParameter(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) {
    auto* node = audioEngine.getGraph().getNodeForId(nodeId);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr) {
        statusBar.showMessage("Can't automate: module not found");
        return;
    }

    // Ensure-uuid, mirrored into the processor in the same breath — the same idiom
    // createTrackInNode() and AIStateMapper use at every uuid writer site (see
    // ModuleBase::setNodeUuid). getNodeUuid() is the audio-safe mirror; the node property is the
    // canonical copy addLane keys on.
    juce::String uuid = node->properties["uuid"].toString();
    if (uuid.isEmpty()) {
        uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        module->setNodeUuid(uuid);
    }

    juce::RangedAudioParameter* param = nullptr;
    for (auto* p : node->getProcessor()->getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (ranged != nullptr && ranged->paramID == paramId) {
            param = ranged;
            break;
        }
    }
    if (param == nullptr) {
        statusBar.showMessage("Can't automate: parameter not found");
        return;
    }

    // find-or-create the doc's ONE Automation-kind track, then bind the lane — both in the SAME
    // mutation lambda, so creating the track (when this is the first automated parameter in the
    // whole patch) and binding the lane is ONE undo step, not two. addLane dedupes doc-wide, so a
    // repeat call for a parameter that already has a lane mutates nothing and this is a no-op.
    synth::LaneId laneId;
    const juce::String uuidCopy = uuid;
    auto mutate = [this, &laneId, uuidCopy, paramId, param] {
        synth::TrackId trackId;
        for (const auto& track : timelineDoc.getTracks()) {
            if (track.kind == synth::TrackKind::Automation) {
                trackId = track.id;
                break;
            }
        }
        if (!trackId.isValid())
            trackId = timelineDoc.addTrack(synth::TrackKind::Automation, "Automation");
        if (!trackId.isValid())
            return; // kMaxTracks reached — nothing to bind onto

        synth::AutomationLane::RangeSnapshot range;
        range.minValue = param->getNormalisableRange().start;
        range.maxValue = param->getNormalisableRange().end;
        range.defaultValue = param->convertFrom0to1(param->getDefaultValue());
        laneId = timelineDoc.addLane(trackId, uuidCopy, paramId, range);
    };
    undoManager.recordTimelineChange(timelineDoc, mutate);
    if (!laneId.isValid())
        return;

    // Reuse the toggle path exactly (same call simulateToggleTimelineClick() makes) rather than
    // duplicating what it does to isTimelineVisible/persistence/layout.
    if (!isTimelineVisible && toggleTimelineButton.onClick)
        toggleTimelineButton.onClick();
    timelinePanel.showAutomationLane(laneId);
}
