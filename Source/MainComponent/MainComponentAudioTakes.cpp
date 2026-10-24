// Concern: audio takes -- which armed Audio tracks record and from where, starting their captures (the track's own
// input through the engine's input recorder, or the whole mix through the master Rec Tap), the live strips, and
// the one commit that turns every take into a clip.

#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include "Mixer/TrackChannelLink.h"
#include "Modules/ChannelStripModule.h"
#include "ProjectBundle.h"
#include "Timeline/TakePlacement.h"
#include "UserSettings.h"
#include <algorithm>

namespace {

using AudioTake = synth::maincomponent::AudioTake;
using TakeResult = synth::TakeCaptureWriter::TakeResult;

// Upper bound on the take-number search. A folder with 10000 takes in it is a bug report, not a
// session, and an unbounded loop on a stat() call is not something a UI click should be able to do.
constexpr int kMaxTakeNumber = 10000;

// Points the live tap of the strip `node` at a recording slot's buffer. False when the strip is gone.
bool armStripLiveTap(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node,
                     juce::AudioBuffer<float>* buffer) {
    auto* found = graph.getNodeForId(node);
    auto* strip = found != nullptr ? dynamic_cast<ChannelStripModule*>(found->getProcessor()) : nullptr;
    if (strip == nullptr || buffer == nullptr)
        return false;
    strip->setLiveTapBuffer(buffer);
    return true;
}

// Disarms by scanning every strip rather than remembering one: an undo mid-take may have rebuilt the graph, so the
// strip that was armed can be a different object (or gone) by the time the take ends.
void disarmStripLiveTaps(juce::AudioProcessorGraph& graph, const juce::AudioBuffer<float>* buffer) {
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor()))
                if (strip->getLiveTapBuffer() == buffer)
                    strip->setLiveTapBuffer(nullptr);
}

// Stops one take's capture wherever it runs. A tap that vanished (an undo mid-take rebuilt the graph without it)
// has nothing to stop, which loses that take rather than crashing.
TakeResult stopTakeCapture(const AudioTake& take, RecordTapModule* tap, synth::InputTakeRecorder& recorder,
                           juce::AudioProcessorGraph& graph) {
    if (take.source == AudioTake::Source::Track)
        disarmStripLiveTaps(graph, recorder.getStripSlotBuffer(take.inputSlot));
    if (take.source != AudioTake::Source::WholeMix)
        return recorder.stopSlot(take.inputSlot);
    return tap != nullptr ? tap->stopCapture() : TakeResult{};
}

void deleteTakeFiles(const AudioTake& take) {
    take.wavFile.deleteFile();
    take.peaksFile.deleteFile();
}

// Samples -> beats through the tempo, rate and round-trip latency FROZEN at record-on, never the live engine
// values: a device change mid-take forces an early commit, and by then the engine may already run the new rate
// while the take's anchor and WAV are still in the old one. See Source/Timeline/TakePlacement.h.
synth::TakePlacement placeTake(const AudioTake& take, const TakeResult& result) {
    synth::TakePlacementInput input;
    input.takeLengthSamples = result.lengthSamples;
    input.captureStartValid = result.captureStartValid;
    input.captureStartTimelineSample = result.captureStartTimelineSample;
    input.captureStartBlockOffset = result.captureStartBlockOffset;
    input.punchInBeat = take.punchInBeat;
    input.recordingLatencySamples = take.captureRecordingLatencySamples;
    input.sampleRate = take.captureSampleRate > 0.0 ? take.captureSampleRate : 44100.0;
    input.bpm = take.captureBpm > 0.0 ? take.captureBpm : 120.0;
    input.minClipLengthBeats = detail::kMinAudioClipLengthBeats;
    return synth::computeTakePlacement(input);
}

} // namespace

// The live master tap, or nullptr. Resolves `hint` first and falls back to a scan: an undo taken mid-take rebuilds
// the graph and renumbers nodes, and losing the tap that way must lose the take, not crash. The tap is a
// singleton, so the scan still identifies it unambiguously.
RecordTapModule* MainComponent::findMasterRecordTap(juce::AudioProcessorGraph::NodeID hint) const {
    auto& graph = const_cast<MainComponent*>(this)->audioEngine.getGraph();
    if (auto* node = graph.getNodeForId(hint))
        if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor()))
            return tap;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor()))
                return tap;
    return nullptr;
}

// Where this take's files go: `<bundle>/Audio/take-N.wav` + `<bundle>/Peaks/take-N.agpk` for a saved project,
// `<app data>/Recordings/take-N.wav` + `.agpk` for one never saved (see the unsaved-project policy on
// ProjectBundle). N is the first number free on disk and not already given to a take in `reserved` (the other
// takes of the same Record press, whose files may not exist yet). Fills the file/assetRef fields of `take`;
// false if the directories could not be created or no number is free.
bool MainComponent::chooseTakeFiles(AudioTake& take, const std::vector<AudioTake>& reserved) const {
    juce::File audioDir;
    juce::File peaksDir;
    juce::String refPrefix;

    if (currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        audioDir = currentBundleDir_.getChildFile(synth::ProjectBundle::kAudioSubdirName);
        peaksDir = currentBundleDir_.getChildFile(synth::ProjectBundle::kPeaksSubdirName);
        refPrefix = juce::String(synth::ProjectBundle::kAudioSubdirName) + "/";
    } else {
        // An UNSAVED project has no bundle, so takes land in app data and the clip's assetRef carries the reserved
        // "Recordings/" prefix; saveToFile() moves them into the bundle on the first save
        // (synth::AssetManager::adoptRecordingsAssets) and rewrites the refs, so project.json never carries one.
        auto root = synth::userSettingsRootDirectory().getChildFile(detail::kRecordingsFolderName);
        audioDir = root;
        peaksDir = root;
        refPrefix = juce::String(detail::kRecordingsFolderName) + "/";
    }

    if (!audioDir.exists() && !audioDir.createDirectory().wasOk())
        return false;
    if (!peaksDir.exists() && !peaksDir.createDirectory().wasOk())
        return false;

    // Both files are checked so a take never half-overwrites an earlier one.
    for (int n = 1; n <= kMaxTakeNumber; ++n) {
        const juce::String stem = "take-" + juce::String(n);
        const auto wav = audioDir.getChildFile(stem + ".wav");
        const auto peaks = peaksDir.getChildFile(stem + ".agpk");
        const bool taken = std::any_of(reserved.begin(), reserved.end(),
                                       [&wav](const AudioTake& other) { return other.wavFile == wav; });
        if (taken || wav.exists() || peaks.exists())
            continue;
        take.wavFile = wav;
        take.peaksFile = peaks;
        take.assetRef = refPrefix + stem + ".wav";
        return true;
    }
    return false;
}

// Decides, before the transport moves, which armed Audio tracks record and from where: a Device input that is
// connected records it; Whole mix records the master tap (at most one track per press, since the tap is one
// take); Another track records that track's mixer channel (a source whose track was removed or has no channel is
// skipped, and one channel feeds at most one take per press); No input, or an input that is not connected, records
// nothing and says so in `notice`. False -- with the
// record button already reset and the reason on the status bar -- refuses the whole press, exactly like a single
// take used to: no Audio Output to tap, or a take file that cannot be created.
bool MainComponent::resolveAudioTakes(std::vector<AudioTake>& takes, juce::String& notice) {
    const auto refuse = [this](const juce::String& message) {
        timelinePanel.getTransportBar().setRecordingState(false);
        statusBar.showMessage(message);
        return false;
    };
    const auto inputInfo = audioEngine.getAudioInputInfo();
    juce::StringArray problems;
    bool wholeMixTaken = false;
    std::vector<juce::AudioProcessorGraph::NodeID> stripsTaken;

    for (const auto& track : timelineDoc.getTracks()) {
        if (!track.armed || track.kind != synth::TrackKind::Audio)
            continue;
        AudioTake take;
        take.track = track.id;
        const auto& input = track.audioInput;
        if (input.kind == synth::AudioInputChoice::Kind::None) {
            problems.add(track.name + " has no input - pick one in the side pane");
            continue;
        }
        if (input.kind == synth::AudioInputChoice::Kind::WholeMix) {
            if (wholeMixTaken) {
                problems.add(track.name + ": the whole mix is already recording on another track");
                continue;
            }
            auto* tapNode = ensureMasterRecordTap();
            if (tapNode == nullptr || dynamic_cast<RecordTapModule*>(tapNode->getProcessor()) == nullptr)
                return refuse("Can't record audio: no Audio Output in the patch");
            take.source = AudioTake::Source::WholeMix;
            take.tapNode = tapNode->nodeID;
            take.numChannels = RecordTapModule::kNumChannels;
            wholeMixTaken = true;
        } else if (input.kind == synth::AudioInputChoice::Kind::Track) {
            const auto* source = timelineDoc.getTrack(input.sourceTrack);
            if (source == nullptr) {
                problems.add(track.name + ": its source track was removed");
                continue;
            }
            const auto link = synth::resolveTrackChannelLink(audioEngine.getGraph(), timelineDoc, input.sourceTrack);
            if (!link.hasChannel) {
                problems.add(track.name + ": its source track has no mixer channel");
                continue;
            }
            if (std::find(stripsTaken.begin(), stripsTaken.end(), link.stripId) != stripsTaken.end()) {
                problems.add(track.name + ": that channel is already recording on another track");
                continue;
            }
            stripsTaken.push_back(link.stripId);
            take.source = AudioTake::Source::Track;
            take.stripNode = link.stripId;
            take.numChannels = 2;
        } else {
            if (!synth::isAudioInputConnected(input, inputInfo)) {
                problems.add(track.name + ": its input is not connected");
                continue;
            }
            take.source = AudioTake::Source::Input;
            take.firstChannel = input.firstChannel;
            take.numChannels = input.numChannels;
        }
        if (!chooseTakeFiles(take, takes))
            return refuse("Can't record audio: could not create the take file");
        takes.push_back(take);
    }
    notice = problems.joinIntoString("; ");
    return true;
}

// Starts every resolved take at record-on, after the locate/play the caller posted (a frame captured before a
// locate would belong to the OLD position). The count-in's pre-roll is therefore recorded and trimmed out at
// commit, from each capture's own sample-accurate anchor. If any capture fails to start, the ones already
// started are stopped and every file of this press is deleted, and the press is refused.
bool MainComponent::startAudioTakes(std::vector<AudioTake>& takes, double punchInBeat, double sampleRate, double bpm) {
    auto& recorder = audioEngine.getInputRecorder();
    auto& graph = audioEngine.getGraph();
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (std::size_t i = 0; i < takes.size(); ++i) {
        auto& take = takes[i];
        bool started = false;
        if (take.source == AudioTake::Source::Input) {
            take.inputSlot =
                recorder.startSlot(take.firstChannel, take.numChannels, take.wavFile, take.peaksFile, rate);
            started = take.inputSlot >= 0;
        } else if (take.source == AudioTake::Source::Track) {
            take.inputSlot = recorder.startStripSlot(take.wavFile, take.peaksFile, rate);
            started = take.inputSlot >= 0;
            if (started && !armStripLiveTap(graph, take.stripNode, recorder.getStripSlotBuffer(take.inputSlot))) {
                recorder.stopSlot(take.inputSlot);
                started = false;
            }
        } else if (auto* tap = findMasterRecordTap(take.tapNode)) {
            started = tap->startCapture(take.wavFile, take.peaksFile, rate, RecordTapModule::kNumChannels);
        }
        if (!started) {
            for (std::size_t j = 0; j < i; ++j)
                stopTakeCapture(takes[j], findMasterRecordTap(takes[j].tapNode), recorder, graph);
            for (const auto& each : takes)
                deleteTakeFiles(each);
            timelinePanel.getTransportBar().setRecordingState(false);
            audioEngine.getMetronome().setForcedOn(false);
            statusBar.showMessage("Can't record audio: the take file could not be opened");
            return false;
        }
        take.punchInBeat = punchInBeat;
        // Frozen NOW, not re-read at commit time -- see the AudioTake field comments.
        take.captureSampleRate = rate;
        take.captureBpm = bpm > 0.0 ? bpm : 120.0;
        // An internal source has no round trip through the device, so nothing is shifted back for it.
        take.captureRecordingLatencySamples =
            take.source == AudioTake::Source::Track ? 0 : audioEngine.getRecordingLatencySamples();
    }
    audioTakes_ = takes;
    return true;
}

// Counterpart to commitMidiRecording(): the ONE place audio takes commit. Stops every capture, places each take,
// then adds every clip in a single recordTimelineChange -- one undo step for the whole press. A no-op unless a
// take is in flight, so both callers (the Record-off click and the poll's commit-on-stop) call it unconditionally.
void MainComponent::commitAudioRecording() {
    if (audioTakes_.empty())
        return;

    // Cleared FIRST: every path out of here means the takes are over, and leaving the list set would let the
    // 10 Hz poll re-enter this on the next tick.
    const std::vector<AudioTake> takes = std::move(audioTakes_);
    audioTakes_.clear();

    struct Placed {
        const AudioTake* take;
        synth::TakePlacement placement;
    };
    std::vector<Placed> placed;
    bool overran = false;
    for (const auto& take : takes) {
        const auto result = stopTakeCapture(take, findMasterRecordTap(take.tapNode), audioEngine.getInputRecorder(),
                                            audioEngine.getGraph());
        overran = overran || result.overran;
        // Nothing captured (record disengaged during the count-in, or the capture never armed), or everything sits
        // before the punch: the file stays, no clip -- MidiRecorder's "an empty take commits nothing" rule.
        if (!result.ok || result.lengthSamples <= 0)
            continue;
        const auto placement = placeTake(take, result);
        if (placement.hasContent)
            placed.push_back({&take, placement});
    }

    timelinePanel.getTransportBar().setRecordingState(false);
    // Every stop ends any in-flight count-in pre-roll, exactly like commitMidiRecording's own call.
    audioEngine.getMetronome().setForcedOn(false);

    if (!placed.empty()) {
        undoManager.recordTimelineChange(timelineDoc, [&] {
            for (const auto& entry : placed) {
                const auto clip = timelineDoc.addClip(entry.take->track, entry.placement.clipStartBeat,
                                                      entry.placement.clipLengthBeats, "Take");
                if (!clip.isValid())
                    continue; // the track went away, or it is at kMaxClipsPerTrack
                // The pre-roll (and the latency shift's overhang at timeline 0) is excluded by the WINDOW, not by
                // rewriting the WAV: the file keeps every frame that was captured.
                timelineDoc.setClipAsset(clip, entry.take->assetRef, entry.placement.sourceStartSeconds);
            }
        });
    }

    if (overran)
        statusBar.showMessage("Dropped audio during recording");
}

// One growing strip per rolling take, on the poll. A whole-mix take reads the tap's peaks, an input or track take its
// recorder slot's; neither source is held past the call.
void MainComponent::updateLiveRecordingStrips(double currentBeat) {
    std::vector<synth::ui::TimelineClipLaneArea::LiveRecordingInfo> strips;
    auto& recorder = audioEngine.getInputRecorder();
    for (const auto& take : audioTakes_) {
        synth::ui::TimelineClipLaneArea::LiveRecordingInfo info;
        info.active = true;
        info.track = take.track;
        info.punchBeat = take.punchInBeat;
        info.currentBeat = currentBeat;
        info.numChannels = take.numChannels;
        if (take.source != AudioTake::Source::WholeMix) {
            const int slot = take.inputSlot;
            info.copyPeaks = [&recorder, slot](std::vector<std::pair<float, float>>& out) {
                recorder.copyLivePeaks(slot, out);
            };
        } else {
            info.tap = findMasterRecordTap(take.tapNode);
            if (info.tap == nullptr)
                continue;
        }
        strips.push_back(std::move(info));
    }
    timelinePanel.getClipLaneArea().updateLiveRecordings(strips);
}
