// UndoGlideSurfacesLaneLine.cpp -- an automation lane's line when a dot comes or goes: Delete on a middle dot and a
// double-click on empty space, each undone and redone through the real AppUndoManager. drawn() is the line's height
// at the changed dot's x, so a cross-fade of two curves (the line never between them) fails the registry's glide
// checks; the test below also checks every x of every frame and the dots that stay.
#include "../Timeline/AutomationLanes/AutomationLanesTestFixture.h"
#include "UI/Layout/PointEditing/LineMorph.h"
#include "UI/Layout/ReducedMotion.h"
#include "UndoGlideSurfaces.h"
#include <cmath>
#include <gtest/gtest.h>

namespace undo_glide_test {

namespace {

using synth::ui::AutomationLaneEditor;
using Line = synth::ui::line_morph::Line;

/** A Cutoff lane with four dots; the edit is a Delete on the second or a double-click where a fifth goes. */
class LaneLineSurface : public Surface {
public:
    explicit LaneLineSurface(bool add)
        : add_(add) {
        const auto bass = panel_.doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane_ = panel_.addLane(bass, "cutoff");
        panel_.panel.setTrackAutomationExpanded(bass, true);
        editor_ = panel_.panel.laneEditorForTest(lane_);
        editor_->setTool(AutomationLaneEditor::Tool::Pointer);
        const auto points =
            add ? std::vector<std::pair<double, double>>{{1.0, 20.0}, {3.0, 30.0}, {4.0, 60.0}}
                : std::vector<std::pair<double, double>>{{1.0, 20.0}, {2.0, 90.0}, {3.0, 30.0}, {4.0, 60.0}};
        for (const auto& [beat, value] : points)
            panel_.doc.addBreakpoint(lane_, beat, value);
        editor_->forceGlideAnimateForTest(true);
        editor_->laneDocChanged();
        probeX_ = (float)panel_.panel.getViewState().beatToX(kChangedBeat);
    }

    void move() override {
        const juce::Point<float> at{probeX_, (float)editor_->valueToY(90.0)};
        if (add_) {
            editor_->mouseDoubleClick(makeClickEvent(*editor_, at, automation_lanes_test::leftButton()));
        } else {
            editor_->mouseDown(makeClickEvent(*editor_, at, automation_lanes_test::leftButton()));
            editor_->mouseUp(makeClickEvent(*editor_, at, automation_lanes_test::leftButton()));
            editor_->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
        }
        editor_->laneDocChanged();
    }
    bool undo() override { return restore(panel_.undo.undo()); }
    bool redo() override { return restore(panel_.undo.redo()); }
    double drawn() const override { return synth::ui::line_morph::lineYAt(editor_->getDrawnLineForTest(), probeX_); }
    bool gliding() const override { return editor_->getPointGlideForTest().isRunning(); }
    void stepTo(double elapsedMs) override { editor_->getPointGlideForTest().stepForTest(elapsedMs); }
    // The new line's opacity: below 1 only while Reduce Motion cross-fades the old line into it.
    float alpha() const override {
        const auto& glide = editor_->getPointGlideForTest();
        return glide.fadesOnly() ? glide.amount() : 1.0f;
    }
    juce::Component* paintedComponent() override { return editor_; }

    AutomationLaneEditor& editor() { return *editor_; }
    const std::vector<synth::ui::LaneBreakpoint>& points() const { return panel_.doc.getLane(lane_)->points; }
    float probeX() const { return probeX_; }
    static constexpr double kChangedBeat = 2.0;

private:
    bool restore(bool did) {
        editor_->laneDocChanged(); // the lane pool forwards the doc notification
        return did;
    }

    bool add_;
    automation_lanes_test::LanesPanel panel_;
    synth::LaneId lane_;
    AutomationLaneEditor* editor_ = nullptr;
    float probeX_ = 0.0f;
};

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

// Every frame of a step: the line at every x lies between the two shapes (never outside them, as two overlaid curves
// would read), it has left the old one mid-way, and the dots on both sides are drawn whole.
void expectLineGlidesAndDotsStay(LaneLineSurface& s, const Line& from, const Line& to, const char* what) {
    SCOPED_TRACE(what);
    auto& glide = s.editor().getPointGlideForTest();
    ASSERT_TRUE(glide.isRunning());
    bool leftTheOldShape = false;
    for (int frame = 0; frame <= 12 && glide.isRunning(); ++frame) {
        glide.stepForTest(16.0 * frame);
        const auto line = s.editor().getDrawnLineForTest();
        for (float x = from.front().x; x < from.back().x; x += 7.0f) {
            const float a = synth::ui::line_morph::lineYAt(from, x), b = synth::ui::line_morph::lineYAt(to, x);
            const float at = synth::ui::line_morph::lineYAt(line, x);
            EXPECT_GE(at, std::min(a, b) - 0.5f) << "frame " << frame << " x " << x;
            EXPECT_LE(at, std::max(a, b) + 0.5f) << "frame " << frame << " x " << x;
        }
        const float was = synth::ui::line_morph::lineYAt(from, s.probeX());
        const float is = synth::ui::line_morph::lineYAt(to, s.probeX());
        const float y = synth::ui::line_morph::lineYAt(line, s.probeX());
        if (std::abs(y - was) > 1.0f && std::abs(y - is) > 1.0f)
            leftTheOldShape = true;
        for (const auto& p : s.points())
            if (p.beat != LaneLineSurface::kChangedBeat)
                EXPECT_FLOAT_EQ(glide.presence(p.beat), 1.0f) << "a dot that stays never fades (frame " << frame << ")";
    }
    EXPECT_TRUE(leftTheOldShape) << "some frame draws the line between its two shapes";
}

} // namespace

std::unique_ptr<Surface> makeLaneLineDelete() { return std::make_unique<LaneLineSurface>(false); }
std::unique_ptr<Surface> makeLaneLineAdd() { return std::make_unique<LaneLineSurface>(true); }

} // namespace undo_glide_test

// Regression test for FRO2291: a lane dot deleted or added cross-faded the old curve into the new one; the line now
// glides between them while the dot shrinks or grows, and the dots that stay never blink.
TEST(LaneLineGlide, DeleteAddUndoAndRedoGlideTheLineAndKeepTheOtherDots) {
    using namespace undo_glide_test;
    for (const bool add : {false, true}) {
        SCOPED_TRACE(add ? "add" : "delete");
        FullMotion motion;
        Line before, after;
        {
            LaneLineSurface probe(add);
            before = probe.editor().getDrawnLineForTest();
            probe.move();
            probe.stepTo(1000.0);
            after = probe.editor().getDrawnLineForTest();
        }
        LaneLineSurface s(add);
        s.move();
        EXPECT_EQ((int)s.points().size(), add ? 4 : 3);
        expectLineGlidesAndDotsStay(s, before, after, add ? "the double-click" : "the Delete");
        s.stepTo(1000.0);
        ASSERT_TRUE(s.undo());
        expectLineGlidesAndDotsStay(s, after, before, "undo");
        s.stepTo(1000.0);
        ASSERT_TRUE(s.redo());
        expectLineGlidesAndDotsStay(s, before, after, "redo");
    }
}

// A Jump is drawn as a true vertical step at its LEFT point (the mirror of a Hold's at its right point), and the line
// glide of a dot added inside the Jump keeps that step vertical on every frame while its top level eases.
TEST(LaneLineGlide, AJumpIsAVerticalStepAtItsLeftPointThroughAGlide) {
    using namespace undo_glide_test;
    FullMotion motion;
    automation_lanes_test::LanesPanel panel;
    const auto bass = panel.doc.addTrack(synth::TrackKind::Midi, "Bass");
    const auto lane = panel.addLane(bass, "cutoff");
    panel.panel.setTrackAutomationExpanded(bass, true);
    auto* editor = panel.panel.laneEditorForTest(lane);
    editor->setTool(synth::ui::AutomationLaneEditor::Tool::Pointer);
    panel.doc.addBreakpoint(lane, 1.0, 20.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Jump));
    panel.doc.addBreakpoint(lane, 3.0, 80.0);
    panel.doc.addBreakpoint(lane, 4.0, 60.0);
    editor->forceGlideAnimateForTest(true);
    editor->laneDocChanged();
    const float leftX = (float)panel.panel.getViewState().beatToX(1.0);
    const auto levelsAtLeft = [&] {
        std::vector<float> ys;
        for (const auto& p : editor->getDrawnLineForTest())
            if (std::abs(p.x - leftX) < 1e-3f && (ys.empty() || std::abs(ys.back() - p.y) > 1e-3f))
                ys.push_back(p.y); // a sample column on the point's x repeats the level it leaves at
        return ys;
    };
    const auto rest = levelsAtLeft();
    ASSERT_EQ(rest.size(), 2u) << "the step's two levels share the left point's x";
    EXPECT_NEAR(rest[0], editor->valueToY(20.0), 0.5);
    EXPECT_NEAR(rest[1], editor->valueToY(80.0), 0.5);
    const float midX = (float)panel.panel.getViewState().beatToX(2.0);
    EXPECT_NEAR(synth::ui::line_morph::lineYAt(editor->getDrawnLineForTest(), midX), editor->valueToY(80.0), 0.5)
        << "flat at the end value";

    const juce::Point<float> at{midX, (float)editor->valueToY(40.0)};
    editor->mouseDoubleClick(makeClickEvent(*editor, at, automation_lanes_test::leftButton()));
    editor->laneDocChanged();
    auto& glide = editor->getPointGlideForTest();
    ASSERT_TRUE(glide.isRunning());
    for (int frame = 0; frame <= 12 && glide.isRunning(); ++frame) {
        glide.stepForTest(16.0 * frame);
        EXPECT_EQ(levelsAtLeft().size(), 2u) << "frame " << frame << ": the step stays a vertical step";
    }
}
