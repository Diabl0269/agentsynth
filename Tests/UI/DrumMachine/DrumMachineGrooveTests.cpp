// The drum machine's groove controls: the Swing knob, picking the row the lanes edit, the lane chips and the
// Loudness lane (drawing a ramp, the keyboard, the screen-reader text), and the swung hits' flash timing.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"

using synth::ui::DrumLoudnessLane;
using synth::ui::DrumMachineComponent;

namespace {

struct GrooveFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumMachineComponent grid;
    synth::TrackId track;
    ClipId clip;

    GrooveFixture() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        track = doc.addTrack(TrackKind::Midi, "Drums");
        clip = doc.addClip(track, 0.0, 8.0, "Beat 1");
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, 360);
        grid.openClip(clip);
    }

    const synth::Clip& c() { return *doc.getClip(clip); }

    void addHits(int pitch, std::initializer_list<int> steps, int velocity = 100) {
        for (const int step : steps) {
            auto note = makeNote(0.25 * step, pitch, 0.25);
            note.velocity = velocity;
            doc.addNote(clip, note);
        }
        grid.refreshFromDoc();
        undo.clearUndoHistory();
    }

    void clickChip() {
        auto* chip = grid.getLaneChips().getChip(0);
        chip->setToggleState(!chip->getToggleState(), juce::sendNotificationSync); // what a click does
    }

    void showLoudness() {
        clickChip();
        grid.finishMotionsForTest();
    }

    std::vector<int> velocities(int pitch) {
        std::vector<int> out;
        for (const auto& n : c().notes)
            if (n.pitch == pitch)
                out.push_back(n.velocity);
        return out;
    }
};

juce::Point<float> inLane(DrumLoudnessLane& lane, int hitIndex, float fractionFromBottom) {
    const auto bar = lane.barBounds(hitIndex);
    const float x = static_cast<float>(lane.getBounds().isEmpty() ? bar.getCentreX() : bar.getCentreX());
    const float y = static_cast<float>(lane.getHeight() - DrumLoudnessLane::kBarInsetPx) -
                    fractionFromBottom * static_cast<float>(lane.getHeight() - 2 * DrumLoudnessLane::kBarInsetPx);
    return {x, y};
}

} // namespace

//==============================================================================
// Swing

TEST(DrumMachineSwingTest, TheKnobIsNamedAndTipped) {
    GrooveFixture f;
    auto& knob = f.grid.getSwingKnob();
    EXPECT_EQ(knob.getTitle(), "Swing");
    EXPECT_EQ(knob.getTooltip(), "Swing: push every second step later (50% is straight)");
    EXPECT_TRUE(knob.getWantsKeyboardFocus());
    knob.setValue(56.0, juce::dontSendNotification);
    EXPECT_EQ(knob.getTextFromValue(knob.getValue()), "56%");
    EXPECT_DOUBLE_EQ(knob.getMinimum(), 50.0);
    EXPECT_DOUBLE_EQ(knob.getMaximum(), 75.0);
}

TEST(DrumMachineSwingTest, ADragOnTheKnobIsOneUndoStepCommittedOnRelease) {
    GrooveFixture f;
    auto& knob = f.grid.getSwingKnob();
    ASSERT_FALSE(knob.getBounds().isEmpty());
    const auto start = centreOf(knob.getLocalBounds());
    knob.mouseDown(leftClick(knob, start));
    for (int dy = -20; dy >= -100; dy -= 20)
        knob.mouseDrag(leftDrag(knob, start + juce::Point<float>(0.0f, (float)dy), start));
    EXPECT_GT(knob.getValue(), 50.0);
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5) << "nothing is written while the knob is held";
    knob.mouseUp(leftDrag(knob, start + juce::Point<float>(0.0f, -100.0f), start));
    EXPECT_GT(f.c().swing, 0.5);
    EXPECT_NEAR(f.c().swing * 100.0, knob.getValue(), 1e-9);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole turn was one undo step";
    f.grid.refreshFromDoc(); // the panel does this when the timeline changes
    EXPECT_DOUBLE_EQ(f.grid.getSwingKnob().getValue(), 50.0) << "the knob follows the undo";
}

TEST(DrumMachineSwingTest, AKeyPressOnTheKnobIsOneUndoStep) {
    GrooveFixture f;
    auto& knob = f.grid.getSwingKnob();
    EXPECT_TRUE(knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_DOUBLE_EQ(f.c().swing, 0.51);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineSwingTest, ASwungHitShowsATickAndAStraightOneDoesNot) {
    GrooveFixture f;
    f.addHits(36, {1});
    const auto cell = f.grid.cellBounds(f.grid.rowOfPitch(36), 1);
    const auto pixelAtTick = [&] {
        juce::Image image(juce::Image::ARGB, f.grid.getWidth(), f.grid.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        f.grid.paintEntireComponent(g, false);
        return image.getPixelAt(cell.getX() + cell.getWidth() / 2, cell.getCentreY());
    };
    const auto straight = pixelAtTick();
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    f.grid.refreshFromDoc();
    const auto swung = pixelAtTick();
    EXPECT_GT(swung.getGreen(), straight.getGreen()) << "the tick at half a step is lighter than the hit";
}

TEST(DrumMachineSwingTest, ASwungHitFlashesWhenItPlaysNotWhenTheColumnIsEntered) {
    GrooveFixture f;
    f.addHits(42, {1});
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    f.grid.refreshFromDoc();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    const int row = f.grid.rowOfPitch(42);
    f.grid.setTransportPlaying(true);
    f.grid.setPlayheadBeat(0.0);
    f.grid.setPlayheadBeat(0.30); // step 1 begins at 0.25 and its hit plays at 0.375
    EXPECT_FALSE(f.grid.isFlashActive(row, 1));
    f.grid.setPlayheadBeat(0.40);
    EXPECT_TRUE(f.grid.isFlashActive(row, 1));
    f.grid.finishMotionsForTest();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

//==============================================================================
// The row the lanes edit, the chips and the Loudness lane

TEST(DrumMachineLoudnessTest, KickIsSelectedUntilARowNameIsClicked) {
    GrooveFixture f;
    EXPECT_EQ(f.grid.getSelectedPitch(), 36);
    auto* kick = f.grid.getRowLabels().getNameButton(36);
    auto* hat = f.grid.getRowLabels().getNameButton(42);
    ASSERT_NE(kick, nullptr);
    ASSERT_NE(hat, nullptr);
    EXPECT_TRUE(kick->getToggleState()) << "the selected row's name is the lit one, drawn in the accent colour";
    EXPECT_FALSE(hat->getToggleState());
    EXPECT_EQ(hat->getTitle(), "Closed hat row");
    hat->onClick();
    EXPECT_EQ(f.grid.getSelectedPitch(), 42);
    EXPECT_TRUE(hat->getToggleState());
    EXPECT_FALSE(kick->getToggleState());
}

TEST(DrumMachineLoudnessTest, TheChipGrowsTheLaneAndFoldsItBack) {
    GrooveFixture f;
    auto& chips = f.grid.getLaneChips();
    ASSERT_EQ(chips.getNumChips(), 1);
    EXPECT_EQ(chips.getChip(0)->getButtonText(), "Loudness");
    EXPECT_FALSE(f.grid.getLoudnessLane().isVisible());
    const int closedBand = f.grid.bottomBandHeight();
    f.showLoudness();
    EXPECT_TRUE(f.grid.isLoudnessShown());
    EXPECT_TRUE(f.grid.getLoudnessLane().isVisible());
    EXPECT_EQ(f.grid.bottomBandHeight() - closedBand, DrumMachineComponent::kLaneHeight);
    EXPECT_LE(f.grid.getLoudnessLane().getBottom(), chips.getY()) << "the lane sits under the grid, above the chips";
    EXPECT_LE(f.grid.gridRegion().getBottom(), f.grid.getLoudnessLane().getY());
    f.clickChip();
    f.grid.finishMotionsForTest();
    EXPECT_FALSE(f.grid.getLoudnessLane().isVisible());
    EXPECT_EQ(f.grid.bottomBandHeight(), closedBand);
}

TEST(DrumMachineLoudnessTest, TheLaneGrowsOverTimeWhenAnimationsAreOnScreenAndLandsAtOnceOtherwise) {
    GrooveFixture f;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    f.clickChip();
    EXPECT_TRUE(f.grid.isLaneMotionRunning());
    f.grid.finishMotionsForTest();
    EXPECT_FALSE(f.grid.isLaneMotionRunning());
    EXPECT_EQ(f.grid.getLoudnessLane().getHeight(), DrumMachineComponent::kLaneHeight);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}

TEST(DrumMachineLoudnessTest, TheLaneShowsOneBarPerHitOfTheSelectedRow) {
    GrooveFixture f;
    f.addHits(42, {0, 2, 4, 6}, 80);
    f.addHits(36, {0, 8});
    f.showLoudness();
    auto& lane = f.grid.getLoudnessLane();
    EXPECT_EQ(lane.getHits().size(), 2u) << "Kick is selected by default";
    f.grid.getRowLabels().getNameButton(42)->onClick();
    ASSERT_EQ(lane.getHits().size(), 4u);
    EXPECT_EQ(lane.getHits()[1].step, 2);
    EXPECT_EQ(lane.getHits()[1].velocity, 80);
    EXPECT_GT(lane.barBounds(0).getHeight(), 0);
    EXPECT_LT(lane.barBounds(0).getHeight(), lane.getHeight());
}

TEST(DrumMachineLoudnessTest, DrawingAcrossFourHatsSetsARampAsOneUndoStep) {
    GrooveFixture f;
    f.addHits(42, {0, 2, 4, 6});
    f.grid.getRowLabels().getNameButton(42)->onClick();
    f.showLoudness();
    auto& lane = f.grid.getLoudnessLane();
    ASSERT_EQ(lane.getHits().size(), 4u);

    const auto first = inLane(lane, 0, 0.1f);
    const auto last = inLane(lane, 3, 0.9f);
    lane.mouseDown(leftClick(lane, first));
    for (int i = 1; i <= 8; ++i) {
        const float t = static_cast<float>(i) / 8.0f;
        lane.mouseDrag(leftDrag(lane, first + (last - first) * t, first));
    }
    EXPECT_EQ(f.velocities(42), (std::vector<int>{100, 100, 100, 100})) << "nothing is written while the drag is live";
    EXPECT_NE(lane.velocityOf(0), 100) << "the bars preview it";
    lane.mouseUp(leftDrag(lane, last, first));

    const auto v = f.velocities(42);
    ASSERT_EQ(v.size(), 4u);
    EXPECT_LT(v[0], v[1]);
    EXPECT_LT(v[1], v[2]);
    EXPECT_LT(v[2], v[3]);
    EXPECT_NEAR(v[0], 13, 3);
    EXPECT_NEAR(v[3], 114, 3);
    EXPECT_NEAR(v[1] - v[0], v[2] - v[1], 3) << "a straight ramp";

    f.undo.undo();
    EXPECT_EQ(f.velocities(42), (std::vector<int>{100, 100, 100, 100}));
    EXPECT_FALSE(f.undo.canUndo()) << "the whole drag was one undo step";
}

TEST(DrumMachineLoudnessTest, TheLaneKeyboardMovesBetweenHitsAndChangesLoudnessByFive) {
    GrooveFixture f;
    f.addHits(36, {0, 4, 8});
    f.showLoudness();
    auto& lane = f.grid.getLoudnessLane();
    EXPECT_TRUE(lane.getWantsKeyboardFocus());
    EXPECT_EQ(lane.getTitle(), "Loudness lane");
    lane.setFocusedHit(0);
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(lane.getFocusedHit(), 1);
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 5, loudness 100");
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.velocities(36), (std::vector<int>{100, 105, 100}));
    EXPECT_EQ(lane.getFocusedHit(), 1) << "the keyboard stays on the hit it changed";
    EXPECT_EQ(lane.getAccessibilityValueText(), "Kick, step 5, loudness 105");
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(f.velocities(36), (std::vector<int>{100, 80, 100}));
    f.undo.undo();
    EXPECT_EQ(f.velocities(36), (std::vector<int>{100, 105, 100})) << "each key press is its own undo step";
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(lane.getFocusedHit(), 0) << "Left stops at the first hit";
}

TEST(DrumMachineLoudnessTest, AnEmptyRowSaysSo) {
    GrooveFixture f;
    f.showLoudness();
    EXPECT_EQ(f.grid.getLoudnessLane().getAccessibilityValueText(), "Kick, no hits");
}
