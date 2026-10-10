#include "AutomationLaneEditor.h"
#include "AppUndoManager.h"
#include "Timeline/AutomationKernel.h"
#include "Timeline/AutomationRecorder.h"
#include "Transport/TransportService.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/AutomationHandleDensity.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneBipolarGuide.h"
#include "UI/Timeline/AutomationLanes/AutomationToolMapping.h"
#include "UI/Timeline/AutomationLanes/LaneMenuHook.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include "UI/Timeline/ToolCursors.h"
#include "UI/Timeline/TrackColour.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace synth::ui {

//==============================================================================
AutomationLaneEditor::AutomationLaneEditor(TimelineViewState& viewState)
    : viewState_(viewState) {
    setComponentID("automationLaneEditor");
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);
    setTitle("Automation curve");
    setTooltip("Automation curve: drag a point to move it, double-click to add one; Shift-click or drag a box to "
               "select several, Cmd+A selects all, Delete removes the selection, arrows nudge it");
    selection_.onChange = [this] { selectionChanged(); };
    refreshDescription();
}

void AutomationLaneEditor::setActiveLane(synth::LaneId id) {
    laneId_ = id;
    dragMode_ = DragMode::None;
    hoveredBeat_.reset();
    bubble_.hide();
    valueField_.close(false);
    shapeGesture_.cancel();
    cancelStretch();
    selection_.cancelGesture();
    selection_.clear();
    selection_.setCursor(std::nullopt);
    syncedRevision_ = -1;
    syncToDoc();
    glide_.reset(lanePoints() != nullptr ? *lanePoints() : std::vector<LaneBreakpoint>{});
    repaint();
}

void AutomationLaneEditor::setTool(Tool tool) {
    editTool_.reset();
    tool_ = tool;
    refreshStretchBox();
}

void AutomationLaneEditor::setEditTool(EditTool tool) {
    editTool_ = tool;
    tool_ = automationToolFor(tool, false, getDrawShape());
    refreshStretchBox();
}

void AutomationLaneEditor::setDrawShape(DrawShape shape) noexcept {
    shapeGesture_.setDrawShape(shape);
    if (editTool_.has_value())
        tool_ = automationToolFor(*editTool_, false, shape);
}

void AutomationLaneEditor::setCurveColour(juce::Colour colour) {
    if (curveColour_ == colour)
        return;
    curveColour_ = colour;
    repaint();
}

// The lane background paintGridBackdrop() fills, which the curve colour has to read against.
static juce::Colour laneBackground(const juce::Component& c) { return synth::theme::themeOf(c).colors.bg1; }

juce::Colour AutomationLaneEditor::getResolvedCurveColour() const {
    const juce::Colour colour = curveColour_.value_or(synth::theme::themeOf(*this).colors.modWire);
    return readableOn(colour, laneBackground(*this));
}

// The draw tool is the pen whether it is followed from the timeline (Shift swaps the stroke for a
// line only at mouse-down) or picked directly as Pencil/Line. Built lazily from the themed Draw icon,
// the same cursor the clip lanes and the piano roll show under that tool.
juce::MouseCursor AutomationLaneEditor::getMouseCursor() {
    if (const auto resize = stretchCursor())
        return *resize;
    // The hand is for a drag that has started, never for hovering a point
    // (docs/layout/animation.md#drag-and-drop-cursor).
    if (dragMode_ == DragMode::MoveHandle || dragMode_ == DragMode::TensionScrub || dragMode_ == DragMode::LaneConstant)
        return dragGrabCursor();
    const bool drawing =
        editTool_.has_value() ? *editTool_ == EditTool::Draw : (tool_ == Tool::Pencil || tool_ == Tool::Line);
    if (!drawing)
        return juce::MouseCursor(juce::MouseCursor::NormalCursor);
    if (!penCursorBuilt_) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        std::unique_ptr<juce::Drawable> icon = lf != nullptr ? lf->getIcon(synth::theme::Icon::ToolDraw) : nullptr;
        penCursor_ = makeToolCursor(EditTool::Draw, icon.get());
        penCursorBuilt_ = true;
    }
    return penCursor_;
}

void AutomationLaneEditor::lookAndFeelChanged() {
    penCursorBuilt_ = false; // re-tinted icon -> different cursor image
    repaint();
}

void AutomationLaneEditor::focusGained(juce::Component::FocusChangeType) {
    repaint();
    if (onFocused)
        onFocused();
}

void AutomationLaneEditor::focusLost(juce::Component::FocusChangeType) {
    if (hoveredBeat_ == std::nullopt)
        bubble_.hide();
    repaint();
}

//==============================================================================
double AutomationLaneEditor::currentBeatsPerBar() const { return beatsPerBarFor(transport_); }

double AutomationLaneEditor::snappedBeatAt(double rawBeat) const {
    return viewState_.snapBeat(rawBeat, currentBeatsPerBar());
}

double AutomationLaneEditor::clampValue(double value) const {
    if (doc_ != nullptr && laneId_.isValid())
        if (const auto* lane = doc_->getLane(laneId_))
            return juce::jlimit((double)lane->range.minValue, (double)lane->range.maxValue, value);
    return value;
}

double AutomationLaneEditor::valueToY(double value) const {
    double minV = 0.0, maxV = 1.0;
    if (doc_ != nullptr && laneId_.isValid())
        if (const auto* lane = doc_->getLane(laneId_)) {
            minV = lane->range.minValue;
            maxV = lane->range.maxValue;
        }
    const double range = maxV - minV;
    const double t = range > 0.0 ? juce::jlimit(0.0, 1.0, (value - minV) / range) : 0.5;
    const double pad = plotPadPx();
    return pad + (1.0 - t) * ((double)getHeight() - 2.0 * pad);
}

// The lane's min and max sit this far inside its top and bottom edge, so a point or the line at either limit is
// drawn whole (never half or fully outside the lane); a very short lane gets a smaller margin.
double AutomationLaneEditor::plotPadPx() const {
    return std::min((double)kPlotPadPx, std::max(0.0, (double)getHeight() / 4.0));
}

double AutomationLaneEditor::yToValue(double y) const {
    double minV = 0.0, maxV = 1.0;
    if (doc_ != nullptr && laneId_.isValid())
        if (const auto* lane = doc_->getLane(laneId_)) {
            minV = lane->range.minValue;
            maxV = lane->range.maxValue;
        }
    const double pad = plotPadPx();
    const double h = (double)getHeight() - 2.0 * pad;
    const double t = h > 0.0 ? 1.0 - ((y - pad) / h) : 0.5;
    return minV + t * (maxV - minV);
}

//==============================================================================
std::optional<AutomationLaneEditor::HandleHit> AutomationLaneEditor::hitTestHandle(juce::Point<int> pos) const {
    if (doc_ == nullptr || !laneId_.isValid())
        return std::nullopt;
    const auto* lane = doc_->getLane(laneId_);
    if (lane == nullptr)
        return std::nullopt;

    for (const auto& bp : lane->points) {
        const double x = viewState_.beatToX(bp.beat);
        const double y = valueToY(bp.value);
        if (std::abs((double)pos.x - x) <= (double)kHandleHitRadiusPx &&
            std::abs((double)pos.y - y) <= (double)kHandleHitRadiusPx)
            return HandleHit{bp.beat, bp.value, bp.tension, bp.curve};
    }
    return std::nullopt;
}

bool AutomationLaneEditor::onCurve(juce::Point<int> pos) const {
    const auto* lane = doc_ != nullptr && laneId_.isValid() ? doc_->getLane(laneId_) : nullptr;
    if (lane == nullptr || lane->points.size() < 2)
        return false;
    std::vector<TimelineSnapshot::Point> pts;
    pts.reserve(lane->points.size());
    for (const auto& bp : lane->points)
        pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});
    AutomationCursor cursor{};
    const double value = AutomationKernel::evaluate(pts.data(), (int)pts.size(), viewState_.xToBeat((double)pos.x),
                                                    (double)lane->range.defaultValue, cursor);
    return std::abs(valueToY(value) - (double)pos.y) <= (double)kHandleHitRadiusPx;
}

std::optional<int> AutomationLaneEditor::hitTestSegmentLeftIndex(int x) const {
    if (doc_ == nullptr || !laneId_.isValid())
        return std::nullopt;
    const auto* lane = doc_->getLane(laneId_);
    if (lane == nullptr || lane->points.size() < 2)
        return std::nullopt;

    const double beat = viewState_.xToBeat((double)x);
    for (int i = 0; i + 1 < (int)lane->points.size(); ++i)
        if (beat >= lane->points[(size_t)i].beat && beat < lane->points[(size_t)(i + 1)].beat)
            return i;
    return std::nullopt;
}

juce::Rectangle<int> AutomationLaneEditor::getHandleRectForTest(double beat) const {
    if (doc_ == nullptr || !laneId_.isValid())
        return {};
    const auto* lane = doc_->getLane(laneId_);
    if (lane == nullptr)
        return {};
    for (const auto& bp : lane->points) {
        if (bp.beat != beat)
            continue;
        const int x = (int)std::llround(viewState_.beatToX(bp.beat));
        const int y = (int)std::llround(valueToY(bp.value));
        const int r = (int)kHandleRadiusPx;
        return {x - r, y - r, r * 2, r * 2};
    }
    return {};
}

//==============================================================================
std::vector<double> AutomationLaneEditor::collectBeatsInSpan(double loBeat, double hiBeat) const {
    std::vector<double> beats;
    if (doc_ == nullptr || !laneId_.isValid())
        return beats;
    const auto* lane = doc_->getLane(laneId_);
    if (lane == nullptr)
        return beats;

    for (const auto& bp : lane->points)
        if (bp.beat >= loBeat && bp.beat <= hiBeat)
            beats.push_back(bp.beat);
    return beats;
}

//==============================================================================
// ---- Painting ----

void AutomationLaneEditor::paint(juce::Graphics& g) {
    syncToDoc();
    paintGridBackdrop(g);
    if (doc_ == nullptr || !laneId_.isValid())
        return;
    const auto* lane = doc_->getLane(laneId_);
    if (lane == nullptr)
        return;

    paintBipolarGuide(g, *this, lane->range.minValue, lane->range.maxValue, (float)valueToY(0.0));
    shapeGesture_.paintUnderCurve(g);
    paintCommittedCurve(g, *lane);
    paintToolPreview(g);
    paintHandles(g, *lane);
    shapeGesture_.paintOverCurve(g);
    selection_.paintMarquee(g);
    paintStretchBox(g);
    bubble_.paint(g);
    if (hasKeyboardFocus(false)) {
        const auto cursor = selection_.getCursor();
        const auto* points = lanePoints();
        const LaneBreakpoint* focused = nullptr;
        if (cursor.has_value() && points != nullptr)
            for (const auto& bp : *points)
                if (bp.beat == *cursor)
                    focused = &bp;
        const auto area = focused != nullptr ? juce::Rectangle<float>((float)viewState_.beatToX(focused->beat),
                                                                      (float)valueToY(focused->value), 0.0f, 0.0f)
                                                   .expanded(kHandleRadiusPx + 2.0f)
                                             : getLocalBounds().toFloat();
        paintFocusRing(g, area, *this, focused != nullptr ? area.getWidth() / 2.0f : 0.0f);
    }
}

void AutomationLaneEditor::paintGridBackdrop(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour bg, border;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.bg1;
        border = c.border;
    } else {
        bg = juce::Colours::darkgrey.darker(0.6f);
        border = juce::Colours::grey;
    }

    g.fillAll(bg);
    g.setColour(border.withAlpha(0.4f));
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());
    g.drawHorizontalLine(getHeight() / 2, 0.0f, (float)getWidth());
    g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());
}

void AutomationLaneEditor::paintCommittedCurve(juce::Graphics& g, const synth::AutomationLane& lane) {
    // MoveHandle/TensionScrub previews are cheap to re-evaluate through the SAME kernel real
    // playback uses, so the curve shown while dragging is exactly what will play — Pencil/Line/
    // Eraser previews are drawn as separate overlays instead (paintToolPreview), since they don't
    // yet describe a committed breakpoint run.
    const bool previewing = dragMode_ == DragMode::MoveHandle || dragMode_ == DragMode::TensionScrub ||
                            dragMode_ == DragMode::LaneConstant || dragMode_ == DragMode::Stretch;
    std::vector<LaneBreakpoint> pts = lane.points;
    if (dragMode_ == DragMode::MoveHandle) {
        std::vector<double> removeBeats;
        for (const auto& p : dragPoints_)
            removeBeats.push_back(p.beat);
        pts = replacePoints(pts, removeBeats, dragMovedPoints());
    } else if (dragMode_ == DragMode::Stretch) {
        pts = replacePoints(pts, stretchRemoveBeats(stretch_.original(), stretch_.result()),
                            stretchAddPoints(stretch_.result()));
    } else if (dragMode_ == DragMode::TensionScrub) {
        for (auto& p : pts)
            if (p.beat == tensionSegLeftBeat_)
                p.tension = previewTension_;
    }

    const juce::Colour curveColour = getResolvedCurveColour();
    if (glide_.isRunning() && !previewing) {
        if (glide_.isMelting()) {
            paintMeltedCurve(g, pts, lane, curveColour);
            return;
        }
        // A delete, undo or paste re-forms the curve: the old shape fades out as the new one fades in.
        strokeCurve(g, glide_.before(), lane, curveColour.withMultipliedAlpha(1.0f - glide_.amount()), 1.5f);
        strokeCurve(g, pts, lane, curveColour.withMultipliedAlpha(glide_.amount()), 1.5f);
        return;
    }
    strokeCurve(g, pts, lane, previewing ? curveColour.brighter(0.4f) : curveColour, previewing ? 2.0f : 1.5f);
}

void AutomationLaneEditor::strokeCurve(juce::Graphics& g, const std::vector<LaneBreakpoint>& points,
                                       const synth::AutomationLane& lane, juce::Colour colour, float thickness) const {
    std::vector<TimelineSnapshot::Point> pts;
    pts.reserve(points.size());
    for (const auto& bp : points)
        pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});

    juce::Path path;
    AutomationCursor cursor{};
    const int width = getWidth();
    const double flat = dragMode_ == DragMode::LaneConstant ? previewValue_ : (double)lane.range.defaultValue;
    for (int x = 0; x < width; x += 2) {
        const double beat = viewState_.xToBeat((double)x);
        const double value = pts.empty() ? flat
                                         : AutomationKernel::evaluate(pts.data(), (int)pts.size(), beat,
                                                                      (double)lane.range.defaultValue, cursor);
        const float y = (float)valueToY(value);
        if (x == 0)
            path.startNewSubPath((float)x, y);
        else
            path.lineTo((float)x, y);
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(thickness));
}

void AutomationLaneEditor::paintToolPreview(juce::Graphics& g) {
    juce::Colour accent;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;
    else
        accent = juce::Colours::yellow;

    if (dragMode_ == DragMode::Pencil && pencilSamples_.size() >= 2) {
        juce::Path path;
        path.startNewSubPath((float)viewState_.beatToX(pencilSamples_.front().beat),
                             (float)valueToY(pencilSamples_.front().value));
        for (size_t i = 1; i < pencilSamples_.size(); ++i)
            path.lineTo((float)viewState_.beatToX(pencilSamples_[i].beat), (float)valueToY(pencilSamples_[i].value));
        g.setColour(accent);
        g.strokePath(path, juce::PathStrokeType(2.0f));
    } else if (dragMode_ == DragMode::Line) {
        const float x0 = (float)viewState_.beatToX(snappedBeatAt(lineStartBeat_));
        const float y0 = (float)valueToY(lineStartValue_);
        const float x1 = (float)viewState_.beatToX(snappedBeatAt(lineEndBeat_));
        const float y1 = (float)valueToY(lineEndValue_);
        g.setColour(accent);
        g.drawLine(x0, y0, x1, y1, 2.0f);
    }
}

void AutomationLaneEditor::paintHandles(juce::Graphics& g, const synth::AutomationLane& lane) {
    using namespace synth::theme;
    // Points are the curve's own colour (the owning track's), outlined in the lane background so they
    // stay distinct where they sit on the line; a selected point is a filled accent dot.
    const juce::Colour normal = getResolvedCurveColour();
    const juce::Colour outline = laneBackground(*this);
    juce::Colour accent, erase;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        accent = c.accent;
        erase = c.error;
    } else {
        accent = juce::Colours::yellow;
        erase = juce::Colours::red;
    }

    std::map<double, LaneBreakpoint> moved; // the grabbed points where the drag has put them
    if (dragMode_ == DragMode::MoveHandle) {
        const auto now = dragMovedPoints();
        for (std::size_t i = 0; i < dragPoints_.size(); ++i)
            moved[dragPoints_[i].beat] = now[i];
    } else if (dragMode_ == DragMode::Stretch) {
        const auto& result = stretch_.result();
        for (std::size_t i = 0; i < result.moved.size(); ++i)
            moved[stretch_.original()[i].beat] = result.moved[i];
        for (std::size_t i = 0; i < result.pushed.size(); ++i)
            moved[result.pushedFrom[i]] = result.pushed[i];
    }
    const auto drawDot = [&](float x, float y, juce::Colour fill, float presence) {
        const float r = kHandleRadiusPx * (0.5f + 0.5f * presence);
        g.setColour(fill.withMultipliedAlpha(presence));
        g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);
        g.setColour(outline.withMultipliedAlpha(presence));
        g.drawEllipse(x - r, y - r, r * 2.0f, r * 2.0f, 1.0f);
    };

    // Crowded handles are left out (visibleHandleMask) so a dense run reads as its curve; the one being
    // dragged, scrubbed, erased, hovered or selected is always drawn.
    const auto visible = visibleHandleMask(handleScreenPositions(lane), kHandleRadiusPx * 4.0f);
    for (std::size_t i = 0; i < lane.points.size(); ++i) {
        const auto& bp = lane.points[i];
        double beat = bp.beat;
        double value = bp.value;
        bool active = false;
        if (const auto it = moved.find(bp.beat); it != moved.end()) {
            beat = it->second.beat;
            value = it->second.value;
            active = true;
        } else if (dragMode_ == DragMode::TensionScrub && bp.beat == tensionSegLeftBeat_) {
            active = true;
        }

        const bool erased = dragMode_ == DragMode::Eraser && erasedBeats_.count(bp.beat) > 0;
        const bool hovered = hoveredBeat_ == bp.beat;
        const bool selected = selection_.contains(bp.beat);
        if (!visible[i] && !active && !erased && !hovered && !selected)
            continue;
        drawDot((float)viewState_.beatToX(beat), (float)valueToY(value),
                erased ? erase : (active || selected ? accent : normal), glide_.presence(bp.beat));
    }
    for (const auto& gone : glide_.leaving()) // removed points shrink and fade out where they were
        drawDot((float)viewState_.beatToX(gone.beat), (float)valueToY(gone.value), normal, glide_.presence(gone.beat));
}

std::vector<juce::Point<float>> AutomationLaneEditor::handleScreenPositions(const synth::AutomationLane& lane) const {
    std::vector<juce::Point<float>> screen;
    screen.reserve(lane.points.size());
    for (const auto& bp : lane.points)
        screen.push_back({(float)viewState_.beatToX(bp.beat), (float)valueToY(bp.value)});
    return screen;
}

int AutomationLaneEditor::visibleHandleCountForTest() const {
    const auto* lane = doc_ != nullptr ? doc_->getLane(laneId_) : nullptr;
    if (lane == nullptr)
        return 0;
    const auto mask = visibleHandleMask(handleScreenPositions(*lane), kHandleRadiusPx * 4.0f);
    return (int)std::count(mask.begin(), mask.end(), true);
}

// A hidden handle under the pointer is drawn, so the point about to be grabbed is always visible.
void AutomationLaneEditor::updateHover(juce::Point<int> pos) {
    trackStretchHover(pos);
    std::optional<double> hovered;
    std::optional<HandleHit> hit = hoverHandle_ == StretchHandle::None ? hitTestHandle(pos) : std::nullopt;
    if (hit)
        hovered = hit->beat;
    if (hit)
        showBubbleAt(hit->beat, hit->value);
    else
        bubble_.hide();
    if (hovered != hoveredBeat_) {
        hoveredBeat_ = hovered;
        updateMouseCursor();
        repaint();
    }
}

void AutomationLaneEditor::showBubbleAt(double beat, double value) {
    if (valueField_.isOpen())
        return;
    bubble_.show({(float)viewState_.beatToX(beat), (float)valueToY(value)}, valueText(value));
}

juce::String AutomationLaneEditor::valueText(double value) const {
    if (valueToText)
        if (auto text = valueToText(value); text.isNotEmpty())
            return text;
    return juce::String(value, 2);
}

bool AutomationLaneEditor::onFlatLine(juce::Point<int> pos) const {
    const auto* lane = doc_ != nullptr && laneId_.isValid() ? doc_->getLane(laneId_) : nullptr;
    return lane != nullptr && lane->points.empty() &&
           std::abs((double)pos.y - valueToY((double)lane->range.defaultValue)) <= (double)kHandleHitRadiusPx;
}

void AutomationLaneEditor::mouseMove(const juce::MouseEvent& e) { updateHover(e.getPosition()); }

// A drag in progress keeps the bubble: JUCE delivers the exit only after the release.
void AutomationLaneEditor::mouseExit(const juce::MouseEvent&) {
    if (dragMode_ != DragMode::None)
        return;
    bubble_.hide();
    trackStretchHover({-1000, -1000});
    if (hoveredBeat_.has_value()) {
        hoveredBeat_.reset();
        updateMouseCursor();
        repaint();
    }
}

//==============================================================================
// ---- Mouse ----

void AutomationLaneEditor::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    syncToDoc();
    dragMode_ = DragMode::None;
    selection_.cancelGesture();

    if (doc_ == nullptr || !laneId_.isValid() || doc_->getLane(laneId_) == nullptr)
        return;

    const auto pos = e.getPosition();

    if (e.mods.isPopupMenu()) {
        handlePopupClick(e);
        return;
    }

    if (!e.mods.isLeftButtonDown())
        return;
    if (shapeGesture_.mouseDown(e, editTool_)) {
        repaint();
        return;
    }

    // Shift is read here rather than when the tool was picked: the Draw tool draws a line only
    // while Shift is held as the gesture starts.
    if (editTool_.has_value())
        tool_ = automationToolFor(*editTool_, e.mods.isShiftDown(), getDrawShape());
    mouseDownPos_ = pos;
    if (tool_ == Tool::Pointer && beginStretchAt(pos)) {
        repaint();
        return;
    }

    switch (tool_) {
    case Tool::Pointer: {
        if (auto hit = hitTestHandle(pos)) {
            if (e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown()) {
                // A modifier-click edits the selection; it never begins a drag.
                selection_.toggle(hit->beat);
                selection_.setCursor(hit->beat);
                hoveredBeat_ = hit->beat;
                showBubbleAt(hit->beat, hit->value);
            } else {
                grabPoint(*hit);
            }
        } else if (beginBoxMove(pos)) {
            // dragging inside the stretch box moves the whole selection
        } else if (onFlatLine(pos)) {
            dragMode_ = DragMode::LaneConstant;
            dragOriginalValue_ = (double)doc_->getLane(laneId_)->range.defaultValue;
            previewValue_ = dragOriginalValue_;
            showBubbleAt(viewState_.xToBeat((double)pos.x), previewValue_);
        } else if (auto segIdx = onCurve(pos) ? hitTestSegmentLeftIndex(pos.x) : std::nullopt) {
            const auto* lane = doc_->getLane(laneId_);
            dragMode_ = DragMode::TensionScrub;
            tensionSegLeftBeat_ = lane->points[(size_t)*segIdx].beat;
            tensionOriginal_ = lane->points[(size_t)*segIdx].tension;
            previewTension_ = tensionOriginal_;
        } else {
            selection_.pressEmpty(pos, e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown());
        }
        break;
    }
    case Tool::Pencil:
        dragMode_ = DragMode::Pencil;
        pencilSamples_.clear();
        pencilSamples_.push_back({viewState_.xToBeat((double)pos.x), clampValue(yToValue(pos.y))});
        break;
    case Tool::Line:
        dragMode_ = DragMode::Line;
        lineStartBeat_ = viewState_.xToBeat((double)pos.x);
        lineStartValue_ = clampValue(yToValue(pos.y));
        lineEndBeat_ = lineStartBeat_;
        lineEndValue_ = lineStartValue_;
        break;
    case Tool::Eraser:
        dragMode_ = DragMode::Eraser;
        erasedBeats_.clear();
        if (auto hit = hitTestHandle(pos))
            erasedBeats_.insert(hit->beat);
        break;
    }

    updateMouseCursor();
    repaint();
}

void AutomationLaneEditor::mouseDrag(const juce::MouseEvent& e) {
    if (doc_ == nullptr || !laneId_.isValid() || shapeGesture_.mouseDrag(e))
        return;
    const auto pos = e.getPosition();

    switch (dragMode_) {
    case DragMode::MoveHandle: {
        // The whole selection moves by the grabbed point's delta; the block stops at beat 0 as a unit.
        const double target = std::max(0.0, snappedBeatAt(viewState_.xToBeat((double)pos.x)));
        const double delta = clampBeatDelta(dragPoints_, target - dragOriginalBeat_);
        previewBeat_ = delta == target - dragOriginalBeat_ ? target : dragOriginalBeat_ + delta;
        previewValue_ = clampValue(yToValue(pos.y));
        showBubbleAt(previewBeat_, previewValue_);
        break;
    }
    case DragMode::Stretch:
        dragStretch(pos);
        break;
    case DragMode::LaneConstant:
        previewValue_ = clampValue(dragOriginalValue_ + yToValue(pos.y) - yToValue(mouseDownPos_.y));
        showBubbleAt(viewState_.xToBeat((double)pos.x), previewValue_);
        break;
    case DragMode::TensionScrub: {
        const double delta = ((double)mouseDownPos_.y - (double)pos.y) * 0.01;
        previewTension_ = juce::jlimit(-1.0f, 1.0f, tensionOriginal_ + (float)delta);
        break;
    }
    case DragMode::Pencil:
        pencilSamples_.push_back({viewState_.xToBeat((double)pos.x), clampValue(yToValue(pos.y))});
        break;
    case DragMode::Line:
        lineEndBeat_ = viewState_.xToBeat((double)pos.x);
        lineEndValue_ = clampValue(yToValue(pos.y));
        break;
    case DragMode::Eraser:
        if (auto hit = hitTestHandle(pos))
            erasedBeats_.insert(hit->beat);
        break;
    case DragMode::None:
        if (const auto* points = lanePoints())
            selection_.dragTo(pos, *points, pointMapper());
        return;
    }

    repaint();
}

void AutomationLaneEditor::mouseUp(const juce::MouseEvent& e) {
    if (shapeGesture_.mouseUp(e))
        return;
    if (doc_ == nullptr || !laneId_.isValid()) {
        dragMode_ = DragMode::None;
        selection_.cancelGesture();
        return;
    }
    const auto laneId = laneId_;
    if (dragMode_ == DragMode::None)
        selection_.release();

    switch (dragMode_) {
    case DragMode::MoveHandle:
        commitPointMove();
        break;
    case DragMode::Stretch:
        commitStretch();
        break;
    case DragMode::LaneConstant: {
        const double value = previewValue_;
        if (std::abs(value - dragOriginalValue_) > 1e-9) {
            auto mutate = [this, laneId, value] { doc_->setLaneConstantValue(laneId, value); };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        }
        break;
    }
    case DragMode::TensionScrub: {
        if (std::abs((double)previewTension_ - (double)tensionOriginal_) > 1e-6) {
            const double beat = tensionSegLeftBeat_;
            const float tension = previewTension_;
            double value = 0.0;
            int curve = static_cast<int>(synth::BreakpointCurve::Linear);
            if (const auto* lane = doc_->getLane(laneId_))
                for (const auto& bp : lane->points)
                    if (bp.beat == beat) {
                        value = bp.value;
                        curve = bp.curve;
                        break;
                    }
            auto mutate = [this, laneId, beat, value, tension, curve] {
                doc_->addBreakpoint(laneId, beat, value, tension, curve);
            };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        }
        break;
    }
    case DragMode::Pencil: {
        if (pencilSamples_.size() >= 2) {
            double lo = pencilSamples_.front().beat;
            double hi = pencilSamples_.front().beat;
            for (const auto& s : pencilSamples_) {
                lo = std::min(lo, s.beat);
                hi = std::max(hi, s.beat);
            }

            double rangeSpan = 1.0;
            if (const auto* lane = doc_->getLane(laneId_))
                rangeSpan = std::abs((double)(lane->range.maxValue - lane->range.minValue));
            const double epsilon = synth::AutomationRecorder::kThinningEpsilonFraction * rangeSpan;

            std::vector<synth::AutomationRecorder::CapturedPoint> raw;
            raw.reserve(pencilSamples_.size());
            for (const auto& s : pencilSamples_)
                raw.push_back({s.beat, s.value});
            const auto thinned = synth::AutomationRecorder::thinPoints(raw, epsilon);

            const auto removeBeats = collectBeatsInSpan(lo, hi);
            std::vector<synth::AutomationLane::Breakpoint> addPoints;
            addPoints.reserve(thinned.size());
            for (const auto& p : thinned)
                addPoints.push_back({p.beat, p.value, 0.0f, static_cast<int>(synth::BreakpointCurve::Linear)});

            auto mutate = [this, laneId, removeBeats, addPoints] {
                doc_->editBreakpoints(laneId, removeBeats, addPoints);
            };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        }
        pencilSamples_.clear();
        break;
    }
    case DragMode::Line: {
        const double b0 = snappedBeatAt(lineStartBeat_);
        const double b1 = snappedBeatAt(lineEndBeat_);
        if (std::abs(b1 - b0) > 1e-9) {
            const double lo = std::min(b0, b1);
            const double hi = std::max(b0, b1);
            const auto removeBeats = collectBeatsInSpan(lo, hi);
            const std::vector<synth::AutomationLane::Breakpoint> addPoints{
                {b0, lineStartValue_, 0.0f, static_cast<int>(synth::BreakpointCurve::Linear)},
                {b1, lineEndValue_, 0.0f, static_cast<int>(synth::BreakpointCurve::Linear)}};
            auto mutate = [this, laneId, removeBeats, addPoints] {
                doc_->editBreakpoints(laneId, removeBeats, addPoints);
            };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        }
        break;
    }
    case DragMode::Eraser: {
        if (!erasedBeats_.empty()) {
            const std::vector<double> removeBeats(erasedBeats_.begin(), erasedBeats_.end());
            auto mutate = [this, laneId, removeBeats] { doc_->editBreakpoints(laneId, removeBeats, {}); };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        }
        erasedBeats_.clear();
        break;
    }
    case DragMode::None:
        break;
    }

    dragMode_ = DragMode::None;
    updateMouseCursor();
    updateHover(e.getPosition());
    repaint();
}

void AutomationLaneEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    if (doc_ == nullptr || !laneId_.isValid() || doc_->getLane(laneId_) == nullptr)
        return;
    const auto pos = e.getPosition();
    if (const auto hit = hitTestHandle(pos)) {
        if (tool_ == Tool::Pointer)
            openValueField(hit->beat); // a double-click on a point types its value; it never adds one
        return;
    }

    const double beat = std::max(0.0, snappedBeatAt(viewState_.xToBeat((double)pos.x)));
    const double value = clampValue(yToValue(pos.y));
    const auto laneId = laneId_;
    auto mutate = [this, laneId, beat, value] { doc_->addBreakpoint(laneId, beat, value); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    repaint();
}

//==============================================================================
bool AutomationLaneEditor::keyPressed(const juce::KeyPress& key) {
    if (onLaneKey && onLaneKey(key))
        return true;
    syncToDoc();
    if (shapeGesture_.keyPressed(key))
        return true;
    if (key == juce::KeyPress::escapeKey) {
        if (dragMode_ != DragMode::None || selection_.isBoxActive()) {
            dragMode_ = DragMode::None;
            cancelStretch();
            selection_.cancelGesture();
            pencilSamples_.clear();
            erasedBeats_.clear();
            bubble_.hide();
            hoveredBeat_.reset();
            updateMouseCursor();
            repaint();
            return true;
        }
        return handleSelectionKey(key); // clears a selection; idle otherwise — the key belongs to the panel
    }
    if (key == juce::KeyPress::returnKey && tool_ == Tool::Pointer && openValueFieldOnCursor())
        return true;
    return handleSelectionKey(key);
}

//==============================================================================
void AutomationLaneEditor::applySegmentCurveChoice(double leftBeat, int curve) {
    if (doc_ == nullptr || !laneId_.isValid())
        return;
    const auto* lane = doc_->getLane(laneId_);
    if (lane == nullptr)
        return;

    double value = 0.0;
    float tension = 0.0f;
    bool found = false;
    for (const auto& bp : lane->points) {
        if (bp.beat == leftBeat) {
            value = bp.value;
            tension = bp.tension;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    const auto laneId = laneId_;
    auto mutate = [this, laneId, leftBeat, value, tension, curve] {
        doc_->addBreakpoint(laneId, leftBeat, value, tension, curve);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    repaint();
}

// A handle has its own menu, and so has the curve between two points; anywhere else on the lane is the lane's
// own menu, which only the owner of the lane can build (the parameter, the track, the host).
void AutomationLaneEditor::handlePopupClick(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (auto hit = hitTestHandle(pos))
        showHandleContextMenu(hit->beat);
    else if (auto segIdx = onCurve(pos) ? hitTestSegmentLeftIndex(pos.x) : std::nullopt)
        showSegmentContextMenu(*segIdx);
    else if (onLaneMenuRequested)
        onLaneMenuRequested(synth::ui::contextMenuOptionsAtPoint(e.getScreenPosition()));
}

bool AutomationLaneEditor::showContextMenuForKeyboardFocus() {
    if (!onLaneMenuRequested || doc_ == nullptr || doc_->getLane(laneId_) == nullptr)
        return false;
    onLaneMenuRequested(synth::ui::contextMenuOptionsAtPoint(getScreenBounds().getPosition()));
    return true;
}

// The handle and segment menus go through the shared test hook too, so a test can tell which menu a click opened.
void AutomationLaneEditor::showContextMenu(juce::PopupMenu menu) {
    const auto options = synth::ui::contextMenuOptionsAtPointer();
    if (auto& hook = synth::ui::test_hooks::laneMenuHookForTest()) {
        hook(menu, options);
        return;
    }
    menu.showMenuAsync(options);
}

void AutomationLaneEditor::showHandleContextMenu(double beat) {
    const auto laneId = laneId_;
    juce::PopupMenu menu;
    menu.addItem("Delete point", [this, laneId, beat] {
        auto mutate = [this, laneId, beat] { doc_->removeBreakpoint(laneId, beat); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        repaint();
    });
    showContextMenu(menu);
}

void AutomationLaneEditor::showSegmentContextMenu(int leftIndex) {
    const auto* lane = doc_->getLane(laneId_);
    if (lane == nullptr || leftIndex < 0 || (size_t)leftIndex >= lane->points.size())
        return;

    const double leftBeat = lane->points[(size_t)leftIndex].beat;
    const int currentCurve = lane->points[(size_t)leftIndex].curve;

    juce::PopupMenu menu;
    menu.addItem("Hold", true, currentCurve == static_cast<int>(synth::BreakpointCurve::Hold), [this, leftBeat] {
        applySegmentCurveChoice(leftBeat, static_cast<int>(synth::BreakpointCurve::Hold));
    });
    menu.addItem("Linear", true, currentCurve == static_cast<int>(synth::BreakpointCurve::Linear), [this, leftBeat] {
        applySegmentCurveChoice(leftBeat, static_cast<int>(synth::BreakpointCurve::Linear));
    });
    showContextMenu(menu);
}

} // namespace synth::ui
