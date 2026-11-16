// Lowering Steps per bar keeps every note at its beat, so a cell can hold several: it draws their count, the tooltip
// and the screen-reader text say it, and going back to the finer grid restores the original look.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"

using synth::ui::DrumMachineComponent;

namespace {

struct StackedFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumMachineComponent grid;
    synth::TrackId track;
    ClipId clip;

    // `hits` kick notes, one every `spacing` beats, on a clip two bars (8 beats) long at `steps` per bar.
    StackedFixture(int steps, int hits, double spacing) {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        track = doc.addTrack(TrackKind::Midi, "Drums");
        clip = doc.addClip(track, 0.0, 4.0, "Beat 1");
        EXPECT_TRUE(doc.setClipStepsPerBar(clip, steps));
        for (int i = 0; i < hits; ++i)
            doc.addNote(clip, makeNote(spacing * i, 36, spacing));
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, 320);
        grid.openClip(clip);
    }
    ~StackedFixture() { grid.setLookAndFeel(nullptr); }

    const synth::Clip& c() { return *doc.getClip(clip); }
    int kickRow() { return grid.rowOfPitch(36); }
    void setSteps(int steps) {
        ASSERT_TRUE(doc.setClipStepsPerBar(clip, steps));
        grid.refreshFromDoc();
        grid.finishMotionsForTest();
    }
    juce::Image render() {
        juce::Image image(juce::Image::ARGB, grid.getWidth(), grid.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        grid.paintEntireComponent(g, false);
        return image;
    }
};

// True when any pixel of the cell's top right quarter differs between the two renders.
bool cornerDiffers(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> cell) {
    const auto corner = cell.removeFromTop(cell.getHeight() / 2).removeFromRight(cell.getWidth() / 2);
    for (int y = corner.getY(); y < corner.getBottom(); ++y)
        for (int x = corner.getX(); x < corner.getRight(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return true;
    return false;
}

} // namespace

// Regression test for FRO1671: lowering Steps per bar stacked notes in a cell with nothing to say so.
TEST(DrumMachineStackedHitsTest, AFullRowAtThirtyTwoShowsTwoHitsPerCellAtSixteenAndPlainCellsBackAtThirtyTwo) {
    StackedFixture f(32, 32, 0.125);
    ASSERT_EQ(f.c().notes.size(), 32u);
    const auto original = f.c().notes;
    for (int step = 0; step < 32; ++step)
        EXPECT_EQ(f.grid.getStackedHitCount(f.kickRow(), step), 0) << step;
    f.setSteps(16);
    for (int step = 0; step < 16; ++step) {
        EXPECT_EQ(f.grid.getStackedHitCount(f.kickRow(), step), 2) << step;
        EXPECT_EQ(f.grid.getCellMarks(f.kickRow(), step).stacked, 2) << step;
    }
    f.setSteps(32);
    for (int step = 0; step < 32; ++step)
        EXPECT_EQ(f.grid.getStackedHitCount(f.kickRow(), step), 0) << step;
    ASSERT_EQ(f.c().notes.size(), original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(f.c().notes[i].startBeat, original[i].startBeat);
        EXPECT_EQ(f.c().notes[i].velocity, original[i].velocity);
    }
}

TEST(DrumMachineStackedHitsTest, TheTooltipAndTheScreenReaderTextSayTwoHitsHere) {
    StackedFixture f(32, 32, 0.125);
    f.setSteps(16);
    const auto at = [&](int step) { return centreOf(f.grid.cellBounds(f.kickRow(), step)); };
    f.grid.mouseMove(hover(f.grid, at(3)));
    EXPECT_EQ(f.grid.getTooltip(), "2 hits here");
    f.grid.setFocusCell({f.kickRow(), 3});
    EXPECT_TRUE(f.grid.getAccessibilityValueText().contains(", 2 hits here")) << f.grid.getAccessibilityValueText();
    f.setSteps(32);
    f.grid.mouseMove(hover(f.grid, at(3)));
    EXPECT_NE(f.grid.getTooltip(), "2 hits here");
    EXPECT_FALSE(f.grid.getAccessibilityValueText().contains("hits here"));
}

TEST(DrumMachineStackedHitsTest, ThreeNotesInACellSayThreeHits) {
    StackedFixture f(32, 0, 0.125);
    for (int i = 0; i < 3; ++i)
        f.doc.addNote(f.clip, makeNote(0.01 * i, 36, 0.05));
    f.grid.refreshFromDoc();
    EXPECT_EQ(f.grid.getStackedHitCount(f.kickRow(), 0), 3);
    f.grid.mouseMove(hover(f.grid, centreOf(f.grid.cellBounds(f.kickRow(), 0))));
    EXPECT_EQ(f.grid.getTooltip(), "3 hits here");
}

// The count is drawn: the cell's top right corner differs from the same cell holding one note, in every theme.
TEST(DrumMachineStackedHitsTest, TheCountMarkIsDrawnInTheCornerInEveryTheme) {
    synth::theme::ThemeManager themes;
    synth::theme::AppLookAndFeel lf;
    for (const auto& theme : themes.getThemes()) {
        SCOPED_TRACE(theme.id.toStdString());
        lf.applyTheme(theme);
        StackedFixture stacked(16, 32, 0.125);
        StackedFixture single(16, 16, 0.25);
        stacked.grid.setLookAndFeel(&lf);
        single.grid.setLookAndFeel(&lf);
        stacked.grid.finishMotionsForTest();
        single.grid.finishMotionsForTest();
        ASSERT_EQ(stacked.grid.getStackedHitCount(stacked.kickRow(), 3), 2);
        ASSERT_EQ(single.grid.getStackedHitCount(single.kickRow(), 3), 0);
        EXPECT_TRUE(cornerDiffers(stacked.render(), single.render(), stacked.grid.cellBounds(stacked.kickRow(), 3)));
        const auto bottomEdge = stacked.grid.cellBounds(stacked.kickRow(), 3);
        EXPECT_EQ(stacked.render().getPixelAt(bottomEdge.getX() + 3, bottomEdge.getBottom() - 2),
                  single.render().getPixelAt(bottomEdge.getX() + 3, bottomEdge.getBottom() - 2))
            << "the loudness fill at the bottom is not covered";
    }
}
