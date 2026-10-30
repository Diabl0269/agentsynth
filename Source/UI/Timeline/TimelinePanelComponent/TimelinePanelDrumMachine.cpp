// TimelinePanelComponent: the drum machine as the second clip editor. It takes the piano roll's rect and cross-fade,
// and the panel's one predicate, clipEditorOpen(), answers "is an editor covering the clip lanes" for every branch that
// used to ask the roll alone.

#include "AppUndoManager.h"
#include "TimelinePanelComponent.h"
#include "UI/DrumMachine/DrumMachineHost.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include <cmath>
#include <limits>

namespace synth::ui {

bool TimelinePanelComponent::clipEditorOpen() const noexcept {
    return pianoRoll_.isOpen() || (drum_ != nullptr && drum_->grid.isOpen());
}

DrumMachineComponent& TimelinePanelComponent::getDrumMachine() const noexcept { return drum_->grid; }

bool TimelinePanelComponent::isDrumKitClip(synth::ClipId id) const {
    const auto* track = doc_ != nullptr ? doc_->getTrackForClip(id) : nullptr;
    return track != nullptr && trackHeaderHost_ != nullptr && trackHeaderHost_->isDrumKitTrack(track->id);
}

void TimelinePanelComponent::wireDrumMachine() {
    drum_ = std::make_unique<DrumMachineHost>();
    auto& grid = drum_->grid;
    addChildComponent(grid);
    grid.setComponentID("timelineDrumMachine");
    grid.onAboutToClose = [this] { rollLeaving_.capture(drum_->grid); };
    grid.onCloseRequested = [this] { closeDrumMachine(); };
    grid.onHorizontalViewChanged = [this] {
        if (drum_->grid.isOpen())
            ruler_.setMappingOverride(&drum_->grid.getViewState(), drum_->grid.leftGutterWidth());
        ruler_.repaint();
    };
    grid.onPianoRollRequested = [this](synth::ClipId id) {
        closeDrumMachine();
        openPianoRoll(id);
    };
    grid.onAuditionNote = [this](int pitch, float velocity01, bool on) {
        auditionClipNote(drum_->grid.getClipId(), pitch, velocity01, on);
    };
    grid.onClipCreated = [this](synth::ClipId id) { clipSelection_.setSelection({id}); };
    wireClipPreview();
    // A clip on a Drum Kit track takes its rows from the kit's pads; the panel's timer keeps them current.
    grid.kitRowsProvider = [this](KitRows& rows) {
        const auto* track = doc_ != nullptr ? doc_->getTrackForClip(drum_->grid.getClipId()) : nullptr;
        return track != nullptr && trackHeaderHost_ != nullptr && trackHeaderHost_->drumKitRows(track->id, &rows);
    };
    grid.onShowAllKitPads = [this] {
        const auto* track = doc_ != nullptr ? doc_->getTrackForClip(drum_->grid.getClipId()) : nullptr;
        if (track != nullptr && trackHeaderHost_ != nullptr)
            trackHeaderHost_->showAllDrumKitPads(track->id);
        drum_->grid.refreshKitRows();
    };
    pianoRoll_.onDrumMachineRequested = [this](synth::ClipId id) {
        closePianoRoll();
        openDrumMachine(id);
    };
}

// Play clip: the grid asks, the app's host starts the loop on the clip's Track In. The track is latched at the start
// because the clip may already be gone when the loop stops (a delete, an undo).
void TimelinePanelComponent::wireClipPreview() {
    auto& grid = drum_->grid;
    grid.onStartClipPreview = [this](double startBeat, double endBeat) {
        const auto* track = doc_ != nullptr ? doc_->getTrackForClip(drum_->grid.getClipId()) : nullptr;
        if (track == nullptr || trackHeaderHost_ == nullptr)
            return false;
        const auto id = track->id;
        if (drum_->previewTrack.isValid() && !(drum_->previewTrack == id))
            trackHeaderHost_->stopClipPreview(drum_->previewTrack);
        if (!trackHeaderHost_->startClipPreview(id, startBeat, endBeat))
            return false;
        drum_->previewTrack = id;
        return true;
    };
    grid.onStopClipPreview = [this] {
        if (trackHeaderHost_ != nullptr && drum_->previewTrack.isValid())
            trackHeaderHost_->stopClipPreview(drum_->previewTrack);
        drum_->previewTrack = {};
    };
    grid.clipPreviewBeat = [this] {
        return trackHeaderHost_ != nullptr && drum_->previewTrack.isValid()
                   ? trackHeaderHost_->clipPreviewBeat(drum_->previewTrack)
                   : std::numeric_limits<double>::quiet_NaN();
    };
    grid.onClipPreviewChanged = [this](bool playing) { playhead_.setClipPreviewActive(playing); };
}

// The clip under the playhead on `trackId`, else a new one-bar "Beat N" starting at the bar the playhead is in. The
// position is the transport's own, not the drawn (latency-shifted) one, so the bar is the one the user is looking at.
bool TimelinePanelComponent::openDrumMachineOnTrack(synth::TrackId trackId) {
    const auto* track = doc_ != nullptr ? doc_->getTrack(trackId) : nullptr;
    if (track == nullptr || track->kind != synth::TrackKind::Midi)
        return false;
    const double playhead = transport_ != nullptr ? transport_->getPositionSnapshot().ppq : 0.0;
    synth::ClipId target;
    for (const auto& clip : track->clips)
        if (!target.isValid() && playhead >= clip.startBeat && playhead < clip.startBeat + clip.lengthBeats)
            target = clip.id;

    if (!target.isValid()) {
        const double bar = beatsPerBarFor(transport_);
        const double start = std::floor(std::max(0.0, playhead) / bar) * bar;
        const auto nameTaken = [track](const juce::String& name) {
            return std::any_of(track->clips.begin(), track->clips.end(),
                               [&name](const synth::Clip& clip) { return clip.name == name; });
        };
        int number = 1;
        while (nameTaken("Beat " + juce::String(number)))
            ++number;
        const auto name = "Beat " + juce::String(number);
        const auto mutate = [&] { target = doc_->addClip(trackId, start, bar, name); };
        if (undoManager_ != nullptr)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        if (!target.isValid())
            return false; // the track is full
        clipSelection_.setSelection({target});
    }
    openDrumMachine(target);
    return drum_->grid.isOpen();
}

void TimelinePanelComponent::openClipEditor(synth::ClipId id) {
    if (isDrumKitClip(id))
        openDrumMachine(id);
    else
        openPianoRoll(id);
}

void TimelinePanelComponent::closeClipEditor() {
    if (pianoRoll_.isOpen())
        closePianoRoll();
    if (drum_->grid.isOpen())
        closeDrumMachine();
}

void TimelinePanelComponent::openDrumMachine(synth::ClipId id) {
    if (pianoRoll_.isOpen())
        closePianoRoll();
    auto& grid = drum_->grid;
    grid.openClip(id);
    if (!grid.isOpen())
        return;
    clipLaneFade_.setShown(false);
    drum_->fade.setShown(true);
    playhead_.setLocalPlayheadClient(&grid);
    grid.grabKeyboardFocus();
    resized(); // the toolbar row above the ruler appears, exactly as for the piano roll
    ruler_.setMappingOverride(&grid.getViewState(), grid.leftGutterWidth());
    playhead_.repaint();
}

// Leaving cross-fades a picture of the grid as it was over the lanes, as closePianoRoll does for the roll.
void TimelinePanelComponent::closeDrumMachine() {
    auto& grid = drum_->grid;
    if (grid.isOpen())
        rollLeaving_.capture(grid);
    grid.closeGrid();
    if (rollLeaving_.play())
        drum_->fade.snapTo(false);
    else
        drum_->fade.setShown(false);
    clipLaneFade_.setShown(true);
    clipLaneArea_.grabKeyboardFocus();
    resized();
    ruler_.setMappingOverride(nullptr, 0);
    playhead_.repaint();
}

} // namespace synth::ui
