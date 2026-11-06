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
    FilterSpectrumAnalyser analyser;
    const auto samples = sine(1000.0, FilterSpectrumAnalyser::kFftSize);
    analyser.analyse(samples.data(), (int)samples.size(), kRate, FilterSpectrumAnalyser::kMaxStepSeconds);
    EXPECT_NEAR(peakIndex(analyser.getDb()), gridIndexFor(1000.0f), 4);
    EXPECT_GT(analyser.getDb()[(size_t)peakIndex(analyser.getDb())], -12.0f);
    EXPECT_LT(analyser.getDb()[(size_t)gridIndexFor(100.0f)], -40.0f);
}

TEST(FilterSpectrumAnalyser, StartsAtTheFloorAndMovesTowardTheReadingWithTime) {
    FilterSpectrumAnalyser analyser;
    EXPECT_EQ(analyser.getDb().size(), (size_t)FilterSpectrumAnalyser::kNumPoints);
    EXPECT_FLOAT_EQ(analyser.getDb()[100], FilterSpectrumAnalyser::kMinDb);
    const auto samples = sine(1000.0, FilterSpectrumAnalyser::kFftSize);
    analyser.analyse(samples.data(), (int)samples.size(), kRate, 0.002);
    const float once = analyser.getDb()[(size_t)gridIndexFor(1000.0f)];
    EXPECT_GT(once, FilterSpectrumAnalyser::kMinDb);
    analyser.analyse(samples.data(), (int)samples.size(), kRate, 0.002);
    EXPECT_GT(analyser.getDb()[(size_t)gridIndexFor(1000.0f)], once);
    analyser.analyse(samples.data(), (int)samples.size(), kRate, 0.0);
    analyser.reset();
    EXPECT_FLOAT_EQ(analyser.getDb()[(size_t)gridIndexFor(1000.0f)], FilterSpectrumAnalyser::kMinDb);
}

namespace {
double fixedClockSeconds = 0.0;
double fixedClock() { return fixedClockSeconds; }
} // namespace

// The card's Show Spectrum and the shape window read one analyser class over one buffer: same samples, same times,
// same trace.
TEST(FilterSpectrumAnalyser, TheCardAndTheShapeWindowShowTheSameSpectrum) {
    FilterSpectrumAnalyser::setClockForTest(&fixedClock);
    ShapeRig rig;
    const auto samples = sine(440.0, FilterModule::kSpectrumSamples);
    rig.filter->getSpectrumBuffer().pushBlock(samples.data(), (int)samples.size());
    FrequencyResponseComponent view(*rig.filter);
    view.setShowSpectrum(true);
    for (int i = 0; i < 4; ++i) {
        fixedClockSeconds = i / 30.0;
        view.timerCallback();
        rig.adapter->updateSpectrum();
    }
    const auto* window = rig.adapter->updateSpectrum();
    FilterSpectrumAnalyser::setClockForTest(nullptr);
    ASSERT_NE(window, nullptr);
    const auto& card = view.getSpectrumDbForTest();
    ASSERT_EQ(card.size(), window->size());
    for (size_t i = 0; i < card.size(); ++i)
        ASSERT_NEAR(card[i], (*window)[i], 1e-4f) << "point " << i;
    EXPECT_GT(card[(size_t)gridIndexFor(440.0f)], -20.0f);
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
    const auto samples = sine(1000.0, FilterModule::kSpectrumSamples);
    rig.filter->getSpectrumBuffer().pushBlock(samples.data(), (int)samples.size());
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
