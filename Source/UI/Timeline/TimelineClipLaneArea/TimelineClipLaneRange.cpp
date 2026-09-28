// TimelineClipLaneRange.cpp
//
// The Range tool (EditTool::Range): the time-range drag and its Shift-extension, the range's own
// painting, and the range verbs (split at both edges, delete, delete and close the gap).
// TimelineClipLaneArea is declared in TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp files
// in this directory hold the rest of the class.
//
// A range is a beat span across a run of track rows (synth::ui::RangeSelectionModel), deliberately
// independent of the clip selection: it covers parts of clips and empty space alike. The two are
// kept mutually exclusive — making a range clears the clip selection, and the range itself only
// lives while the Range tool is active (setActiveTool clears it) — so Copy/Cut/Delete are never
// ambiguous about which of the two they act on.
//
// Every doc edit here is composed per gesture inside ONE AppUndoManager::recordTimelineChange, and
// passes the doc the transport's tempo (secondsPerBeat) because the doc has none of its own: it is
// what lets an audio clip cut from the left keep playing the audio that was under it.

#include "TimelineClipLaneArea.h"
#include "TimelineClipLaneInternal.h"

#include "AppUndoManager.h"
#include "Transport/TransportService.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

using namespace detail;

namespace {
// How far either side of the range's edge lines a repaint reaches — the lines are 1.5 px and
// antialiased, so an exact-rect repaint would leave a fringe behind when an edge moves.
constexpr int kRangeRepaintMarginPx = 2;

juce::Rectangle<int> unionOf(juce::Rectangle<int> a, juce::Rectangle<int> b) {
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;
    return a.getUnion(b);
}
} // namespace

//==============================================================================
// ---- Geometry ----

// Rows are CLAMPED rather than rejected: dragging above the first row or below the last keeps the
// range's edge on the outermost row, the way a text selection keeps extending past the last line.
// The beat is snapped to the grid (never below 0) so both edges always land where a split can.
std::optional<std::pair<synth::TrackId, double>> TimelineClipLaneArea::rangePointAt(juce::Point<int> pos) const {
    if (doc_ == nullptr || doc_->getTracks().empty())
        return std::nullopt;
    if (getRowHeight() <= 0)
        return std::nullopt;
    // Through TrackRowLayout, like every other row question: an automation lane row counts as its
    // parent track, and y past either end extrapolates, which the clamp below folds back in.
    const int contentY = pos.y + (int)std::llround(viewState_.trackScrollY);
    const int lastRow = (int)doc_->getTracks().size() - 1;
    const int row = std::clamp(getRowLayout().trackIndexForDrag(contentY), 0, lastRow);
    const double beat = std::max(0.0, snappedBeatAt(viewState_.xToBeat((double)pos.x)));
    return std::make_pair(doc_->getTracks()[(std::size_t)row].id, beat);
}

juce::Rectangle<int> TimelineClipLaneArea::rangeRect() const {
    if (doc_ == nullptr || !range_.hasWidth())
        return {};
    const auto rows = range_.coveredRows(*doc_);
    if (!rows)
        return {};
    const int top = rowBounds(rows->first).getY();
    const int bottom = rowBounds(rows->second).getBottom();
    const int left = (int)std::llround(viewState_.beatToX(range_.getStartBeat()));
    const int right = (int)std::llround(viewState_.beatToX(range_.getEndBeat()));
    return {left, top, std::max(1, right - left), bottom - top};
}

std::optional<TimelineClipLaneArea::RangeSpan> TimelineClipLaneArea::getRangeSpan() const {
    if (doc_ == nullptr || !range_.hasWidth())
        return std::nullopt;
    auto tracks = range_.coveredTracks(*doc_);
    if (tracks.empty())
        return std::nullopt;
    return RangeSpan{std::move(tracks), range_.getStartBeat(), range_.getEndBeat()};
}

// Read from the transport at use time (a tempo change between two range edits must be honoured),
// with the same 120 bpm fallback the waveform painter uses when there is no transport at all.
double TimelineClipLaneArea::secondsPerBeat() const {
    double bpm = kFallbackBpm;
    if (transport_ != nullptr) {
        const double live = transport_->getPositionSnapshot().bpm;
        if (std::isfinite(live) && live > 0.0)
            bpm = live;
    }
    return 60.0 / bpm;
}

//==============================================================================
// ---- Setting / clearing ----

void TimelineClipLaneArea::setRange(synth::TrackId anchorTrack, double anchorBeat, synth::TrackId extentTrack,
                                    double extentBeat) {
    const auto before = rangeRect();
    range_.begin(anchorTrack, std::max(0.0, anchorBeat));
    range_.extendTo(extentTrack, std::max(0.0, extentBeat));
    if (!selection_.isEmpty()) {
        selection_.clear();
        repaint(); // the deselected clips' highlight is everywhere, not just under the range
        return;
    }
    requestToolPreviewRepaint(unionOf(before, rangeRect()).expanded(kRangeRepaintMarginPx, 0));
}

void TimelineClipLaneArea::clearRange() {
    if (!range_.isActive())
        return;
    const auto before = rangeRect();
    range_.clear();
    if (dragMode_ == DragMode::Range)
        dragMode_ = DragMode::None;
    if (!before.isEmpty())
        requestToolPreviewRepaint(before.expanded(kRangeRepaintMarginPx, 0));
}

//==============================================================================
// ---- The drag ----

// Shift extends a live range from its ANCHOR (the corner the original press landed on), so the
// far edge stays put while the user reaches for the near one; otherwise the press starts a fresh
// range. Either way the clip selection is dropped — see the file comment for why the two never
// coexist.
void TimelineClipLaneArea::beginRangeGesture(const juce::MouseEvent& e) {
    const auto point = rangePointAt(e.getPosition());
    if (!point)
        return;
    if (e.mods.isShiftDown() && range_.isActive())
        setRange(range_.getAnchorTrack(), range_.getAnchorBeat(), point->first, point->second);
    else
        setRange(point->first, point->second, point->first, point->second);
    dragMode_ = DragMode::Range;
}

// The (track, snapped beat) extent IS the repaint gate: pointer movement inside one snap cell and
// one row repaints nothing, and a change repaints only the union of the old and new rects.
void TimelineClipLaneArea::updateRangeGesture(juce::Point<int> pos) {
    const auto point = rangePointAt(pos);
    if (!point)
        return;
    if (point->first == range_.getExtentTrack() && std::abs(point->second - range_.getExtentBeat()) < 1e-9)
        return;
    const auto before = rangeRect();
    range_.extendTo(point->first, point->second);
    requestToolPreviewRepaint(unionOf(before, rangeRect()).expanded(kRangeRepaintMarginPx, 0));
}

// A press that never gained width is a click, and a click with the Range tool on empty ground is
// how a range is dismissed — so a zero-width range never outlives its own release.
void TimelineClipLaneArea::endRangeGesture() {
    dragMode_ = DragMode::None;
    if (!range_.hasWidth())
        clearRange();
}

//==============================================================================
// ---- Painting ----

// Deliberately NOT the marquee's recipe (accent fill + accent border all round): the range washes
// its whole rows in the text colour and marks only its two time edges in the accent. A marquee
// selects objects and is gone on release; a range is a span of TIME that stays, and the two must
// read differently at a glance when both tools are in use.
void TimelineClipLaneArea::paintRange(juce::Graphics& g) {
    const auto rect = rangeRect();
    if (rect.isEmpty())
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour wash = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    const juce::Colour edge = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

    const auto rectF = rect.toFloat();
    g.setColour(wash.withAlpha(0.16f));
    g.fillRect(rectF);
    g.setColour(edge.withAlpha(0.9f));
    g.drawLine(rectF.getX(), rectF.getY(), rectF.getX(), rectF.getBottom(), 1.5f);
    g.drawLine(rectF.getRight(), rectF.getY(), rectF.getRight(), rectF.getBottom(), 1.5f);
}

//==============================================================================
// ---- The range verbs ----

// One recordTimelineChange for the whole verb, however many clips it splits, trims, removes or
// moves across however many tracks. Whether anything changed is read off the doc's revision, so a
// range over empty time (nothing to split or delete) reports false and — because the mutation was a
// no-op — leaves no undo entry either. The range itself stays, as in Cubase: the next verb (a
// paste, a second delete) acts on the same span.
bool TimelineClipLaneArea::applyRangeChoice(RangeChoice choice) {
    const auto span = getRangeSpan();
    if (!span)
        return false;
    const double spb = secondsPerBeat();
    const auto revisionBefore = doc_->getRevision();

    auto mutate = [this, &span, choice, spb] {
        switch (choice) {
        case RangeChoice::SplitAtEdges:
            doc_->splitClipsAtRangeEdges(span->tracks, span->startBeat, span->endBeat, spb);
            break;
        case RangeChoice::Delete:
            doc_->deleteRange(span->tracks, span->startBeat, span->endBeat, false, spb);
            break;
        case RangeChoice::DeleteCloseGap:
            doc_->deleteRange(span->tracks, span->startBeat, span->endBeat, true, spb);
            break;
        }
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    repaint();
    return doc_->getRevision() != revisionBefore;
}

// Protected virtual for the display-less-runner reason spelled out on showClipContextMenu: a real
// menu must never be reached from a test. Every outcome is driven through applyRangeChoice.
void TimelineClipLaneArea::showRangeContextMenu(juce::Point<int> localPos) {
    juce::ignoreUnused(localPos);
    if (!getRangeSpan())
        return;
    juce::PopupMenu menu;
    menu.addItem("Split at range edges", [this] { applyRangeChoice(RangeChoice::SplitAtEdges); });
    menu.addItem("Delete range", [this] { applyRangeChoice(RangeChoice::Delete); });
    menu.addItem("Delete range and close gap", [this] { applyRangeChoice(RangeChoice::DeleteCloseGap); });
    menu.showMenuAsync(juce::PopupMenu::Options());
}

} // namespace synth::ui
