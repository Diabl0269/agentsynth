// The automation-lane marker's pure parts (docs/layout/module-card.md#automated-marker): where the glyph sits
// against the MIDI dot, how it fades in and out, and what its tooltip and screen-reader text say.

#include "UI/Layout/AutomatedMarker.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::AutomatedMarkerFade;

TEST(AutomatedMarkerTests, GlyphSitsAtTheTopLeftAndNeverTouchesTheMidiDot) {
    for (const auto cell : {juce::Rectangle<int>(10, 20, 40, 40), juce::Rectangle<int>(0, 0, 14, 60),
                            juce::Rectangle<int>(300, 12, 28, 120), juce::Rectangle<int>(-5, -8, 64, 64)}) {
        const auto marker = synth::ui::automatedMarkerRect(cell);
        const auto dot = synth::ui::midilearn::midiMappedDotRect(cell);
        EXPECT_EQ(marker.getX(), cell.getX());
        EXPECT_EQ(marker.getY(), cell.getY()) << "same inset from the corner as the MIDI dot";
        EXPECT_EQ(dot.getY(), cell.getY());
        EXPECT_TRUE(cell.contains(marker));
        EXPECT_FALSE(marker.intersects(dot)) << "cell " << cell.toString();
    }
    EXPECT_LE(synth::ui::kAutomatedMarkerWidth, 8);
    EXPECT_GE(synth::ui::kAutomatedMarkerWidth, 6);
}

TEST(AutomatedMarkerTests, FadesInOver160MsAndOutOver110Ms) {
    AutomatedMarkerFade fade;
    EXPECT_EQ(fade.level(0.0), 0.0f);
    fade.setAutomated(true, 1000.0, false);
    EXPECT_EQ(fade.level(1000.0), 0.0f);
    EXPECT_GT(fade.level(1060.0), 0.0f);
    EXPECT_LT(fade.level(1060.0), 1.0f);
    EXPECT_FALSE(fade.isSettled(1100.0));
    EXPECT_EQ(fade.level(1160.0), 1.0f);
    EXPECT_TRUE(fade.isSettled(1160.0));

    fade.setAutomated(false, 2000.0, false);
    EXPECT_LT(fade.level(2050.0), 1.0f);
    EXPECT_FALSE(fade.isSettled(2100.0));
    EXPECT_EQ(fade.level(2110.0), 0.0f);
    EXPECT_TRUE(fade.isSettled(2110.0));
}

TEST(AutomatedMarkerTests, AReTargetMidFadeContinuesFromTheCurrentLevel) {
    AutomatedMarkerFade fade;
    fade.setAutomated(true, 0.0, false);
    const float half = fade.level(80.0);
    ASSERT_GT(half, 0.0f);
    fade.setAutomated(false, 80.0, false);
    EXPECT_FLOAT_EQ(fade.level(80.0), half) << "no jump when the lane is removed mid fade-in";
    EXPECT_LT(fade.level(120.0), half);
}

TEST(AutomatedMarkerTests, ReducedMotionIsAShortPlainFade) {
    AutomatedMarkerFade fade;
    fade.setAutomated(true, 0.0, true);
    EXPECT_EQ(fade.level(AutomatedMarkerFade::kReducedMs), 1.0f);
    EXPECT_TRUE(fade.isSettled(AutomatedMarkerFade::kReducedMs));
}

TEST(AutomatedMarkerTests, ColourIsTextPrimaryAt70PercentScaledByTheFade) {
    const auto c = synth::ui::automatedMarkerColour(juce::Colours::white, 1.0f);
    EXPECT_NEAR(c.getFloatAlpha(), 0.7f, 0.01f);
    EXPECT_NEAR(synth::ui::automatedMarkerColour(juce::Colours::white, 0.5f).getFloatAlpha(), 0.35f, 0.01f);
}

TEST(AutomatedMarkerTests, ControlStateAddsAndRemovesTheTooltipLineAndDescription) {
    juce::Slider slider;
    slider.setTooltip("Channel level");
    synth::ui::AutomatedControlState state;

    EXPECT_TRUE(state.update(slider, true, "Level", 0.0, false));
    EXPECT_EQ(slider.getTooltip(), "Channel level\nAutomated: Level");
    EXPECT_TRUE(slider.getDescription().containsIgnoreCase("automated"));
    EXPECT_FALSE(state.update(slider, true, "Level", 1.0, false)) << "no change, no rewrite";
    EXPECT_EQ(slider.getTooltip(), "Channel level\nAutomated: Level");

    EXPECT_TRUE(state.update(slider, false, "Level", 2.0, false));
    EXPECT_EQ(slider.getTooltip(), "Channel level");
    EXPECT_TRUE(slider.getDescription().isEmpty());
}

TEST(AutomatedMarkerTests, ARebuiltStateStillRestoresTheControlsOwnTooltip) {
    juce::Slider slider;
    slider.setTooltip("Pan");
    {
        synth::ui::AutomatedControlState first;
        first.update(slider, true, "Pan", 0.0, false);
    } // the owner rebuilt its state (a column rebinding) without the lane going away first
    synth::ui::AutomatedControlState second;
    second.update(slider, true, "Pan", 1.0, false);
    EXPECT_EQ(slider.getTooltip(), "Pan\nAutomated: Pan") << "no doubled line";
    second.update(slider, false, "Pan", 2.0, false);
    EXPECT_EQ(slider.getTooltip(), "Pan");
}
