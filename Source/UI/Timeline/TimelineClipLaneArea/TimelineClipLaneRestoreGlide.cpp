// TimelineClipLaneArea -- an undo or redo, a section edit or a repeat stamp that moves or trims clips: each is drawn
// gliding from where it was to its new place (KeyedRectTween, keyed by clip id), the doc geometry final at once. The
// class is declared in TimelineClipLaneArea.h.

#include "AppUndoManager.h"
#include "TimelineClipLaneArea.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/KeyGlide/RestoreWatch.h"
#include "UI/Timeline/TimelineClipLaneArea/ClipLaneEnterExit.h"

namespace synth::ui {

juce::Rectangle<int> TimelineClipLaneArea::drawnClipRect(synth::ClipId id, juce::Rectangle<int> resting) const {
    if (!clipGlide_->isAnimating())
        return resting;
    const auto drawn = clipGlide_->drawnAt(id.value, resting.toFloat()).toNearestInt();
    return drawn.withWidth(std::max(drawn.getWidth(), 1));
}

// Where the clip rests right now: its place in the document, or the live drag's preview of it.
juce::Rectangle<int> TimelineClipLaneArea::getDrawnClipRectForTest(synth::ClipId id) const {
    if (doc_ == nullptr)
        return {};
    const auto layout = getRowLayout();
    const auto& tracks = doc_->getTracks();
    for (int row = 0; row < (int)tracks.size(); ++row)
        for (const auto& clip : tracks[(size_t)row].clips)
            if (clip.id == id) {
                const auto geometry = effectiveGeometryFor(clip);
                return drawnClipRect(
                    id, computeClipRect(viewState_, layout, effectiveRowFor(id, row), geometry.start, geometry.length));
            }
    return {};
}

double TimelineClipLaneArea::glidedNoteX(synth::ClipId id, double restingX, juce::Rectangle<int> restingRect) const {
    if (!clipGlide_->isAnimating())
        return restingX;
    const auto mode = noteGlide_.find(id.value);
    if (mode == noteGlide_.end())
        return restingX;
    const auto rest = restingRect.toFloat();
    const auto drawn = clipGlide_->drawnAt(id.value, rest);
    return synth::glidedNoteX(mode->second, restingX, rest.getX(), rest.getWidth(), drawn.getX(), drawn.getWidth());
}

// The notes are the ones painted: a live edge drag shows the notes the drop will leave (resizePreviewFor).
double TimelineClipLaneArea::getDrawnNoteXForTest(synth::ClipId id, int index) const {
    if (doc_ == nullptr)
        return 0.0;
    const auto layout = getRowLayout();
    const auto& tracks = doc_->getTracks();
    for (int row = 0; row < (int)tracks.size(); ++row)
        for (const auto& clip : tracks[(size_t)row].clips) {
            if (clip.id != id)
                continue;
            const auto geometry = effectiveGeometryFor(clip);
            const auto rest =
                computeClipRect(viewState_, layout, effectiveRowFor(id, row), geometry.start, geometry.length);
            const auto preview = resizePreviewFor(clip);
            const auto& notes = preview ? preview->notes : clip.notes;
            if (index < 0 || index >= (int)notes.size())
                return 0.0;
            return glidedNoteX(id, viewState_.beatToX(geometry.start + notes[(size_t)index].startBeat), rest);
        }
    return 0.0;
}

// Runs at the end of every refresh from the doc. A restore (see RestoreWatch), a finished section edit (move, place,
// delete with its bars) and a repeat stamp move the clips they changed: glideChangedClips. Any other change lands at
// once. A refresh between the steps of a section edit keeps the last sync, so the edit's last step is compared with
// the state before its first and a moved clip glides instead of leaving and coming back.
void TimelineClipLaneArea::glideRestoredClips() {
    bool restored = false;
    if (undoManager_ != watchedUndo_) {
        watchedUndo_ = undoManager_;
        restoreWatch_ = std::make_unique<RestoreWatch>(undoManager_ != nullptr ? undoManager_->getRestoreSerial() : 0);
    } else if (undoManager_ != nullptr) {
        restored = restoreWatch_->take(undoManager_->isRestoring(), undoManager_->getRestoreSerial());
    }
    const auto change = doc_ != nullptr ? enterExit_->classify(*doc_, restored) : ClipLaneChange{};
    if (change.intermediate)
        return;

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
            clipGlide_->erase(id);
    enterExit_->dropReturned([&](std::int64_t id) { return now.find(id) != now.end(); });

    if (change.animates)
        glideChangedClips(change, now, noteStartsNow);
    std::erase_if(noteGlide_, [&](const auto& kv) { return now.find(kv.first) == now.end(); });
    clipPlaces_ = std::move(now);
    clipNoteStarts_ = std::move(noteStartsNow);
    if (doc_ != nullptr)
        enterExit_->remember(*doc_);
}

} // namespace synth::ui
