// TimelineClipLaneArea -- an undo or redo that moves or trims clips: each is drawn gliding from where it was to its
// restored place (KeyedRectTween, keyed by clip id), the doc geometry final at once. The class is declared in
// TimelineClipLaneArea.h.

#include "AppUndoManager.h"
#include "TimelineClipLaneArea.h"

namespace synth::ui {

juce::Rectangle<int> TimelineClipLaneArea::drawnClipRect(synth::ClipId id, juce::Rectangle<int> resting) const {
    if (!clipGlide_.isAnimating())
        return resting;
    const auto drawn = clipGlide_.drawnAt(id.value, resting.toFloat()).toNearestInt();
    return drawn.withWidth(std::max(drawn.getWidth(), 1));
}

juce::Rectangle<int> TimelineClipLaneArea::getDrawnClipRectForTest(synth::ClipId id) const {
    for (const auto& [clipId, rect] : collectClipRects())
        if (clipId == id)
            return drawnClipRect(id, rect);
    return {};
}

double TimelineClipLaneArea::glidedNoteX(synth::ClipId id, double restingX, juce::Rectangle<int> restingRect) const {
    if (!clipGlide_.isAnimating())
        return restingX;
    const auto mode = noteGlide_.find(id.value);
    if (mode == noteGlide_.end())
        return restingX;
    const auto rest = restingRect.toFloat();
    const auto drawn = clipGlide_.drawnAt(id.value, rest);
    return synth::glidedNoteX(mode->second, restingX, rest.getX(), rest.getWidth(), drawn.getX(), drawn.getWidth());
}

double TimelineClipLaneArea::getDrawnNoteXForTest(synth::ClipId id, int index) const {
    const auto* clip = doc_ != nullptr ? doc_->getClip(id) : nullptr;
    if (clip == nullptr || index < 0 || index >= (int)clip->notes.size())
        return 0.0;
    for (const auto& [clipId, rect] : collectClipRects())
        if (clipId == id)
            return glidedNoteX(id, viewState_.beatToX(clip->startBeat + clip->notes[(size_t)index].startBeat), rect);
    return 0.0;
}

// Runs at the end of every refresh from the doc. A restore (see RestoreWatch) glides the clips whose start, length or
// row differ from the last sync, and works out from the notes' relative starts how their notes ride along; any other
// change lands at once.
void TimelineClipLaneArea::glideRestoredClips() {
    bool restored = false;
    if (undoManager_ != watchedUndo_) {
        watchedUndo_ = undoManager_;
        restoreWatch_ = RestoreWatch(undoManager_ != nullptr ? undoManager_->getRestoreSerial() : 0);
    } else if (undoManager_ != nullptr) {
        restored = restoreWatch_.take(undoManager_->isRestoring(), undoManager_->getRestoreSerial());
    }

    std::map<std::int64_t, ClipPlace> now;
    std::map<std::int64_t, std::vector<synth::ClipNoteStart>> noteStartsNow;
    if (doc_ != nullptr) {
        const auto& tracks = doc_->getTracks();
        for (int row = 0; row < (int)tracks.size(); ++row)
            for (const auto& clip : tracks[(size_t)row].clips) {
                now[clip.id.value] = {clip.startBeat, clip.lengthBeats, row};
                noteStartsNow[clip.id.value] = synth::clipNoteStarts(clip.notes);
            }
    }
    for (const auto& [id, place] : clipPlaces_)
        if (now.find(id) == now.end())
            clipGlide_.erase(id);

    if (restored) {
        const auto layout = getRowLayout();
        const auto rectOf = [&](const ClipPlace& p) {
            return computeClipRect(viewState_, layout, p.row, p.start, p.length).toFloat();
        };
        for (const auto& [id, place] : now) {
            const auto was = clipPlaces_.find(id);
            if (was == clipPlaces_.end() || was->second == place)
                continue;
            noteGlide_[id] =
                synth::classifyClipNoteGlide(was->second.length, place.length, clipNoteStarts_[id], noteStartsNow[id]);
            clipGlide_.retarget(id, clipGlide_.drawnAt(id, rectOf(was->second)), rectOf(place), true);
        }
    }
    std::erase_if(noteGlide_, [&](const auto& kv) { return now.find(kv.first) == now.end(); });
    clipPlaces_ = std::move(now);
    clipNoteStarts_ = std::move(noteStartsNow);
}

} // namespace synth::ui
