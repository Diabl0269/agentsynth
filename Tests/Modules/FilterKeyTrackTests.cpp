// Filter Key Track: the cutoff follows the Pitch input by `keyTrack` octaves per octave around
// middle C. 0 % (the default) and an empty Pitch jack leave every patch exactly as it was.
//
// Raw channels are written as literals on purpose (they are part of the saved-patch contract):
// Pitch = ch19 in mono, ch19-26 in poly; Key Track CV = ch27; mono Cutoff CV = ch1.

#include "Modules/FilterModule.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

namespace {

constexpr double kSr = 44100.0;
constexpr int kBlock = 512;
constexpr int kPitchCh = 19;
constexpr int kKeyTrackCVCh = 27;
constexpr int kChannels = 28;
constexpr float kMiddleC = 261.63f;

void setFloat(FilterModule& m, const juce::String& id, float v) {
    for (auto* p : m.getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p); f != nullptr && f->paramID == id) {
            *f = v;
            return;
        }
    FAIL() << "no float parameter \"" << id << "\"";
}

void setBool(FilterModule& m, const juce::String& id, bool v) {
    for (auto* p : m.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p); b != nullptr && b->paramID == id) {
            *b = v;
            return;
        }
    FAIL() << "no bool parameter \"" << id << "\"";
}

void setChoice(FilterModule& m, const juce::String& id, int index) {
    for (auto* p : m.getParameters())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p); c != nullptr && c->paramID == id) {
            *c = index;
            return;
        }
    FAIL() << "no choice parameter \"" << id << "\"";
}

/** Pitch (Hz) and Key Track CV held on their channels for every sample; <= 0 pitch leaves the
    jack empty. Audio: a sine at `toneHz` on ch0. */
struct Drive {
    float pitchHz = 0.0f;
    float keyTrackCV = 0.0f;
    float toneHz = 2000.0f;
    float cutoffCV = 0.0f;
};

/** Runs `blocks` blocks of a mono filter and returns the last block (all 28 channels). */
juce::AudioBuffer<float> runMono(FilterModule& m, const Drive& d, int blocks = 6) {
    m.prepareToPlay(kSr, kBlock);
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    int n = 0;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        for (int i = 0; i < kBlock; ++i, ++n) {
            buf.setSample(0, i, 0.5f * std::sin(juce::MathConstants<float>::twoPi * d.toneHz * (float)n / (float)kSr));
            buf.setSample(1, i, d.cutoffCV);
            buf.setSample(kPitchCh, i, d.pitchHz);
            buf.setSample(kKeyTrackCVCh, i, d.keyTrackCV);
        }
        m.processBlock(buf, midi);
    }
    return buf;
}

float effectiveCutoff(float cutoff, float keyTrackPercent, float pitchHz, float keyTrackCV = 0.0f,
                      float cutoffCV = 0.0f) {
    FilterModule m;
    setFloat(m, "cutoff", cutoff);
    setFloat(m, "keyTrack", keyTrackPercent);
    Drive d;
    d.pitchHz = pitchHz;
    d.keyTrackCV = keyTrackCV;
    d.cutoffCV = cutoffCV;
    runMono(m, d);
    return m.getCurrentCutoff();
}

float rmsOf(const juce::AudioBuffer<float>& b, int ch) { return b.getRMSLevel(ch, 0, b.getNumSamples()); }

juce::MemoryBlock legacyState() {
    juce::ValueTree state("ModuleState");          // saved before Key Track and the Pitch jack existed
    state.setProperty("resonance", 0.1f, nullptr); // saved values are normalised 0..1
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), block);
    return block;
}

} // namespace

TEST(FilterKeyTrack, ParameterIsZeroToHundredPercentAtZero) {
    FilterModule f;
    juce::AudioParameterFloat* kt = nullptr;
    for (auto* q : f.getParameters())
        if (auto* fl = dynamic_cast<juce::AudioParameterFloat*>(q); fl != nullptr && fl->paramID == "keyTrack")
            kt = fl;
    ASSERT_NE(kt, nullptr);
    EXPECT_FLOAT_EQ(kt->get(), 0.0f);
    EXPECT_FLOAT_EQ(kt->range.start, 0.0f);
    EXPECT_FLOAT_EQ(kt->range.end, 100.0f);
}

TEST(FilterKeyTrack, OutputLevelAndMuteStayLastAfterTheNewParameter) {
    FilterModule f;
    auto& params = f.getParameters();
    std::vector<juce::String> ids;
    for (auto* p : params)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
            ids.push_back(r->paramID);
    const auto kt = std::find(ids.begin(), ids.end(), "keyTrack");
    const auto level = std::find(ids.begin(), ids.end(), "outputLevel");
    const auto mute = std::find(ids.begin(), ids.end(), "muted");
    ASSERT_NE(kt, ids.end());
    ASSERT_NE(level, ids.end());
    ASSERT_NE(mute, ids.end());
    EXPECT_LT(kt, level);
    EXPECT_LT(kt, mute);
}

TEST(FilterKeyTrack, ZeroPercentIgnoresPitchExactly) {
    FilterModule plain;
    Drive silent;
    const auto a = runMono(plain, silent);

    FilterModule withPitch;
    Drive d;
    d.pitchHz = 523.25f;
    const auto b = runMono(withPitch, d);

    for (int i = 0; i < kBlock; ++i)
        ASSERT_EQ(a.getSample(0, i), b.getSample(0, i)) << "sample " << i;
    EXPECT_FLOAT_EQ(withPitch.getCurrentCutoff(), 440.0f);
}

TEST(FilterKeyTrack, StateWithoutKeyTrackLoadsAtZeroAndRendersTheSame) {
    FilterModule reference;
    setFloat(reference, "cutoff", 1500.0f);
    Drive d;
    d.toneHz = 1800.0f;
    const auto expected = runMono(reference, d);

    FilterModule loaded;
    const auto blob = legacyState();
    loaded.setStateInformation(blob.getData(), (int)blob.getSize());
    setFloat(loaded, "cutoff", 1500.0f);
    d.pitchHz = 880.0f; // a patched Pitch jack must not matter at 0 %
    const auto actual = runMono(loaded, d);
    for (int i = 0; i < kBlock; ++i)
        ASSERT_EQ(expected.getSample(0, i), actual.getSample(0, i)) << "sample " << i;
}

TEST(FilterKeyTrack, FullTrackingFollowsThePitchOneOctavePerOctave) {
    EXPECT_NEAR(effectiveCutoff(1000.0f, 100.0f, kMiddleC), 1000.0f, 2.0f);
    EXPECT_NEAR(effectiveCutoff(1000.0f, 100.0f, 523.25f), 2000.0f, 6.0f);
    EXPECT_NEAR(effectiveCutoff(1000.0f, 100.0f, 130.815f), 500.0f, 3.0f);
}

TEST(FilterKeyTrack, OctaveUpDoublesTheEffectiveCutoffComparedWithMiddleC) {
    const float atC = effectiveCutoff(800.0f, 100.0f, 261.63f);
    const float octaveUp = effectiveCutoff(800.0f, 100.0f, 523.25f);
    EXPECT_NEAR(octaveUp / atC, 2.0f, 0.01f);
}

TEST(FilterKeyTrack, HalfTrackingMovesHalfAsFar) {
    // Two octaves up at 50 % is one octave of cutoff.
    EXPECT_NEAR(effectiveCutoff(1000.0f, 50.0f, 1046.52f), 2000.0f, 8.0f);
}

TEST(FilterKeyTrack, AppliesAfterTheCutoffCVAndClampsToTheFilterRange) {
    EXPECT_NEAR(effectiveCutoff(15000.0f, 100.0f, 4186.0f), 20000.0f, 0.5f);
    EXPECT_NEAR(effectiveCutoff(40.0f, 100.0f, 32.7f), 20.0f, 0.5f);

    // Cutoff CV +0.5 first takes 1000 Hz to 1000 * 20^0.5 ~ 4472 Hz, then Key Track doubles it.
    const float cvOnly = effectiveCutoff(1000.0f, 0.0f, 0.0f, 0.0f, 0.5f);
    const float both = effectiveCutoff(1000.0f, 100.0f, 523.25f, 0.0f, 0.5f);
    EXPECT_NEAR(cvOnly, 1000.0f * std::pow(20.0f, 0.5f), 10.0f);
    EXPECT_NEAR(both, 2.0f * cvOnly, 0.02f * cvOnly);
}

TEST(FilterKeyTrack, NothingHappensWithoutAPitchSignal) {
    EXPECT_FLOAT_EQ(effectiveCutoff(1000.0f, 100.0f, 0.0f), 1000.0f);
    EXPECT_FLOAT_EQ(effectiveCutoff(1000.0f, 100.0f, -50.0f), 1000.0f);
}

TEST(FilterKeyTrack, KeyTrackCVIsAKnobBoundJackInNormalisedUnits) {
    // +0.5 on a 0..100 % knob is 50 %: one octave up is a half octave of cutoff.
    EXPECT_NEAR(effectiveCutoff(1000.0f, 0.0f, 523.25f, 0.5f), 1000.0f * std::sqrt(2.0f), 6.0f);
    // And it lifts a knob that sits at 0: with the knob at 100 %, -1 takes it back to nothing.
    EXPECT_NEAR(effectiveCutoff(1000.0f, 100.0f, 523.25f, -1.0f), 1000.0f, 0.5f);
}

TEST(FilterKeyTrack, TheResponseFollowsTheNote) {
    // A 3 kHz tone through a 1 kHz 24 dB low-pass: a low note keeps the filter shut, a note two
    // octaves above middle C (cutoff x4) opens it.
    Drive low;
    low.toneHz = 3000.0f;
    low.pitchHz = kMiddleC;
    Drive high = low;
    high.pitchHz = kMiddleC * 4.0f;

    FilterModule closed;
    setFloat(closed, "cutoff", 1000.0f);
    setFloat(closed, "keyTrack", 100.0f);
    const float closedRms = rmsOf(runMono(closed, low), 0);

    FilterModule open;
    setFloat(open, "cutoff", 1000.0f);
    setFloat(open, "keyTrack", 100.0f);
    const float openRms = rmsOf(runMono(open, high), 0);

    EXPECT_GT(openRms, 4.0f * closedRms);
}

TEST(FilterKeyTrack, PitchInputsAreClearedAndNeverReachTheOutput) {
    FilterModule f;
    setFloat(f, "keyTrack", 100.0f);
    Drive d;
    d.pitchHz = 440.0f;
    d.keyTrackCV = 0.3f;
    const auto out = runMono(f, d);
    for (int ch = kPitchCh; ch < kChannels; ++ch)
        EXPECT_EQ(rmsOf(out, ch), 0.0f) << "channel " << ch;
}

TEST(FilterKeyTrack, PolyVoicesEachTrackTheirOwnPitch) {
    FilterModule f;
    setBool(f, "poly", true);
    setFloat(f, "cutoff", 500.0f);
    setFloat(f, "keyTrack", 100.0f);
    setChoice(f, "filterType", 0);
    f.prepareToPlay(kSr, kBlock);

    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    float rms0 = 0.0f, rms1 = 0.0f;
    int n = 0;
    for (int b = 0; b < 8; ++b) {
        buf.clear();
        for (int i = 0; i < kBlock; ++i, ++n) {
            const float s = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 2000.0f * (float)n / (float)kSr);
            buf.setSample(0, i, s); // voice 0: middle C -> cutoff stays 500 Hz, the 2 kHz tone is cut
            buf.setSample(1, i, s); // voice 1: three octaves up -> cutoff 4 kHz, the tone passes
            buf.setSample(kPitchCh + 0, i, kMiddleC);
            buf.setSample(kPitchCh + 1, i, kMiddleC * 8.0f);
        }
        f.processBlock(buf, midi);
        rms0 = rmsOf(buf, 0);
        rms1 = rmsOf(buf, 1);
    }
    EXPECT_GT(rms1, 5.0f * rms0);
}

TEST(FilterKeyTrack, PitchAndKeyTrackJacksFollowDrive) {
    FilterModule f;
    EXPECT_EQ(f.getTotalNumInputChannels(), kChannels);
    EXPECT_EQ(f.getTotalNumOutputChannels(), kChannels);
    ASSERT_EQ(f.getVisibleInputPortCount(), 7); // Audio L, Audio R, Cutoff, Resonance, Drive, Pitch, Key Track
    EXPECT_EQ(f.getInputPortLabel(4), "Drive");
    EXPECT_EQ(f.getInputPortLabel(5), "Pitch");
    EXPECT_EQ(f.getInputPortLabel(6), "Key Track");

    const auto pitch = f.mapInputChannel(kPitchCh);
    EXPECT_EQ(pitch.role, PortRole::Pitch);
    EXPECT_EQ(pitch.visibleJackIndex, 5);
    EXPECT_TRUE(pitch.isPolyGroupHead);
    EXPECT_EQ(pitch.polyVoiceSpan, 1);
    EXPECT_FALSE(f.mapInputChannel(kPitchCh + 1).isPolyGroupHead) << "mono: ch20 is not a jack head";

    const auto kt = f.mapInputChannel(kKeyTrackCVCh);
    EXPECT_EQ(kt.role, PortRole::ModCV);
    EXPECT_EQ(kt.visibleJackIndex, 6);

    for (int jack = 0; jack < f.getVisibleInputPortCount(); ++jack)
        EXPECT_EQ(f.getJackTargets(jack, true).size(), 1u) << "visible input jack " << jack;
    EXPECT_FALSE(f.mapOutputChannel(kPitchCh).isPolyGroupHead) << "Pitch is an input only";

    // Existing channels did not move.
    EXPECT_EQ(f.mapInputChannel(1).role, PortRole::ModCV);
    EXPECT_EQ(f.mapInputChannel(FilterModule::kRightBase).role, PortRole::Audio);
}

TEST(FilterKeyTrack, PolyPitchIsAnEightWideGroupOnOneJack) {
    FilterModule f;
    setBool(f, "poly", true);
    const auto head = f.mapInputChannel(kPitchCh);
    EXPECT_EQ(head.role, PortRole::Pitch);
    EXPECT_TRUE(head.isPolyGroupHead);
    EXPECT_EQ(head.polyVoiceSpan, 8);
    EXPECT_FALSE(f.mapInputChannel(kPitchCh + 3).isPolyGroupHead);
    EXPECT_EQ(f.mapInputChannel(kPitchCh + 3).visibleJackIndex, head.visibleJackIndex);
    EXPECT_EQ(f.mapInputChannel(kKeyTrackCVCh).visibleJackIndex, head.visibleJackIndex + 1);
    // One jack, one wire: the eight voices ride a single cable (the head's span), like every poly jack.
    for (int jack = 0; jack < f.getVisibleInputPortCount(); ++jack)
        EXPECT_EQ(f.getJackTargets(jack, true).size(), 1u) << "jack " << jack;
}

TEST(FilterKeyTrack, KeyTrackIsAModulationTargetInBothVoiceModes) {
    FilterModule f;
    for (const bool poly : {false, true}) {
        setBool(f, "poly", poly);
        const auto targets = f.getModulationTargets();
        ASSERT_EQ(targets.size(), 4u);
        EXPECT_EQ(targets[3].name, "Key Track");
        EXPECT_EQ(targets[3].channelIndex, kKeyTrackCVCh);
        ASSERT_NE(f.parameterForModTarget(targets[3]), nullptr);
        EXPECT_EQ(f.parameterForModTarget(targets[3])->paramID, "keyTrack");
    }
}
