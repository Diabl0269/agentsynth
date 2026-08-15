#include "AutomationLaneEditor.h"
#include "../AppUndoManager.h"
#include "../Timeline/AutomationKernel.h"
#include "../Timeline/AutomationRecorder.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

//==============================================================================
AutomationLaneEditor::AutomationLaneEditor(TimelineViewState& viewState)
    : viewState_(viewState) {
    setComponentID("automationLaneEditor");
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);
}

//==============================================================================
double AutomationLaneEditor::currentBeatsPerBar() const {
    double beatsPerBar = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        const double tsBeatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
        if (tsBeatsPerBar > 0.0)
            beatsPerBar = tsBeatsPerBar;
    }
    return beatsPerBar;
}

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
    return (1.0 - t) * (double)getHeight();
}

double AutomationLaneEditor::yToValue(double y) const {
    double minV = 0.0, maxV = 1.0;
    if (doc_ != nullptr && laneId_.isValid())
        if (const auto* lane = doc_->getLane(laneId_)) {
            minV = lane->range.minValue;
            maxV = lane->range.maxValue;
        }
    const double h = (double)getHeight();
    const double t = h > 0.0 ? 1.0 - (y / h) : 0.5;
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
    paintGridBackdrop(g);
    if (doc_ == nullptr || !laneId_.isValid())
        return;
    const auto* lane = doc_->getLane(laneId_);
    if (lane == nullptr)
        return;

    paintCommittedCurve(g, *lane);
    paintToolPreview(g);
    paintHandles(g, *lane);
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
    std::vector<TimelineSnapshot::Point> pts;
    pts.reserve(lane.points.size());
    for (const auto& bp : lane.points)
        pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});

    // MoveHandle/TensionScrub previews are cheap to re-evaluate through the SAME kernel real
    // playback uses, so the curve shown while dragging is exactly what will play — Pencil/Line/
    // Eraser previews are drawn as separate overlays instead (paintToolPreview), since they don't
    // yet describe a committed breakpoint run.
    const bool previewing = dragMode_ == DragMode::MoveHandle || dragMode_ == DragMode::TensionScrub;
    if (previewing && !pts.empty()) {
        if (dragMode_ == DragMode::MoveHandle) {
            for (auto& p : pts)
                if (p.beat == dragOriginalBeat_) {
                    p.beat = previewBeat_;
                    p.value = previewValue_;
                    break;
                }
            std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
        } else {
            for (auto& p : pts)
                if (p.beat == tensionSegLeftBeat_) {
                    p.tension = previewTension_;
                    break;
                }
        }
    }

    juce::Colour curveColour;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        curveColour = lf->getTheme().colors.modWire;
    else
        curveColour = juce::Colours::cyan;

    juce::Path path;
    AutomationCursor cursor{};
    const int width = getWidth();
    bool started = false;
    for (int x = 0; x < width; x += 2) {
        const double beat = viewState_.xToBeat((double)x);
        const double value = pts.empty() ? (double)lane.range.defaultValue
                                         : AutomationKernel::evaluate(pts.data(), (int)pts.size(), beat,
                                                                      (double)lane.range.defaultValue, cursor);
        const float y = (float)valueToY(value);
        if (!started) {
            path.startNewSubPath((float)x, y);
            started = true;
        } else {
            path.lineTo((float)x, y);
        }
    }

    g.setColour(previewing ? curveColour.brighter(0.4f) : curveColour);
    g.strokePath(path, juce::PathStrokeType(previewing ? 2.0f : 1.5f));
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
    juce::Colour normal, accent, erase;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        normal = c.textPrimary;
        accent = c.accent;
        erase = c.error;
    } else {
        normal = juce::Colours::white;
        accent = juce::Colours::yellow;
        erase = juce::Colours::red;
    }

    for (const auto& bp : lane.points) {
        double beat = bp.beat;
        double value = bp.value;
        bool active = false;
        if (dragMode_ == DragMode::MoveHandle && bp.beat == dragOriginalBeat_) {
            beat = previewBeat_;
            value = previewValue_;
            active = true;
        } else if (dragMode_ == DragMode::TensionScrub && bp.beat == tensionSegLeftBeat_) {
            active = true;
        }

        const bool erased = dragMode_ == DragMode::Eraser && erasedBeats_.count(bp.beat) > 0;
        const float x = (float)viewState_.beatToX(beat);
        const float y = (float)valueToY(value);
        g.setColour(erased ? erase : (active ? accent : normal));
        g.fillEllipse(x - kHandleRadiusPx, y - kHandleRadiusPx, kHandleRadiusPx * 2.0f, kHandleRadiusPx * 2.0f);
    }
}

//==============================================================================
// ---- Mouse ----

void AutomationLaneEditor::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;

    if (doc_ == nullptr || !laneId_.isValid() || doc_->getLane(laneId_) == nullptr)
        return;

    const auto pos = e.getPosition();

    if (e.mods.isPopupMenu()) {
        if (auto hit = hitTestHandle(pos))
            showHandleContextMenu(hit->beat);
        else if (auto segIdx = hitTestSegmentLeftIndex(pos.x))
            showSegmentContextMenu(*segIdx);
        return;
    }

    if (!e.mods.isLeftButtonDown())
        return;

    mouseDownPos_ = pos;

    switch (tool_) {
    case Tool::Pointer: {
        if (auto hit = hitTestHandle(pos)) {
            dragMode_ = DragMode::MoveHandle;
            dragOriginalBeat_ = hit->beat;
            dragOriginalValue_ = hit->value;
            dragOriginalTension_ = hit->tension;
            dragOriginalCurve_ = hit->curve;
            previewBeat_ = dragOriginalBeat_;
            previewValue_ = dragOriginalValue_;
        } else if (auto segIdx = hitTestSegmentLeftIndex(pos.x)) {
            const auto* lane = doc_->getLane(laneId_);
            dragMode_ = DragMode::TensionScrub;
            tensionSegLeftBeat_ = lane->points[(size_t)*segIdx].beat;
            tensionOriginal_ = lane->points[(size_t)*segIdx].tension;
            previewTension_ = tensionOriginal_;
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

    repaint();
}

void AutomationLaneEditor::mouseDrag(const juce::MouseEvent& e) {
    if (doc_ == nullptr || !laneId_.isValid())
        return;
    const auto pos = e.getPosition();

    switch (dragMode_) {
    case DragMode::MoveHandle:
        previewBeat_ = std::max(0.0, snappedBeatAt(viewState_.xToBeat((double)pos.x)));
        previewValue_ = clampValue(yToValue(pos.y));
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
        return;
    }

    repaint();
}

void AutomationLaneEditor::mouseUp(const juce::MouseEvent&) {
    if (doc_ == nullptr || !laneId_.isValid()) {
        dragMode_ = DragMode::None;
        return;
    }
    const auto laneId = laneId_;

    switch (dragMode_) {
    case DragMode::MoveHandle: {
        if (std::abs(previewBeat_ - dragOriginalBeat_) > 1e-9 || std::abs(previewValue_ - dragOriginalValue_) > 1e-9) {
            // A move can land on a DIFFERENT beat, so this is a remove-at-old + add-at-new — routed
            // through editBreakpoints (not a plain removeBreakpoint + addBreakpoint pair) so the
            // whole drag costs exactly one revision bump, whether or not the beat actually moved.
            const std::vector<double> removeBeats{dragOriginalBeat_};
            const std::vector<synth::AutomationLane::Breakpoint> addPoints{
                {previewBeat_, previewValue_, dragOriginalTension_, dragOriginalCurve_}};
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
    repaint();
}

void AutomationLaneEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    if (doc_ == nullptr || !laneId_.isValid() || doc_->getLane(laneId_) == nullptr)
        return;
    const auto pos = e.getPosition();
    if (hitTestHandle(pos))
        return; // no-op on an existing handle

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
    if (key == juce::KeyPress::escapeKey) {
        if (dragMode_ != DragMode::None) {
            dragMode_ = DragMode::None;
            pencilSamples_.clear();
            erasedBeats_.clear();
            repaint();
            return true;
        }
        return false; // idle — let the panel decide whether to close the strip
    }
    return false;
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
    menu.showMenuAsync(juce::PopupMenu::Options());
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
    menu.showMenuAsync(juce::PopupMenu::Options());
}

} // namespace synth::ui
