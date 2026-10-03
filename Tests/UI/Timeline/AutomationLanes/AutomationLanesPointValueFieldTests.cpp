// AutomationLanesPointValueFieldTests.cpp -- typing an exact value for an automation point: double-click (or Return on
// the keyboard cursor) opens the field, Return commits one undo step, Escape and invalid text commit nothing. Real
// mouse and key events on the real panel's editor.

#include "AutomationLanesTestFixture.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLanes/PointReadout/PointValueField.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::PointValueField;

namespace {

// A volume-like lane over [-60, 12] with three points at beats 1, 2 and 3 (values -6, 6, -30; the middle one carries a
// tension and a curve so a value edit can be seen to keep them). Snap is a quarter note (one beat).
struct TypedLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    TypedLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        synth::AutomationLane::RangeSnapshot range;
        range.minValue = -60.0f;
        range.maxValue = 12.0f;
        range.defaultValue = 0.0f;
        lane = doc.addLane(bass, "node-volume", "volume", range);
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        editor->valueToText = [](double v) { return juce::String(v, 1) + " dB"; };
        doc.addBreakpoint(lane, 1.0, -6.0);
        doc.addBreakpoint(lane, 2.0, 6.0, 0.4f, 1);
        doc.addBreakpoint(lane, 3.0, -30.0);
    }

    const synth::AutomationLane& theLane() const { return *doc.getLane(lane); }
    PointValueField& field() { return editor->getValueFieldForTest(); }
    juce::Point<float> at(double beat, double value) {
        return {(float)panel.getViewState().beatToX(beat), (float)editor->valueToY(value)};
    }
    void doubleClick(juce::Point<float> p) {
        editor->mouseDown(makeClickEvent(*editor, p, leftButton()));
        editor->mouseUp(makeClickEvent(*editor, p, leftButton()));
        editor->mouseDown(makeClickEvent(*editor, p, leftButton()));
        editor->mouseDoubleClick(makeClickEvent(*editor, p, leftButton()));
        editor->mouseUp(makeClickEvent(*editor, p, leftButton()));
    }
    void openOnMiddlePoint() { doubleClick(at(2.0, 6.0)); }
    // TextEditor reports a focus loss as a message, so it lands a turn after the event.
    void loseFocus() {
        field().focusLost(juce::Component::focusChangedDirectly);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }
    void type(const juce::String& text) { field().insertTextAtCaret(text); }
    bool press(int code, int mods = 0) { return field().keyPressed(juce::KeyPress(code, juce::ModifierKeys(mods), 0)); }
    double valueAt(double beat) const {
        for (const auto& p : theLane().points)
            if (p.beat == beat)
                return p.value;
        return -1000.0;
    }
};

struct ReducedMotionGuard {
    ReducedMotionGuard() { synth::ui::setReducedMotionForTest(true); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

TEST(AutomationLanesPointValueFieldTest, DoubleClickingAPointOpensTheFieldWithItsValueTextSelected) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    ASSERT_NE(f.editor, nullptr);
    f.editor->laneLabel = [] { return juce::String("Volume"); };
    EXPECT_FALSE(f.field().isOpen());
    const auto rev = f.doc.getRevision();

    f.openOnMiddlePoint();

    ASSERT_TRUE(f.field().isOpen());
    EXPECT_EQ(f.field().getText(), "6.0 dB") << "the value in the parameter's own text";
    EXPECT_EQ(f.field().getHighlightedText(), "6.0 dB") << "all of it is selected, so typing replaces it";
    EXPECT_EQ(f.field().getTitle(), "Value of Volume point");
    EXPECT_TRUE(f.field().getTooltip().contains("Return"));
    EXPECT_TRUE(f.field().getTooltip().contains("Escape"));
    EXPECT_TRUE(f.field().isVisible());
    EXPECT_FLOAT_EQ(f.field().getOpacity(), 1.0f);
    const auto point = f.at(2.0, 6.0);
    EXPECT_GT(f.field().getX(), (int)point.x) << "beside the point, not over it";
    EXPECT_LE(f.field().getY(), (int)point.y);
    EXPECT_GE(f.field().getBottom(), (int)point.y);
    EXPECT_FALSE(f.editor->getPointBubbleForTest().isShown()) << "the bubble steps aside for the field";
    EXPECT_EQ(f.doc.getRevision(), rev) << "opening writes nothing";
    EXPECT_EQ(f.theLane().points.size(), 3u) << "a double-click on a point never adds one";
}

TEST(AutomationLanesPointValueFieldTest, TypingAValueAndReturnSetsThatPointInOneUndoStep) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.openOnMiddlePoint();
    f.type("-12");
    EXPECT_EQ(f.field().getText(), "-12") << "typing replaced the selected text";
    EXPECT_TRUE(f.press(juce::KeyPress::returnKey));

    EXPECT_FALSE(f.field().isOpen());
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), -12.0);
    EXPECT_DOUBLE_EQ(f.valueAt(1.0), -6.0);
    EXPECT_DOUBLE_EQ(f.valueAt(3.0), -30.0);
    const auto& moved = f.theLane().points[1];
    EXPECT_DOUBLE_EQ(moved.beat, 2.0);
    EXPECT_FLOAT_EQ(moved.tension, 0.4f) << "only the value changed";
    EXPECT_EQ(moved.curve, 1);
    EXPECT_EQ(f.theLane().points.size(), 3u);

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), 6.0) << "one Cmd+Z puts it back";
    EXPECT_FALSE(f.undo.canUndo()) << "the edit was exactly one undo step";
}

TEST(AutomationLanesPointValueFieldTest, ATypedUnitIsAccepted) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.openOnMiddlePoint();
    f.type("-12.5 dB");
    f.press(juce::KeyPress::returnKey);
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), -12.5);
}

TEST(AutomationLanesPointValueFieldTest, EscapeCancelsWithNoDocChange) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.openOnMiddlePoint();
    f.type("-12");
    const auto rev = f.doc.getRevision();
    EXPECT_TRUE(f.press(juce::KeyPress::escapeKey));

    EXPECT_FALSE(f.field().isOpen());
    EXPECT_FALSE(f.field().isVisible());
    EXPECT_EQ(f.doc.getRevision(), rev);
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), 6.0);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_FALSE(f.editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) && f.field().isOpen());
}

TEST(AutomationLanesPointValueFieldTest, InvalidTextKeepsTheFieldOpenInTheErrorColourAndCommitsNothing) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.openOnMiddlePoint();
    const auto normal = f.field().findColour(juce::TextEditor::textColourId);
    const auto rev = f.doc.getRevision();

    for (const char* bad : {"abc", "1.2.3", "5-3", ""}) {
        f.field().setText(bad, false);
        f.press(juce::KeyPress::returnKey);
        EXPECT_TRUE(f.field().isOpen()) << bad;
        EXPECT_TRUE(f.field().isInvalid()) << bad;
        EXPECT_NE(f.field().findColour(juce::TextEditor::textColourId), normal) << "the error colour for: " << bad;
        EXPECT_EQ(f.doc.getRevision(), rev) << bad;
    }

    f.field().selectAll();
    f.field().keyPressed(juce::KeyPress('-', juce::ModifierKeys(), '-'));
    f.field().keyPressed(juce::KeyPress('3', juce::ModifierKeys(), '3'));
    EXPECT_EQ(f.field().getText(), "-3");
    EXPECT_FALSE(f.field().isInvalid()) << "typing again clears the error";
    EXPECT_EQ(f.field().findColour(juce::TextEditor::textColourId), normal);
    f.press(juce::KeyPress::returnKey);
    EXPECT_FALSE(f.field().isOpen());
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), -3.0);
}

TEST(AutomationLanesPointValueFieldTest, ATypedValueIsClampedToTheLaneRange) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.openOnMiddlePoint();
    f.type("500");
    f.press(juce::KeyPress::returnKey);
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), 12.0);

    f.doubleClick(f.at(2.0, 12.0));
    ASSERT_TRUE(f.field().isOpen());
    f.type("-500");
    f.press(juce::KeyPress::returnKey);
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), -60.0);
}

TEST(AutomationLanesPointValueFieldTest, TypingTheCurrentValueWritesNothing) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.openOnMiddlePoint();
    const auto rev = f.doc.getRevision();
    f.type("6");
    f.press(juce::KeyPress::returnKey);
    EXPECT_FALSE(f.field().isOpen());
    EXPECT_EQ(f.doc.getRevision(), rev);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(AutomationLanesPointValueFieldTest, DoubleClickingEmptySpaceStillAddsOnePointAndOpensNoField) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.doubleClick(f.at(5.0, 0.0));
    EXPECT_FALSE(f.field().isOpen());
    ASSERT_EQ(f.theLane().points.size(), 4u) << "exactly one point added";
    EXPECT_DOUBLE_EQ(f.theLane().points.back().beat, 5.0);
    f.undo.undo();
    EXPECT_EQ(f.theLane().points.size(), 3u);
}

TEST(AutomationLanesPointValueFieldTest, ReturnWithTheKeyboardCursorOnAPointOpensTheField) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    EXPECT_FALSE(f.editor->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)))
        << "no cursor point: Return belongs to someone else";
    EXPECT_FALSE(f.field().isOpen());

    ASSERT_TRUE(f.editor->keyPressed(
        juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0)));
    ASSERT_TRUE(f.editor->keyPressed(
        juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0)));
    EXPECT_TRUE(f.editor->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_TRUE(f.field().isOpen());
    EXPECT_EQ(f.field().getText(), "6.0 dB") << "the second point holds the cursor";
    f.type("0");
    f.press(juce::KeyPress::returnKey);
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), 0.0);
}

TEST(AutomationLanesPointValueFieldTest, AClickOnAPointPutsTheCursorThereSoReturnOpensIt) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    const auto p = f.at(3.0, -30.0);
    f.editor->mouseDown(makeClickEvent(*f.editor, p, leftButton()));
    f.editor->mouseUp(makeClickEvent(*f.editor, p, leftButton()));
    EXPECT_TRUE(f.editor->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_TRUE(f.field().isOpen());
    EXPECT_EQ(f.field().getText(), "-30.0 dB");
}

TEST(AutomationLanesPointValueFieldTest, TheParametersOwnParserIsUsedWhenSet) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    // A percentage over the lane's range: "50" is halfway, -24 dB.
    f.editor->textToValue = [](const juce::String& t) -> std::optional<double> {
        return -60.0 + 72.0 * t.getDoubleValue() / 100.0;
    };
    f.openOnMiddlePoint();
    f.type("50");
    f.press(juce::KeyPress::returnKey);
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), -24.0);

    f.editor->textToValue = [](const juce::String&) -> std::optional<double> { return std::nullopt; };
    f.doubleClick(f.at(2.0, -24.0));
    ASSERT_TRUE(f.field().isOpen());
    f.type("7");
    f.press(juce::KeyPress::returnKey);
    EXPECT_TRUE(f.field().isOpen()) << "a parser that refuses the text keeps the field open";
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), -24.0);
}

TEST(AutomationLanesPointValueFieldTest, LosingFocusCommitsWhatParsesAndDropsWhatDoesNot) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.openOnMiddlePoint();
    f.type("-20");
    f.loseFocus();
    EXPECT_FALSE(f.field().isOpen());
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), -20.0);

    const auto rev = f.doc.getRevision();
    f.doubleClick(f.at(2.0, -20.0));
    ASSERT_TRUE(f.field().isOpen());
    f.type("nope");
    f.loseFocus();
    EXPECT_FALSE(f.field().isOpen());
    EXPECT_EQ(f.doc.getRevision(), rev);
}

TEST(AutomationLanesPointValueFieldTest, WithSeveralPointsSelectedOnlyTheDoubleClickedOneTakesTheValue) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    ASSERT_TRUE(f.editor->selectAllPoints());
    f.openOnMiddlePoint();
    f.type("-1");
    f.press(juce::KeyPress::returnKey);
    EXPECT_DOUBLE_EQ(f.valueAt(2.0), -1.0);
    EXPECT_DOUBLE_EQ(f.valueAt(1.0), -6.0);
    EXPECT_DOUBLE_EQ(f.valueAt(3.0), -30.0);
}

TEST(AutomationLanesPointValueFieldTest, TheFieldClosesWhenItsPointIsUndoneAway) {
    ReducedMotionGuard noMotion;
    TypedLane f;
    f.doubleClick(f.at(5.0, 0.0)); // adds a point at beat 5 (one undo step)
    f.doubleClick(f.at(5.0, 0.0));
    ASSERT_TRUE(f.field().isOpen());
    f.undo.undo();
    f.editor->laneDocChanged();
    EXPECT_FALSE(f.field().isOpen()) << "nothing left to type into";
}

TEST(AutomationLanesPointValueFieldTest, ParseNumberTakesANumberWithAnOptionalUnit) {
    EXPECT_EQ(PointValueField::parseNumber("-12"), -12.0);
    EXPECT_EQ(PointValueField::parseNumber("  +3.5 "), 3.5);
    EXPECT_EQ(PointValueField::parseNumber("440 Hz"), 440.0);
    EXPECT_EQ(PointValueField::parseNumber("12%"), 12.0);
    EXPECT_EQ(PointValueField::parseNumber(".5"), 0.5);
    EXPECT_FALSE(PointValueField::parseNumber("").has_value());
    EXPECT_FALSE(PointValueField::parseNumber("dB").has_value());
    EXPECT_FALSE(PointValueField::parseNumber("-").has_value());
    EXPECT_FALSE(PointValueField::parseNumber("1.2.3").has_value());
    EXPECT_FALSE(PointValueField::parseNumber("5 - 3").has_value());
}
