// AutomationLanesAmountBandTests.cpp -- the modulator band as an amount lane, against a real MainComponent and
// real presses routed to whichever component takes them: no lane until the first stroke (which creates it with
// its points in one undo step), the knob drag, Up/Down and double-click while there is none, editing and erasing
// an existing lane (the last point takes the lane), the bipolar picture, and the band's keyboard and
// screen-reader surface.

#include "AutomationLanesAmountFixture.h"

using namespace amount_test;

namespace {
juce::Image renderBand(AmountScene& s) {
    auto* editor = s.band()->getEditor();
    juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    editor->paintEntireComponent(g, false);
    return image;
}
} // namespace

TEST_F(TimelinePanelIntegrationTest, NoAmountLaneUntilTheFirstPenStrokeWhichCreatesItWithItsPointsInOneUndoStep) {
    AmountScene s;
    ASSERT_NE(s.band(), nullptr);
    ASSERT_TRUE(s.attenUuid.isNotEmpty());
    EXPECT_EQ(s.amountLane(), nullptr) << "a new modulator has no amount lane: its knob plays";
    const auto& proxy = s.band()->getProxyDoc();
    const auto* flat = proxy.getLane(s.band()->getEditor()->getActiveLane());
    ASSERT_NE(flat, nullptr);
    EXPECT_FLOAT_EQ(flat->range.defaultValue, 0.5f) << "the flat line sits at the knob's amount";
    EXPECT_TRUE(flat->points.empty());
    const auto before = s.doc().toVar();

    NotificationCounter counter;
    s.doc().addListener(&counter);
    s.penStroke(8.0, -0.5, 24.0, 0.5);
    s.doc().removeListener(&counter);

    const auto* lane = s.amountLane();
    ASSERT_NE(lane, nullptr) << "the stroke created the lane";
    ASSERT_GE(lane->points.size(), 2u);
    EXPECT_NEAR(lane->points.front().beat, 8.0, 0.2);
    EXPECT_NEAR(lane->points.front().value, -0.5, 0.05) << "below the centre line: inverted";
    EXPECT_NEAR(lane->points.back().beat, 24.0, 0.2);
    EXPECT_NEAR(lane->points.back().value, 0.5, 0.05);
    EXPECT_EQ(lane->recordMode, static_cast<int>(synth::LaneRecordMode::Read));
    EXPECT_EQ(s.doc().getTrackForLane(lane->id)->id, s.track) << "on the modulated lane's track";
    EXPECT_GT(counter.count, 0);
    EXPECT_EQ(s.panel().laneHeaderForTest(lane->id), nullptr) << "drawn as the band, never as a lane row";
    EXPECT_FLOAT_EQ(s.amount(), 0.5f) << "the knob itself is untouched";

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.amountLane(), nullptr) << "one undo takes the lane and its points together";
    EXPECT_TRUE(juce::JSON::toString(s.doc().toVar()) == juce::JSON::toString(before));
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "and nothing before it";
    s.flush();
    EXPECT_TRUE(proxy.getLane(s.band()->getEditor()->getActiveLane())->points.empty()) << "the band follows";
    ASSERT_TRUE(s.undo().redo());
    ASSERT_NE(s.amountLane(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, WithNoLaneTheSelectToolDragsTheKnobAsOneGraphStepAndTheKeysNudgeIt) {
    AmountScene s;
    auto* band = s.band();
    ASSERT_NE(band, nullptr);
    const auto mid = juce::Point<float>(s.x(16.0), s.yFor(0.5));
    EXPECT_EQ(s.target(mid).first, band) << "with no lane and the pointer, the band itself takes the press";

    s.drag(mid, mid.translated(0.0f, (float)band->getHeight() / 2.0f), 10);
    EXPECT_NEAR(s.amount(), -0.5f, 1.0e-3f) << "half the band's height down is 100% down";
    EXPECT_EQ(s.amountLane(), nullptr) << "a knob drag creates no lane";
    ASSERT_TRUE(s.undo().undo());
    EXPECT_FLOAT_EQ(s.amount(), 0.5f) << "the whole drag is one step";

    band->grabKeyboardFocus();
    EXPECT_TRUE(band->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_NEAR(s.amount(), 0.51f, 1.0e-4f);
    EXPECT_TRUE(band->keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(s.amount(), 0.41f, 1.0e-4f) << "Shift nudges by 10%";
    ASSERT_TRUE(s.undo().undo());
    EXPECT_NEAR(s.amount(), 0.51f, 1.0e-4f) << "each key press is its own step";
    s.tick();
    s.flush();
    EXPECT_FLOAT_EQ(s.band()->getProxyDoc().getLane(band->getEditor()->getActiveLane())->range.defaultValue, 0.51f)
        << "the flat line follows the knob";
}

TEST_F(TimelinePanelIntegrationTest, ADoubleClickWithTheSelectToolStartsTheLaneWithOnePoint) {
    AmountScene s;
    s.doubleClick({s.x(16.0), s.yFor(-0.25)});
    const auto* lane = s.amountLane();
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 1u);
    EXPECT_EQ(lane->points[0].beat, 16.0) << "snapped to the bar";
    EXPECT_NEAR(lane->points[0].value, -0.25, 0.05);
    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.amountLane(), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, AnExistingLaneIsEditedThroughTheEditorAndErasingItsLastPointRemovesIt) {
    AmountScene s;
    s.doubleClick({s.x(16.0), s.yFor(-0.25)});
    ASSERT_NE(s.amountLane(), nullptr);
    s.flush();
    auto* editor = s.band()->getEditor();
    const auto handle = editor->getHandleRectForTest(16.0).getCentre().toFloat();
    ASSERT_FALSE(handle.isOrigin());
    EXPECT_EQ(s.target(handle).first, editor) << "with a lane, the curve editor takes the press";

    // Select: drag the point up to +75%, one step.
    s.drag(handle, {handle.x, s.yFor(0.75)}, 6);
    ASSERT_NE(s.amountLane(), nullptr);
    ASSERT_EQ(s.amountLane()->points.size(), 1u);
    EXPECT_NEAR(s.amountLane()->points[0].value, 0.75, 0.05);
    ASSERT_TRUE(s.undo().undo());
    EXPECT_NEAR(s.amountLane()->points[0].value, -0.25, 0.05);
    s.flush();

    // Erase the only point: the lane goes in the same step, and the knob plays again.
    s.panel().setActiveTool(synth::ui::EditTool::Erase);
    const auto again = editor->getHandleRectForTest(16.0).getCentre().toFloat();
    s.drag(again, again.translated(2.0f, 0.0f), 2);
    EXPECT_EQ(s.amountLane(), nullptr) << "erasing the last point removes the lane";
    ASSERT_TRUE(s.undo().undo());
    ASSERT_NE(s.amountLane(), nullptr) << "one undo brings the lane and its point back";
    EXPECT_EQ(s.amountLane()->points.size(), 1u);
}

TEST_F(TimelinePanelIntegrationTest, TheBandIsBipolarWithADashedCentreLineAndScaleLabels) {
    AmountScene s;
    // The flat line well away from the centre and from the top label.
    s.band()->setKnobAmount(-0.5);
    s.tick();
    s.flush();
    const auto image = renderBand(s);
    const int zeroY = juce::roundToInt(s.band()->getEditor()->valueToY(0.0));
    EXPECT_EQ(zeroY, image.getHeight() / 2) << "-100%..+100%: 0 is the middle";
    const auto background = image.getPixelAt(image.getWidth() - 4, 4);

    // Dashed: along the centre row the dashes alternate with what lies under them (the backdrop's plain grid
    // line), where a solid line would be one colour all the way.
    int changes = 0;
    for (int x = 61; x < 120; ++x)
        changes += image.getPixelAt(x, zeroY) != image.getPixelAt(x - 1, zeroY) ? 1 : 0;
    EXPECT_GE(changes, 10) << "a dashed centre line";

    // "+100%" at the top left.
    int text = 0;
    for (int y = 1; y < 11; ++y)
        for (int x = 3; x < 30; ++x)
            text += image.getPixelAt(x, y) != background ? 1 : 0;
    EXPECT_GT(text, 5) << "the +100% label is drawn";
}

TEST_F(TimelinePanelIntegrationTest, TheBandIsOneNamedTabStopWithAValueAndATooltip) {
    AmountScene s;
    auto* band = s.band();
    ASSERT_NE(band, nullptr);
    const auto title = s.row()->getInfo().sourceTitle;
    EXPECT_EQ(band->getTitle(), "Cutoff " + title + " amount");
    EXPECT_TRUE(band->getWantsKeyboardFocus());
    EXPECT_FALSE(band->getEditor()->getWantsKeyboardFocus()) << "one Tab stop, not two";
    EXPECT_FALSE(band->getEditor()->isAccessible());
    EXPECT_EQ(band->getTooltip().upToFirstOccurrenceOf(".", false, false),
              "Drag to set how much " + title + " moves Cutoff; draw to change it over time");
    EXPECT_TRUE(band->getTooltip().contains("Up/Down"));

    // Built directly: JUCE hands out a component's handler only once it is on a native window.
    const auto handlerOwner = band->createAccessibilityHandler();
    auto* handler = handlerOwner.get();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::slider);
    auto* value = handler->getValueInterface();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->getCurrentValueAsString(), "+50%");
    EXPECT_FALSE(value->isReadOnly());
    value->setValue(-0.2);
    EXPECT_NEAR(s.amount(), -0.2f, 1.0e-4f) << "a screen reader sets the knob";

    s.panel().setActiveTool(synth::ui::EditTool::Draw);
    s.doubleClick({s.x(16.0), s.yFor(0.3)});
    s.tick(20.0);
    EXPECT_TRUE(value->isReadOnly()) << "the lane decides now";
    EXPECT_EQ(value->getCurrentValueAsString(), "+30%");
    EXPECT_TRUE(band->getTooltip().startsWith("Draw to change how much"));
}
