// AutomationLanesRangeToolTests.cpp -- the Range tool on every lane surface: a
// lane's own curve and a modulator row's amount curve. A drag selects a stretch
// owned by the surface it began on, the lane verbs (stamp a shape, delete the
// points) act on that surface and are ONE undo step, and what a press does
// matches the cursor the surface shows. Real panel, real mouse events.

#include "AutomationLanesAmountFixture.h"
#include "UI/Timeline/AutomationLanes/LaneToolCursors.h"
#include "UI/Timeline/EditTool.h"

using namespace amount_test;
using synth::ui::EditTool;

namespace {
const juce::MouseCursor kArrow(juce::MouseCursor::NormalCursor);
constexpr int kLinear = static_cast<int>(synth::BreakpointCurve::Linear);

// Every surface of the lane block a tool can act on. A new lane surface joins
// this list, and so the behaviour loop.
enum class Surface { LaneEditor, BandWithAmount, FlatBand };
constexpr Surface kAllSurfaces[] = {Surface::LaneEditor, Surface::BandWithAmount, Surface::FlatBand};

bool isBand(Surface surface) { return surface != Surface::LaneEditor; }
double midValue(Surface surface) { return isBand(surface) ? 0.0 : 0.5; }

synth::ui::AutomationLaneEditor* editorOf(AmountScene& s, Surface surface) {
    return isBand(surface) ? s.band()->getEditor() : s.panel().laneEditorForTest(s.lane);
}

const synth::AutomationLane* laneOf(AmountScene& s, Surface surface) {
    return isBand(surface) ? s.amountLane() : s.doc().getLane(s.lane);
}

std::vector<double> beatsOf(const synth::AutomationLane* lane) {
    std::vector<double> beats;
    if (lane != nullptr)
        for (const auto& point : lane->points)
            beats.push_back(point.beat);
    return beats;
}

// Three flat points at beats 8, 12 and 16, written without an undo step so the
// stack starts at the verb under test.
void seedPoints(AmountScene& s, Surface surface) {
    const double value = midValue(surface);
    if (surface == Surface::LaneEditor) {
        for (const double beat : {8.0, 12.0, 16.0})
            ASSERT_TRUE(s.doc().addBreakpoint(s.lane, beat, value));
    } else if (surface == Surface::BandWithAmount) {
        const std::vector<synth::AutomationLane::Breakpoint> points{
            {8.0, value, 0.0f, kLinear}, {12.0, value, 0.0f, kLinear}, {16.0, value, 0.0f, kLinear}};
        ASSERT_TRUE(synth::ui::writeAmountLane(s.doc(), s.track, s.attenUuid, points));
    }
    s.flush();
}

// A press at `fromBeat` and a drag to `toBeat` along the points' line,
// delivered to what a real click lands on.
void pressDrag(AmountScene& s, Surface surface, double fromBeat, double toBeat) {
    auto* editor = editorOf(s, surface);
    ASSERT_NE(editor, nullptr);
    const float y = (float)editor->valueToY(midValue(surface));
    if (surface == Surface::LaneEditor)
        dragAcross(*editor, {s.x(fromBeat), y}, {s.x(toBeat), y}, 12);
    else
        s.drag({s.x(fromBeat), y}, {s.x(toBeat), y}, 12);
    s.flush();
}

juce::MouseCursor cursorOf(AmountScene& s, Surface surface) {
    return isBand(surface) ? s.band()->getMouseCursor() : editorOf(s, surface)->getMouseCursor();
}

synth::ui::LaneRangeSelection& rangeOf(AmountScene& s) { return s.panel().getAutomationLanes().getLaneRange(); }
} // namespace

// Regression test for FRO1473: the Range tool showed its cursor on a mod row's
// band but the drag behaved like the pointer, because only lane editors were
// handed the lane range.
TEST_F(TimelinePanelIntegrationTest, ARangeDragOnALaneEditorBeginsAndExtendsTheRangeOwnedByThatEditor) {
    AmountScene s(EditTool::Range);
    auto* editor = editorOf(s, Surface::LaneEditor);
    ASSERT_NE(editor, nullptr);
    const float y = (float)editor->valueToY(0.5);

    editor->mouseDown(makeClickEvent(*editor, {s.x(8.0), y}, leftButton()));
    EXPECT_TRUE(rangeOf(s).isActive());
    EXPECT_EQ(rangeOf(s).getOwner(), editor);
    EXPECT_FALSE(rangeOf(s).hasWidth());
    editor->mouseDrag(makeDragEvent(*editor, {s.x(20.0), y}, {s.x(8.0), y}, leftButton()));
    EXPECT_TRUE(rangeOf(s).hasWidth());
    EXPECT_DOUBLE_EQ(rangeOf(s).getStartBeat(), 8.0);
    EXPECT_DOUBLE_EQ(rangeOf(s).getEndBeat(), 20.0);
    editor->mouseUp(makeDragEvent(*editor, {s.x(20.0), y}, {s.x(8.0), y}, leftButton()));
    EXPECT_TRUE(s.panel().getAutomationLanes().hasLaneRange());
    EXPECT_TRUE(s.panel().getAutomationLanes().laneRangeIsOnLane(s.lane));
}

// Regression test for FRO1473: the band's private doc reuses the real docs'
// lane ids, so a range on the band must be told apart from a range on the lane
// that happens to share its id.
TEST_F(TimelinePanelIntegrationTest, ARangeDragOnAModBandIsOwnedByTheBandAndNeverReadAsTheLanesOwn) {
    AmountScene s(EditTool::Range);
    seedPoints(s, Surface::BandWithAmount);
    auto* band = s.band();
    ASSERT_NE(band->getEditor(), nullptr);

    pressDrag(s, Surface::BandWithAmount, 8.0, 20.0);

    EXPECT_TRUE(rangeOf(s).hasWidth()) << "the drag selected a stretch";
    EXPECT_DOUBLE_EQ(rangeOf(s).getStartBeat(), 8.0);
    EXPECT_DOUBLE_EQ(rangeOf(s).getEndBeat(), 20.0);
    EXPECT_EQ(rangeOf(s).getOwner(), band->getEditor());
    EXPECT_TRUE(s.panel().getAutomationLanes().hasLaneRange());
    EXPECT_TRUE(band->getEditor()->getShapeGesture().rangeIsOnThisLane());

    auto* laneEditor = s.panel().laneEditorForTest(s.lane);
    ASSERT_NE(laneEditor, nullptr);
    EXPECT_FALSE(laneEditor->getShapeGesture().rangeIsOnThisLane()) << "not on the lane, whatever its id";
    EXPECT_FALSE(s.panel().getAutomationLanes().laneRangeIsOnLane(s.lane));
    auto* header = s.panel().laneHeaderForTest(s.lane);
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(static_cast<bool>(header->laneRangeProvider));
    EXPECT_FALSE(header->laneRangeProvider().has_value()) << "so Create custom LFO is not offered for it";
}

TEST_F(TimelinePanelIntegrationTest, StampingAndDeletingOverAModBandRangeActOnTheAmountLaneEachInOneUndoStep) {
    AmountScene s(EditTool::Range);
    seedPoints(s, Surface::BandWithAmount);
    auto& lanes = s.panel().getAutomationLanes();
    const auto seeded = beatsOf(s.amountLane());
    const auto laneBefore = beatsOf(s.doc().getLane(s.lane));

    pressDrag(s, Surface::BandWithAmount, 8.0, 16.0);
    ASSERT_TRUE(lanes.hasLaneRange());
    ASSERT_TRUE(lanes.stampShapeOnLaneRange(synth::ui::DrawShape::Sine));
    const auto stamped = beatsOf(s.amountLane());
    EXPECT_NE(stamped, seeded) << "the amount lane took the shape";
    EXPECT_GT(stamped.size(), seeded.size());
    EXPECT_EQ(beatsOf(s.doc().getLane(s.lane)), laneBefore) << "the lane itself is untouched";
    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(beatsOf(s.amountLane()), seeded) << "one undo restores the stamp";

    pressDrag(s, Surface::BandWithAmount, 8.0, 16.0);
    ASSERT_TRUE(lanes.hasLaneRange());
    ASSERT_TRUE(lanes.deleteLaneRangePoints());
    EXPECT_EQ(s.amountLane(), nullptr) << "every point sat inside the range, so the lane went back to the knob";
    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(beatsOf(s.amountLane()), seeded) << "one undo restores the deleted points";
}

TEST_F(TimelinePanelIntegrationTest, ARangeOnTheLanesOwnCurveStillStampsTheLaneAndNotTheAmountLane) {
    AmountScene s(EditTool::Range);
    seedPoints(s, Surface::LaneEditor);
    seedPoints(s, Surface::BandWithAmount);
    const auto amountBefore = beatsOf(s.amountLane());

    pressDrag(s, Surface::LaneEditor, 8.0, 16.0);
    ASSERT_TRUE(s.panel().getAutomationLanes().deleteLaneRangePoints());
    EXPECT_TRUE(beatsOf(s.doc().getLane(s.lane)).empty()) << "the lane's points went";
    EXPECT_EQ(beatsOf(s.amountLane()), amountBefore) << "the amount lane's did not";
}

TEST_F(TimelinePanelIntegrationTest, ARangeFollowsTheSelectToolsNextPressOnALaneAndOnABand) {
    for (const auto surface : {Surface::LaneEditor, Surface::BandWithAmount}) {
        AmountScene s(EditTool::Range);
        seedPoints(s, surface);
        pressDrag(s, surface, 8.0, 16.0);
        ASSERT_TRUE(rangeOf(s).hasWidth()) << "surface " << (int)surface;
        s.panel().setActiveTool(EditTool::Select);
        EXPECT_TRUE(rangeOf(s).isActive()) << "picking a tool keeps the range";
        pressDrag(s, surface, 20.0, 22.0);
        EXPECT_FALSE(rangeOf(s).isActive()) << "the next press with the Select tool drops it, surface " << (int)surface;
    }
}

// Guard for the sibling-gap kind on lane surfaces: for every surface and every
// tool, what a press-and-drag does is what the cursor promised. Range cursor ->
// a range starts (owned by that surface), Draw cursor -> the curve is drawn,
// Erase cursor -> points go, arrow -> none of that. A flat band (no amount lane
// yet) takes only the Draw tool's press, so it shows the arrow for the rest. A
// new lane surface joins kAllSurfaces.
TEST_F(TimelinePanelIntegrationTest, EverySurfaceActsOnAPressTheWayItsCursorPromisesForEveryTool) {
    for (const auto surface : kAllSurfaces) {
        for (const auto tool : synth::ui::kAllEditTools) {
            AmountScene s;
            seedPoints(s, surface);
            s.panel().setActiveTool(tool);
            const bool toolCursor = !(cursorOf(s, surface) == kArrow);
            const auto revisionBefore = s.doc().getRevision();
            const auto pointsBefore = beatsOf(laneOf(s, surface));
            const auto context =
                juce::String("surface ") + juce::String((int)surface) + ", tool " + juce::String((int)tool);

            pressDrag(s, surface, 6.0, 18.0);

            const bool rangeStarted = rangeOf(s).isActive() && rangeOf(s).getOwner() == editorOf(s, surface);
            const bool docChanged = s.doc().getRevision() != revisionBefore;
            EXPECT_EQ(toolCursor, rangeStarted || docChanged) << context;
            switch (tool) {
            case EditTool::Range:
                EXPECT_EQ(rangeStarted, toolCursor) << context;
                EXPECT_FALSE(docChanged) << context;
                break;
            case EditTool::Erase:
                EXPECT_EQ(beatsOf(laneOf(s, surface)).size() < pointsBefore.size(), toolCursor) << context;
                break;
            case EditTool::Draw:
                EXPECT_EQ(docChanged, toolCursor) << context;
                break;
            default:
                EXPECT_FALSE(toolCursor) << context;
                EXPECT_FALSE(rangeOf(s).isActive()) << context;
                EXPECT_FALSE(docChanged) << context;
            }
        }
    }
}
