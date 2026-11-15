// ShapeWindowSpectrumTests.cpp -- the shared spectrum analyser and the shape window's Show spectrum toggle.
#include "../../../TestSettingsHelpers.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/FilterSpectrumAnalyser.h"
#include "UI/ModuleViews/FrequencyGrid.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"
#include <algorithm>
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

// Muting a Filter lets its spectrum fall to the floor in the card's response view and the shape window: the audio goes
// through ModuleBase::processBlock past the click-free mute fade, then the views tick on a simulated 30 Hz clock.
namespace {

constexpr double kMuteRate = 44100.0;
constexpr int kMuteBlock = 512;
constexpr int kMuteTicks = 45; // 1.5 s at 30 Hz: the ballistics reach the floor in about a second
constexpr float kMuteFloor = FilterSpectrumAnalyser::kMinDb;

double gMuteNow = 0.0;
double muteTickClock() { return gMuteNow; }

/** Feeds a 1 kHz tone on every channel of `m` for `blocks` blocks through ModuleBase::processBlock. */
void feedTone(ModuleBase& m, int blocks, long& phase) {
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buffer(std::max(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels()),
                                        kMuteBlock);
        buffer.clear();
        for (int i = 0; i < kMuteBlock; ++i, ++phase) {
            const auto v =
                (float)(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * (double)phase / kMuteRate));
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, v);
        }
        m.processBlock(buffer, midi);
    }
}

void muteOpenCutoff(FilterModule& f) {
    for (auto* p : f.getParameters())
        if (p->getName(100) == "Cutoff")
            p->setValueNotifyingHost(1.0f);
}

void muteSetBool(ModuleBase& m, const char* id, bool on) {
    for (auto* p : m.getParameters())
        if (auto* pid = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); pid != nullptr && pid->paramID == id)
            p->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

float muteDbPeak(const std::vector<float>& db) { return *std::max_element(db.begin(), db.end()); }

struct MuteClockGuard {
    MuteClockGuard() {
        gMuteNow = 0.0;
        FilterSpectrumAnalyser::setClockForTest(&muteTickClock);
    }
    ~MuteClockGuard() { FilterSpectrumAnalyser::setClockForTest(nullptr); }
};

enum class Layout { Mono, Poly, DualIO };

/** A Filter with the tone playing (the spectrum shows it), then muted and run past the fade. */
void mutePlayTone(FilterModule& f, Layout layout, long& phase) {
    f.prepareToPlay(kMuteRate, kMuteBlock);
    muteSetBool(f, "poly", layout == Layout::Poly);
    muteSetBool(f, "dualIO", layout == Layout::DualIO);
    muteOpenCutoff(f);
    feedTone(f, 20, phase);
}

void muteAndRunOn(FilterModule& f, long& phase) {
    f.setMuted(true);
    feedTone(f, 40, phase); // the 5 ms fade ends in the first block; the rest is the muted branch
}

} // namespace

// Muted, the analyser reads no samples: every point only releases from where it is, at the live release time.
TEST(FilterSpectrumAnalyser, MutedReleasesEveryPointMonotonicallyToTheFloorWithoutReadingTheBuffer) {
    MuteClockGuard clock;
    FilterSpectrumAnalyser a;
    const auto tone = sine(1000.0, FilterSpectrumAnalyser::kFftSize);
    for (int i = 0; i < 12; ++i)
        a.analyse(tone.data(), (int)tone.size(), kRate, 1.0 / 30.0);
    const auto start = a.getDb();
    ASSERT_GT(*std::max_element(start.begin(), start.end()), -10.0f);

    VisualBuffer loud(FilterSpectrumAnalyser::kFftSize);
    loud.pushBlock(tone.data(), (int)tone.size()); // a full-scale tone is in the buffer; a muted read ignores it
    auto prev = start;
    for (int frame = 0; frame < 60; ++frame) {
        gMuteNow += 1.0 / 30.0;
        a.analyse(loud, kRate, true);
        const auto& now = a.getDb();
        for (size_t k = 0; k < now.size(); ++k) {
            ASSERT_LE(now[k], prev[k]) << "frame " << frame << " point " << k << " rose";
            ASSERT_LE(now[k], start[k]) << "frame " << frame << " point " << k << " is above its start";
        }
        prev = now;
    }
    EXPECT_FLOAT_EQ(*std::max_element(prev.begin(), prev.end()), FilterSpectrumAnalyser::kMinDb);
}

class MuteSpectrumFall : public ::testing::TestWithParam<Layout> {};

TEST_P(MuteSpectrumFall, TheCardsResponseViewFallsToTheFloorAfterMute) {
    MuteClockGuard clock;
    ShapeRig rig;
    long phase = 0;
    mutePlayTone(*rig.filter, GetParam(), phase);
    FrequencyResponseComponent view(*rig.filter);
    view.setBounds(0, 0, 300, 120);
    view.setShowSpectrum(true);
    gMuteNow += 1.0 / 30.0;
    view.timerCallback();
    ASSERT_GT(muteDbPeak(view.getSpectrumDbForTest()), -30.0f) << "the tone must show before the mute";

    muteAndRunOn(*rig.filter, phase);
    for (int i = 0; i < kMuteTicks; ++i) {
        gMuteNow += 1.0 / 30.0;
        view.timerCallback();
    }
    EXPECT_FLOAT_EQ(muteDbPeak(view.getSpectrumDbForTest()), kMuteFloor);

    EXPECT_FLOAT_EQ(view.getSpectrumPeakDbForTest(), kMuteFloor);
}

TEST_P(MuteSpectrumFall, TheShapeWindowFallsToTheFloorAfterMute) {
    SpectrumKeysGuard keys;
    MuteClockGuard clock;
    ShapeRig rig;
    long phase = 0;
    mutePlayTone(*rig.filter, GetParam(), phase);
    ShapeListEditor editor(rig.adapter, 0);
    editor.setBounds(0, 0, ShapeListEditor::kWidth, ShapeListEditor::kPerNoteHeight);
    editor.getSpectrumToggle()->setToggleState(true, juce::dontSendNotification);
    gMuteNow += 1.0 / 30.0;
    editor.pollSpectrum();
    ASSERT_GT(editor.spectrumPeakDbForTest(), -30.0f) << "the tone must show before the mute";

    muteAndRunOn(*rig.filter, phase);
    for (int i = 0; i < kMuteTicks; ++i) {
        gMuteNow += 1.0 / 30.0;
        editor.pollSpectrum();
    }
    EXPECT_NEAR(editor.spectrumPeakDbForTest(), kMuteFloor, 0.01f);
}

TEST_P(MuteSpectrumFall, UnmutingBringsTheTraceBack) {
    MuteClockGuard clock;
    ShapeRig rig;
    long phase = 0;
    mutePlayTone(*rig.filter, GetParam(), phase);
    muteAndRunOn(*rig.filter, phase);
    FrequencyResponseComponent view(*rig.filter);
    view.setShowSpectrum(true);
    for (int i = 0; i < kMuteTicks; ++i) {
        gMuteNow += 1.0 / 30.0;
        view.timerCallback();
    }
    ASSERT_FLOAT_EQ(muteDbPeak(view.getSpectrumDbForTest()), kMuteFloor);

    rig.filter->setMuted(false);
    feedTone(*rig.filter, 20, phase);
    for (int i = 0; i < 6; ++i) {
        gMuteNow += 1.0 / 30.0;
        view.timerCallback();
    }
    EXPECT_GT(muteDbPeak(view.getSpectrumDbForTest()), -30.0f);
}

// Frame by frame: the audio runs through the real mute fade in 3-block steps while the view ticks at 30 Hz,
// the way the app does. Mute is one fall: no frame above the one before it, no point above where it started, none
// rising first; un-mute is one rise: no frame below the one before it, no peak past the live level.
namespace {

constexpr int kFrameBlocks = 3; // 1536 samples: one 30 Hz tick of audio
constexpr double kFrameSeconds = (double)(kFrameBlocks * kMuteBlock) / kMuteRate;
constexpr float kRippleDb = 0.15f; // the steady 1 kHz trace ripples about 0.1 dB with where a block edge falls
// Not asserted per point on un-mute: a tone that has just entered the window is a short burst, so its first frames are
// wider and lower than the steady line and can read several dB over it beside the tone. The peak is what must not
// overshoot.
constexpr float kEps = 0.05f; // the steady trace wobbles about 0.02 dB with where a block edge falls in the window

struct Frames {
    std::vector<float> peaks;
    std::vector<std::vector<float>> points; // card view only
};

template <typename Tick, typename Read>
void runFrames(FilterModule& f, long& phase, int frames, Frames& out, Tick tick, Read read) {
    for (int i = 0; i < frames; ++i) {
        feedTone(f, kFrameBlocks, phase);
        gMuteNow += kFrameSeconds;
        tick();
        read(out);
    }
}

void expectOneFall(const Frames& fall, const std::vector<float>& start, float startPeak) {
    ASSERT_FALSE(fall.peaks.empty());
    float prev = startPeak;
    for (size_t i = 0; i < fall.peaks.size(); ++i) {
        EXPECT_LE(fall.peaks[i], prev + kEps) << "frame " << i << " rose above the one before";
        EXPECT_LE(fall.peaks[i], startPeak + kEps) << "frame " << i << " is above the pre-mute peak";
        prev = fall.peaks[i];
    }
    for (size_t i = 0; i < fall.points.size(); ++i)
        for (size_t k = 0; k < start.size(); ++k)
            ASSERT_LE(fall.points[i][k], start[k] + kEps) << "frame " << i << " point " << k << " is above its start";
    EXPECT_FLOAT_EQ(fall.peaks.back(), kMuteFloor);
}

} // namespace

TEST_P(MuteSpectrumFall, TheCardFallsInOneMotionAndRisesWithoutOvershoot) {
    MuteClockGuard clock;
    ShapeRig rig;
    long phase = 0;
    mutePlayTone(*rig.filter, GetParam(), phase);
    FrequencyResponseComponent view(*rig.filter);
    view.setBounds(0, 0, 300, 120);
    view.setShowSpectrum(true);
    auto tick = [&] { view.timerCallback(); };
    auto read = [&](Frames& o) {
        o.peaks.push_back(view.getSpectrumPeakDbForTest());
        o.points.push_back(view.getSpectrumDbForTest());
    };
    Frames warm;
    runFrames(*rig.filter, phase, 12, warm, tick, read);
    const auto start = warm.points.back();
    const float startPeak = warm.peaks.back();
    ASSERT_GT(startPeak, -30.0f);

    rig.filter->setMuted(true);
    Frames fall;
    runFrames(*rig.filter, phase, 45, fall, tick, read);
    expectOneFall(fall, start, startPeak);

    rig.filter->setMuted(false);
    Frames rise;
    runFrames(*rig.filter, phase, 40, rise, tick, read);
    float prev = kMuteFloor;
    for (size_t i = 0; i < rise.peaks.size(); ++i) {
        EXPECT_GE(rise.peaks[i], prev - kRippleDb) << "frame " << i << " dipped";
        EXPECT_LE(rise.peaks[i], startPeak + 0.5f) << "frame " << i << " overshot the live level";
        prev = rise.peaks[i];
    }
    EXPECT_GT(rise.peaks.back(), -30.0f);
}

TEST_P(MuteSpectrumFall, TheWindowFallsInOneMotionAndRisesWithoutOvershoot) {
    SpectrumKeysGuard keys;
    MuteClockGuard clock;
    ShapeRig rig;
    long phase = 0;
    mutePlayTone(*rig.filter, GetParam(), phase);
    ShapeListEditor editor(rig.adapter, 0);
    editor.setBounds(0, 0, ShapeListEditor::kWidth, ShapeListEditor::kPerNoteHeight);
    editor.getSpectrumToggle()->setToggleState(true, juce::dontSendNotification);
    auto tick = [&] { editor.pollSpectrum(); };
    auto read = [&](Frames& o) { o.peaks.push_back(editor.spectrumPeakDbForTest()); };
    Frames warm;
    runFrames(*rig.filter, phase, 12, warm, tick, read);
    const float startPeak = warm.peaks.back();
    ASSERT_GT(startPeak, -30.0f);

    rig.filter->setMuted(true);
    Frames fall;
    runFrames(*rig.filter, phase, 45, fall, tick, read);
    expectOneFall(fall, {}, startPeak);

    rig.filter->setMuted(false);
    Frames rise;
    runFrames(*rig.filter, phase, 40, rise, tick, read);
    float prev = kMuteFloor;
    for (size_t i = 0; i < rise.peaks.size(); ++i) {
        EXPECT_GE(rise.peaks[i], prev - kRippleDb) << "frame " << i << " dipped";
        EXPECT_LE(rise.peaks[i], startPeak + 0.5f) << "frame " << i << " overshot the live level";
        prev = rise.peaks[i];
    }
}

INSTANTIATE_TEST_SUITE_P(Layouts, MuteSpectrumFall, ::testing::Values(Layout::Mono, Layout::Poly, Layout::DualIO));
