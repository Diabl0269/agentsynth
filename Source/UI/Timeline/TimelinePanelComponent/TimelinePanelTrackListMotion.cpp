// Concern: TimelinePanelComponent's delete, add and undo motion of the track rows -- the picture taken before a
// removal, which rows a rebuild added or removed, and the overlay (ExitEnterListMotion) that shrinks a removed row
// away, closes the gap, and on undo opens the gap, grows the row back and fades an outline around it. The header and
// its whole lane line are one picture, so they move as one row. docs/layout/animation.md "Delete and undo animation".
#include "AppUndoManager.h"
#include "TimelinePanelComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
juce::String keyFor(synth::TrackId id) { return juce::String(static_cast<juce::int64>(id.value)); }
} // namespace

// Every track's whole slot (clip row plus its automation lanes) down the picture, scrolled as the view is.
std::vector<ExitEnterListRow> TimelinePanelComponent::trackListRows() const {
    std::vector<ExitEnterListRow> rows;
    if (doc_ == nullptr)
        return rows;
    const auto layout = rowLayout();
    const int scroll = static_cast<int>(std::llround(viewState_.trackScrollY));
    int index = 0;
    for (const auto& track : doc_->getTracks()) {
        const auto span = layout.trackSpan(index++);
        rows.push_back(
            {keyFor(track.id), static_cast<float>(span.getStart() - scroll), static_cast<float>(span.getLength())});
    }
    return rows;
}

// The header column and the lanes beside it, below the ruler: what a row is made of.
juce::Rectangle<int> TimelinePanelComponent::trackListMotionArea() const {
    return {trackHeaderBounds_.getX(), gridLanesBounds_.getY(), lanesBounds_.getRight() - trackHeaderBounds_.getX(),
            gridLanesBounds_.getHeight()};
}

void TimelinePanelComponent::noteDuplicateOrigin(synth::TrackId copy, synth::TrackId source) {
    duplicateOrigins_[keyFor(copy)] = keyFor(source);
}

void TimelinePanelComponent::noteTracksLeaving() {
    trackListMotion_.finishNow();
    pendingTrackPicture_.reset();
    trackRowsAdded_.clear();
    trackRowsRemoved_.clear();
    if (doc_ == nullptr || !canAnimateTrackGlide() || clipEditorOpen() || trackListMotionArea().isEmpty())
        return;
    TrackListPicture picture;
    picture.rows = trackListRows();
    picture.image = ExitEnterListMotion::pictureOf(*this, trackListMotionArea(), picture.scale);
    pendingTrackPicture_ = std::move(picture);
}

// Called by the rebuild when the set of tracks changed: which rows went and which came back. Only a delete (a picture
// is waiting), an undo/redo, or an add or duplicate (armTrackInsertMotion) is animated; a track added any other way
// lands at once.
void TimelinePanelComponent::noteTrackSetChange(const std::vector<synth::Track>& tracks) {
    trackListMotion_.finishNow();
    if (!trackInsertArmed_) { // an armed insert collects the rows of every rebuild it causes (one per copy)
        trackRowsAdded_.clear();
        trackRowsRemoved_.clear();
    }
    trackSetRestored_ = undoManager_ != nullptr && undoManager_->isRestoring();
    if (!trackSetRestored_ && doc_ != nullptr && originsReplaceSeen_ != doc_->getReplaceCount()) {
        originsReplaceSeen_ = doc_->getReplaceCount(); // a project was loaded: its ids mean nothing to the old ones
        duplicateOrigins_.clear();
    }
    if (!trackSetRestored_) { // a track the user removes is no copy any more: its undo grows it back where it stood
        const auto stillThere = [&tracks](const juce::String& key) {
            return std::any_of(tracks.begin(), tracks.end(),
                               [&key](const synth::Track& t) { return keyFor(t.id) == key; });
        };
        for (auto it = duplicateOrigins_.begin(); it != duplicateOrigins_.end();)
            it = stillThere(it->first) ? std::next(it) : duplicateOrigins_.erase(it);
    }
    if (!pendingTrackPicture_ && !trackInsertArmed_ && !trackSetRestored_)
        return;
    for (const auto& track : tracks) {
        const bool had = std::any_of(trackHeaderList_.headers.begin(), trackHeaderList_.headers.end(),
                                     [&track](const auto* h) { return h->getTrackId() == track.id; });
        if (!had)
            trackRowsAdded_.push_back(keyFor(track.id));
    }
    for (const auto* header : trackHeaderList_.headers) {
        const auto id = header->getTrackId();
        const bool stays =
            std::any_of(tracks.begin(), tracks.end(), [id](const synth::Track& t) { return t.id == id; });
        if (!stays && !header->isSectionHeader())
            trackRowsRemoved_.push_back(keyFor(id));
    }
}

void TimelinePanelComponent::finishTrackListChange() {
    auto pending = std::exchange(pendingTrackPicture_, std::nullopt);
    const auto added = std::exchange(trackRowsAdded_, {});
    const auto removed = std::exchange(trackRowsRemoved_, {});
    trackInsertArmed_ = false;
    if (doc_ == nullptr || !canAnimateTrackGlide() || clipEditorOpen() || added.empty() == removed.empty())
        return; // nothing came or went, or both did (lands at once)

    const auto area = trackListMotionArea();
    if (area.isEmpty())
        return;
    const auto& colours = synth::theme::themeOf(*this).colors;
    ExitEnterListMotion::Job job;
    job.axis = ListAxis::Vertical;
    job.area = area;
    job.background = colours.bg0;
    job.accent = colours.accent;
    const auto after = trackListRows();
    const float extent = static_cast<float>(area.getHeight());
    if (!removed.empty()) {
        if (!pending)
            return;
        job.picture = pending->image;
        job.pictureScale = pending->scale;
        if (!trackSetRestored_ ||
            !ExitEnterListPlan::forRemovalIntoSource(pending->rows, after, duplicateOrigins_, extent, job.items))
            job.items = ExitEnterListPlan::forRemoval(pending->rows, after, extent);
    } else {
        job.picture = ExitEnterListMotion::pictureOf(*this, area, job.pictureScale);
        if (!ExitEnterListPlan::forInsertionFromSource(after, added, duplicateOrigins_, extent, job.items))
            job.items = ExitEnterListPlan::forInsertion(after, added, extent);
    }
    trackListMotion_.start(std::move(job));
}

} // namespace synth::ui
