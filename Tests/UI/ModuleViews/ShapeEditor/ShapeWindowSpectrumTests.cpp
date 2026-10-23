// ShapeWindowSpectrumTests.cpp -- the shared spectrum analyser and the shape window's Show spectrum toggle.
#include "../../../TestSettingsHelpers.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/FilterSpectrumAnalyser.h"
#include "UI/ModuleViews/FrequencyGrid.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"
#include <cmath>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr double kRate = 44100.0;

std::vector<float> sine(double hz, int n) {
    std::vector<float> out((size_t)n);
    for (int i = 0; i < n; ++i)
        out[(size_t)i] = (float)std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / kRate);
    return out;
}

int peakIndex(const std::vector<float>& db) { return (int)(std::max_element(db.begin(), db.end()) - db.begin()); }

/** The index of the log grid point nearest `hz`. */
int gridIndexFor(float hz) {
    return (int)std::lround(
        FrequencyGrid::kMinFreq * 0 +
        (std::log(hz / FrequencyGrid::kMinFreq) / std::log(FrequencyGrid::kMaxFreq / FrequencyGrid::kMinFreq)) *
            (FilterSpectrumAnalyser::kNumPoints - 1));
}

/** The reading the Filter's response view computed inline before the analyser was shared: the reference. */
std::vector<float> referenceSpectrum(const std::vector<float>& samples, int passes) {
    constexpr int fftSize = 1024;
    juce::dsp::FFT fft{10};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> fftData(fftSize * 2, 0.0f);
    std::vector<float> magnitudes(1024, -80.0f);
    for (int pass = 0; pass < passes; ++pass) {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        const int copySize = std::min((int)samples.size(), fftSize);
        for (int i = 0; i < copySize; ++i)
            fftData[(size_t)i] = samples[(size_t)i];
        window.multiplyWithWindowingTable(fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(fftData.data());
        const float binWidth = static_cast<float>(kRate) / static_cast<float>(fftSize);
        const float normFactor = 2.0f / static_cast<float>(fftSize);
        for (int i = 0; i < 1024; ++i) {
            const float freq = FrequencyGrid::indexToFreq(i, 1024);
            const float exactBin = freq / binWidth;
            const int bin0 = static_cast<int>(exactBin);
            const int bin1 = bin0 + 1;
            const float frac = exactBin - static_cast<float>(bin0);
            float mag = 0.0f;
            if (bin0 >= 0 && bin1 < fftSize / 2)
                mag = fftData[(size_t)bin0] * (1.0f - frac) + fftData[(size_t)bin1] * frac;
            else if (bin0 >= 0 && bin0 < fftSize / 2)
                mag = fftData[(size_t)bin0];
            mag *= normFactor;
            const float db = 20.0f * std::log10(std::max(mag, 0.00001f));
            const float target = juce::jlimit(-80.0f, 0.0f, db);
            magnitudes[(size_t)i] += 0.3f * (target - magnitudes[(size_t)i]);
        }
    }
    return magnitudes;
}

juce::Image render(juce::Component& c) {
    juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    c.paintEntireComponent(g, false);
    return image;
}

bool sameImage(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

struct SpectrumKeysGuard {
    synth::test::PersistedKeysGuard guard{juce::StringArray{shapewindowprefs::spectrumKey("filter")}};
};

} // namespace

TEST(FilterSpectrumAnalyser, ASineAt1kHzPeaksAtTheThousandHertzGridPoint) {
    FilterSpectrumAnalyser analyser(1.0f);
    const auto samples = sine(1000.0, 1024);
    analyser.analyse(samples.data(), (int)samples.size(), kRate);
    EXPECT_NEAR(peakIndex(analyser.getDb()), gridIndexFor(1000.0f), 4);
    EXPECT_GT(analyser.getDb()[(size_t)peakIndex(analyser.getDb())], -12.0f);
    EXPECT_LT(analyser.getDb()[(size_t)gridIndexFor(100.0f)], -40.0f);
}

TEST(FilterSpectrumAnalyser, StartsAtTheFloorAndSmoothsTowardTheReading) {
    FilterSpectrumAnalyser analyser;
    EXPECT_EQ(analyser.getDb().size(), (size_t)FilterSpectrumAnalyser::kNumPoints);
    EXPECT_FLOAT_EQ(analyser.getDb()[100], FilterSpectrumAnalyser::kMinDb);
    const auto samples = sine(1000.0, 1024);
    analyser.analyse(samples.data(), (int)samples.size(), kRate);
    const float once = analyser.getDb()[(size_t)gridIndexFor(1000.0f)];
    analyser.analyse(samples.data(), (int)samples.size(), kRate);
    EXPECT_GT(analyser.getDb()[(size_t)gridIndexFor(1000.0f)], once);
    analyser.reset();
    EXPECT_FLOAT_EQ(analyser.getDb()[(size_t)gridIndexFor(1000.0f)], FilterSpectrumAnalyser::kMinDb);
}

TEST(FilterSpectrumAnalyser, TheResponseViewStillShowsTheSpectrumItShowedBeforeTheAnalyserWasShared) {
    FilterModule filter;
    const auto samples = sine(1000.0, 1024);
    filter.getVisualBuffer()->pushBlock(samples.data(), (int)samples.size());
    FrequencyResponseComponent view(filter);
    view.setShowSpectrum(true);
    for (int i = 0; i < 3; ++i)
        view.timerCallback();

    std::vector<float> stored(1024);
    filter.getVisualBuffer()->copyTo(stored);
    const auto reference = referenceSpectrum(stored, 3);
    const auto& actual = view.getSpectrumDbForTest();
    ASSERT_EQ(actual.size(), reference.size());
    for (size_t i = 0; i < actual.size(); ++i)
        ASSERT_NEAR(actual[i], reference[i], 1e-4f) << "point " << i;
}

TEST(ShapeWindowSpectrum, OnlyTheFilterWindowOffersTheToggleAndItStartsOff) {
    SpectrumKeysGuard keys;
    ShapeRig filter;
    ShapeListEditor filterEditor(filter.adapter, 0);
    ASSERT_NE(filterEditor.getSpectrumToggle(), nullptr);
    EXPECT_FALSE(filterEditor.getSpectrumToggle()->getToggleState());
    EXPECT_FALSE(filterEditor.getSpectrumToggle()->getTitle().isEmpty());
    EXPECT_FALSE(filterEditor.getSpectrumToggle()->getTooltip().isEmpty());
    EXPECT_TRUE(filterEditor.getSpectrumToggle()->getWantsKeyboardFocus());

    AudioEngine engine;
    auto* lfo = new LFOModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    AppUndoManager undo;
    auto adapter = std::make_shared<LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
    ShapeListEditor lfoEditor(adapter, 0);
    EXPECT_EQ(lfoEditor.getSpectrumToggle(), nullptr);
    EXPECT_FALSE(lfoEditor.isSpectrumTimerRunning());
}

TEST(ShapeWindowSpectrum, TheTimerRunsOnlyWhileTheToggleIsOnAndTheWindowIsVisible) {
    SpectrumKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setVisible(true);
    EXPECT_FALSE(editor.isSpectrumTimerRunning()) << "off: no timer";

    auto& toggle = *editor.getSpectrumToggle();
    toggle.setToggleState(true, juce::dontSendNotification);
    toggle.onClick();
    EXPECT_TRUE(editor.isSpectrumTimerRunning());
    editor.setVisible(false);
    EXPECT_FALSE(editor.isSpectrumTimerRunning()) << "hidden: stopped";
    editor.setVisible(true);
    EXPECT_TRUE(editor.isSpectrumTimerRunning());
    toggle.setToggleState(false, juce::dontSendNotification);
    toggle.onClick();
    EXPECT_FALSE(editor.isSpectrumTimerRunning());
}

TEST(ShapeWindowSpectrum, ThePaintedSpectrumAppearsWithTheToggleAndLeavesNoTraceWhenOff) {
    SpectrumKeysGuard keys;
    ShapeRig rig;
    const auto samples = sine(1000.0, 1024);
    rig.filter->getVisualBuffer()->pushBlock(samples.data(), (int)samples.size());
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    const auto before = render(curve);

    auto& toggle = *editor.getSpectrumToggle();
    toggle.setToggleState(true, juce::dontSendNotification);
    toggle.onClick();
    EXPECT_FALSE(sameImage(before, render(curve))) << "the spectrum is drawn";

    toggle.setToggleState(false, juce::dontSendNotification);
    toggle.onClick();
    EXPECT_TRUE(sameImage(before, render(curve))) << "off paints what the editor painted before";
}

TEST(ShapeWindowSpectrum, TheToggleIsRememberedAcrossWindows) {
    SpectrumKeysGuard keys;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    shapewindowprefs::saveFlag(&props, shapewindowprefs::spectrumKey("filter"), false);
    ShapeRig rig;
    rig.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(rig.adapter, 0);
        editor.getSpectrumToggle()->setToggleState(true, juce::dontSendNotification);
        editor.getSpectrumToggle()->onClick();
    }
    ShapeListEditor again(rig.adapter, 0);
    EXPECT_TRUE(again.getSpectrumToggle()->getToggleState());
}
