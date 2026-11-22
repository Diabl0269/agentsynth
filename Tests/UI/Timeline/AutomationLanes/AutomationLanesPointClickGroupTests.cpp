// AutomationLanesPointClickGroupTests.cpp -- a plain click on one of a selected group of lane points picks just that
// point on release, a jittery click is still a click, and a box leaves the cursor on the point it took in last.
// Real mouse events on the real panel's editor.

#include "AutomationLanesTestFixture.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;

namespace {

// Three points at beats 1, 2 and 3 (values 20, 60, 40) on an open lane.
struct SelectLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    SelectLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        doc.addBreakpoint(lane, 1.0, 20.0);
        doc.addBreakpoint(lane, 2.0, 60.0);
        doc.addBreakpoint(lane, 3.0, 40.0);
    }

    juce::Point<float> at(double beat, double value) {
        return {(float)panel.getViewState().beatToX(beat), (float)editor->valueToY(value)};
    }
    juce::Point<float> top(double beat) { return {(float)panel.getViewState().beatToX(beat), 2.0f}; }
    void click(juce::Point<float> p) {
        editor->mouseDown(makeClickEvent(*editor, p, leftButton()));
        editor->mouseUp(makeClickEvent(*editor, p, leftButton()));
    }
    bool key(const juce::KeyPress& k) { return editor->keyPressed(k); }
    std::vector<double> selected() const { return editor->getPointSelection().getSelected(); }
};

using Beats = std::vector<double>;

} // namespace

TEST(AutomationLanesClickGroupTest, AClickOnOneOfAGroupPicksJustThatPointOnRelease) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
    const auto rev = f.doc.getRevision();
    const int steps = f.undo.getEditSerial();
    f.click(f.at(2.0, 60.0));
    EXPECT_EQ(f.selected(), Beats({2.0}))
        << "a click that never travels picks just that point, as in the curve editors";
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(2.0));
    EXPECT_EQ(f.doc.getRevision(), rev);
    EXPECT_EQ(f.undo.getEditSerial(), steps) << "a click opens no undo step";
}

TEST(AutomationLanesClickGroupTest, ThePressKeepsTheGroupUntilTheReleaseSoItCanBeDragged) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    const auto p = f.at(2.0, 60.0);
    f.editor->mouseDown(makeClickEvent(*f.editor, p, leftButton()));
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0})) << "still the group while the button is down";
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(2.0));
    f.editor->mouseUp(makeClickEvent(*f.editor, p, leftButton()));
    EXPECT_EQ(f.selected(), Beats({2.0}));
}

TEST(AutomationLanesClickGroupTest, AClickThatJittersAPixelOrTwoIsStillAClick) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0, 3.0});
    const auto rev = f.doc.getRevision();
    const auto p = f.at(2.0, 60.0);
    const auto to = p + juce::Point<float>(2.0f, 1.0f);
    f.editor->mouseDown(makeClickEvent(*f.editor, p, leftButton()));
    f.editor->mouseDrag(makeClickEvent(*f.editor, to, leftButton())); // under JUCE's drag threshold: not a drag
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0, 3.0}));
    f.editor->mouseUp(makeClickEvent(*f.editor, to, leftButton()));
    EXPECT_EQ(f.selected(), Beats({2.0})) << "no group move began, so the release picks the point";
    EXPECT_EQ(f.doc.getRevision(), rev);
}

TEST(AutomationLanesClickGroupTest, EscapeDuringAPressOnOneOfAGroupKeepsTheGroup) {
    SelectLane f;
    f.editor->getPointSelection().setSelection({1.0, 2.0});
    const auto p = f.at(2.0, 60.0);
    f.editor->mouseDown(makeClickEvent(*f.editor, p, leftButton()));
    f.editor->mouseDrag(makeClickEvent(*f.editor, p + juce::Point<float>(1.0f, 0.0f), leftButton()));
    EXPECT_TRUE(f.key(juce::KeyPress(juce::KeyPress::escapeKey)));
    f.editor->mouseUp(makeClickEvent(*f.editor, p, leftButton()));
    EXPECT_EQ(f.selected(), Beats({1.0, 2.0})) << "a cancelled press changes nothing";
}

TEST(AutomationLanesClickGroupTest, ABoxLeavesTheCursorOnThePointItTookInLastFromEitherSide) {
    SelectLane f;
    // Small steps, so the points enter the box one at a time the way a hand draws it.
    dragAcross(*f.editor, f.top(0.5), juce::Point<float>(f.at(2.5, 0).x, (float)f.editor->getHeight() - 2.0f), 80,
               leftButton());
    ASSERT_EQ(f.selected(), Beats({1.0, 2.0}));
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(2.0)) << "grown rightward";
    dragAcross(*f.editor, f.top(3.5), juce::Point<float>(f.at(1.5, 0).x, (float)f.editor->getHeight() - 2.0f), 80,
               leftButton());
    ASSERT_EQ(f.selected(), Beats({2.0, 3.0}));
    EXPECT_EQ(f.editor->getPointSelection().getCursor(), std::optional<double>(2.0)) << "grown leftward";
}
