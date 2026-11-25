#include "AutomationLaneShapeGesture.h"

#include "AppUndoManager.h"
#include "LaneShapeGenerator.h"
#include "Timeline/AutomationKernel.h"
#include "UI/ModuleViews/ShapeEditor/CycleCountStepper.h"
#include "UI/ModuleViews/ShapeEditor/ShapeDrawStroke.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

// Concern: the Draw tool's box stamp, the Range tool's lane-range drag and the lane-range verbs of ONE
// automation lane editor. The editor forwards here before its own tools, so a gesture this class takes
// never reaches them.

namespace {

constexpr double kBeatEpsilon = 1e-9;
constexpr int kStatusShowMs = 2500;
constexpr int kLinear = static_cast<int>(synth::BreakpointCurve::Linear);

struct Palette {
    juce::Colour accent, wash, error, chipBg, chipText;
};

Palette paletteFor(const juce::Component& c) {
    const auto& t = synth::theme::themeOf(c).colors;
    return {t.accent, t.textPrimary, t.error, t.bg1, t.textPrimary};
}

// "4", "2.5", "0.25": up to two decimals, trailing zeros dropped.
juce::String formatAmount(double v) {
    if (std::abs(v - std::round(v)) < 1e-6)
        return juce::String((long long)std::llround(v));
    return juce::String(v, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
}

juce::String plural(double v, const char* one, const char* many) {
    return formatAmount(v) + " " + (std::abs(v - 1.0) < 1e-6 ? one : many);
}

} // namespace

AutomationLaneShapeGesture::AutomationLaneShapeGesture(AutomationLaneEditor& editor, TimelineViewState& viewState)
    : editor_(editor)
    , viewState_(viewState)
    , stepper_(std::make_unique<CycleCountStepper>()) {}

AutomationLaneShapeGesture::~AutomationLaneShapeGesture() { stopTimer(); }

//==============================================================================
// ---- Geometry helpers ----

double AutomationLaneShapeGesture::beatsPerBar() const { return beatsPerBarFor(editor_.getTransport()); }

double AutomationLaneShapeGesture::snappedBeatAt(int x) const {
    return std::max(0.0, viewState_.snapBeat(viewState_.xToBeat((double)x), beatsPerBar()));
}

double AutomationLaneShapeGesture::clampValue(double value) const {
    if (auto* doc = editor_.getTimelineDoc())
        if (const auto* lane = doc->getLane(editor_.getActiveLane()))
            return juce::jlimit((double)lane->range.minValue, (double)lane->range.maxValue, value);
    return value;
}

// The grid sets the cycles while Snap is on (one per snap step, so the shape follows the grid the person is already
// working on); with Snap off the count is the person's.
bool AutomationLaneShapeGesture::countApplies() const { return viewState_.divisionBeats(beatsPerBar()) <= 0.0; }

// Points the lane keeps when [startBeat, endBeat] is replaced.
long long AutomationLaneShapeGesture::pointsKeptOutside(double startBeat, double endBeat) const {
    const auto* doc = editor_.getTimelineDoc();
    const auto* lane = doc != nullptr ? doc->getLane(editor_.getActiveLane()) : nullptr;
    long long kept = 0;
    if (lane != nullptr)
        for (const auto& bp : lane->points)
            if (bp.beat < startBeat - kBeatEpsilon || bp.beat > endBeat + kBeatEpsilon)
                ++kept;
    return kept;
}

// The most whole cycles the shape stamps over the span without passing the lane's point cap (1 when not even one
// fits: the stamp is then refused with its reason).
int AutomationLaneShapeGesture::maxCycles(double startBeat, double endBeat) const {
    const double span = endBeat - startBeat;
    if (span <= kBeatEpsilon)
        return 1;
    const long long kept = pointsKeptOutside(startBeat, endBeat);
    const auto fits = [&](int n) {
        return kept + estimateShapePointCount(shape_, startBeat, endBeat, span / (double)n) <=
               synth::TimelineDoc::kMaxBreakpointsPerLane;
    };
    int lo = 1, hi = kMaxDrawCycles;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (fits(mid))
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

// A snap step while Snap is on, else the remembered width (a beat until set), widened when the span would take more
// cycles than fit: what is asked for is what is drawn.
double AutomationLaneShapeGesture::cycleBeatsFor(double startBeat, double endBeat) const {
    const double division = viewState_.divisionBeats(beatsPerBar());
    if (division > 0.0)
        return division;
    const double width = cycleWidthBeats_.value_or(1.0);
    const double span = endBeat - startBeat;
    return span > kBeatEpsilon ? std::max(width, span / (double)maxCycles(startBeat, endBeat)) : width;
}

bool AutomationLaneShapeGesture::cycleCountClamped(double startBeat, double endBeat) const {
    if (!countApplies())
        return false;
    const double span = endBeat - startBeat;
    return span > kBeatEpsilon && span / cycleWidthBeats_.value_or(1.0) > (double)maxCycles(startBeat, endBeat) + 1e-6;
}

// The owner check matters on a modulator band, whose private doc reuses the real docs' lane ids.
bool AutomationLaneShapeGesture::rangeIsOnThisLane() const {
    return laneRange_ != nullptr && laneRange_->isActive() && laneRange_->getOwner() == &editor_ &&
           laneRange_->getLane() == editor_.getActiveLane();
}

juce::Rectangle<float> AutomationLaneShapeGesture::boxRect() const {
    const float x0 = (float)viewState_.beatToX(std::min(boxStartBeat_, boxEndBeat_));
    const float x1 = (float)viewState_.beatToX(std::max(boxStartBeat_, boxEndBeat_));
    const float y0 = (float)editor_.valueToY(std::max(boxStartValue_, boxEndValue_));
    const float y1 = (float)editor_.valueToY(std::min(boxStartValue_, boxEndValue_));
    return {x0, y0, x1 - x0, y1 - y0};
}

//==============================================================================
// ---- Mouse and keys ----

// Any press on a lane drops an existing lane range ("a click elsewhere clears it") unless it is the
// Range tool starting the next one. Only a FOLLOWED edit tool reaches the shapes: a tool picked directly
// with setTool() keeps its old meaning.
bool AutomationLaneShapeGesture::mouseDown(const juce::MouseEvent& e, std::optional<EditTool> followedTool) {
    mode_ = Mode::None;
    auto* doc = editor_.getTimelineDoc();
    if (doc == nullptr || doc->getLane(editor_.getActiveLane()) == nullptr)
        return false;

    if (followedTool == EditTool::Range && laneRange_ != nullptr) {
        mode_ = Mode::Range;
        laneRange_->begin(editor_.getActiveLane(), snappedBeatAt(e.getPosition().x), &editor_);
        return true;
    }
    if (laneRange_ != nullptr)
        laneRange_->clear();

    if (followedTool == EditTool::Draw && isPeriodicShape(shape_)) {
        mode_ = Mode::Box;
        statusText_.clear();
        noteText_.clear();
        stepper_->reset();
        gridHint_ = atLimit_ = false;
        boxStartBeat_ = boxEndBeat_ = snappedBeatAt(e.getPosition().x);
        boxStartValue_ = boxEndValue_ = clampValue(editor_.yToValue(e.getPosition().y));
        editor_.repaint();
        return true;
    }
    return false;
}

bool AutomationLaneShapeGesture::mouseDrag(const juce::MouseEvent& e) {
    switch (mode_) {
    case Mode::Range:
        laneRange_->extendTo(snappedBeatAt(e.getPosition().x));
        return true;
    case Mode::Box:
        if (e.mods.isAltDown()) {
            dragCount(e); // Option: the box stays, up/down is the count
            editor_.repaint();
            return true;
        }
        stepper_->reset();
        gridHint_ = atLimit_ = false;
        boxEndBeat_ = snappedBeatAt(e.getPosition().x);
        boxEndValue_ = clampValue(editor_.yToValue(e.getPosition().y));
        editor_.repaint();
        return true;
    case Mode::None:
        break;
    }
    return false;
}

// A Range press that never gained width was a click, which is how a lane range is dismissed.
bool AutomationLaneShapeGesture::mouseUp(const juce::MouseEvent&) {
    const auto mode = mode_;
    mode_ = Mode::None;
    stepper_->reset();
    switch (mode) {
    case Mode::Range:
        if (!laneRange_->hasWidth())
            laneRange_->clear();
        return true;
    case Mode::Box: {
        const double start = std::min(boxStartBeat_, boxEndBeat_);
        const double end = std::max(boxStartBeat_, boxEndBeat_);
        if (end - start > kBeatEpsilon)
            commitStamp(shape_, start, end, std::min(boxStartValue_, boxEndValue_),
                        std::max(boxStartValue_, boxEndValue_));
        editor_.repaint();
        return true;
    }
    case Mode::None:
        break;
    }
    return false;
}

// A KeyPress compares its modifiers too, so only the bare bracket keys step (Cmd+[ and Ctrl+[ stay with the app).
bool AutomationLaneShapeGesture::keyPressed(const juce::KeyPress& key, std::optional<EditTool> followedTool) {
    const bool bracket = key == juce::KeyPress('[') || key == juce::KeyPress(']');
    if (bracket && followedTool == EditTool::Draw && isPeriodicShape(shape_) &&
        dynamic_cast<juce::TextInputTarget*>(juce::Component::getCurrentlyFocusedComponent()) == nullptr)
        return stepCount(key == juce::KeyPress(']') ? 1 : -1);
    if (key != juce::KeyPress::escapeKey || mode_ == Mode::None)
        return false;
    if (mode_ == Mode::Range && laneRange_ != nullptr)
        laneRange_->clear();
    cancel();
    return true;
}

//==============================================================================
// ---- The cycle count ----

// Remembers `cycles` across `span` beats as a cycle width.
void AutomationLaneShapeGesture::setCycles(double span, int cycles, bool over) {
    atLimit_ = over;
    cycleWidthBeats_ = span / (double)cycles;
}

// Option held: the dragged box is frozen and the pointer's height off where Option went down counts the cycles.
void AutomationLaneShapeGesture::dragCount(const juce::MouseEvent& e) {
    if (!countApplies()) {
        gridHint_ = true;
        return;
    }
    const double start = std::min(boxStartBeat_, boxEndBeat_);
    const double end = std::max(boxStartBeat_, boxEndBeat_);
    const double span = end - start;
    if (span <= kBeatEpsilon)
        return;
    const int most = maxCycles(start, end);
    if (!stepper_->held())
        stepper_->begin(e.position.y, (int)std::lround(span / cycleBeatsFor(start, end)), most);
    if (const auto count = stepper_->dragTo(e.position.y, most))
        setCycles(span, count->cycles, count->over);
}

// `[` (delta -1) and `]` (+1): one more or fewer whole cycle in the box being dragged, else per bar for the next box.
// Taken, saying why, even when Snap sets the count instead.
bool AutomationLaneShapeGesture::stepCount(int delta) {
    if (!countApplies()) {
        showNote("A grid sets the cycles: turn Grid Off to change the count");
        return true;
    }
    const bool dragging = mode_ == Mode::Box;
    const double start = dragging ? std::min(boxStartBeat_, boxEndBeat_) : 0.0;
    const double end = dragging ? std::max(boxStartBeat_, boxEndBeat_) : beatsPerBar();
    const double span = end - start;
    if (span <= kBeatEpsilon)
        return true;
    const int most = dragging ? maxCycles(start, end) : kMaxDrawCycles;
    const int now = std::clamp((int)std::lround(span / cycleBeatsFor(start, end)), 1, most); // the drawn count
    const auto next = CycleCountStepper::step(now, delta, most);
    setCycles(span, next.cycles, next.over);
    if (dragging && atLimit_)
        showNote(juce::String(most) + " cycles (most that fit)");
    else if (!dragging)
        showNote(juce::String(next.cycles) + (next.cycles == 1 ? " cycle" : " cycles") + " per bar");
    editor_.repaint();
    return true;
}

void AutomationLaneShapeGesture::showNote(const juce::String& message) {
    noteText_ = message;
    startTimer(kStatusShowMs);
    editor_.repaint();
}

void AutomationLaneShapeGesture::cancel() {
    if (mode_ == Mode::None)
        return;
    mode_ = Mode::None;
    stepper_->reset();
    gridHint_ = atLimit_ = false;
    editor_.repaint();
}

//==============================================================================
// ---- Commits ----

// The span is replaced wholesale: every point inside [start, end] goes, the new points take their place,
// in ONE editBreakpoints call. The cap is checked here first rather than left to editBreakpoints, so a
// refused stamp never reaches the undo stack.
bool AutomationLaneShapeGesture::commitReplace(double startBeat, double endBeat,
                                               std::vector<synth::AutomationLane::Breakpoint> points) {
    auto* doc = editor_.getTimelineDoc();
    const auto laneId = editor_.getActiveLane();
    const auto* lane = doc != nullptr ? doc->getLane(laneId) : nullptr;
    if (lane == nullptr)
        return false;

    std::vector<double> removeBeats;
    for (const auto& bp : lane->points)
        if (bp.beat >= startBeat - kBeatEpsilon && bp.beat <= endBeat + kBeatEpsilon)
            removeBeats.push_back(bp.beat);
    const auto kept = (long long)(lane->points.size() - removeBeats.size());
    if (kept + (long long)points.size() > synth::TimelineDoc::kMaxBreakpointsPerLane)
        return refuse("Too many points for this lane: nothing was stamped");
    if (removeBeats.empty() && points.empty())
        return false;

    const auto revisionBefore = doc->getRevision();
    auto mutate = [doc, laneId, removeBeats, points] { doc->editBreakpoints(laneId, removeBeats, points); };
    if (auto* undo = editor_.getUndoManager())
        undo->recordTimelineChange(*doc, mutate);
    else
        mutate();
    editor_.repaint();
    return doc->getRevision() != revisionBefore;
}

// Counted before anything is generated: a fine grid over a long span can ask for millions of points.
bool AutomationLaneShapeGesture::commitStamp(DrawShape shape, double startBeat, double endBeat, double lo, double hi) {
    const auto* doc = editor_.getTimelineDoc();
    const auto* lane = doc != nullptr ? doc->getLane(editor_.getActiveLane()) : nullptr;
    if (lane == nullptr)
        return false;
    const double cycle = cycleBeatsFor(startBeat, endBeat);
    const long long kept = pointsKeptOutside(startBeat, endBeat);
    if (kept + estimateShapePointCount(shape, startBeat, endBeat, cycle) > synth::TimelineDoc::kMaxBreakpointsPerLane)
        return refuse("Too many points for this lane: nothing was stamped");
    return commitReplace(startBeat, endBeat, generateShapePoints(shape, startBeat, endBeat, cycle, lo, hi));
}

bool AutomationLaneShapeGesture::refuse(const juce::String& message) {
    statusText_ = message;
    startTimer(kStatusShowMs);
    editor_.repaint();
    return false;
}

void AutomationLaneShapeGesture::timerCallback() {
    stopTimer();
    statusText_.clear();
    noteText_.clear();
    editor_.repaint();
}

// Line on a range keeps the curve continuous at both edges: it ramps from whatever the lane plays at the
// start to whatever it plays at the end. Free has no meaning without a stroke.
bool AutomationLaneShapeGesture::stampOverRange(DrawShape shape) {
    if (!rangeIsOnThisLane() || !laneRange_->hasWidth())
        return false;
    const auto* lane = editor_.getTimelineDoc()->getLane(editor_.getActiveLane());
    if (lane == nullptr)
        return false;
    const double start = laneRange_->getStartBeat();
    const double end = laneRange_->getEndBeat();

    if (isPeriodicShape(shape))
        return commitStamp(shape, start, end, lane->range.minValue, lane->range.maxValue);
    if (shape != DrawShape::Line)
        return false;

    std::vector<TimelineSnapshot::Point> pts;
    for (const auto& bp : lane->points)
        pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});
    AutomationCursor cursor{};
    const double fallback = lane->range.defaultValue;
    const double v0 = AutomationKernel::evaluate(pts.data(), (int)pts.size(), start, fallback, cursor);
    const double v1 = AutomationKernel::evaluate(pts.data(), (int)pts.size(), end, fallback, cursor);
    return commitReplace(start, end, {{start, v0, 0.0f, kLinear}, {end, v1, 0.0f, kLinear}});
}

bool AutomationLaneShapeGesture::deleteRangePoints() {
    if (!rangeIsOnThisLane() || !laneRange_->hasWidth())
        return false;
    return commitReplace(laneRange_->getStartBeat(), laneRange_->getEndBeat(), {});
}

//==============================================================================
// ---- Painting ----

juce::String AutomationLaneShapeGesture::getChipText() const {
    if (mode_ != Mode::Box)
        return {};
    const double start = std::min(boxStartBeat_, boxEndBeat_);
    const double end = std::max(boxStartBeat_, boxEndBeat_);
    const double span = end - start;
    if (span <= kBeatEpsilon)
        return {};
    if (gridHint_)
        return "A grid sets the cycles: turn Grid Off to count";
    const juce::String limit = atLimit_ || cycleCountClamped(start, end) ? " (most that fit)" : "";
    const double bpb = beatsPerBar();
    const double bars = span / bpb;
    const bool wholeBars = bars >= 1.0 && std::abs(bars - std::round(bars)) < 1e-6;
    const auto length = wholeBars ? plural(bars, "bar", "bars") : plural(span, "beat", "beats");
    return plural(span / cycleBeatsFor(start, end), "cycle", "cycles") + limit +
           juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + length;
}

// Drawn like the clip lanes' range (a wash with accent edges) so the two read as the same kind of thing.
void AutomationLaneShapeGesture::paintUnderCurve(juce::Graphics& g) {
    if (!rangeIsOnThisLane() || !laneRange_->hasWidth())
        return;
    const auto p = paletteFor(editor_);
    const float x0 = (float)viewState_.beatToX(laneRange_->getStartBeat());
    const float x1 = (float)viewState_.beatToX(laneRange_->getEndBeat());
    const float h = (float)editor_.getHeight();
    g.setColour(p.wash.withAlpha(0.16f));
    g.fillRect(juce::Rectangle<float>(x0, 0.0f, x1 - x0, h));
    g.setColour(p.accent.withAlpha(0.9f));
    g.drawLine(x0, 0.0f, x0, h, 1.5f);
    g.drawLine(x1, 0.0f, x1, h, 1.5f);
}

void AutomationLaneShapeGesture::paintOverCurve(juce::Graphics& g) {
    const auto p = paletteFor(editor_);
    if (mode_ == Mode::Box)
        paintBoxPreview(g, p.accent);
    if (statusText_.isNotEmpty())
        paintChip(g, statusText_, {4.0f, 2.0f, 0.0f, 0.0f}, p.error);
    else if (noteText_.isNotEmpty())
        paintChip(g, noteText_, {4.0f, 2.0f, 0.0f, 0.0f}, p.accent);
}

void AutomationLaneShapeGesture::paintBoxPreview(juce::Graphics& g, juce::Colour accent) {
    const auto box = boxRect();
    const float dash[] = {4.0f, 3.0f};
    g.setColour(accent.withAlpha(0.8f));
    for (const auto& edge : {juce::Line<float>(box.getTopLeft(), box.getTopRight()),
                             juce::Line<float>(box.getTopRight(), box.getBottomRight()),
                             juce::Line<float>(box.getBottomRight(), box.getBottomLeft()),
                             juce::Line<float>(box.getBottomLeft(), box.getTopLeft())})
        g.drawDashedLine(edge, dash, 2, 1.0f);

    const double start = std::min(boxStartBeat_, boxEndBeat_);
    const double end = std::max(boxStartBeat_, boxEndBeat_);
    const double cycle = cycleBeatsFor(start, end);
    if (end - start > kBeatEpsilon &&
        estimateShapePointCount(shape_, start, end, cycle) <= synth::TimelineDoc::kMaxBreakpointsPerLane) {
        const auto points = generateShapePoints(shape_, start, end, cycle, std::min(boxStartValue_, boxEndValue_),
                                                std::max(boxStartValue_, boxEndValue_));
        std::vector<TimelineSnapshot::Point> pts;
        pts.reserve(points.size());
        for (const auto& bp : points)
            pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});
        // A flag, not path.isEmpty(): a juce::Path holding only its start point still reports empty, so
        // every column would restart the sub-path and nothing would ever be stroked.
        juce::Path path;
        AutomationCursor cursor{};
        bool started = false;
        for (int x = (int)box.getX(); x <= (int)box.getRight(); ++x) {
            const double v =
                AutomationKernel::evaluate(pts.data(), (int)pts.size(), viewState_.xToBeat((double)x), 0.0, cursor);
            const auto y = (float)editor_.valueToY(v);
            if (!started)
                path.startNewSubPath((float)x, y);
            else
                path.lineTo((float)x, y);
            started = true;
        }
        g.setColour(accent);
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

    const auto chip = getChipText();
    if (chip.isNotEmpty())
        paintChip(g, chip, box, accent);
}

void AutomationLaneShapeGesture::paintChip(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> anchor,
                                           juce::Colour colour) {
    const auto p = paletteFor(editor_);
    const juce::Font font(juce::FontOptions(11.0f));
    const float w = (float)juce::GlyphArrangement::getStringWidthInt(font, text) + 10.0f;
    const float h = 15.0f;
    const float x = juce::jlimit(0.0f, std::max(0.0f, (float)editor_.getWidth() - w), anchor.getX() + 2.0f);
    const float y = juce::jlimit(0.0f, std::max(0.0f, (float)editor_.getHeight() - h), anchor.getY() + 2.0f);
    const juce::Rectangle<float> r(x, y, w, h);
    g.setColour(p.chipBg.withAlpha(0.92f));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(colour);
    g.drawRoundedRectangle(r, 3.0f, 1.0f);
    g.setColour(p.chipText);
    g.setFont(font);
    g.drawText(text, r, juce::Justification::centred, false);
}

} // namespace synth::ui
