// AutomationLaneEditor -- the curve's path: the kernel real playback uses, sampled every two pixels and at the exact
// place of every point, so a step narrower than a sample (two points a tick apart, a Hold) is drawn as a true
// vertical step instead of a ramp across the sample. And the curve's shape per segment (Hold or Linear): a right-click
// on the curve between two points picks the shape of that segment; inside a selected stretch (the Range tool's
// selection, else the selected points' span) it picks the shape of every segment of the stretch, so the items act on
// what is selected, as the Create custom LFO item below them does. The change melts the old curve into the new one
// and is one undo step. The class is declared in AutomationLaneEditor.h.

#include "AppUndoManager.h"
#include "AutomationLaneEditor.h"
#include "Timeline/AutomationKernel.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kCurveSamplePx = 2;
// A point's value is read here just before its beat as well as at it: far below the tick a duplicate leaves between
// two points, and far above a double's rounding at any beat a song reaches.
constexpr double kJustBeforePointBeats = 1e-7;

struct CurveStop {
    double x = 0.0;
    double beat = 0.0;
    bool point = false;
};
} // namespace

// Every sample and every point inside the width, in x order. A point adds two vertices at its x, the value just before
// it and its own, so the segment arriving at it and the one leaving it meet at its exact place.
juce::Path AutomationLaneEditor::curvePath(const std::vector<LaneBreakpoint>& points,
                                           const synth::AutomationLane& lane) const {
    std::vector<TimelineSnapshot::Point> pts;
    pts.reserve(points.size());
    for (const auto& bp : points)
        pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});

    const int width = getWidth();
    std::vector<CurveStop> stops;
    stops.reserve((std::size_t)(width / kCurveSamplePx + 1) + points.size());
    for (int x = 0; x < width; x += kCurveSamplePx)
        stops.push_back({(double)x, viewState_.xToBeat((double)x), false});
    for (const auto& bp : points) {
        const double x = viewState_.beatToX(bp.beat);
        if (x > 0.0 && x < (double)width)
            stops.push_back({x, bp.beat, true});
    }
    // A point on a sample's column goes first: its value just before, then the value the sample reads as well.
    std::stable_sort(stops.begin(), stops.end(), [](const CurveStop& a, const CurveStop& b) {
        return a.x < b.x || (a.x == b.x && a.point && !b.point);
    });

    juce::Path path;
    AutomationCursor cursor{};
    const double flat = dragMode_ == DragMode::LaneConstant ? previewValue_ : (double)lane.range.defaultValue;
    const auto yAt = [&](double beat) {
        const double value = pts.empty() ? flat
                                         : AutomationKernel::evaluate(pts.data(), (int)pts.size(), beat,
                                                                      (double)lane.range.defaultValue, cursor);
        return (float)valueToY(value);
    };
    // Path::isEmpty stays true across a flat start, so the first vertex is tracked here.
    bool started = false;
    const auto add = [&path, &started](float x, float y) {
        if (started)
            path.lineTo(x, y);
        else
            path.startNewSubPath(x, y);
        started = true;
    };
    for (const auto& stop : stops) {
        if (stop.point)
            add((float)stop.x, yAt(stop.beat - kJustBeforePointBeats));
        add((float)stop.x, yAt(stop.beat));
    }
    return path;
}

namespace {
constexpr int kHoldCurve = static_cast<int>(synth::BreakpointCurve::Hold);
constexpr int kLinearCurve = static_cast<int>(synth::BreakpointCurve::Linear);
} // namespace

std::optional<std::pair<double, double>> AutomationLaneEditor::selectedStretch() const {
    const auto* range = shapeGesture_.getLaneRange();
    if (range != nullptr && range->hasWidth() && shapeGesture_.rangeIsOnThisLane())
        return std::make_pair(range->getStartBeat(), range->getEndBeat());
    return pointSelectionSpan();
}

// A segment belongs to the stretch when any part of it lies inside: its left point is before the stretch's end and
// its right point after its start.
std::vector<double> AutomationLaneEditor::segmentLeftBeatsIn(double startBeat, double endBeat) const {
    std::vector<double> lefts;
    const auto* points = lanePoints();
    if (points == nullptr)
        return lefts;
    for (std::size_t i = 0; i + 1 < points->size(); ++i)
        if ((*points)[i].beat < endBeat && (*points)[i + 1].beat > startBeat)
            lefts.push_back((*points)[i].beat);
    return lefts;
}

// The header says what the items act on; an item is ticked only when every segment it would change already has that
// shape, so a stretch of mixed shapes ticks neither.
void AutomationLaneEditor::showSegmentContextMenu(int leftIndex, juce::Point<int> pos) {
    const auto* points = lanePoints();
    if (points == nullptr || leftIndex < 0 || (std::size_t)leftIndex + 1 >= points->size())
        return;

    std::vector<double> lefts{(*points)[(std::size_t)leftIndex].beat};
    bool wholeStretch = false;
    if (const auto stretch = isInSelectedRange(pos) ? selectedStretch() : std::nullopt) {
        if (auto inStretch = segmentLeftBeatsIn(stretch->first, stretch->second); !inStretch.empty()) {
            lefts = std::move(inStretch);
            wholeStretch = true;
        }
    }
    const auto allAre = [points, &lefts](int curve) {
        return std::all_of(lefts.begin(), lefts.end(), [points, curve](double beat) {
            return std::any_of(points->begin(), points->end(),
                               [beat, curve](const LaneBreakpoint& p) { return p.beat == beat && p.curve == curve; });
        });
    };

    juce::PopupMenu menu;
    menu.addSectionHeader(wholeStretch ? kSelectionCurveHeader : kSegmentCurveHeader);
    menu.addItem("Hold", true, allAre(kHoldCurve), [this, lefts] { applySegmentCurves(lefts, kHoldCurve); });
    menu.addItem("Linear", true, allAre(kLinearCurve), [this, lefts] { applySegmentCurves(lefts, kLinearCurve); });
    showContextMenu(menu, pos);
}

void AutomationLaneEditor::applySegmentCurveChoice(double leftBeat, int curve) {
    applySegmentCurves({leftBeat}, curve);
}

// Only the points whose shape differs change; nothing differing is no edit and no undo step. The melt covers from the
// first changed segment's left point to the last one's right point.
void AutomationLaneEditor::applySegmentCurves(const std::vector<double>& leftBeats, int curve) {
    const auto* points = lanePoints();
    if (points == nullptr || doc_ == nullptr)
        return;
    std::vector<LaneBreakpoint> changed;
    double meltEnd = 0.0;
    for (std::size_t i = 0; i < points->size(); ++i) {
        const auto& p = (*points)[i];
        if (p.curve == curve || std::find(leftBeats.begin(), leftBeats.end(), p.beat) == leftBeats.end())
            continue;
        changed.push_back(p);
        meltEnd = i + 1 < points->size() ? (*points)[i + 1].beat : p.beat;
    }
    if (changed.empty())
        return;

    meltRangeNext(changed.front().beat, meltEnd);
    const auto laneId = laneId_;
    auto mutate = [this, laneId, changed, curve] {
        for (const auto& p : changed)
            doc_->addBreakpoint(laneId, p.beat, p.value, p.tension, curve);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    repaint();
}

} // namespace synth::ui
