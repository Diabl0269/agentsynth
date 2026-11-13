// TimelineClipLaneWaveformLookTests.cpp -- how a clip's waveform looks: the peak shape reads against every track
// colour in every built-in theme, the RMS core is a visible step denser than it, the amplitude scale is linear (a
// -6 dB tone is half as tall as a 0 dB one), and a live take and a committed clip draw the same shape.
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/TimelineClipLaneArea/WaveformDraw.h"
#include "UI/Timeline/TrackColour.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::ui::colourContrast;
using synth::ui::fillWaveformColumns;
using synth::ui::WaveformInk;
using synth::ui::waveformInkFor;

// Every way a clip shows on a lane: each palette colour, plain, selected (lifted) and muted, over each theme's bg-1.
std::vector<std::pair<std::string, juce::Colour>> everyClipOnLane() {
    std::vector<std::pair<std::string, juce::Colour>> out;
    for (const auto& theme : synth::theme::builtInThemes())
        for (int i = 0; i < synth::ui::kTrackPaletteSize; ++i)
            for (const bool muted : {false, true})
                for (const bool selected : {false, true}) {
                    const auto base = synth::ui::resolveTrackColour(0, i, muted);
                    const auto fill = selected ? base.brighter(0.15f) : base;
                    out.push_back({theme.name.toStdString() + " colour " + std::to_string(i) + (muted ? " muted" : "") +
                                       (selected ? " selected" : ""),
                                   theme.colors.bg1.overlaidWith(fill.withAlpha(selected ? 0.85f : 0.65f))});
                }
    return out;
}

TEST(WaveformLook, ThePeakShapeReadsOnEveryClipInEveryTheme) {
    const auto clips = everyClipOnLane();
    ASSERT_GE(clips.size(), 4u * 8u * 4u);
    for (const auto& [name, clip] : clips) {
        const auto ink = waveformInkFor(clip);
        EXPECT_GE(colourContrast(ink.outline, clip), synth::ui::kWaveformMinContrast) << name;
        EXPECT_EQ(ink.outline.getAlpha(), 255) << name << ": the lane must never show through the wave";
        EXPECT_EQ(ink.flat, ink.outline) << name << ": without RMS the wave is the same peak shape";
    }
}

TEST(WaveformLook, TheRmsCoreIsADenserStepOfTheSameShadeNotASecondGrey) {
    for (const auto& [name, clip] : everyClipOnLane()) {
        const auto ink = waveformInkFor(clip);
        const float step = colourContrast(ink.body, ink.outline);
        EXPECT_GE(step, 1.15f) << name << ": the core does not show inside the peak shape";
        EXPECT_GT(colourContrast(ink.body, clip), colourContrast(ink.outline, clip))
            << name << ": the core must sit further from the clip than the peak shape";
        if (clip.getSaturation() > 0.25f && ink.outline.getSaturation() > 0.1f)
            EXPECT_NEAR(ink.outline.getHue(), clip.getHue(), 0.08f) << name << ": the wave is a shade of the clip";
    }
}

TEST(WaveformLook, ADarkClipGetsALightWave) {
    const juce::Colour nearBlack(0xff15171C);
    const auto ink = waveformInkFor(nearBlack);
    EXPECT_GT(ink.outline.getPerceivedBrightness(), nearBlack.getPerceivedBrightness());
    EXPECT_GE(colourContrast(ink.outline, nearBlack), synth::ui::kWaveformMinContrast);
}

// Rows of `img` in column `x` whose colour moved away from `background`.
int inkedRows(const juce::Image& img, int x, juce::Colour background) {
    int rows = 0;
    for (int y = 0; y < img.getHeight(); ++y)
        rows += colourContrast(img.getPixelAt(x, y), background) > 1.3f ? 1 : 0;
    return rows;
}

juce::Image render(float level, const WaveformInk& ink, juce::Colour clip, bool withRms) {
    juce::Image img(juce::Image::ARGB, 64, 102, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    g.fillAll(clip);
    const std::vector<std::pair<float, float>> columns(62, {-level, level});
    const std::vector<float> rms = withRms ? std::vector<float>(62, level * 0.7071f) : std::vector<float>{};
    fillWaveformColumns(g, {1, 1, 62, 100}, 0, columns, rms, ink);
    return img;
}

TEST(WaveformLook, TheAmplitudeScaleIsLinearSoMinusSixDecibelsIsHalfTheHeight) {
    const juce::Colour clip(0xff7A9CC6);
    const auto ink = waveformInkFor(clip);
    const int full = inkedRows(render(1.0f, ink, clip, true), 30, clip);
    const int half = inkedRows(render(juce::Decibels::decibelsToGain(-6.0206f), ink, clip, true), 30, clip);
    const int quiet = inkedRows(render(juce::Decibels::decibelsToGain(-20.0f), ink, clip, true), 30, clip);
    EXPECT_NEAR(full, 100, 2);
    EXPECT_NEAR(half, full / 2, 2) << "a -6 dB tone draws half as tall";
    EXPECT_NEAR(quiet, full / 10, 2) << "a -20 dB tone draws a tenth as tall";
}

TEST(WaveformLook, ASineShowsItsRmsCoreAtSeventyPercentOfThePeak) {
    const juce::Colour clip(0xffC67A9C);
    const auto ink = waveformInkFor(clip);
    const auto img = render(0.8f, ink, clip, true);
    int core = 0;
    for (int y = 0; y < img.getHeight(); ++y)
        core += colourContrast(img.getPixelAt(30, y), ink.body) < 1.05f ? 1 : 0;
    EXPECT_NEAR(core, juce::roundToInt(80.0f * 0.7071f), 3);
}

TEST(WaveformLook, ALiveTakeAndTheCommittedClipDrawTheSameShape) {
    // Same peaks, drawn with the strip's ink (accent fill) and with a committed clip's ink: same rows covered.
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto strip = theme.colors.bg1.overlaidWith(theme.colors.accent.withAlpha(0.12f));
        const auto committed = theme.colors.bg1.overlaidWith(synth::ui::trackPaletteColour(0).withAlpha(0.65f));
        for (const float level : {1.0f, 0.5f, 0.1f})
            EXPECT_NEAR(inkedRows(render(level, waveformInkFor(strip), strip, true), 30, strip),
                        inkedRows(render(level, waveformInkFor(committed), committed, true), 30, committed), 1)
                << theme.name << " level " << level;
    }
}

} // namespace
