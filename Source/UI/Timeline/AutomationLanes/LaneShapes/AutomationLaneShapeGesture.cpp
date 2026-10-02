#include "AutomationLaneShapeGesture.h"

#include "AppUndoManager.h"
#include "LaneShapeGenerator.h"
#include "Timeline/AutomationKernel.h"
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
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel())) {
        const auto& t = lf->getTheme().colors;
        return {t.accent, t.textPrimary, t.error, t.bg1, t.textPrimary};
    }
    return {juce::Colour(0xff00D1FF), juce::Colours::white, juce::Colours::red, juce::Colours::black,
            juce::Colours::white};
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
    , viewState_(viewState) {}

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

// One cycle per snap step, so the shape follows the grid the person is already working on; with snap off
// there is no step to follow, and a beat is the musical default.
double AutomationLaneShapeGesture::cycleBeats() const {
    const double division = viewState_.divisionBeats(beatsPerBar());
    return division > 0.0 ? division : 1.0;
}

bool AutomationLaneShapeGesture::rangeIsOnThisLane() const {
    return laneRange_ != nullptr && laneRange_->isActive() && laneRange_->getLane() == editor_.getActiveLane();
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
        laneRange_->begin(editor_.getActiveLane(), snappedBeatAt(e.getPosition().x));
        return true;
    }
    if (laneRange_ != nullptr)
        laneRange_->clear();

    if (followedTool == EditTool::Draw && isPeriodicShape(shape_)) {
        mode_ = Mode::Box;
        statusText_.clear();
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

bool AutomationLaneShapeGesture::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey || mode_ == Mode::None)
        return false;
    if (mode_ == Mode::Range && laneRange_ != nullptr)
        laneRange_->clear();
    cancel();
    return true;
}

void AutomationLaneShapeGesture::cancel() {
    if (mode_ == Mode::None)
        return;
    mode_ = Mode::None;
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
    const double cycle = cycleBeats();
    long long kept = 0;
    for (const auto& bp : lane->points)
        if (bp.beat < startBeat - kBeatEpsilon || bp.beat > endBeat + kBeatEpsilon)
            ++kept;
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
    const double span = std::abs(boxEndBeat_ - boxStartBeat_);
    if (span <= kBeatEpsilon)
        return {};
    const double bpb = beatsPerBar();
    const double bars = span / bpb;
    const bool wholeBars = bars >= 1.0 && std::abs(bars - std::round(bars)) < 1e-6;
    const auto length = wholeBars ? plural(bars, "bar", "bars") : plural(span, "beat", "beats");
    return plural(span / cycleBeats(), "cycle", "cycles") + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + length;
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
    const double cycle = cycleBeats();
    if (end - start > kBeatEpsilon &&
        estimateShapePointCount(shape_, start, end, cycle) <= synth::TimelineDoc::kMaxBreakpointsPerLane) {
        const auto points = generateShapePoints(shape_, start, end, cycle, std::min(boxStartValue_, boxEndValue_),
                                                std::max(boxStartValue_, boxEndValue_));
        std::vector<TimelineSnapshot::Point> pts;
        pts.reserve(points.size());
        for (const auto& bp : points)
            pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});
        juce::Path path;
        AutomationCursor cursor{};
        for (int x = (int)box.getX(); x <= (int)box.getRight(); ++x) {
            const double v =
                AutomationKernel::evaluate(pts.data(), (int)pts.size(), viewState_.xToBeat((double)x), 0.0, cursor);
            const auto y = (float)editor_.valueToY(v);
            if (path.isEmpty())
                path.startNewSubPath((float)x, y);
            else
                path.lineTo((float)x, y);
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
