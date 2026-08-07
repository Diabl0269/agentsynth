// EQCurveTests.cpp
// Tests for the Parametric EQ visualiser, its interaction model, and the axis maths it shares
// with FrequencyResponseComponent (issue #154):
//   • synth::ui::FrequencyGrid — log-frequency and dB mappings, their inverses, label formatting
//   • FrequencyResponseComponent's public statics still agree with the shared grid after the
//     mapping code was hoisted out of it
//   • EQCurveComponent — coordinate mapping, hit-testing, and the add/remove/drag/scroll gestures
//     (exercised through the same methods the mouse handlers call)
//   • EQCurveComponent / EQWindow — paint smoke tests including the empty state and FFT overlay

#include "../Source/Modules/FX/ParametricEQModule.h"
#include "../Source/UI/EQCurveComponent.h"
#include "../Source/UI/EQWindow.h"
#include "../Source/UI/FrequencyGrid.h"
#include "../Source/UI/FrequencyResponseComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::FrequencyGrid;

namespace {

constexpr int kCurveWidth = 400;
constexpr int kCurveHeight = 180;

// True if the image has at least one non-transparent pixel.
bool hasOpaquePixel(const juce::Image& img) {
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                return true;
    return false;
}

void paintInto(EQCurveComponent& comp, int w = kCurveWidth, int h = kCurveHeight) {
    comp.setBounds(0, 0, w, h);
    juce::Image img(juce::Image::ARGB, std::max(1, w), std::max(1, h), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
    if (w > 0 && h > 0)
        EXPECT_TRUE(hasOpaquePixel(img)) << "painted image should have at least one opaque pixel";
}

} // namespace

// ---------------------------------------------------------------------------
// FrequencyGrid — frequency axis
// ---------------------------------------------------------------------------

TEST(FrequencyGridTest, FreqToXSpansTheFullWidth) {
    constexpr float w = 400.0f;
    EXPECT_NEAR(FrequencyGrid::freqToX(FrequencyGrid::kMinFreq, w), 0.0f, 0.01f);
    EXPECT_NEAR(FrequencyGrid::freqToX(FrequencyGrid::kMaxFreq, w), w, 0.01f);
}

TEST(FrequencyGridTest, FreqToXIsLogarithmic) {
    constexpr float w = 400.0f;
    // Equal frequency ratios must map to equal pixel distances on a log axis.
    const float d1 = FrequencyGrid::freqToX(200.0f, w) - FrequencyGrid::freqToX(20.0f, w);
    const float d2 = FrequencyGrid::freqToX(2000.0f, w) - FrequencyGrid::freqToX(200.0f, w);
    const float d3 = FrequencyGrid::freqToX(20000.0f, w) - FrequencyGrid::freqToX(2000.0f, w);
    EXPECT_NEAR(d1, d2, 0.01f);
    EXPECT_NEAR(d2, d3, 0.01f);
}

TEST(FrequencyGridTest, FreqToXIsMonotonic) {
    constexpr float w = 512.0f;
    float previous = -1.0f;
    for (float freq = 20.0f; freq <= 20000.0f; freq *= 1.3f) {
        const float x = FrequencyGrid::freqToX(freq, w);
        EXPECT_GT(x, previous) << "at " << freq << " Hz";
        previous = x;
    }
}

TEST(FrequencyGridTest, XToFreqInvertsFreqToX) {
    constexpr float w = 400.0f;
    for (float freq : {20.0f, 100.0f, 440.0f, 1000.0f, 7500.0f, 20000.0f}) {
        const float roundTripped = FrequencyGrid::xToFreq(FrequencyGrid::freqToX(freq, w), w);
        EXPECT_NEAR(roundTripped, freq, freq * 0.001f) << "at " << freq << " Hz";
    }
}

TEST(FrequencyGridTest, XToFreqHandlesZeroWidth) {
    EXPECT_FLOAT_EQ(FrequencyGrid::xToFreq(10.0f, 0.0f), FrequencyGrid::kMinFreq);
    EXPECT_FLOAT_EQ(FrequencyGrid::xToFreq(10.0f, -5.0f), FrequencyGrid::kMinFreq);
}

TEST(FrequencyGridTest, IndexToFreqSpansTheAxisAndIsMonotonic) {
    constexpr int numPoints = 256;
    EXPECT_NEAR(FrequencyGrid::indexToFreq(0, numPoints), FrequencyGrid::kMinFreq, 0.01f);
    EXPECT_NEAR(FrequencyGrid::indexToFreq(numPoints - 1, numPoints), FrequencyGrid::kMaxFreq, 1.0f);

    float previous = 0.0f;
    for (int i = 0; i < numPoints; ++i) {
        const float freq = FrequencyGrid::indexToFreq(i, numPoints);
        EXPECT_GT(freq, previous) << "index " << i;
        previous = freq;
    }
}

TEST(FrequencyGridTest, IndexToFreqHandlesDegenerateCounts) {
    EXPECT_FLOAT_EQ(FrequencyGrid::indexToFreq(0, 1), FrequencyGrid::kMinFreq);
    EXPECT_FLOAT_EQ(FrequencyGrid::indexToFreq(0, 0), FrequencyGrid::kMinFreq);
}

// ---------------------------------------------------------------------------
// FrequencyGrid — dB axis
// ---------------------------------------------------------------------------

TEST(FrequencyGridTest, DbToYPutsMaxAtTopAndMinAtBottom) {
    constexpr float h = 120.0f;
    EXPECT_NEAR(FrequencyGrid::dbToY(30.0f, h, -30.0f, 30.0f), 0.0f, 0.01f);
    EXPECT_NEAR(FrequencyGrid::dbToY(-30.0f, h, -30.0f, 30.0f), h, 0.01f);
    // A symmetric window puts 0 dB exactly halfway down.
    EXPECT_NEAR(FrequencyGrid::dbToY(0.0f, h, -30.0f, 30.0f), h * 0.5f, 0.01f);
}

TEST(FrequencyGridTest, DbToYIsMonotonicDownwards) {
    constexpr float h = 200.0f;
    float previous = -1.0f;
    for (float db = 50.0f; db >= -40.0f; db -= 5.0f) {
        const float y = FrequencyGrid::dbToY(db, h, -40.0f, 50.0f);
        EXPECT_GT(y, previous) << "at " << db << " dB";
        previous = y;
    }
}

TEST(FrequencyGridTest, YToDbInvertsDbToY) {
    constexpr float h = 130.0f;
    for (float db : {-30.0f, -12.0f, 0.0f, 6.0f, 30.0f}) {
        const float roundTripped = FrequencyGrid::yToDb(FrequencyGrid::dbToY(db, h, -30.0f, 30.0f), h, -30.0f, 30.0f);
        EXPECT_NEAR(roundTripped, db, 0.01f) << "at " << db << " dB";
    }
}

TEST(FrequencyGridTest, YToDbHandlesZeroHeight) {
    EXPECT_FLOAT_EQ(FrequencyGrid::yToDb(5.0f, 0.0f, -30.0f, 30.0f), 30.0f);
}

// ---------------------------------------------------------------------------
// FrequencyGrid — labels and peak finding
// ---------------------------------------------------------------------------

TEST(FrequencyGridTest, FormatHzLabel) {
    EXPECT_EQ(FrequencyGrid::formatHzLabel(100.0f), "100Hz");
    EXPECT_EQ(FrequencyGrid::formatHzLabel(999.0f), "999Hz");
    EXPECT_EQ(FrequencyGrid::formatHzLabel(1000.0f), "1kHz");
    EXPECT_EQ(FrequencyGrid::formatHzLabel(1500.0f), "1.5kHz");
    EXPECT_EQ(FrequencyGrid::formatHzLabel(10000.0f), "10kHz");
}

TEST(FrequencyGridTest, FindPeakBin) {
    const float mags[] = {0.1f, 0.4f, 0.9f, 0.3f};
    EXPECT_EQ(FrequencyGrid::findPeakBin(mags, 4), 2);
    // Ties resolve to the first occurrence.
    const float tied[] = {0.5f, 0.5f};
    EXPECT_EQ(FrequencyGrid::findPeakBin(tied, 2), 0);
    EXPECT_EQ(FrequencyGrid::findPeakBin(mags, 0), -1);
    EXPECT_EQ(FrequencyGrid::findPeakBin(nullptr, 4), -1);
}

// ---------------------------------------------------------------------------
// The two visualisers agree with the shared grid
// ---------------------------------------------------------------------------

TEST(FrequencyGridTest, FilterViewStaticsDelegateToTheSharedGrid) {
    constexpr float w = 400.0f;
    constexpr float h = 200.0f;
    for (float freq : {20.0f, 440.0f, 5000.0f, 20000.0f})
        EXPECT_FLOAT_EQ(FrequencyResponseComponent::freqToXStatic(freq, w), FrequencyGrid::freqToX(freq, w));
    for (float db : {-40.0f, 0.0f, 20.0f, 50.0f})
        EXPECT_FLOAT_EQ(
            FrequencyResponseComponent::dbToYStatic(db, h),
            FrequencyGrid::dbToY(db, h, FrequencyResponseComponent::minDb, FrequencyResponseComponent::maxDb));
    EXPECT_EQ(FrequencyResponseComponent::formatHzLabel(1500.0f), FrequencyGrid::formatHzLabel(1500.0f));
}

TEST(EQCurveTest, StaticsUseASymmetricThirtyDbWindow) {
    constexpr float h = 130.0f;
    EXPECT_FLOAT_EQ(EQCurveComponent::minDb, -30.0f);
    EXPECT_FLOAT_EQ(EQCurveComponent::maxDb, 30.0f);
    // 0 dB is the visual centre — that is what makes boosts and cuts read symmetrically.
    EXPECT_NEAR(EQCurveComponent::dbToYStatic(0.0f, h), h * 0.5f, 0.01f);
    EXPECT_NEAR(EQCurveComponent::dbToYStatic(30.0f, h), 0.0f, 0.01f);
    EXPECT_NEAR(EQCurveComponent::dbToYStatic(-30.0f, h), h, 0.01f);
    EXPECT_FLOAT_EQ(EQCurveComponent::freqToXStatic(1000.0f, 400.0f), FrequencyGrid::freqToX(1000.0f, 400.0f));
}

// ---------------------------------------------------------------------------
// EQCurveComponent — coordinate mapping against its own bounds
// ---------------------------------------------------------------------------

TEST(EQCurveTest, SpectrumIsOnByDefaultSoTheCurveHasABackdrop) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    EXPECT_TRUE(comp.getShowSpectrum());
    comp.setShowSpectrum(false);
    EXPECT_FALSE(comp.getShowSpectrum());
}

TEST(EQCurveTest, FreqAtXAndGainAtYInvertTheDrawingTransform) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    for (float freq : {50.0f, 440.0f, 1000.0f, 9000.0f}) {
        const float x = EQCurveComponent::freqToXStatic(freq, (float)kCurveWidth);
        EXPECT_NEAR(comp.freqAtX(x), freq, freq * 0.002f) << "at " << freq << " Hz";
    }
    for (float db : {-18.0f, -6.0f, 0.0f, 6.0f, 18.0f}) {
        const float y = EQCurveComponent::dbToYStatic(db, (float)kCurveHeight);
        EXPECT_NEAR(comp.gainAtY(y), db, 0.05f) << "at " << db << " dB";
    }
}

TEST(EQCurveTest, GainAtYClampsToTheBandGainRange) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);
    // The ±30 dB window is wider than the ±24 dB parameter range, so the top of the view must
    // clamp rather than asking for a gain the parameter cannot hold.
    EXPECT_FLOAT_EQ(comp.gainAtY(0.0f), ParametricEQModule::kMaxGainDb);
    EXPECT_FLOAT_EQ(comp.gainAtY((float)kCurveHeight), -ParametricEQModule::kMaxGainDb);
}

// ---------------------------------------------------------------------------
// EQCurveComponent — add / remove / drag / scroll
// ---------------------------------------------------------------------------

TEST(EQCurveInteraction, AddPointEnablesTheBandAtTheClickedPosition) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);
    ASSERT_EQ(eq.getEnabledBandCount(), 0);

    const float x = EQCurveComponent::freqToXStatic(2000.0f, (float)kCurveWidth);
    const float y = EQCurveComponent::dbToYStatic(6.0f, (float)kCurveHeight);
    const int band = comp.addPointAt({x, y});

    ASSERT_GE(band, 0);
    EXPECT_TRUE(eq.isBandEnabled(band));
    EXPECT_EQ(eq.getEnabledBandCount(), 1);
    EXPECT_EQ(comp.getSelectedBand(), band);

    const auto bands = eq.getBandSnapshots();
    EXPECT_NEAR(bands[(size_t)band].freqHz, 2000.0f, 30.0f);
    EXPECT_NEAR(bands[(size_t)band].gainDb, 6.0f, 0.3f);
    EXPECT_NEAR(bands[(size_t)band].q, ParametricEQModule::kDefaultQ, 0.01f);
}

TEST(EQCurveInteraction, AddPointPicksTheSlotMatchingTheClickedFrequency) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    const float lowX = EQCurveComponent::freqToXStatic(60.0f, (float)kCurveWidth);
    EXPECT_EQ(comp.addPointAt({lowX, (float)kCurveHeight * 0.5f}), 0) << "a low click should take the low shelf";

    const float highX = EQCurveComponent::freqToXStatic(14000.0f, (float)kCurveWidth);
    EXPECT_EQ(comp.addPointAt({highX, (float)kCurveHeight * 0.5f}), 3) << "a high click should take the high shelf";
}

TEST(EQCurveInteraction, AddPointReturnsMinusOneOnceAllSlotsAreUsed) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    for (int i = 0; i < ParametricEQModule::kNumBands; ++i)
        EXPECT_GE(comp.addPointAt({(float)(40 + i * 60), (float)kCurveHeight * 0.5f}), 0) << "point " << i;

    EXPECT_EQ(eq.getEnabledBandCount(), ParametricEQModule::kNumBands);
    EXPECT_EQ(comp.addPointAt({200.0f, 50.0f}), -1) << "a fifth point has nowhere to go";
    EXPECT_EQ(eq.getEnabledBandCount(), ParametricEQModule::kNumBands);
}

TEST(EQCurveInteraction, RemoveBandDisablesItAndClearsSelection) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    const int band = comp.addPointAt({150.0f, 60.0f});
    ASSERT_GE(band, 0);
    ASSERT_EQ(comp.getSelectedBand(), band);

    comp.removeBand(band);
    EXPECT_FALSE(eq.isBandEnabled(band));
    EXPECT_EQ(eq.getEnabledBandCount(), 0);
    EXPECT_EQ(comp.getSelectedBand(), -1);

    // Removing an already-disabled or out-of-range band is a no-op, not a crash.
    EXPECT_NO_FATAL_FAILURE(comp.removeBand(band));
    EXPECT_NO_FATAL_FAILURE(comp.removeBand(-1));
    EXPECT_NO_FATAL_FAILURE(comp.removeBand(ParametricEQModule::kNumBands));
}

TEST(EQCurveInteraction, HitTestFindsEnabledHandlesAndIgnoresDisabledOnes) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    const float x = EQCurveComponent::freqToXStatic(1000.0f, (float)kCurveWidth);
    const float y = EQCurveComponent::dbToYStatic(9.0f, (float)kCurveHeight);
    const int band = comp.addPointAt({x, y});
    ASSERT_GE(band, 0);

    EXPECT_EQ(comp.hitTestBand({x, y}), band) << "dead on the handle";
    EXPECT_EQ(comp.hitTestBand({x + 3.0f, y - 3.0f}), band) << "just inside the hit radius";
    EXPECT_EQ(comp.hitTestBand({x + 60.0f, y}), -1) << "well away from any handle";

    // Once disabled, the handle is gone and must not be hit-testable.
    comp.removeBand(band);
    EXPECT_EQ(comp.hitTestBand({x, y}), -1);
}

TEST(EQCurveInteraction, DragMovesTheBandInBothFrequencyAndGain) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    const int band = comp.addPointAt({EQCurveComponent::freqToXStatic(500.0f, (float)kCurveWidth),
                                      EQCurveComponent::dbToYStatic(0.0f, (float)kCurveHeight)});
    ASSERT_GE(band, 0);

    const float targetX = EQCurveComponent::freqToXStatic(4000.0f, (float)kCurveWidth);
    const float targetY = EQCurveComponent::dbToYStatic(-12.0f, (float)kCurveHeight);
    comp.dragBandTo(band, {targetX, targetY});

    const auto bands = eq.getBandSnapshots();
    EXPECT_NEAR(bands[(size_t)band].freqHz, 4000.0f, 60.0f);
    EXPECT_NEAR(bands[(size_t)band].gainDb, -12.0f, 0.3f);

    // Dragging past the edges clamps to the parameter range instead of wrapping.
    comp.dragBandTo(band, {-500.0f, -500.0f});
    const auto clamped = eq.getBandSnapshots();
    EXPECT_GE(clamped[(size_t)band].freqHz, ParametricEQModule::kMinFreq - 0.5f);
    EXPECT_LE(clamped[(size_t)band].gainDb, ParametricEQModule::kMaxGainDb + 0.01f);

    EXPECT_NO_FATAL_FAILURE(comp.dragBandTo(-1, {10.0f, 10.0f}));
    EXPECT_NO_FATAL_FAILURE(comp.dragBandTo(ParametricEQModule::kNumBands, {10.0f, 10.0f}));
}

TEST(EQCurveInteraction, ScrollAdjustsQMultiplicatively) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    const int band = comp.addPointAt({200.0f, 60.0f});
    ASSERT_GE(band, 0);
    const float startQ = eq.getBandSnapshots()[(size_t)band].q;

    comp.nudgeBandQ(band, 1.0f); // one notch "up" doubles Q
    const float narrowed = eq.getBandSnapshots()[(size_t)band].q;
    EXPECT_GT(narrowed, startQ);
    EXPECT_NEAR(narrowed, startQ * 2.0f, startQ * 0.1f);

    comp.nudgeBandQ(band, -1.0f); // and back down again
    EXPECT_NEAR(eq.getBandSnapshots()[(size_t)band].q, startQ, startQ * 0.1f);

    // Q saturates at the parameter bounds rather than running away.
    for (int i = 0; i < 12; ++i)
        comp.nudgeBandQ(band, 1.0f);
    EXPECT_NEAR(eq.getBandSnapshots()[(size_t)band].q, ParametricEQModule::kMaxQ, 0.05f);
    for (int i = 0; i < 24; ++i)
        comp.nudgeBandQ(band, -1.0f);
    EXPECT_NEAR(eq.getBandSnapshots()[(size_t)band].q, ParametricEQModule::kMinQ, 0.05f);

    EXPECT_NO_FATAL_FAILURE(comp.nudgeBandQ(-1, 1.0f));
    EXPECT_NO_FATAL_FAILURE(comp.nudgeBandQ(ParametricEQModule::kNumBands, 1.0f));
}

TEST(EQCurveInteraction, EveryEditIsBracketedByExactlyOneGesture) {
    // The host wires these to undo capture/push, so an unbalanced pair would corrupt the undo
    // stack or drop the edit out of history entirely.
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    int starts = 0;
    int ends = 0;
    comp.onGestureStart = [&starts] { ++starts; };
    comp.onGestureEnd = [&ends] { ++ends; };

    const int band = comp.addPointAt({200.0f, 60.0f});
    ASSERT_GE(band, 0);
    EXPECT_EQ(starts, 1);
    EXPECT_EQ(ends, 1);

    comp.nudgeBandQ(band, 0.5f);
    EXPECT_EQ(starts, 2);
    EXPECT_EQ(ends, 2);

    comp.removeBand(band);
    EXPECT_EQ(starts, 3);
    EXPECT_EQ(ends, 3);

    // A rejected add (all slots full) must not open a gesture at all.
    for (int i = 0; i < ParametricEQModule::kNumBands; ++i)
        comp.addPointAt({(float)(40 + i * 60), 60.0f});
    const int startsAfterFill = starts;
    EXPECT_EQ(comp.addPointAt({210.0f, 60.0f}), -1);
    EXPECT_EQ(starts, startsAfterFill);
    EXPECT_EQ(ends, startsAfterFill);
}

TEST(EQCurveInteraction, DragIsNotBracketedPerStepSoOneDragIsOneUndoStep) {
    // dragBandTo is called repeatedly during a mouse drag; the gesture is opened once by
    // mouseDrag, so the primitive itself must not push undo entries.
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    const int band = comp.addPointAt({200.0f, 60.0f});
    ASSERT_GE(band, 0);

    int starts = 0;
    comp.onGestureStart = [&starts] { ++starts; };
    for (int i = 0; i < 10; ++i)
        comp.dragBandTo(band, {200.0f + i, 60.0f});
    EXPECT_EQ(starts, 0);
}

TEST(EQCurveInteraction, ComponentPicksUpBandChangesMadeOnTheModule) {
    // The pop-out window and the inline card edit the same module, so each view has to notice
    // the other's edits on its next timer tick.
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    const float x = EQCurveComponent::freqToXStatic(1000.0f, (float)kCurveWidth);
    const float y = EQCurveComponent::dbToYStatic(0.0f, (float)kCurveHeight);
    EXPECT_EQ(comp.hitTestBand({x, y}), -1);

    eq.setBandFreq(1, 1000.0f);
    eq.setBandGain(1, 0.0f);
    eq.setBandEnabled(1, true);
    comp.timerCallback();

    EXPECT_EQ(comp.hitTestBand({x, y}), 1) << "the handle should appear after the snapshot refresh";
}

// ---------------------------------------------------------------------------
// EQCurveComponent — paint smoke tests
// ---------------------------------------------------------------------------

TEST(EQCurveTest, PaintSmokeEmptyShowsTheHint) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    ASSERT_EQ(eq.getEnabledBandCount(), 0);
    paintInto(comp);
}

TEST(EQCurveTest, PaintSmokeWithActiveBands) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);

    comp.addPointAt({EQCurveComponent::freqToXStatic(120.0f, (float)kCurveWidth),
                     EQCurveComponent::dbToYStatic(12.0f, (float)kCurveHeight)});
    comp.addPointAt({EQCurveComponent::freqToXStatic(1500.0f, (float)kCurveWidth),
                     EQCurveComponent::dbToYStatic(-18.0f, (float)kCurveHeight)});
    ASSERT_EQ(eq.getEnabledBandCount(), 2);

    paintInto(comp);
}

TEST(EQCurveTest, PaintSmokeWithAllFourBandsAndSpectrum) {
    ParametricEQModule eq;
    eq.prepareToPlay(48000.0, 512);

    // Feed the module real audio so its VisualBuffer has something for the FFT to chew on.
    juce::AudioBuffer<float> buffer(6, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 4; ++b) {
        buffer.clear();
        for (int i = 0; i < 512; ++i) {
            const float s = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 1000.0f *
                                            static_cast<float>(b * 512 + i) / 48000.0f);
            buffer.setSample(0, i, s);
            buffer.setSample(1, i, s);
        }
        eq.processBlock(buffer, midi);
    }

    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);
    for (int i = 0; i < ParametricEQModule::kNumBands; ++i)
        comp.addPointAt({(float)(40 + i * 80), (float)(40 + i * 20)});

    ASSERT_TRUE(comp.getShowSpectrum());
    // timerCallback drives the FFT; run it explicitly since no message loop is pumping here.
    EXPECT_NO_THROW(comp.timerCallback());
    paintInto(comp);
}

TEST(EQCurveTest, SilentInputDoesNotLeaveTheSpectrumRunning) {
    // The analyser gates on signal so a default-on spectrum costs nothing on an idle patch.
    ParametricEQModule eq;
    eq.prepareToPlay(48000.0, 512);

    juce::AudioBuffer<float> buffer(6, 512);
    juce::MidiBuffer midi;
    buffer.clear();
    eq.processBlock(buffer, midi);

    EQCurveComponent comp(eq);
    comp.setBounds(0, 0, kCurveWidth, kCurveHeight);
    ASSERT_TRUE(comp.getShowSpectrum());
    EXPECT_NO_THROW(comp.timerCallback());
    EXPECT_NO_THROW(comp.timerCallback());
    paintInto(comp);
}

TEST(EQCurveTest, PaintAtDegenerateSizesDoesNotCrash) {
    ParametricEQModule eq;
    EQCurveComponent comp(eq);
    comp.addPointAt({100.0f, 40.0f});

    // Zero size: paint must bail out rather than divide by a zero width/height.
    comp.setBounds(0, 0, 0, 0);
    juce::Image tiny(juce::Image::ARGB, 1, 1, true);
    juce::Graphics gTiny(tiny);
    EXPECT_NO_THROW(comp.paint(gTiny));

    // Narrower than the readout/hint threshold: text is skipped, nothing overflows.
    comp.setBounds(0, 0, 20, 40);
    juce::Image narrow(juce::Image::ARGB, 20, 40, true);
    juce::Graphics gNarrow(narrow);
    EXPECT_NO_THROW(comp.paint(gNarrow));
}

// ---------------------------------------------------------------------------
// EQWindow — the pop-out editor
// ---------------------------------------------------------------------------

TEST(EQWindowTest, HostsACurveOverTheSameModule) {
    ParametricEQModule eq;
    EQWindow window(eq);

    // The window sizes itself to something usable and lays the curve out inside it.
    EXPECT_GT(window.getWidth(), 400);
    EXPECT_GT(window.getHeight(), 300);
    window.resized();
    EXPECT_GT(window.getCurve().getWidth(), 0);
    EXPECT_GT(window.getCurve().getHeight(), 0);

    // Edits through the window land on the shared module.
    const int band = window.getCurve().addPointAt({100.0f, 80.0f});
    ASSERT_GE(band, 0);
    EXPECT_TRUE(eq.isBandEnabled(band));
}

TEST(EQWindowTest, SpectrumToggleTracksTheCurve) {
    ParametricEQModule eq;
    EQWindow window(eq);
    EXPECT_TRUE(window.getSpectrumToggle().getToggleState());

    window.getSpectrumToggle().setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(window.getCurve().getShowSpectrum());
}

TEST(EQWindowTest, ForwardsGestureCallbacksToTheCurve) {
    ParametricEQModule eq;
    EQWindow window(eq);

    int starts = 0;
    int ends = 0;
    window.setGestureCallbacks([&starts] { ++starts; }, [&ends] { ++ends; });

    window.resized();
    ASSERT_GE(window.getCurve().addPointAt({100.0f, 80.0f}), 0);
    EXPECT_EQ(starts, 1);
    EXPECT_EQ(ends, 1);
}

TEST(EQWindowTest, PaintSmoke) {
    ParametricEQModule eq;
    EQWindow window(eq);
    window.setSize(700, 400);
    window.resized();

    juce::Image img(juce::Image::ARGB, 700, 400, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(window.paintEntireComponent(g, false));
}
