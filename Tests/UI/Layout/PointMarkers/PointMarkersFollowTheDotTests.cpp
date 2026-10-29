// PointMarkersFollowTheDotTests.cpp -- the guard that every marker painted around a point (its dot, halo, selection
// ring, keyboard focus ring or focus stop, and the selection box) sits on the place the point is DRAWN, never where
// it rests in the document: at the last frame of a mouse drag, at the last frame of a key hold and halfway through an
// undo glide. It walks one list of point editors (pointSurfaces) and checks what PointMarkerProbe records, so a new
// marker is checked the day it is painted and a new point editor adds one entry.
#include "../../Layout/KeyGlide/KeyGlideTestHost.h"
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "../../Timeline/AutomationLanes/AutomationLanesTestFixture.h"
#include "PointMarkerProbe.h"
#include "PointMarkerSurfaceNames.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <functional>
#include <memory>
#include <set>

using namespace point_marker_test;

namespace {

using synth::ui::MorphMap;

struct FullMotion {
    FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::setReducedMotionForTest(false);
    }
    ~FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

/** Where a point is: the place it is drawn now and the place it rests at (or left). */
struct Places {
    juce::Point<float> drawn;
    juce::Point<float> stale;
};

/** One point editor. Each phase leaves the point mid-move and says where it should be drawn. */
class PointSurface {
public:
    virtual ~PointSurface() = default;
    virtual juce::Component& painted() = 0;
    /** Pressed and dragged with the mouse, not released. */
    virtual Places mouseDragLastFrame() = 0;
    /** An arrow held down past the tap, one frame of the glide run. */
    virtual Places keyHoldLastFrame() = 0;
    /** Moved, settled, undone, and 40 ms into the glide back. */
    virtual Places undoGlideMidway() = 0;
    /** The rings that must sit on the dot (focus ring, selection ring, halo, focus stop). */
    virtual int ringsOnDot() const = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// Automation lane point
// ---------------------------------------------------------------------------------------------------------------

class LanePoint : public PointSurface {
public:
    LanePoint() {
        track_ = f_.doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane_ = f_.addLane(track_, "cutoff");
        f_.panel.setTrackAutomationExpanded(track_, true);
        editor_ = f_.panel.laneEditorForTest(lane_);
        editor_->setTool(synth::ui::AutomationLaneEditor::Tool::Pointer);
        for (const auto& [beat, value] : {std::pair{2.0, 30.0}, {4.0, 55.0}, {6.0, 45.0}, {8.0, 70.0}})
            f_.doc.addBreakpoint(lane_, beat, value);
        f_.panel.getViewState().snapEnabled = false;
        editor_->getKeyHoldForTest().host = keys_.host();
    }
    juce::Component& painted() override { return *editor_; }
    int ringsOnDot() const override { return 1; } // the keyboard focus ring

    juce::Point<float> at(double beat, double value) {
        return {(float)f_.panel.getViewState().beatToX(beat), (float)editor_->valueToY(value)};
    }
    juce::Point<float> previewOf(std::size_t i) {
        const auto moved = editor_->getDragMovedPointsForTest();
        return moved.size() > i ? at(moved[i].beat, moved[i].value) : juce::Point<float>();
    }
    void select(std::vector<double> beats, double cursor) {
        editor_->getPointSelection().setSelection(beats);
        editor_->getPointSelection().setCursor(cursor);
    }

    Places mouseDragLastFrame() override {
        select({4.0}, 4.0);
        const auto from = at(4.0, 55.0);
        press(from, from + juce::Point<float>(30.0f, -8.0f));
        return {previewOf(0), from};
    }
    Places keyHoldLastFrame() override {
        select({4.0}, 4.0);
        holdAltRight(600.0);
        return {previewOf(0), at(4.0, 55.0)};
    }
    Places undoGlideMidway() override {
        select({4.0}, 4.0);
        editor_->getPointTweenForTest().forceAnimateForTest(true);
        const auto from = at(4.0, 55.0);
        automation_lanes_test::dragAcross(*editor_, from, at(4.0, 75.0), 6);
        editor_->getPointTweenForTest().stepForTest(1000.0);
        f_.undo.undo();
        editor_->getPointTweenForTest().stepForTest(40.0);
        const auto offset = editor_->getPointTweenForTest().offsetFor(synth::ui::lane_key::tweenKey(4.0));
        return {from + offset, from};
    }

    /** Two selected points (4 and 6) and the places their dots should be drawn after `phase`. */
    std::vector<juce::Point<float>> twoPointsAfter(int phase) {
        select({4.0, 6.0}, 4.0);
        if (phase == 0) {
            const auto from = at(4.0, 55.0);
            press(from, from + juce::Point<float>(30.0f, -4.0f));
            return {previewOf(0), previewOf(1)};
        }
        if (phase == 1) {
            holdAltRight(600.0);
            return {previewOf(0), previewOf(1)};
        }
        editor_->getPointTweenForTest().forceAnimateForTest(true);
        const auto from = at(4.0, 55.0);
        automation_lanes_test::dragAcross(*editor_, from, from + juce::Point<float>(60.0f, 0.0f), 6);
        editor_->getPointTweenForTest().stepForTest(1000.0);
        f_.undo.undo();
        // The undo drops the selection (its beats are gone); picking the two points again shows the box at once, and
        // from then on the box's own move animates too, so a box that eased on its own would show.
        select({4.0, 6.0}, 4.0);
        editor_->getStretchForTest().forceAnimateForTest(true);
        editor_->getPointTweenForTest().stepForTest(40.0);
        editor_->getStretchForTest().stepForTest(40.0);
        auto& tween = editor_->getPointTweenForTest();
        return {at(4.0, 55.0) + tween.offsetFor(synth::ui::lane_key::tweenKey(4.0)),
                at(6.0, 45.0) + tween.offsetFor(synth::ui::lane_key::tweenKey(6.0))};
    }

private:
    void press(juce::Point<float> from, juce::Point<float> to) {
        using namespace automation_lanes_test;
        editor_->mouseDown(makeClickEvent(*editor_, from, leftButton()));
        for (int i = 1; i <= 4; ++i)
            editor_->mouseDrag(makeDragEvent(*editor_, from + (to - from) * ((float)i / 4.0f), from, leftButton()));
    }
    void holdAltRight(double glideMs) {
        keys_.down = {juce::KeyPress::rightKey};
        keys_.alt = true;
        EXPECT_TRUE(editor_->keyPressed(key(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)));
        keys_.now = 1000.0 + synth::ui::lane_key::kTapMs + glideMs;
        editor_->getKeyHoldForTest().tick();
    }

    automation_lanes_test::LanesPanel f_;
    synth::TrackId track_;
    synth::LaneId lane_;
    synth::ui::AutomationLaneEditor* editor_ = nullptr;
    key_glide_test::FakeKeys keys_;
};

// ---------------------------------------------------------------------------------------------------------------
// Curve editor node (the shape window's)
// ---------------------------------------------------------------------------------------------------------------

synth::ui::CurveEditorGeometry shapeWindowGeometry(const synth::ui::CurveEditorComponent& curve) {
    synth::ui::CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    config.zeroSegmentPx = 0.0f;
    return synth::ui::CurveEditorGeometry(curve.getModel(), curve.getLocalBounds().toFloat(), config);
}

class CurveNode : public PointSurface {
public:
    CurveNode()
        : editor_(rig_.adapter, 0) {
        curve().getKeyHoldForTest().host = keys_.host();
    }
    juce::Component& painted() override { return curve(); }
    int ringsOnDot() const override { return 1; } // the selected point's halo

    synth::ui::CurveEditorComponent& curve() { return editor_.getCurveEditor(); }
    juce::Point<float> place() { return shapeWindowGeometry(curve()).nodePosition(kNode); }

    Places mouseDragLastFrame() override {
        const auto from = place();
        const auto to = from + juce::Point<float>(0.0f, -30.0f);
        curve().mouseDown(synth::ui::test::curveLeftClick(curve(), from));
        curve().mouseDrag(synth::ui::test::curveLeftDrag(curve(), to, from));
        return {place(), from};
    }
    Places keyHoldLastFrame() override {
        clickNode();
        const auto from = place();
        keys_.down = {juce::KeyPress::upKey};
        EXPECT_TRUE(curve().keyPressed(key(juce::KeyPress::upKey)));
        keys_.now = 1000.0 + synth::ui::lane_key::kTapMs + 600.0;
        curve().getKeyHoldForTest().tick();
        return {place(), from};
    }
    Places undoGlideMidway() override {
        curve().getPointTweenForTest().forceAnimateForTest(true);
        const auto rest = place();
        const auto to = rest + juce::Point<float>(0.0f, -30.0f);
        curve().mouseDown(synth::ui::test::curveLeftClick(curve(), rest));
        curve().mouseDrag(synth::ui::test::curveLeftDrag(curve(), to, rest));
        curve().mouseUp(synth::ui::test::curveLeftDrag(curve(), to, rest));
        curve().getPointTweenForTest().stepForTest(1000.0);
        rig_.undo.undo();
        editor_.refresh();
        curve().getPointTweenForTest().stepForTest(40.0);
        return {place() + curve().getPointTweenForTest().offsetFor(kNode), place()};
    }

private:
    static constexpr int kNode = 3;
    void clickNode() {
        const auto p = place();
        curve().mouseDown(synth::ui::test::curveLeftClick(curve(), p));
        curve().mouseUp(synth::ui::test::curveLeftClick(curve(), p));
    }

    synth::ui::test::ShapeRig rig_;
    synth::ui::ShapeListEditor editor_;
    key_glide_test::FakeKeys keys_;
};

// ---------------------------------------------------------------------------------------------------------------
// Morph map dot
// ---------------------------------------------------------------------------------------------------------------

class MorphDot : public PointSurface {
public:
    MorphDot() {
        MorphMap::setAnimateOffScreenForTest(true);
        map_.setSize(252, 150);
        controller_.refresh();
        map_.setGlideHostForTest(keys_.host());
    }
    ~MorphDot() override { MorphMap::setAnimateOffScreenForTest(false); }
    juce::Component& painted() override { return map_; }
    int ringsOnDot() const override { return 2; } // the selection ring and the dot's focus stop

    juce::Point<float> drawn(int i) const { return map_.toPixels(map_.getDrawnDot(i).x, map_.getDrawnDot(i).y); }

    Places mouseDragLastFrame() override {
        const auto from = map_.getDotCentre(kDot);
        const auto to = map_.toPixels(0.4f, 0.55f);
        map_.mouseDown(synth::ui::test::curveLeftClick(map_, from));
        map_.mouseDrag(synth::ui::test::curveLeftDrag(map_, to, from));
        return {map_.getDotCentre(kDot), from};
    }
    Places keyHoldLastFrame() override {
        const auto from = map_.getDotCentre(kDot);
        map_.mouseDown(synth::ui::test::curveLeftClick(map_, from)); // selects it
        map_.mouseUp(synth::ui::test::curveLeftClick(map_, from));
        keys_.down = {juce::KeyPress::downKey};
        map_.getDotStop(kDot)->keyPressed(key(juce::KeyPress::downKey));
        keys_.now = 1000.0 + synth::ui::morph_glide::kTapMs + 600.0;
        map_.glideTickForTest();
        return {map_.getDotCentre(kDot), from};
    }
    Places undoGlideMidway() override {
        const auto from = map_.getDotCentre(kDot);
        const auto to = map_.toPixels(0.4f, 0.55f);
        map_.mouseDown(synth::ui::test::curveLeftClick(map_, from));
        map_.mouseDrag(synth::ui::test::curveLeftDrag(map_, to, from));
        map_.mouseUp(synth::ui::test::curveLeftDrag(map_, to, from));
        map_.stepMoveGlideForTest(1000.0);
        rig_.undo.undo();
        controller_.refresh();
        map_.stepMoveGlideForTest(40.0);
        return {drawn(kDot), map_.getDotCentre(kDot)};
    }

private:
    static constexpr int kDot = 0;
    synth::ui::test::ShapeRig rig_;
    MorphMap map_;
    synth::ui::ShapeMapController controller_{map_, *rig_.adapter};
    struct Keys {
        double now = 1000.0;
        std::set<int> down;
        MorphMap::GlideHost host() {
            MorphMap::GlideHost h;
            h.nowMs = [this] { return now; };
            h.isArrowDown = [this](int code) { return down.count(code) > 0; };
            h.isShiftDown = [] { return false; };
            h.isAltDown = [] { return false; };
            return h;
        }
    } keys_;
};

struct Entry {
    const char* name;
    std::function<std::unique_ptr<PointSurface>()> make;
};

/** Every point editor. A new one adds its entry here. */
const std::vector<Entry>& pointSurfaces() {
    static const std::vector<Entry> list{
        {"Automation lane point", [] { return std::make_unique<LanePoint>(); }},
        {"Curve editor point", [] { return std::make_unique<CurveNode>(); }},
        {"Morph map dot", [] { return std::make_unique<MorphDot>(); }},
    };
    return list;
}

void expectPhase(const Entry& entry, const char* phase, const std::function<Places(PointSurface&)>& run) {
    FullMotion motion;
    auto s = entry.make();
    const auto places = run(*s);
    ASSERT_GT(places.drawn.getDistanceFrom(places.stale), 2.0f)
        << entry.name << ", " << phase << ": the phase did not move the drawn point";
    expectMarkersOnDot(paintMarks(s->painted()), places.drawn, places.stale, s->ringsOnDot(),
                       std::string(entry.name) + ", " + phase);
}

} // namespace

std::vector<std::string> pointMarkerSurfaceNames() {
    std::vector<std::string> names;
    for (const auto& entry : pointSurfaces())
        names.push_back(entry.name);
    return names;
}

// Regression test for FRO1187: a lane point dragged with the mouse left its keyboard focus ring at the old place.
TEST(PointMarkersFollowTheDot, AtTheLastFrameOfAMouseDrag) {
    for (const auto& entry : pointSurfaces())
        expectPhase(entry, "mouse drag", [](PointSurface& s) { return s.mouseDragLastFrame(); });
}

TEST(PointMarkersFollowTheDot, AtTheLastFrameOfAKeyHold) {
    for (const auto& entry : pointSurfaces())
        expectPhase(entry, "key hold", [](PointSurface& s) { return s.keyHoldLastFrame(); });
}

TEST(PointMarkersFollowTheDot, HalfwayThroughAnUndoGlide) {
    for (const auto& entry : pointSurfaces())
        expectPhase(entry, "undo glide", [](PointSurface& s) { return s.undoGlideMidway(); });
}

TEST(PointMarkersFollowTheDot, TheLaneSelectionBoxIsAroundTheDrawnDots) {
    const char* phases[] = {"mouse drag", "key hold", "undo glide"};
    for (int phase = 0; phase < 3; ++phase) {
        FullMotion motion;
        LanePoint lane;
        const auto dots = lane.twoPointsAfter(phase);
        expectBoxAroundDots(paintMarks(lane.painted()), dots, std::string("selection box, ") + phases[phase]);
    }
}
