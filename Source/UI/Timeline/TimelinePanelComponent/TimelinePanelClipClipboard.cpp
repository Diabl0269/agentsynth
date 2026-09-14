// TimelinePanelClipClipboard.cpp
//
// Clip clipboard: copy/paste/cut/duplicate/repeat/select-all for the clip selection.
// TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling
// TimelinePanel*.cpp files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "AppUndoManager.h"

namespace synth::ui {

//==============================================================================
// ---- Clip clipboard ----

synth::TrackId TimelinePanelComponent::firstTrackOfKind(synth::TrackKind kind) const {
    if (doc_ == nullptr)
        return {};
    for (const auto& track : doc_->getTracks())
        if (track.kind == kind)
            return track.id;
    return {};
}

double TimelinePanelComponent::currentBeatsPerBarForPaste() const {
    double beatsPerBar = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        const double tsBeatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
        if (tsBeatsPerBar > 0.0)
            beatsPerBar = tsBeatsPerBar;
    }
    return beatsPerBar;
}

bool TimelinePanelComponent::copySelectedClips() {
    if (doc_ == nullptr)
        return false;

    const auto selected = clipSelection_.getSelected(); // ascending id order
    if (selected.empty())
        return false;

    // Pass 1: the earliest selected clip's start — every captured entry is stored relative to it.
    bool haveEarliest = false;
    double earliestStart = 0.0;
    for (auto id : selected) {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            continue;
        if (!haveEarliest || clip->startBeat < earliestStart) {
            earliestStart = clip->startBeat;
            haveEarliest = true;
        }
    }
    if (!haveEarliest)
        return false; // every selected id was stale

    // Pass 2: capture each clip relative to that start.
    std::vector<ClipboardClip> captured;
    for (auto id : selected) {
        const auto* clip = doc_->getClip(id);
        const auto* track = doc_->getTrackForClip(id);
        if (clip == nullptr || track == nullptr)
            continue;
        ClipboardClip entry;
        entry.originalTrack = track->id;
        // The payload, not the source row, decides where this can be pasted — see
        // ClipboardClip::requiredKind.
        entry.requiredKind = clip->assetRef.isNotEmpty() ? synth::TrackKind::Audio : synth::TrackKind::Midi;
        entry.relativeStartBeat = clip->startBeat - earliestStart;
        entry.lengthBeats = clip->lengthBeats;
        entry.name = clip->name;
        entry.notes = clip->notes; // MidiNote copies carry each note's own muted flag
        entry.muted = clip->muted;
        entry.assetRef = clip->assetRef;
        entry.gainDb = clip->gainDb;
        entry.fadeInBeats = clip->fadeInBeats;
        entry.fadeOutBeats = clip->fadeOutBeats;
        entry.sourceStartSeconds = clip->sourceStartSeconds;
        captured.push_back(std::move(entry));
    }
    if (captured.empty())
        return false;

    clipClipboard_ = std::move(captured);
    return true;
}

bool TimelinePanelComponent::pasteClipsAtPlayhead() {
    if (doc_ == nullptr || clipClipboard_.empty())
        return false;

    double playheadBeat = 0.0;
    if (transport_ != nullptr)
        playheadBeat = transport_->getPositionSnapshot().ppq;
    const double snappedPlayhead = viewState_.snapBeat(playheadBeat, currentBeatsPerBarForPaste());

    // Resolved ONCE, before the mutation: the target row for every entry, so the loop below does
    // no lookups against a doc it is halfway through mutating. The original track only counts if
    // it still plays this clip's payload (see ClipboardClip::requiredKind); otherwise the first
    // track of the required kind, and otherwise nothing at all.
    std::vector<synth::TrackId> targets;
    targets.reserve(clipClipboard_.size());
    for (const auto& entry : clipClipboard_) {
        const auto* original = doc_->getTrack(entry.originalTrack);
        targets.push_back(original != nullptr && original->kind == entry.requiredKind
                              ? entry.originalTrack
                              : firstTrackOfKind(entry.requiredKind));
    }

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, snappedPlayhead, &targets, &newIds] {
        for (std::size_t i = 0; i < clipClipboard_.size(); ++i) {
            const auto& entry = clipClipboard_[i];
            if (!targets[i].isValid())
                continue; // nowhere this clip could play — skip it rather than park it

            const double startBeat = std::max(0.0, snappedPlayhead + entry.relativeStartBeat);
            const auto newId = doc_->addClip(targets[i], startBeat, entry.lengthBeats, entry.name);
            if (!newId.isValid())
                continue;
            for (const auto& note : entry.notes)
                doc_->addNote(newId, note);
            // Through the setters, not into the struct: setClipAsset is the gate that rejects an
            // assetRef that is not bundle-relative, and a clipboard is not a trusted source.
            if (entry.assetRef.isNotEmpty() || entry.sourceStartSeconds != 0.0)
                doc_->setClipAsset(newId, entry.assetRef, entry.sourceStartSeconds);
            doc_->setClipGainDb(newId, entry.gainDb);
            doc_->setClipFades(newId, entry.fadeInBeats, entry.fadeOutBeats);
            doc_->setClipMuted(newId, entry.muted);
            newIds.push_back(newId);
        }
    };

    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    clipSelection_.setSelection(newIds);
    return true;
}

bool TimelinePanelComponent::duplicateSelectedClips() {
    if (doc_ == nullptr)
        return false;

    const auto selected = clipSelection_.getSelected();
    if (selected.empty())
        return false;

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, &selected, &newIds] {
        for (auto id : selected) {
            const auto newId = doc_->duplicateClip(id);
            if (newId.isValid())
                newIds.push_back(newId);
        }
    };

    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    clipSelection_.setSelection(newIds);
    return true;
}

bool TimelinePanelComponent::canCutClips() const noexcept { return doc_ != nullptr && !clipSelection_.isEmpty(); }

bool TimelinePanelComponent::hasClipSelection() const noexcept { return !clipSelection_.isEmpty(); }

bool TimelinePanelComponent::cutSelectedClips() {
    // The copy half also validates (no doc / nothing selected / every id stale all fail there), so
    // nothing is deleted unless something was actually captured.
    if (!copySelectedClips())
        return false;

    const auto selected = clipSelection_.getSelected();
    auto mutate = [this, selected] {
        for (auto id : selected)
            doc_->removeClip(id);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    clipSelection_.clear();
    clipLaneArea_.repaint();
    return true;
}

bool TimelinePanelComponent::selectAllClips() {
    if (doc_ == nullptr)
        return false;

    std::vector<synth::ClipId> all;
    for (const auto& track : doc_->getTracks())
        for (const auto& clip : track.clips)
            all.push_back(clip.id);
    if (all.empty())
        return false;

    clipSelection_.setSelection(all);
    clipLaneArea_.repaint();
    return true;
}

bool TimelinePanelComponent::repeatSelectedClips(int count) {
    if (doc_ == nullptr || count < 1)
        return false;

    // Snapshot the source geometry BEFORE mutating: every duplicate re-seats its track's clip
    // vector, so a Clip pointer (or a re-read of the selection) taken mid-loop would be stale.
    struct Source {
        synth::ClipId id;
        synth::TrackId track;
        double startBeat = 0.0;
    };
    std::vector<Source> sources;
    bool haveSpan = false;
    double spanStart = 0.0, spanEnd = 0.0;
    for (auto id : clipSelection_.getSelected()) {
        const auto* clip = doc_->getClip(id);
        const auto* track = doc_->getTrackForClip(id);
        if (clip == nullptr || track == nullptr)
            continue;
        sources.push_back({id, track->id, clip->startBeat});
        const double end = clip->startBeat + clip->lengthBeats;
        spanStart = haveSpan ? std::min(spanStart, clip->startBeat) : clip->startBeat;
        spanEnd = haveSpan ? std::max(spanEnd, end) : end;
        haveSpan = true;
    }
    if (!haveSpan || !(spanEnd > spanStart))
        return false;

    const double blockLength = spanEnd - spanStart;

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, sources, blockLength, count, &newIds] {
        for (int repeat = 1; repeat <= count; ++repeat) {
            for (const auto& source : sources) {
                const auto dup = doc_->duplicateClip(source.id);
                if (!dup.isValid())
                    continue;
                // duplicateClip drops the copy one clip-length after its source; moving it to
                // (its own start + n block lengths) is what tiles the whole selection forward.
                // Same track by construction, so the kind check never engages.
                doc_->moveClipToTrack(dup, source.track, source.startBeat + (double)repeat * blockLength);
                newIds.push_back(dup);
            }
        }
    };

    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    clipSelection_.setSelection(newIds);
    clipLaneArea_.repaint();
    return true;
}

} // namespace synth::ui
