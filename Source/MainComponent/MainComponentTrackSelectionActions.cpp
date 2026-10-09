// MainComponentTrackSelectionActions.cpp — the track-row commands that act on every selected track
// (docs/timeline/tracks.md#selecting-several-tracks): duplicate, copy and paste, delete, and the host seams the header
// uses for mute, solo and colour. Each reuses the single-track path per track (duplicateTrackBody, deleteTrack, the
// header's own toggles) inside ONE undo step, so a bulk gesture is one Cmd+Z and costs one audio-graph rebuild.
#include "AI/AIStateMapper/GraphRebuildBatch.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include <algorithm>

// The whole selection (timeline order, tracks only) when `clicked` is one of two or more selected tracks, else just
// `clicked`: a track outside the selection is acted on alone, like Finder and Logic.
std::vector<synth::TrackId> MainComponent::tracksActedOnBy(synth::TrackId clicked) const {
    if (!timelinePanel.isTrackSelected(clicked))
        return {clicked};
    std::vector<synth::TrackId> tracks;
    for (const auto id : timelinePanel.selectedTracks())
        if (const auto* t = timelineDoc.getTrack(id); t != nullptr && t->kind != synth::TrackKind::Automation)
            tracks.push_back(id);
    if (tracks.size() < 2 || std::find(tracks.begin(), tracks.end(), clicked) == tracks.end())
        return {clicked};
    return tracks;
}

// Every record*Change inside `edits` joins one undo transaction (AppUndoManager::ScopedUndoGroup).
void MainComponent::performTrackGroupEdit(const std::function<void()>& edits) {
    if (!edits)
        return;
    {
        const AppUndoManager::ScopedUndoGroup group(undoManager);
        edits();
    }
    // A linked track's M/S live on its channel strip, which no doc notification reports: every row re-reads it.
    for (int i = 0; i < timelinePanel.getTrackHeaderCount(); ++i)
        if (auto* header = timelinePanel.getTrackHeaderAt(i))
            header->refreshFromDoc();
}

void MainComponent::copyTracks(const std::vector<synth::TrackId>& tracks) {
    trackClipboard_ = tracks;
    statusBar.showMessage("Copied " + juce::String(static_cast<int>(tracks.size())) + " tracks");
}

bool MainComponent::canPasteTracks() const {
    return std::any_of(trackClipboard_.begin(), trackClipboard_.end(), [this](synth::TrackId id) {
        const auto* t = timelineDoc.getTrack(id);
        return t != nullptr && t->kind != synth::TrackKind::Automation;
    });
}

void MainComponent::pasteTracks(synth::TrackId after) {
    std::vector<synth::TrackId> sources;
    for (const auto id : trackClipboard_) // a copied track deleted since is skipped
        if (const auto* t = timelineDoc.getTrack(id); t != nullptr && t->kind != synth::TrackKind::Automation)
            sources.push_back(id);
    if (sources.empty()) {
        statusBar.showMessage("Nothing to paste - the copied tracks are gone");
        return;
    }
    duplicateTracksBelow(sources, after, "Pasted");
}

void MainComponent::duplicateTracksBelow(const std::vector<synth::TrackId>& sources, synth::TrackId after,
                                         const juce::String& verb) {
    if (static_cast<int>(timelineDoc.getTracks().size() + sources.size()) > synth::TimelineDoc::kMaxTracks) {
        statusBar.showMessage("Could not duplicate the tracks - the timeline is full");
        return;
    }
    std::vector<synth::TrackId> copies;
    timelinePanel.armTrackDuplicateGlide(after);
    bool pushed = false;
    {
        const juce::ScopedValueSetter<int> reconcileFollows(fullReconcileFollowsDepth_, fullReconcileFollowsDepth_ + 1);
        pushed = undoManager.recordGraphTimelineAndMacroChange(
            audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(), [this, &sources, after, &copies] {
                synth::GraphRebuildBatch oneRebuild(audioEngine.getGraph()); // one render-sequence rebuild for all
                for (const auto source : sources)
                    if (const auto* t = timelineDoc.getTrack(source)) {
                        const auto copy = duplicateTrackBody(source, t->name + " copy");
                        if (copy.isValid())
                            copies.push_back(copy);
                    }
                // Each copy landed right below its own source; gather them below `after`, in the sources' order.
                std::vector<synth::TrackId> order;
                for (const auto& t : timelineDoc.getTracks())
                    if (std::find(copies.begin(), copies.end(), t.id) == copies.end())
                        order.push_back(t.id);
                const auto anchor = std::find(order.begin(), order.end(), after);
                if (anchor != order.end())
                    order.insert(anchor + 1, copies.begin(), copies.end());
                else
                    order.insert(order.end(), copies.begin(), copies.end());
                for (size_t i = 0; i < order.size(); ++i)
                    timelineDoc.moveTrack(order[i], static_cast<int>(i));
            });
    }

    reconcileTimelineAfterGraphChange();
    if (pushed && !copies.empty()) {
        timelinePanel.selectTracks(copies, copies.back());
        statusBar.showMessage(verb + " " + juce::String(static_cast<int>(copies.size())) + " tracks");
    } else {
        timelinePanel.armTrackDuplicateGlide({});
        statusBar.showMessage("Could not duplicate the tracks");
    }
}

// One question for the whole selection (unless it is switched off), then deleteTrack per track in ONE undo step.
void MainComponent::deleteTracksAfterConfirm(const std::vector<synth::TrackId>& tracks) {
    const auto deleteAll = [this, tracks] {
        const AppUndoManager::ScopedUndoGroup group(undoManager);
        for (const auto id : tracks)
            deleteTrack(id);
    };
    const auto* settings = appProperties.getUserSettings();
    if (settings != nullptr && !settings->getBoolValue(synth::ui::kAskBeforeDeletingTrackKey, true)) {
        deleteAll();
        return;
    }
    juce::Component::SafePointer<MainComponent> safeThis(this);
    synth::ui::confirmDeleteTrack(synth::ui::deleteTrackConfirmText(static_cast<int>(tracks.size())),
                                  [safeThis, deleteAll](bool confirmed, bool dontAskAgain) {
                                      auto* self = safeThis.getComponent();
                                      if (self == nullptr || !confirmed)
                                          return;
                                      if (dontAskAgain)
                                          if (auto* userSettings = self->appProperties.getUserSettings()) {
                                              userSettings->setValue(synth::ui::kAskBeforeDeletingTrackKey, "0");
                                              userSettings->saveIfNeeded();
                                          }
                                      deleteAll(); // captures `this`: checked alive through safeThis above
                                  });
}
