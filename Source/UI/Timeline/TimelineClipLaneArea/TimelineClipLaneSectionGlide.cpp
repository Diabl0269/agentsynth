// TimelineClipLaneArea -- the clips a restore, a section edit or a repeat stamp changed: each moved clip glides from
// where it was to its new place, a clip that arrives grows in from its left edge and one that leaves shrinks toward
// its centre where it was (ClipLaneEnterExit). The class is declared in TimelineClipLaneArea.h.

#include "TimelineClipLaneArea.h"
#include "TimelineClipLaneInternal.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Timeline/TimelineClipLaneArea/ClipLaneEnterExit.h"

namespace synth::ui {

using namespace detail;

// Moves keep the clip's id (a moved section's clips do too), so a clip present before and after glides. A clip that
// arrives on a track that was already there grows in, and one that leaves from a track that is still there shrinks
// away; a new or deleted track's clips go with its row's own motion. A doc replaced with no clip surviving is a load:
// nothing grows or shrinks. An undo or redo is an edit even when no clip survives it (redoing the delete of the only
// clip's section), so its clips still come and go with motion. A clip that arrives exactly where one left (a repeat
// rewritten with new ids) is the same clip to the eye, so neither of the pair moves.
void TimelineClipLaneArea::glideChangedClips(const ClipLaneChange& change, const std::map<std::int64_t, ClipPlace>& now,
                                             const NoteStartsById& noteStartsNow) {
    const auto layout = getRowLayout();
    const auto rectOf = [&](const ClipPlace& p) {
        return computeClipRect(viewState_, layout, p.row, p.start, p.length).toFloat();
    };
    const auto& tracks = doc_->getTracks();
    std::vector<std::int64_t> arriving;
    int survivors = 0;
    for (const auto& [id, place] : now) {
        const auto was = clipPlaces_.find(id);
        if (was == clipPlaces_.end()) {
            if (change.covers(place.start) && enterExit_->knewTrack(tracks[(size_t)place.row].id))
                arriving.push_back(id);
            continue;
        }
        ++survivors;
        if (was->second == place || !change.covers(place.start))
            continue;
        const auto starts = noteStartsNow.find(id);
        noteGlide_[id] = synth::classifyClipNoteGlide(
            was->second.length, place.length, clipNoteStarts_[id],
            starts != noteStartsNow.end() ? starts->second : std::vector<synth::ClipNoteStart>{});
        clipGlide_->retarget(id, clipGlide_->drawnAt(id, rectOf(was->second)), rectOf(place), true);
    }
    if (change.replaced && !change.restored && survivors == 0)
        return;

    std::vector<ClipLaneEnterExit::Ghost> leaving;
    std::vector<ClipPlace> leftFrom;
    for (const auto& [id, place] : clipPlaces_) {
        if (now.find(id) != now.end() || !change.covers(place.start))
            continue;
        const auto* ghost = enterExit_->remembered(id);
        if (ghost == nullptr || doc_->getTrack(ghost->track) == nullptr)
            continue;
        leaving.push_back(*ghost);
        leaving.back().rect = rectOf(place).toNearestInt();
        leftFrom.push_back(place);
    }
    std::erase_if(arriving, [&](std::int64_t id) {
        const auto& place = now.at(id);
        for (size_t i = 0; i < leftFrom.size(); ++i)
            if (leftFrom[i] == place) {
                leftFrom.erase(leftFrom.begin() + (std::ptrdiff_t)i);
                leaving.erase(leaving.begin() + (std::ptrdiff_t)i);
                return true;
            }
        return false;
    });

    if ((int)arriving.size() <= ClipLaneEnterExit::kMaxEnterExit)
        for (const auto id : arriving) {
            const auto to = rectOf(now.at(id));
            noteGlide_[id] = synth::ClipNoteGlide::Scale; // the notes fill in with the clip
            clipGlide_->retarget(id, to.withWidth(0.0f), to, true);
        }
    if ((int)leaving.size() <= ClipLaneEnterExit::kMaxEnterExit)
        enterExit_->startExits(std::move(leaving));
}

// The leaving clips, drawn by the same paintClip as a live one, scaled toward their centre (or faded, under Reduce
// Motion) where they rested at the last sync. Nothing is drawn once the exit has landed.
void TimelineClipLaneArea::paintClipGhosts(juce::Graphics& g, const TimelineRowLayout& layout) {
    const auto& ghosts = enterExit_->ghosts();
    const float scale = enterExit_->scale();
    const float alpha = enterExit_->alpha();
    if (ghosts.empty() || doc_ == nullptr || scale <= 0.01f || alpha <= 0.01f)
        return;
    for (const auto& ghost : ghosts) {
        const auto* track = doc_->getTrack(ghost.track);
        if (track == nullptr || !ghost.rect.expanded(3).intersects(g.getClipBounds()))
            continue;
        juce::Graphics::ScopedSaveState save(g);
        const auto centre = ghost.rect.toFloat().getCentre();
        g.addTransform(juce::AffineTransform::scale(scale, scale, centre.x, centre.y));
        if (alpha < 1.0f)
            g.beginTransparencyLayer(alpha);
        paintClip(g, ghost.clip, *track, ghost.row, layout);
        if (alpha < 1.0f)
            g.endTransparencyLayer();
    }
}

} // namespace synth::ui
