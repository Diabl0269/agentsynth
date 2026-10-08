// Concern: TimelinePanelComponent's delete and undo motion of the track rows -- the picture taken before a removal,
// which rows a rebuild added or removed, and the overlay (ExitEnterListMotion) that shrinks a removed row away, closes
// the gap, and on undo opens the gap, grows the row back and fades an outline around it. The header and its whole lane
// line are one picture, so they move as one row. docs/layout/animation.md "Delete and undo animation".
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

void TimelinePanelComponent::noteTracksLeaving() {
    trackListMotion_.finishNow();
    pendingTrackPicture_.reset();
    trackRowsAdded_.clear();
    trackRowsRemoved_.clear();
    if (doc_ == nullptr || !canAnimateTrackGlide() || pianoRoll_.isOpen() || trackListMotionArea().isEmpty())
        return;
    TrackListPicture picture;
    picture.rows = trackListRows();
    picture.image = ExitEnterListMotion::pictureOf(*this, trackListMotionArea(), picture.scale);
    pendingTrackPicture_ = std::move(picture);
}

// Called by the rebuild when the set of tracks changed: which rows went and which came back. Only a delete (a picture
// is waiting) or an undo/redo is animated; a track added any other way lands at once.
void TimelinePanelComponent::noteTrackSetChange(const std::vector<synth::Track>& tracks) {
    trackListMotion_.finishNow();
    trackRowsAdded_.clear();
    trackRowsRemoved_.clear();
    if (!pendingTrackPicture_ && (undoManager_ == nullptr || !undoManager_->isRestoring()))
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
    if (doc_ == nullptr || !canAnimateTrackGlide() || pianoRoll_.isOpen() || added.empty() == removed.empty())
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
        job.items = ExitEnterListPlan::forRemoval(pending->rows, after, extent);
    } else {
        job.picture = ExitEnterListMotion::pictureOf(*this, area, job.pictureScale);
        job.items = ExitEnterListPlan::forInsertion(after, added, extent);
    }
    trackListMotion_.start(std::move(job));
}

} // namespace synth::ui
