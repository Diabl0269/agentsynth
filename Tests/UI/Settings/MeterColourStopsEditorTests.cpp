// MeterColourStopsEditorTests.cpp -- FRO147: Source/UI/Settings/MeterColourStopsEditor.{h,cpp},
// the Settings > Appearance "Meter Colours" section's scale/handle editor. Headless, driven with
// synthesized juce::MouseEvents for the drag gestures (docs/development/test-patterns.md's real
// mouse path, same idiom as Tests/UI/Graph/DragStateResetTests.cpp's realMouseEvent()).

#include "UI/Settings/MeterColourStopsEditor.h"
#include "UI/Theme/BuiltInThemes.h"
#include <cstdlib>
#include <gtest/gtest.h>

using namespace synth::ui;

namespace {

// Same fixed-mouseDownPosition idiom as DragStateResetTests.cpp's realMouseEvent: JUCE holds
// e.getMouseDownPosition() fixed at the original press point for the whole gesture while
// e.getPosition() tracks wherever the cursor claims to be right now.
juce::MouseEvent realMouseEvent(juce::Component& eventComp, juce::Point<int> localPos,
                                juce::Point<int> mouseDownLocalPos, bool wasDragged = false) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(), juce::ModifierKeys(),
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &eventComp, &eventComp, juce::Time::getCurrentTime(),
                            mouseDownLocalPos.toFloat(), juce::Time::getCurrentTime(), 1, wasDragged);
}

juce::MouseEvent shiftMouseEvent(juce::Component& eventComp, juce::Point<int> localPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::shiftModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &eventComp, &eventComp, juce::Time::getCurrentTime(), localPos.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

MeterColourStops fourDefaultStops() {
    return MeterColourStops({
        {kMeterMinDb, juce::Colour(0xff112233)},
        {-18.0f, juce::Colour(0xff223344)},
        {-6.0f, juce::Colour(0xff334455)},
        {0.0f, juce::Colour(0xff445566)},
    });
}

// Wires onChanged to a simple recorder so a test can assert both the intermediate (write-through,
// committed=false) and final (committed=true) calls a gesture makes.
struct ChangeRecorder {
    int callCount = 0;
    bool lastCommitted = false;
    MeterColourStops lastStops;

    std::function<void(const MeterColourStops&, bool)> callback() {
        return [this](const MeterColourStops& s, bool committed) {
            ++callCount;
            lastCommitted = committed;
            lastStops = s;
        };
    }
};

} // namespace

class MeterColourStopsEditorTest : public ::testing::Test {
protected:
    void SetUp() override {
        editor.setSize(320, 300);
        editor.setStops(fourDefaultStops());
    }

    MeterColourStopsEditor editor;
};

//==============================================================================
// Basic state
//==============================================================================

TEST_F(MeterColourStopsEditorTest, SetStopsPopulatesGetStopsAndClearsSelection) {
    ASSERT_EQ(editor.getHandleCountForTest(), 4);
    EXPECT_EQ(editor.getSelectedIndexForTest(), -1);
}

TEST_F(MeterColourStopsEditorTest, SelectForTestClampsToValidRange) {
    editor.selectForTest(2);
    EXPECT_EQ(editor.getSelectedIndexForTest(), 2);
    editor.selectForTest(99);
    EXPECT_EQ(editor.getSelectedIndexForTest(), -1);
    editor.selectForTest(-5);
    EXPECT_EQ(editor.getSelectedIndexForTest(), -1);
}

//==============================================================================
// Click a handle -> select; click its swatch -> onColourPickerRequested, no drag
//==============================================================================

TEST_F(MeterColourStopsEditorTest, ClickingAHandleBodySelectsItWithoutFiringOnChanged) {
    ChangeRecorder recorder;
    editor.onChanged = recorder.callback();

    const auto hb = editor.getHandleBoundsForTest(2);
    const auto pos = hb.getBottomRight() - juce::Point<int>(2, 2); // inside the handle, away from the swatch
    editor.mouseDown(realMouseEvent(editor, pos, pos));
    editor.mouseUp(realMouseEvent(editor, pos, pos));

    EXPECT_EQ(editor.getSelectedIndexForTest(), 2);
    EXPECT_EQ(recorder.callCount, 0) << "selecting alone must not write/persist anything";
}

// A swatch press is undecided between "click" (open the picker) and "drag" (move the handle) --
// see MeterColourStopsEditor.h's class comment. Below: a genuine click (press, release, no
// meaningful movement) opens the picker; a press-and-drag from the SAME swatch moves the stop
// like the rest of the row and opens no picker at all.

TEST_F(MeterColourStopsEditorTest, ClickingASwatchWithNoMovementOpensThePickerOnMouseUp) {
    int requestedIndex = -1;
    juce::Colour requestedColour;
    editor.onColourPickerRequested = [&](int index, juce::Rectangle<int>, juce::Colour colour) {
        requestedIndex = index;
        requestedColour = colour;
    };
    ChangeRecorder recorder;
    editor.onChanged = recorder.callback();

    const auto swatch = editor.getSwatchBoundsForTest(1);
    const auto pos = swatch.getCentre();
    editor.mouseDown(realMouseEvent(editor, pos, pos));

    EXPECT_EQ(requestedIndex, -1) << "the picker must not open on mouseDown -- a click isn't known "
                                     "to be a click until mouseUp";

    editor.mouseUp(realMouseEvent(editor, pos, pos));

    EXPECT_EQ(requestedIndex, 1);
    EXPECT_EQ(requestedColour, editor.getStops().getStops()[1].colour);
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[1].dbFrom, -18.0f) << "a plain click must not move the stop";
    EXPECT_EQ(recorder.callCount, 0) << "opening the picker alone must not fire onChanged";
}

TEST_F(MeterColourStopsEditorTest, ClickingASwatchThatMovesOnlyAFewPxStillCountsAsAClick) {
    // Real presses never land pixel-perfect still -- anything short of kSwatchDragThresholdPx must
    // still read as a click, not a drag.
    int requestedIndex = -1;
    editor.onColourPickerRequested = [&](int index, juce::Rectangle<int>, juce::Colour) { requestedIndex = index; };

    const auto swatch = editor.getSwatchBoundsForTest(1);
    const auto pos = swatch.getCentre();
    const auto jitterPos = pos + juce::Point<int>(0, MeterColourStopsEditor::kSwatchDragThresholdPx - 1);
    editor.mouseDown(realMouseEvent(editor, pos, pos));
    editor.mouseDrag(realMouseEvent(editor, jitterPos, pos, true));
    editor.mouseUp(realMouseEvent(editor, jitterPos, pos, true));

    EXPECT_EQ(requestedIndex, 1);
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[1].dbFrom, -18.0f) << "a sub-threshold jitter must not move the stop";
}

TEST_F(MeterColourStopsEditorTest, DraggingFromASwatchMovesTheHandleAndOpensNoPicker) {
    int requestedIndex = -1;
    editor.onColourPickerRequested = [&](int index, juce::Rectangle<int>, juce::Colour) { requestedIndex = index; };
    ChangeRecorder recorder;
    editor.onChanged = recorder.callback();

    const auto swatch = editor.getSwatchBoundsForTest(2); // -6 dB
    const auto startPos = swatch.getCentre();
    const int targetY = editor.yForDbForTest(-3.0f);
    const auto dragPos = juce::Point<int>(startPos.x, targetY);

    editor.mouseDown(realMouseEvent(editor, startPos, startPos));
    editor.mouseDrag(realMouseEvent(editor, dragPos, startPos, true));

    EXPECT_EQ(requestedIndex, -1) << "a press that has already moved past the threshold must not open the picker";
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[2].dbFrom, -3.0f);
    EXPECT_GE(recorder.callCount, 1);
    EXPECT_FALSE(recorder.lastCommitted);

    editor.mouseUp(realMouseEvent(editor, dragPos, startPos, true));

    EXPECT_EQ(requestedIndex, -1) << "mouseUp after a real drag must still not open the picker";
    EXPECT_TRUE(recorder.lastCommitted);
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[2].dbFrom, -3.0f);
}

TEST_F(MeterColourStopsEditorTest, DraggingFromTheFloorsSwatchOpensNoPickerAndNeverMovesIt) {
    // The floor never drags positionally -- dragging its swatch past the threshold must still
    // suppress the click-to-open behaviour (the gesture committed to "drag", it just had nothing
    // to move), not silently fall back to opening the picker.
    int requestedIndex = -1;
    editor.onColourPickerRequested = [&](int index, juce::Rectangle<int>, juce::Colour) { requestedIndex = index; };

    const auto swatch = editor.getSwatchBoundsForTest(0);
    const auto startPos = swatch.getCentre();
    const auto dragPos = startPos + juce::Point<int>(0, 50);

    editor.mouseDown(realMouseEvent(editor, startPos, startPos));
    editor.mouseDrag(realMouseEvent(editor, dragPos, startPos, true));
    editor.mouseUp(realMouseEvent(editor, dragPos, startPos, true));

    EXPECT_EQ(requestedIndex, -1);
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[0].dbFrom, kMeterMinDb);
}

//==============================================================================
// Dragging -- snap, clamp to the scale, and clamp to neighbours
//==============================================================================

TEST_F(MeterColourStopsEditorTest, DraggingAHandleMovesItsDbWithSnapAndFiresLiveThenCommittedChanges) {
    ChangeRecorder recorder;
    editor.onChanged = recorder.callback();

    const int targetY = editor.yForDbForTest(-3.0f); // a value strictly between neighbours -18/-6/0's own stops
    const auto startPos = editor.getHandleBoundsForTest(2).getCentre();
    const auto dragPos = juce::Point<int>(startPos.x, targetY);

    editor.mouseDown(realMouseEvent(editor, startPos, startPos));
    editor.mouseDrag(realMouseEvent(editor, dragPos, startPos, true));
    EXPECT_GE(recorder.callCount, 1);
    EXPECT_FALSE(recorder.lastCommitted) << "an in-progress drag frame must not be reported as committed";
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[2].dbFrom, -3.0f);

    editor.mouseUp(realMouseEvent(editor, dragPos, startPos, true));
    EXPECT_TRUE(recorder.lastCommitted) << "mouse up must fire a final, committed change";
}

TEST_F(MeterColourStopsEditorTest, DraggingClampsAtTheUpperNeighbourRatherThanCrossingIt) {
    // Index 2 (-6 dB)'s upper neighbour is index 3 (0 dB) -- dragging past it must stop kDbSnap
    // below the neighbour, never land on or above it.
    const auto startPos = editor.getHandleBoundsForTest(2).getCentre();
    const int farAboveY = editor.yForDbForTest(kMeterMaxDb); // as far up the scale as the drag can reach
    editor.mouseDown(realMouseEvent(editor, startPos, startPos));
    editor.mouseDrag(realMouseEvent(editor, {startPos.x, farAboveY}, startPos, true));

    EXPECT_FLOAT_EQ(editor.getStops().getStops()[2].dbFrom, 0.0f - MeterColourStopsEditor::kDbSnap);
}

TEST_F(MeterColourStopsEditorTest, DraggingClampsAtTheLowerNeighbourRatherThanCrossingIt) {
    // Index 2 (-6 dB)'s lower neighbour is index 1 (-18 dB).
    const auto startPos = editor.getHandleBoundsForTest(2).getCentre();
    const int farBelowY = editor.yForDbForTest(kMeterMinDb);
    editor.mouseDown(realMouseEvent(editor, startPos, startPos));
    editor.mouseDrag(realMouseEvent(editor, {startPos.x, farBelowY}, startPos, true));

    EXPECT_FLOAT_EQ(editor.getStops().getStops()[2].dbFrom, -18.0f + MeterColourStopsEditor::kDbSnap);
}

TEST_F(MeterColourStopsEditorTest, TheFloorStopNeverMovesEvenWhenDragged) {
    const auto floorPos = editor.getHandleBoundsForTest(0).getBottomRight() - juce::Point<int>(2, 2);
    editor.mouseDown(realMouseEvent(editor, floorPos, floorPos));
    EXPECT_EQ(editor.getSelectedIndexForTest(), 0) << "the floor is still selectable";

    const auto dragTo = juce::Point<int>(floorPos.x, editor.yForDbForTest(-3.0f));
    editor.mouseDrag(realMouseEvent(editor, dragTo, floorPos, true));
    editor.mouseUp(realMouseEvent(editor, dragTo, floorPos, true));

    EXPECT_FLOAT_EQ(editor.getStops().getStops()[0].dbFrom, kMeterMinDb);
}

//==============================================================================
// Click empty area -> add a stop
//==============================================================================

TEST_F(MeterColourStopsEditorTest, ClickingEmptyAreaAddsAStopAtTheSnappedDbAndSelectsIt) {
    ChangeRecorder recorder;
    editor.onChanged = recorder.callback();

    const int y = editor.yForDbForTest(-40.0f); // well clear of every existing handle's row
    editor.mouseDown(realMouseEvent(editor, {editor.getWidth() - 10, y}, {editor.getWidth() - 10, y}));

    ASSERT_EQ(editor.getHandleCountForTest(), 5);
    EXPECT_TRUE(recorder.lastCommitted);
    const int newIndex = editor.getSelectedIndexForTest();
    ASSERT_GE(newIndex, 0);
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[(size_t)newIndex].dbFrom,
                    MeterColourStopsEditor::snapDbForTest(-40.0f));
}

TEST_F(MeterColourStopsEditorTest, AddIsRefusedOnceEightStopsAreAlreadyPresent) {
    std::vector<MeterColourStop> eight;
    for (int i = 0; i < MeterColourStops::kMaxStops; ++i)
        eight.push_back({kMeterMinDb + (float)i * 9.0f, juce::Colour((juce::uint8)(i * 10), 0, 0)});
    editor.setStops(MeterColourStops(eight));
    ASSERT_EQ(editor.getHandleCountForTest(), MeterColourStops::kMaxStops);

    const int y = editor.yForDbForTest(2.0f);
    editor.mouseDown(realMouseEvent(editor, {editor.getWidth() - 10, y}, {editor.getWidth() - 10, y}));
    EXPECT_EQ(editor.getHandleCountForTest(), MeterColourStops::kMaxStops);
}

TEST_F(MeterColourStopsEditorTest, AddIsRefusedWhenItWouldCollideWithAnExistingStop) {
    // A 0.5 dB snap bucket is normally only a couple of px wide at this component's typical
    // Settings-panel size -- entirely swallowed by a handle's own fixed 22px hit box, so there is
    // no "outside the handle but the same bucket" pixel to click at a realistic size. Blow the
    // component up tall enough that a bucket's pixel span exceeds the handle height everywhere on
    // the scale (the taper's FINEST resolution, near 0 dB, is ~1% of the bar height per 0.5 dB --
    // 3000px comfortably clears kHandleHeight=22px there) so the geometry search below is
    // guaranteed to find a real, clickable colliding point.
    editor.setSize(320, 3000);
    // Find a y just OUTSIDE stop 1's own handle box (so hitTest reads it as "empty area") whose
    // snapped dB still equals stop 1's own dbFrom -- geometry-driven so this does not depend on
    // any private layout constant.
    const auto targetDb = editor.getStops().getStops()[1].dbFrom; // -18
    const auto handleBox = editor.getHandleBoundsForTest(1);
    int collidingY = -1;
    for (int y = 0; y < editor.getHeight(); ++y) {
        const juce::Point<int> p(handleBox.getCentreX(), y);
        if (handleBox.contains(p))
            continue; // still inside the handle -- would select it, not add
        if (juce::approximatelyEqual(MeterColourStopsEditor::snapDbForTest(editor.dbForYForTest(y)), targetDb)) {
            collidingY = y;
            break;
        }
    }
    ASSERT_GE(collidingY, 0) << "no colliding-but-outside-handle Y found -- check the editor's own sizing";

    const int before = editor.getHandleCountForTest();
    editor.mouseDown(
        realMouseEvent(editor, {handleBox.getCentreX(), collidingY}, {handleBox.getCentreX(), collidingY}));
    EXPECT_EQ(editor.getHandleCountForTest(), before) << "a colliding add must be refused, not silently deduped away";
}

//==============================================================================
// Remove
//==============================================================================

TEST_F(MeterColourStopsEditorTest, RemoveSelectedStopDeletesItAndFiresACommittedChange) {
    ChangeRecorder recorder;
    editor.onChanged = recorder.callback();
    editor.selectForTest(2);

    editor.removeSelectedStop();

    EXPECT_EQ(editor.getHandleCountForTest(), 3);
    EXPECT_TRUE(recorder.lastCommitted);
}

TEST_F(MeterColourStopsEditorTest, RemoveIsANoOpWithNothingSelected) {
    editor.removeSelectedStop();
    EXPECT_EQ(editor.getHandleCountForTest(), 4);
}

TEST_F(MeterColourStopsEditorTest, TheFloorStopCanNeverBeRemoved) {
    editor.selectForTest(0);
    editor.removeSelectedStop();
    EXPECT_EQ(editor.getHandleCountForTest(), 4);
}

TEST_F(MeterColourStopsEditorTest, TheLastRemainingStopCanNeverBeRemoved) {
    editor.setStops(MeterColourStops({{kMeterMinDb, juce::Colour(0xff112233)}}));
    editor.selectForTest(0);
    editor.removeSelectedStop();
    ASSERT_EQ(editor.getHandleCountForTest(), 1) << "the floor guard alone already covers this, but pin it directly";
}

//==============================================================================
// Colour
//==============================================================================

TEST_F(MeterColourStopsEditorTest, SetStopColourRecoloursInPlaceWithoutMovingTheDb) {
    ChangeRecorder recorder;
    editor.onChanged = recorder.callback();

    editor.setStopColour(1, juce::Colour(0xffff00ff), true);

    EXPECT_EQ(editor.getStops().getStops()[1].colour, juce::Colour(0xffff00ff));
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[1].dbFrom, -18.0f);
    EXPECT_TRUE(recorder.lastCommitted);
}

//==============================================================================
// Keyboard: Delete/Backspace, Up/Down nudge (+ Shift), floor exemption
//==============================================================================

TEST_F(MeterColourStopsEditorTest, DeleteKeyRemovesTheSelectedStop) {
    editor.selectForTest(1);
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(editor.getHandleCountForTest(), 3);
}

TEST_F(MeterColourStopsEditorTest, BackspaceKeyRemovesTheSelectedStop) {
    editor.selectForTest(1);
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    EXPECT_EQ(editor.getHandleCountForTest(), 3);
}

TEST_F(MeterColourStopsEditorTest, UpArrowNudgesTheSelectedStopUpByHalfADb) {
    editor.selectForTest(2); // -6 dB
    ChangeRecorder recorder;
    editor.onChanged = recorder.callback();

    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));

    EXPECT_FLOAT_EQ(editor.getStops().getStops()[2].dbFrom, -5.5f);
    EXPECT_TRUE(recorder.lastCommitted) << "a keyboard nudge is a discrete, committed edit";
}

TEST_F(MeterColourStopsEditorTest, DownArrowNudgesTheSelectedStopDownByHalfADb) {
    editor.selectForTest(2); // -6 dB
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[2].dbFrom, -6.5f);
}

TEST_F(MeterColourStopsEditorTest, ShiftUpArrowNudgesByThreeDb) {
    editor.selectForTest(2); // -6 dB
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[2].dbFrom, -3.0f);
}

TEST_F(MeterColourStopsEditorTest, NudgeStillClampsAtANeighbour) {
    editor.selectForTest(3); // 0 dB, the topmost stop -- its neighbour ceiling is kMeterMaxDb
    for (int i = 0; i < 20; ++i)
        editor.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
    EXPECT_LE(editor.getStops().getStops()[3].dbFrom, kMeterMaxDb);
}

TEST_F(MeterColourStopsEditorTest, ArrowKeysOnTheFloorAreConsumedButNeverMoveIt) {
    editor.selectForTest(0);
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_FLOAT_EQ(editor.getStops().getStops()[0].dbFrom, kMeterMinDb);
}

TEST_F(MeterColourStopsEditorTest, KeyPressesAreIgnoredWithNothingSelected) {
    EXPECT_FALSE(editor.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_FALSE(editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
}

//==============================================================================
// Reset (setStops from the owner, e.g. "Reset to Theme")
//==============================================================================

TEST_F(MeterColourStopsEditorTest, SetStopsFromTheOwnerReplacesTheWorkingSetAndClearsSelection) {
    editor.selectForTest(2);
    const auto themeStops = MeterColourStops::fromTheme(synth::theme::makeObsidian().colors);
    editor.setStops(themeStops);

    EXPECT_EQ(editor.getSelectedIndexForTest(), -1);
    ASSERT_EQ(editor.getHandleCountForTest(), (int)themeStops.getStops().size());
}

//==============================================================================
// PNG render-to-file inspection -- same convention as MixerColumnComponentMeterTests.cpp's
// ClippedMeterRendersToPngForVisualInspection (docs/development/test-patterns.md's real mouse path
// neighbourhood): always painted offscreen so the content assertions run unconditionally in CI,
// written to disk only when METER_COLOUR_EDITOR_PNG is set.
//==============================================================================

TEST_F(MeterColourStopsEditorTest, RendersToPngForVisualInspection) {
    editor.setStops(MeterColourStops::fromTheme(synth::theme::makeObsidian().colors));
    editor.selectForTest(2);

    juce::Image img(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(editor.paint(g));

    // Something was actually drawn -- not a blank/transparent image.
    bool sawOpaquePixel = false;
    for (int y = 0; y < img.getHeight() && !sawOpaquePixel; ++y)
        for (int x = 0; x < img.getWidth() && !sawOpaquePixel; ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                sawOpaquePixel = true;
    EXPECT_TRUE(sawOpaquePixel);

    if (const char* path = std::getenv("METER_COLOUR_EDITOR_PNG")) {
        juce::File file(path);
        juce::FileOutputStream stream(file);
        if (stream.openedOk()) {
            juce::PNGImageFormat png;
            png.writeImageToStream(img, stream);
        }
    } else {
        GTEST_SKIP() << "set METER_COLOUR_EDITOR_PNG=<path> to write the render to disk";
    }
}
