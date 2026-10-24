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

// Runs at the end of every refresh from the doc. A restore (see RestoreWatch) glides the clips whose start, length or
// row differ from the last sync; any other change lands at once.
void TimelineClipLaneArea::glideRestoredClips() {
    bool restored = false;
    if (undoManager_ != watchedUndo_) {
        watchedUndo_ = undoManager_;
        restoreWatch_ = RestoreWatch(undoManager_ != nullptr ? undoManager_->getRestoreSerial() : 0);
    } else if (undoManager_ != nullptr) {
        restored = restoreWatch_.take(undoManager_->isRestoring(), undoManager_->getRestoreSerial());
    }

    std::map<std::int64_t, ClipPlace> now;
    if (doc_ != nullptr) {
        const auto& tracks = doc_->getTracks();
        for (int row = 0; row < (int)tracks.size(); ++row)
            for (const auto& clip : tracks[(size_t)row].clips)
                now[clip.id.value] = {clip.startBeat, clip.lengthBeats, row};
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
            clipGlide_.retarget(id, clipGlide_.drawnAt(id, rectOf(was->second)), rectOf(place), true);
        }
    }
    clipPlaces_ = std::move(now);
}

} // namespace synth::ui
