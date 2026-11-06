// FrequencyResponseModulationTests.cpp -- the Filter card's response view and the shape map follow modulation of
// every control they read: each CV target the Filter offers either moves the plotted curve when only its CV moves,
// or is listed below with the reason it does not reach the response.
#include "Modules/FilterModule.h"
#include "Modules/LadderCoefficientSetter.h"
#include "ShapeEditor/ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include <cmath>
#include <gtest/gtest.h>
#include <set>
#include <thread>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kLpf24 = 0;
constexpr int kNotch = 6;
constexpr float kCV = 0.4f;
constexpr float kMovedDb = 0.25f; // a curve that moved by less than this anywhere did not visibly move

void setValue(FilterModule& m, const char* id, float value) {
    auto* p = findParameterByID(&m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

/** One block: a quiet sine on Audio L and, when `channel` >= 0, `cv` held on that channel. */
void runBlock(FilterModule& m, int channel, float cv) {
    juce::AudioBuffer<float> buffer(m.getTotalNumInputChannels(), kBlock);
    buffer.clear();
    for (int i = 0; i < kBlock; ++i)
        buffer.setSample(0, i, 0.05f * std::sin(juce::MathConstants<float>::twoPi * 300.0f * (float)i / 48000.0f));
    if (channel >= 0)
        for (int i = 0; i < kBlock; ++i)
            buffer.setSample(channel, i, cv);
    juce::MidiBuffer midi;
    m.processBlock(buffer, midi);
}

std::vector<float> plotted(FrequencyResponseComponent& view) {
    view.timerCallback();
    std::vector<float> db(1024);
    for (int i = 0; i < 1024; ++i)
        db[(size_t)i] = view.getMagnitudeDbForTest(i);
    return db;
}

float largestChange(const std::vector<float>& a, const std::vector<float>& b) {
    float most = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        most = std::max(most, std::abs(a[i] - b[i]));
    return most;
}

/** The CV targets that do not reach the drawn response, per filter type, each with the reason. */
std::string whyNotPlotted(const juce::String& paramId, int type) {
    static const std::set<juce::String> customOnly{"morphX", "morphY", "shift", "depth"};
    if (paramId == "outputLevel" || paramId == "mix" || paramId == "pan")
        return "the output stage after the filter, not its response";
    if (paramId == "keyTrack")
        return "moves the cutoff only through the Pitch input, which is empty here";
    if (customOnly.count(paramId) > 0 && type != FilterModule::kCustomTypeIndex)
        return "a Custom-only control";
    if (paramId == "drive" && (type == kNotch || type == FilterModule::kCustomTypeIndex))
        return "Notch and Custom ignore Drive";
    return {};
}

} // namespace

// Regression test for FRO1190: only Cutoff (and Resonance in Custom) followed modulation; Resonance in the other types,
// Drive and Custom's Morph X/Y, Shift and Depth stayed at the knob while the sound moved.
TEST(FrequencyResponseModulation, EveryCvTargetTheViewReadsMovesTheCurveWhenOnlyItsCvMoves) {
    for (const int type : {kLpf24, kNotch, FilterModule::kCustomTypeIndex}) {
        for (const bool poly : {false, true}) {
            FilterModule filter;
            setValue(filter, "filterType", (float)type);
            setValue(filter, "poly", poly ? 1.0f : 0.0f);
            filter.prepareToPlay(kSampleRate, kBlock);
            FrequencyResponseComponent view(filter);
            for (const auto& target : filter.getModulationTargets()) {
                SCOPED_TRACE(target.paramId.toStdString() + " type " + std::to_string(type) +
                             (poly ? " poly" : " mono"));
                runBlock(filter, -1, 0.0f);
                const auto atRest = plotted(view);
                runBlock(filter, target.channelIndex, kCV);
                const auto modulated = plotted(view);
                const std::string reason = whyNotPlotted(target.paramId, type);
                if (reason.empty())
                    EXPECT_GT(largestChange(atRest, modulated), kMovedDb) << "the curve stayed at the knob value";
                else
                    EXPECT_LT(largestChange(atRest, modulated), kMovedDb) << "listed as not plotted: " << reason;
            }
        }
    }
}

TEST(FrequencyResponseModulation, NothingIsRecomputedWhileNothingMoves) {
    FilterModule filter;
    setValue(filter, "filterType", (float)FilterModule::kCustomTypeIndex);
    filter.prepareToPlay(kSampleRate, kBlock);
    FrequencyResponseComponent view(filter);
    runBlock(filter, FilterModule::kMorphXCVChannel, kCV);
    view.timerCallback();
    const int count = view.getRecomputeCountForTest();
    runBlock(filter, FilterModule::kMorphXCVChannel, kCV);
    view.timerCallback();
    view.timerCallback();
    EXPECT_EQ(view.getRecomputeCountForTest(), count) << "same modulated state: no recompute, no repaint";
    runBlock(filter, FilterModule::kMorphXCVChannel, -kCV);
    view.timerCallback();
    EXPECT_EQ(view.getRecomputeCountForTest(), count + 1);
}

TEST(FrequencyResponseModulation, AKnobTurnShowsWithoutAudioRunning) {
    FilterModule filter;
    FrequencyResponseComponent view(filter);
    const auto before = plotted(view);
    setValue(filter, "resonance", 0.8f);
    EXPECT_GT(largestChange(before, plotted(view)), 3.0f);
    setValue(filter, "drive", 6.0f);
    EXPECT_FLOAT_EQ(filter.getModulatedDrive(), 6.0f);
}

TEST(FrequencyResponseModulation, DriveLiftMatchesTheLadderBelowItsSaturation) {
    // The view lifts the ladder types by ladderDriveGain; a quiet sine through the real filter rises by the same.
    auto levelAt = [](float drive) {
        FilterModule filter;
        setValue(filter, "cutoff", 5000.0f);
        setValue(filter, "resonance", 0.0f);
        setValue(filter, "drive", drive);
        filter.prepareToPlay(kSampleRate, kBlock);
        float rms = 0.0f;
        for (int b = 0; b < 40; ++b) {
            juce::AudioBuffer<float> buffer(filter.getTotalNumInputChannels(), kBlock);
            buffer.clear();
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(0, i,
                                 0.001f * std::sin(juce::MathConstants<float>::twoPi * 200.0f *
                                                   (float)(b * kBlock + i) / (float)kSampleRate));
            juce::MidiBuffer midi;
            filter.processBlock(buffer, midi);
            rms = buffer.getRMSLevel(0, 0, kBlock);
        }
        return 20.0f * std::log10(rms);
    };
    const float heard = levelAt(7.0f) - levelAt(1.0f);
    const float drawn = 20.0f * std::log10(synth::ladderDriveGain(7.0f) / synth::ladderDriveGain(1.0f));
    EXPECT_GT(drawn, 3.0f);
    EXPECT_NEAR(heard, drawn, 1.0f);
}

TEST(FrequencyResponseModulation, TheCardMapCursorFollowsMorphCv) {
    ShapeRig rig;
    setValue(*rig.filter, "filterType", (float)FilterModule::kCustomTypeIndex);
    rig.filter->prepareToPlay(kSampleRate, kBlock);
    MorphMap map;
    map.setSize(252, 150);
    ShapeMapController controller{map, *rig.adapter};
    controller.refresh();
    runBlock(*rig.filter, FilterModule::kMorphXCVChannel, kCV);
    controller.syncCursor();
    EXPECT_NEAR(map.getCursor().x, 0.5f + kCV, 1e-4f);
    EXPECT_NEAR(map.getCursor().y, 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(rig.filter->getCurrentMorphX(), 0.5f) << "showing the modulation never writes the knob";
}

TEST(FrequencyResponseModulation, TheEditorWindowMapCursorFollowsMorphCv) {
    ShapeRig rig;
    setValue(*rig.filter, "filterType", (float)FilterModule::kCustomTypeIndex);
    rig.filter->prepareToPlay(kSampleRate, kBlock);
    ShapeListEditor editor{rig.adapter, 0};
    runBlock(*rig.filter, FilterModule::kMorphYCVChannel, -kCV);
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;
    while (std::abs(editor.getMap().getCursor().y - (0.5f - kCV)) > 1e-3f &&
           juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_NEAR(editor.getMap().getCursor().y, 0.5f - kCV, 1e-3f) << "the window's poll moves its map cursor";
}

TEST(FrequencyResponseModulation, TheAudioThreadPublishesWhileTheViewReads) {
    // The modulated values cross threads as relaxed atomics: one thread processes blocks with moving CV while this
    // one polls the view and the getters. A thread-sanitizer build reports any unsynchronised access here.
    FilterModule filter;
    setValue(filter, "filterType", (float)FilterModule::kCustomTypeIndex);
    filter.prepareToPlay(kSampleRate, kBlock);
    FrequencyResponseComponent view(filter);
    std::atomic<bool> done{false};
    std::thread audio([&] {
        for (int b = 0; b < 300; ++b)
            for (const int channel : {FilterModule::kMorphXCVChannel, FilterModule::kShiftCVChannel, 2})
                runBlock(filter, channel, 0.3f * std::sin(0.1f * (float)b));
        done = true;
    });
    int polls = 0;
    while (!done.load()) {
        view.timerCallback();
        EXPECT_TRUE(std::isfinite(filter.getModulatedMorphX() + filter.getModulatedShift() +
                                  filter.getModulatedResonance() + filter.getModulatedDrive()));
        ++polls;
    }
    audio.join();
    EXPECT_GT(polls, 0);
}
