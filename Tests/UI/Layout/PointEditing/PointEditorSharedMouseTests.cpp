// PointEditorSharedMouseTests.cpp -- one mouse script, run on every editor with ordered dots (the curve editor and an
// automation lane) through a small adapter per editor, the sibling of PointEditorSharedKeyTests: box select on empty
// space (plain, additive, a bare click, the marquee, the move after it), checked once for both
// (docs/control/point-editing.md). A new point editor adds an adapter to MouseEditorTypes. The curve editor's Fixed
// mode (pinned dots) has its own tests at the end.

#include "../../ModuleViews/CurveEditor/CurveEditorTestHelpers.h"
#include "../../Timeline/AutomationLanes/AutomationLanesTestFixture.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <algorithm>
#include <gtest/gtest.h>

using automation_lanes_test::leftButton;
using synth::ui::AutomationLaneEditor;
using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;
using synth::ui::CurveMode;
using synth::ui::CurveModel;
using synth::ui::CurveNode;

namespace {

constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

// What both adapters share: the mouse script and the picture of the surface.
template <typename Self>
struct MouseScript {
    void press(juce::Point<float> p, int mods = 0) {
        auto& s = self().surface();
        s.mouseDown(makeClickEvent(s, p, leftButton(mods)));
        anchor_ = p;
    }
    void dragTo(juce::Point<float> p, int mods = 0) {
        auto& s = self().surface();
        s.mouseDrag(makeDragEvent(s, p, anchor_, leftButton(mods)));
    }
    void release(juce::Point<float> p, int mods = 0) {
        auto& s = self().surface();
        s.mouseUp(makeClickEvent(s, p, leftButton(mods)));
    }
    void click(juce::Point<float> p, int mods = 0) {
        press(p, mods);
        release(p, mods);
    }
    // A box from the top edge left of dot `first` to the bottom edge right of dot `last`.
    void box(int first, int last, int mods = 0) {
        const auto from = juce::Point<float>(self().dot(first).x - 10.0f, 2.0f);
        const auto to = juce::Point<float>(self().dot(last).x + 10.0f, (float)self().surface().getHeight() - 2.0f);
        press(from, mods);
        dragTo(to, mods);
        release(to, mods);
    }
    juce::Point<float> emptyBetween(int a) { return {(self().dot(a).x + self().dot(a + 1).x) * 0.5f, 2.0f}; }
    juce::Image render() {
        auto& s = self().surface();
        juce::Image image(juce::Image::ARGB, s.getWidth(), s.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        s.paintEntireComponent(g, true);
        return image;
    }
    bool pressKey(int code, int mods = 0) { return self().surfaceKey(key(code, mods)); }

private:
    Self& self() { return static_cast<Self&>(*this); }
    juce::Point<float> anchor_;
};

// Three loose points between two fixed ends (the ends cannot be picked); one undo step per gesture.
struct CurveAdapter : MouseScript<CurveAdapter> {
    CurveEditorComponent curve;
    int gestures = 0;

    CurveAdapter() {
        curve.setBounds(0, 0, 400, 160);
        CurveModel model(CurveMode::Free);
        model.setNodes({CurveNode{0.0, 0.5f, false, false}, CurveNode{0.25, 0.5f, true, true},
                        CurveNode{0.5, 0.5f, true, true}, CurveNode{0.75, 0.5f, true, true},
                        CurveNode{1.0, 0.5f, false, false}});
        curve.setModel(std::move(model));
        curve.onGestureStart = [this] { ++gestures; };
    }
    juce::Component& surface() { return curve; }
    bool surfaceKey(const juce::KeyPress& k) { return curve.keyPressed(k); }
    juce::Point<float> dot(int i) {
        return CurveEditorGeometry(curve.getModel(), curve.getLocalBounds().toFloat()).nodePosition(i + 1);
    }
    bool selectedDot(int i) {
        const auto& s = curve.getSelectedIndices();
        return std::find(s.begin(), s.end(), i + 1) != s.end();
    }
    int selected() const { return (int)curve.getSelectedIndices().size(); }
    double value(int i) const { return curve.getModel().getNode(i + 1).y; }
    int editSteps() const { return gestures; }
};

// Three points at beats 1, 2 and 3 on an open lane.
struct LaneAdapter
    : MouseScript<LaneAdapter>
    , automation_lanes_test::LanesPanel {
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    LaneAdapter() {
        const auto track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(track, "cutoff");
        panel.setTrackAutomationExpanded(track, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        doc.addBreakpoint(lane, 1.0, 20.0);
        doc.addBreakpoint(lane, 2.0, 60.0);
        doc.addBreakpoint(lane, 3.0, 40.0);
    }
    juce::Component& surface() { return *editor; }
    bool surfaceKey(const juce::KeyPress& k) { return editor->keyPressed(k); }
    juce::Point<float> dot(int i) {
        const auto& p = doc.getLane(lane)->points[(size_t)i];
        return {(float)panel.getViewState().beatToX(p.beat), (float)editor->valueToY(p.value)};
    }
    bool selectedDot(int i) { return editor->getPointSelection().contains(doc.getLane(lane)->points[(size_t)i].beat); }
    int selected() const { return editor->getPointSelection().size(); }
    double value(int i) const { return doc.getLane(lane)->points[(size_t)i].value; }
    int editSteps() const { return undo.getEditSerial(); }
};

template <typename Editor>
class PointEditorSharedMouse : public ::testing::Test {
protected:
    Editor editor;
};

using MouseEditorTypes = ::testing::Types<CurveAdapter, LaneAdapter>;
TYPED_TEST_SUITE(PointEditorSharedMouse, MouseEditorTypes);

} // namespace

// Regression test for the shape window's missing box select: a press on empty space did nothing there.
TYPED_TEST(PointEditorSharedMouse, ABoxOnEmptySpaceSelectsTheDotsInsideAndReplacesTheSelection) {
    auto& e = this->editor;
    e.click(e.dot(2));
    ASSERT_EQ(e.selected(), 1);

    e.box(0, 1);
    EXPECT_EQ(e.selected(), 2);
    EXPECT_TRUE(e.selectedDot(0));
    EXPECT_TRUE(e.selectedDot(1));
    EXPECT_FALSE(e.selectedDot(2)) << "a plain box replaces what was selected";
}

TYPED_TEST(PointEditorSharedMouse, AShiftBoxKeepsTheSelectionAndGrowsIt) {
    auto& e = this->editor;
    e.click(e.dot(0));
    ASSERT_EQ(e.selected(), 1);

    e.box(1, 2, kShift);
    EXPECT_EQ(e.selected(), 3);
}

TYPED_TEST(PointEditorSharedMouse, ABareClickOnEmptySpaceClearsTheSelection) {
    auto& e = this->editor;
    e.box(0, 2);
    ASSERT_EQ(e.selected(), 3);

    const int steps = e.editSteps();
    e.click(e.emptyBetween(0));
    EXPECT_EQ(e.selected(), 0);
    EXPECT_EQ(e.editSteps(), steps) << "selecting edits nothing";
}

TYPED_TEST(PointEditorSharedMouse, TheMarqueeIsPaintedWhileTheBoxIsDragged) {
    auto& e = this->editor;
    const auto from = juce::Point<float>(e.dot(0).x - 10.0f, 2.0f);
    const auto to = juce::Point<float>(e.dot(2).x + 10.0f, (float)e.surface().getHeight() - 2.0f);
    const int probeX = (int)e.dot(1).x;
    const auto before = e.render().getPixelAt(probeX, 2);

    e.press(from);
    e.dragTo(to);
    EXPECT_NE(e.render().getPixelAt(probeX, 2), before) << "the box's top edge is on screen";
    e.release(to);
    EXPECT_EQ(e.render().getPixelAt(probeX, 2), before) << "releasing takes the marquee away";
}

TYPED_TEST(PointEditorSharedMouse, ABoxSelectionMovesTogetherWithAltArrowsInOneEditStep) {
    auto& e = this->editor;
    e.box(0, 1);
    ASSERT_EQ(e.selected(), 2);
    const double a = e.value(0);
    const double b = e.value(1);
    const int steps = e.editSteps();

    EXPECT_TRUE(e.pressKey(juce::KeyPress::upKey, kAlt));
    EXPECT_GT(e.value(0), a);
    EXPECT_GT(e.value(1), b);
    EXPECT_EQ(e.editSteps(), steps + 1) << "one press, one undo step";
}

// ---- Fixed (envelope) mode: pinned dots are never boxed ----

TEST(CurveEditorBoxSelectTest, ABoxOverAnEnvelopeSelectsOnlyTheDotsThatCanMove) {
    CurveEditorComponent curve;
    curve.setBounds(0, 0, 400, 160);
    curve.setModel(synth::ui::test::buildEnvelopeModel());
    int gestures = 0;
    curve.onGestureStart = [&] { ++gestures; };

    const juce::Point<float> from(397.0f, 3.0f);
    const juce::Point<float> to(-5.0f, 165.0f);
    curve.mouseDown(synth::ui::test::curveLeftClick(curve, from));
    curve.mouseDrag(synth::ui::test::curveLeftDrag(curve, to, from));
    curve.mouseUp(synth::ui::test::curveLeftClick(curve, to));

    using synth::ui::test::kOrigin;
    const auto& picked = curve.getSelectedIndices();
    EXPECT_EQ(std::count(picked.begin(), picked.end(), (int)kOrigin), 0) << "the pinned origin is never selected";
    EXPECT_EQ((int)picked.size(), curve.getModel().getNumNodes() - 1) << "every dot that can move is";
    EXPECT_EQ(gestures, 0) << "boxing edits nothing";

    const double x = curve.getModel().getNode(synth::ui::test::kSustain).x;
    EXPECT_TRUE(curve.keyPressed(key(juce::KeyPress::rightKey, kAlt)));
    EXPECT_GT(curve.getModel().getNode(synth::ui::test::kSustain).x, x) << "the boxed dots move together";
    EXPECT_EQ(gestures, 1) << "the move is one undo step";
}
