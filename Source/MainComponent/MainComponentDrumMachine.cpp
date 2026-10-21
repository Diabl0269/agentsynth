// MainComponentDrumMachine.cpp -- what the drum machine needs from the app: the Track In module a track's clip loop
// runs on (Play clip), and the canvas card's "Drum machine" button, which finds the track playing a Drum Kit and opens
// the grid on it. MainComponent is declared in MainComponent.h; the rest of its implementation lives in the sibling
// MainComponent*.cpp units next to this one.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Modules/TimelineMidiSourceModule.h"
#include <cmath>
#include <limits>

// Deliberately the one resolution audition uses: track -> bindingUuid -> live node, because that node's MIDI output is
// where the track's destinations are wired FROM. Going anywhere else (AudioEngine's own midiMessageCollector, say)
// would reach the global MIDI-in path instead of this track's instruments, so a preview would play the wrong thing or
// nothing.
TimelineMidiSourceModule* MainComponent::trackInSource(synth::TrackId trackId) {
    const auto* track = timelineDoc.getTrack(trackId);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return nullptr; // an unbound track plays nowhere
    auto* node = findNodeByUuid(track->bindingUuid);
    return node != nullptr ? dynamic_cast<TimelineMidiSourceModule*>(node->getProcessor()) : nullptr;
}

// Play clip. The request is wait-free and changes nothing in the document or the graph, so there is no undo step.
bool MainComponent::startClipPreview(synth::TrackId trackId, double startBeat, double endBeat) {
    auto* source = trackInSource(trackId);
    if (source == nullptr || audioEngine.getTransport().getPositionSnapshot().playing)
        return false; // the song is playing: the loop would be ended at once, so it never starts
    source->startClipPreview(startBeat, endBeat);
    return true;
}

void MainComponent::stopClipPreview(synth::TrackId trackId) {
    if (auto* source = trackInSource(trackId))
        source->stopClipPreview();
}

double MainComponent::clipPreviewBeat(synth::TrackId trackId) {
    auto* source = trackInSource(trackId);
    return source != nullptr ? source->clipPreviewBeat() : std::numeric_limits<double>::quiet_NaN();
}

synth::TrackId MainComponent::trackPlayingNode(juce::AudioProcessorGraph::NodeID kit) {
    for (const auto& track : timelineDoc.getTracks()) {
        const auto* node = trackModuleNode(track.id);
        if (node != nullptr && node->nodeID == kit)
            return track.id;
    }
    return {};
}

void MainComponent::openDrumMachineForKit(juce::AudioProcessorGraph::NodeID kit) {
    const auto track = trackPlayingNode(kit);
    if (!track.isValid())
        return;
    showBottomDockTab(synth::ui::BottomDockComponent::Tab::Timeline);
    timelinePanel.openDrumMachineOnTrack(track);
}
