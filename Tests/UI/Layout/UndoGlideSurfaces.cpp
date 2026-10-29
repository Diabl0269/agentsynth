// UndoGlideSurfaces.cpp -- the registry of undoable move surfaces and the fixtures of the ones that need little setup.
#include "UndoGlideSurfaces.h"
#include "../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "../Timeline/AutomationLanes/AutomationLanesTestFixture.h"
#include "../Timeline/TimelineClipLane/TimelineClipLaneTestFixture.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"

namespace undo_glide_test {

namespace {

using synth::ui::MorphMap;
using synth::ui::ShapeListEditor;
using synth::ui::ShapeMapController;

constexpr double kForever = 1000.0;

/** The morph map's dot or cursor, dragged with the mouse and restored by the card's refresh after the undo. */
class MorphMapSurface : public Surface {
public:
    explicit MorphMapSurface(bool cursor)
        : cursor_(cursor) {
        MorphMap::setAnimateOffScreenForTest(true);
        map_.setSize(252, 150);
        controller_.refresh();
    }
    ~MorphMapSurface() override { MorphMap::setAnimateOffScreenForTest(false); }

    void move() override {
        const auto from = cursor_ ? map_.getCursorCentre() : map_.getDotCentre(0);
        const auto to = map_.toPixels(0.4f, 0.55f);
        map_.mouseDown(synth::ui::test::curveLeftClick(map_, from));
        map_.mouseDrag(synth::ui::test::curveLeftDrag(map_, to, from));
        map_.mouseUp(synth::ui::test::curveLeftDrag(map_, to, from));
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override {
        const auto at = cursor_ ? map_.getDrawnCursor() : map_.getDrawnDot(0);
        return map_.toPixels(at.x, at.y).x;
    }
    bool gliding() const override { return map_.isMoveGliding(); }
    void stepTo(double elapsedMs) override { map_.stepMoveGlideForTest(elapsedMs); }
    float alpha() const override { return map_.getMoveAlphaForTest(cursor_, 0); }
    juce::Component* paintedComponent() override { return &map_; }

private:
    bool restore(bool forward) {
        const bool did = forward ? rig_.undo.redo() : rig_.undo.undo();
        controller_.refresh(); // what the card's poll runs when the shape list or the morph cursor changed
        return did;
    }

    synth::ui::test::ShapeRig rig_;
    MorphMap map_;
    ShapeMapController controller_{map_, *rig_.adapter};
    bool cursor_;
};

/** The curve editor's geometry as the shape window configures it. */
synth::ui::CurveEditorGeometry shapeWindowGeometry(const synth::ui::CurveEditorComponent& curve) {
    synth::ui::CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    config.zeroSegmentPx = 0.0f;
    return synth::ui::CurveEditorGeometry(curve.getModel(), curve.getLocalBounds().toFloat(), config);
}

/** A curve point of the shape window, dragged with the mouse; the window picks the restore up when it refreshes. */
class CurveSurface : public Surface {
public:
    CurveSurface()
        : editor_(rig_.adapter, 0) {
        editor_.getCurveEditor().getPointTweenForTest().forceAnimateForTest(true);
    }

    void move() override {
        auto& curve = editor_.getCurveEditor();
        const auto from = shapeWindowGeometry(curve).nodePosition(kNode);
        const auto to = from + juce::Point<float>(0.0f, -30.0f);
        curve.mouseDown(synth::ui::test::curveLeftClick(curve, from));
        curve.mouseDrag(synth::ui::test::curveLeftDrag(curve, to, from));
        curve.mouseUp(synth::ui::test::curveLeftDrag(curve, to, from));
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override {
        auto& curve = const_cast<ShapeListEditor&>(editor_).getCurveEditor();
        return shapeWindowGeometry(curve).nodePosition(kNode).y + curve.getPointTweenForTest().offsetFor(kNode).y;
    }
    bool gliding() const override {
        return const_cast<ShapeListEditor&>(editor_).getCurveEditor().getPointTweenForTest().isAnimating();
    }
    void stepTo(double elapsedMs) override { editor_.getCurveEditor().getPointTweenForTest().stepForTest(elapsedMs); }
    float alpha() const override {
        return const_cast<ShapeListEditor&>(editor_).getCurveEditor().getPointTweenForTest().alphaFor(kNode);
    }
    juce::Component* paintedComponent() override { return &editor_.getCurveEditor(); }

private:
    static constexpr int kNode = 3;
    bool restore(bool forward) {
        const bool did = forward ? rig_.undo.redo() : rig_.undo.undo();
        editor_.refresh();
        return did;
    }

    synth::ui::test::ShapeRig rig_;
    ShapeListEditor editor_;
};

/** An automation point dragged to a new value; the lane editor follows the doc notification the undo sends. */
class LanePointSurface : public Surface {
public:
    LanePointSurface() {
        bass_ = panel_.doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane_ = panel_.addLane(bass_, "cutoff");
        panel_.panel.setTrackAutomationExpanded(bass_, true);
        editor_ = panel_.panel.laneEditorForTest(lane_);
        editor_->setTool(synth::ui::AutomationLaneEditor::Tool::Pointer);
        for (const auto& [beat, value] : {std::pair{1.0, 20.0}, {2.0, 60.0}, {3.0, 40.0}})
            panel_.doc.addBreakpoint(lane_, beat, value);
        editor_->getPointTweenForTest().forceAnimateForTest(true);
    }

    void move() override {
        const auto& view = panel_.panel.getViewState();
        const juce::Point<float> from{(float)view.beatToX(2.0), (float)editor_->valueToY(60.0)};
        const juce::Point<float> to{from.x, (float)editor_->valueToY(15.0)};
        automation_lanes_test::dragAcross(*editor_, from, to, 6);
    }
    bool undo() override { return panel_.undo.undo(); }
    bool redo() override { return panel_.undo.redo(); }
    double drawn() const override {
        const auto* lane = panel_.doc.getLane(lane_);
        if (lane == nullptr || lane->points.size() < 2)
            return 0.0;
        const auto& point = lane->points[1];
        return editor_->valueToY(point.value) +
               editor_->getPointTweenForTest().offsetFor(synth::ui::lane_key::tweenKey(point.beat)).y;
    }
    bool gliding() const override { return editor_->getPointTweenForTest().isAnimating(); }
    void stepTo(double elapsedMs) override { editor_->getPointTweenForTest().stepForTest(elapsedMs); }
    float alpha() const override {
        const auto* lane = panel_.doc.getLane(lane_);
        if (lane == nullptr || lane->points.size() < 2)
            return 1.0f;
        return editor_->getPointTweenForTest().alphaFor(synth::ui::lane_key::tweenKey(lane->points[1].beat));
    }
    juce::Component* paintedComponent() override { return editor_; }

private:
    automation_lanes_test::LanesPanel panel_;
    synth::TrackId bass_;
    synth::LaneId lane_;
    synth::ui::AutomationLaneEditor* editor_ = nullptr;
};

/** The left edge of a clip with notes, dragged with the mouse; the lane refreshes from the doc after the undo. */
class ClipTrimSurface : public Surface {
public:
    ClipTrimSurface() {
        const auto track = f_.doc.addTrack(synth::TrackKind::Midi, "Track 1");
        clip_ = f_.doc.addClip(track, 2.0, 4.0, "Clip");
        f_.doc.addNote(clip_, makeNote(0.0, 60, 1.0));
        f_.doc.addNote(clip_, makeNote(3.0, 64, 1.0));
        f_.lane.refreshFromDoc();
        f_.lane.getClipGlideForTest().forceAnimateForTest(true);
    }

    void move() override {
        const auto rect = f_.lane.getClipRect(clip_);
        const juce::Point<float> edge((float)rect.getX() + 3.0f, (float)rect.getCentreY());
        const juce::Point<float> dragged(edge.x + 52.0f, edge.y);
        f_.lane.mouseDown(leftClick(f_.lane, edge));
        f_.lane.mouseDrag(leftDrag(f_.lane, dragged, edge));
        f_.lane.mouseUp(leftDrag(f_.lane, dragged, edge));
        f_.lane.refreshFromDoc();
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return f_.lane.getDrawnClipRectForTest(clip_).getX(); }
    bool gliding() const override { return f_.lane.getClipGlideForTest().isAnimating(); }
    void stepTo(double elapsedMs) override { f_.lane.getClipGlideForTest().stepForTest(elapsedMs); }
    float alpha() const override { return f_.lane.getClipGlideForTest().alphaFor(clip_.value); }
    juce::Component* paintedComponent() override { return &f_.lane; }

private:
    bool restore(bool forward) {
        const bool did = forward ? f_.undo.redo() : f_.undo.undo();
        f_.lane.refreshFromDoc(); // the panel's doc listener
        return did;
    }

    mutable ClipLaneFixture f_;
    synth::ClipId clip_;
};

} // namespace

const std::vector<Entry>& surfaces() {
    static const std::vector<Entry> list{
        {"Morph map dot", true, [] { return std::make_unique<MorphMapSurface>(false); }},
        {"Morph map cursor", true, [] { return std::make_unique<MorphMapSurface>(true); }},
        {"Curve editor point", true, [] { return std::make_unique<CurveSurface>(); }},
        {"Automation lane point", true, [] { return std::make_unique<LanePointSurface>(); }},
        {"Timeline clip edge", true, [] { return std::make_unique<ClipTrimSurface>(); }},
        {"LFO card curve point", true, [] { return makeLfoCardPoint(); }},
        {"Timeline track row", false, [] { return makeTimelineTrackRows(); }},
        {"Automation lane row", false, [] { return makeAutomationLaneReorder(); }},
    };
    static const std::vector<Entry> all = [] {
        auto withContents = list;
        appendClipContentsEntries(withContents);
        return withContents;
    }();
    return all;
}

} // namespace undo_glide_test
