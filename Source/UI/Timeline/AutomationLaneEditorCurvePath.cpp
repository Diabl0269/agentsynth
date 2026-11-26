// AutomationLaneEditor -- the curve's path: the kernel real playback uses, sampled every two pixels and at the exact
// place of every point, so a step narrower than a sample (two points a tick apart, a Hold at its right point, a Jump at
// its left one) is drawn as a true vertical step instead of a ramp across the sample; while points come or go, the line
// drawn gliding between its old and new shape. And the curve's shape per segment (Hold, Jump or Linear): a right-click
// on the curve between two points picks the shape of that segment; inside a selected stretch (the Range tool's
// selection, else the selected points' span) it picks the shape of every segment of the stretch, so the items act on
// what is selected, as the Create custom LFO item below them does. The change melts the old curve into the new one
// and is one undo step. The class is declared in AutomationLaneEditor.h.

#include "AppUndoManager.h"
#include "AutomationLaneEditor.h"
#include "Timeline/AutomationKernel.h"
#include "UI/Layout/PointEditing/LineMorph.h"
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
line_morph::Line AutomationLaneEditor::curveLine(const std::vector<LaneBreakpoint>& points,
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

    line_morph::Line line;
    line.reserve(stops.size() + points.size());
    AutomationCursor cursor{};
    const double flat = dragMode_ == DragMode::LaneConstant ? previewValue_ : (double)lane.range.defaultValue;
    const auto yAt = [&](double beat) {
        const double value = pts.empty() ? flat
                                         : AutomationKernel::evaluate(pts.data(), (int)pts.size(), beat,
                                                                      (double)lane.range.defaultValue, cursor);
        return (float)valueToY(value);
    };
    for (const auto& stop : stops) {
        if (stop.point)
            line.push_back({(float)stop.x, yAt(stop.beat - kJustBeforePointBeats)});
        line.push_back({(float)stop.x, yAt(stop.beat)});
    }
    return line;
}

namespace {
juce::Path pathThrough(const line_morph::Line& line) {
    juce::Path path;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (i == 0)
            path.startNewSubPath(line[i]);
        else
            path.lineTo(line[i]);
    }
    return path;
}
} // namespace

juce::Path AutomationLaneEditor::curvePath(const std::vector<LaneBreakpoint>& points,
                                           const synth::AutomationLane& lane) const {
    return pathThrough(curveLine(points, lane));
}

// The resting points eased by any key or restore tween, blended with the line a glide of points coming or going
// started from, by the glide's weight (line_morph::blendAcrossSteps: a lane's line is a level per x with jumps).
line_morph::Line AutomationLaneEditor::lineDrawnFor(const std::vector<LaneBreakpoint>& points) const {
    const auto* lane = doc_ != nullptr ? doc_->getLane(laneId_) : nullptr;
    if (lane == nullptr)
        return {};
    auto now = curveLine(pointTween_.isAnimating() ? tweenedPoints(points) : points, *lane);
    const float w = glide_.lineWeight();
    return !morphFrom_.empty() && w > 0.0f ? line_morph::blendAcrossSteps(morphFrom_, now, w) : now;
}

std::vector<juce::Point<float>> AutomationLaneEditor::getDrawnLineForTest() const {
    const auto* points = lanePoints();
    return points != nullptr ? lineDrawnFor(*points) : std::vector<juce::Point<float>>{};
}

juce::Path AutomationLaneEditor::getDrawnCurvePathForTest() const { return pathThrough(getDrawnLineForTest()); }

// A delete, add, paste, undo or redo re-forms the curve: the line glides from the shape it was drawn in to the new
// one. Under Reduce Motion (or with no line to start from) the old shape fades out as the new one fades in.
void AutomationLaneEditor::paintGlidingCurve(juce::Graphics& g, const std::vector<LaneBreakpoint>& now,
                                             const synth::AutomationLane& lane, juce::Colour colour) {
    if (glide_.fadesOnly() || morphFrom_.empty()) {
        strokeCurve(g, glide_.before(), lane, colour.withMultipliedAlpha(1.0f - glide_.amount()), 1.5f);
        strokeCurve(g, now, lane, colour.withMultipliedAlpha(glide_.amount()), 1.5f);
        return;
    }
    g.setColour(colour);
    g.strokePath(pathThrough(lineDrawnFor(now)), juce::PathStrokeType(1.5f));
}

namespace {
constexpr int kHoldCurve = static_cast<int>(synth::BreakpointCurve::Hold);
constexpr int kJumpCurve = static_cast<int>(synth::BreakpointCurve::Jump);
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
    menu.addItem("Jump", true, allAre(kJumpCurve), [this, lefts] { applySegmentCurves(lefts, kJumpCurve); });
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
