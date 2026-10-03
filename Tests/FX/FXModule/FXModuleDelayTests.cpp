// FXModuleDelayTests.cpp — Delay module coverage
#include "Modules/FX/DelayModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// ---------------------------------------------------------------------------
// DelayModule tests
// ---------------------------------------------------------------------------

class DelayModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<DelayModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<DelayModule> module;
};

TEST_F(DelayModuleTest, ProcessBlockPassesSignalThrough) {
    // DelayModule: 2 inputs, 2 outputs
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    // Fill both channels with signal
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.8f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // With mix=0.3 (default), output = wet*0.3 + dry*0.7
    // Dry portion alone should keep output non-zero
    float outSample = buffer.getSample(0, 0);
    EXPECT_NE(outSample, 0.0f);
}

TEST_F(DelayModuleTest, FeedbackParameterExists) {
    auto* feedbackParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "feedback"));
    ASSERT_NE(feedbackParam, nullptr);
    EXPECT_FLOAT_EQ(feedbackParam->get(), 0.5f); // default
}

TEST_F(DelayModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.1f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

// ---------------------------------------------------------------------------
// Tempo sync, ping-pong
// ---------------------------------------------------------------------------

namespace {

constexpr double kDelaySr = 44100.0;
constexpr int kDelayBlock = 512;

juce::RangedAudioParameter* delayParam(DelayModule& m, const juce::String& id) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
            return r;
    return nullptr;
}

void setDelayParam(DelayModule& m, const juce::String& id, float actual) {
    auto* p = delayParam(m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

struct FixedTempo : juce::AudioPlayHead {
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setBpm(bpm);
        return info;
    }
};

/** Pushes `settleBlocks` of silence (so the 50 ms time ramp has landed), then a single impulse of
    `impulse` on the left leg (and the right when `bothLegs`), and returns `length` samples of each
    output leg starting at the impulse. */
std::array<std::vector<float>, 2> impulseResponse(DelayModule& m, int length, float impulse = 0.8f,
                                                  bool bothLegs = false, int settleBlocks = 12) {
    juce::AudioBuffer<float> buf(5, kDelayBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < settleBlocks; ++b) {
        buf.clear();
        m.processBlock(buf, midi);
    }
    std::array<std::vector<float>, 2> out;
    for (int done = 0; done < length; done += kDelayBlock) {
        buf.clear();
        if (done == 0) {
            buf.setSample(0, 0, impulse);
            if (bothLegs)
                buf.setSample(1, 0, impulse);
        }
        m.processBlock(buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kDelayBlock && done + i < length; ++i)
                out[(size_t)ch].push_back(buf.getSample(ch, i));
    }
    return out;
}

int firstEcho(const std::vector<float>& leg, float threshold = 1.0e-3f) {
    for (size_t i = 1; i < leg.size(); ++i)
        if (std::abs(leg[i]) > threshold)
            return (int)i;
    return -1;
}

} // namespace

TEST(DelayTempoSync, ParametersDefaultToTheOldBehaviour) {
    DelayModule m;
    ASSERT_NE(delayParam(m, "tempoSync"), nullptr);
    ASSERT_NE(delayParam(m, "timeDiv"), nullptr);
    ASSERT_NE(delayParam(m, "pingPong"), nullptr);
    EXPECT_EQ(delayParam(m, "tempoSync")->getValue(), 0.0f);
    EXPECT_EQ(delayParam(m, "pingPong")->getValue(), 0.0f);
    EXPECT_EQ(static_cast<juce::AudioParameterChoice*>(delayParam(m, "timeDiv"))->getCurrentChoiceName(), "1/4");
}

TEST(DelayTempoSync, OutputLevelAndMuteStayLast) {
    DelayModule m;
    std::vector<juce::String> ids;
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
            ids.push_back(r->paramID);
    const auto pos = [&](const char* id) { return std::find(ids.begin(), ids.end(), juce::String(id)) - ids.begin(); };
    ASSERT_NE(delayParam(m, "pingPong"), nullptr);
    EXPECT_LT(pos("pingPong"), pos("outputLevel"));
    EXPECT_LT(pos("pingPong"), pos("muted"));
}

// Default Delay, no sync, no ping-pong: a plain 250 ms feedback delay. At 44.1 kHz that is exactly
// 11025 samples, so the echoes land on exact samples with exact gains.
TEST(DelayTempoSync, DefaultsRenderTheOldMillisecondDelayExactly) {
    DelayModule m;
    m.prepareToPlay(kDelaySr, kDelayBlock);
    const auto r = impulseResponse(m, 24000);
    EXPECT_NEAR(r[0][0], 0.8f * 0.7f, 1e-6f); // dry at mix 0.3
    EXPECT_NEAR(r[0][11025], 0.8f * 0.3f, 1e-6f);
    EXPECT_NEAR(r[0][22050], 0.8f * 0.5f * 0.3f, 1e-6f);
    EXPECT_NEAR(r[1][11025], 0.0f, 1e-9f) << "the right leg had no input";
    EXPECT_EQ(firstEcho(r[0]), 11025);
}

TEST(DelayTempoSync, StateWithoutTheNewParametersLoadsTheOldBehaviour) {
    juce::ValueTree state("ModuleState");    // saved before sync and ping-pong existed
    state.setProperty("mix", 0.3f, nullptr); // saved values are normalised 0..1
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), blob);

    DelayModule m;
    m.setStateInformation(blob.getData(), (int)blob.getSize());
    m.prepareToPlay(kDelaySr, kDelayBlock);
    ASSERT_NE(delayParam(m, "tempoSync"), nullptr);
    ASSERT_NE(delayParam(m, "pingPong"), nullptr);
    EXPECT_EQ(delayParam(m, "tempoSync")->getValue(), 0.0f);
    EXPECT_EQ(delayParam(m, "pingPong")->getValue(), 0.0f);
    EXPECT_EQ(firstEcho(impulseResponse(m, 12000)[0]), 11025);
}

TEST(DelayTempoSync, QuarterNoteAtOneHundredBpmIsSixHundredMilliseconds) {
    FixedTempo tempo;
    tempo.bpm = 100.0;
    DelayModule m;
    m.setPlayHead(&tempo);
    setDelayParam(m, "tempoSync", 1.0f);
    m.prepareToPlay(kDelaySr, kDelayBlock);
    const auto r = impulseResponse(m, 30000);
    EXPECT_EQ(firstEcho(r[0]), 26460) << "60/100 s = 600 ms = 26460 samples";
    EXPECT_NEAR(r[0][26460], 0.8f * 0.3f, 1e-6f);
}

TEST(DelayTempoSync, WithoutAHostTempoItAssumesOneHundredTwentyBpm) {
    DelayModule m;
    setDelayParam(m, "tempoSync", 1.0f);
    m.prepareToPlay(kDelaySr, kDelayBlock);
    EXPECT_EQ(firstEcho(impulseResponse(m, 23000)[0]), 22050) << "1/4 at 120 BPM is 500 ms";
}

TEST(DelayTempoSync, TheDivisionChoosesTheTime) {
    FixedTempo tempo; // 120 BPM
    DelayModule m;
    m.setPlayHead(&tempo);
    setDelayParam(m, "tempoSync", 1.0f);
    setDelayParam(m, "timeDiv", 4.0f); // 1/8
    m.prepareToPlay(kDelaySr, kDelayBlock);
    EXPECT_EQ(firstEcho(impulseResponse(m, 12000)[0]), 11025);
}

TEST(DelayTempoSync, ALongDivisionIsHeldToTheTimeKnobsRange) {
    DelayModule m; // 1/1 at 120 BPM is 2 s; the line (and the Time knob) top out at 1 s
    setDelayParam(m, "tempoSync", 1.0f);
    setDelayParam(m, "timeDiv", 7.0f); // 1/1
    m.prepareToPlay(kDelaySr, kDelayBlock);
    EXPECT_EQ(firstEcho(impulseResponse(m, 45000)[0]), 44100);
}

TEST(DelayTempoSync, TheTimeKnobAndItsCVAreIgnoredWhileSynced) {
    DelayModule m;
    setDelayParam(m, "tempoSync", 1.0f);
    setDelayParam(m, "time", 80.0f);
    m.prepareToPlay(kDelaySr, kDelayBlock);
    juce::AudioBuffer<float> buf(5, kDelayBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < 12; ++b) {
        buf.clear();
        for (int i = 0; i < kDelayBlock; ++i)
            buf.setSample(2, i, 0.3f); // Time CV
        m.processBlock(buf, midi);
    }
    EXPECT_EQ(firstEcho(impulseResponse(m, 23000)[0]), 22050);
}

TEST(DelayPingPong, OffKeepsEachLegOnItsOwnSide) {
    DelayModule m;
    setDelayParam(m, "mix", 1.0f);
    m.prepareToPlay(kDelaySr, kDelayBlock);
    const auto r = impulseResponse(m, 24000);
    EXPECT_GT(std::abs(r[0][11025]), 0.3f);
    EXPECT_GT(std::abs(r[0][22050]), 0.1f);
    for (float v : r[1])
        EXPECT_EQ(v, 0.0f) << "no bounce to the right leg";
}

TEST(DelayPingPong, TheFirstEchoIsLeftAndTheSecondIsRight) {
    DelayModule m;
    setDelayParam(m, "mix", 1.0f);
    setDelayParam(m, "pingPong", 1.0f);
    m.prepareToPlay(kDelaySr, kDelayBlock);
    const auto r = impulseResponse(m, 34000);
    // The mono sum of an input on Left only is half of it; the cross-feedback then halves each hop.
    EXPECT_NEAR(r[0][11025], 0.5f * 0.8f, 1e-5f) << "first echo on Left";
    EXPECT_NEAR(r[1][11025], 0.0f, 1e-6f);
    EXPECT_NEAR(r[1][22050], 0.5f * 0.8f * 0.5f, 1e-5f) << "second echo on Right";
    EXPECT_NEAR(r[0][22050], 0.0f, 1e-6f);
    EXPECT_NEAR(r[0][33075], 0.5f * 0.8f * 0.25f, 1e-5f) << "third echo back on Left";
    EXPECT_NEAR(r[1][33075], 0.0f, 1e-6f);
}

TEST(DelayPingPong, TheDryLegsKeepTheirImage) {
    DelayModule m;
    setDelayParam(m, "pingPong", 1.0f);
    m.prepareToPlay(kDelaySr, kDelayBlock);
    const auto r = impulseResponse(m, 600);
    EXPECT_NEAR(r[0][0], 0.8f * 0.7f, 1e-6f) << "dry Left at mix 0.3";
    EXPECT_EQ(r[1][0], 0.0f) << "dry Right stays silent";
}

TEST(DelayPingPong, ASummedMonoInputBouncesAtTheSameLevel) {
    DelayModule m;
    setDelayParam(m, "mix", 1.0f);
    setDelayParam(m, "pingPong", 1.0f);
    m.prepareToPlay(kDelaySr, kDelayBlock);
    const auto r = impulseResponse(m, 12000, 0.8f, /*bothLegs=*/true);
    EXPECT_NEAR(r[0][11025], 0.8f, 1e-5f) << "(L + R) / 2 of identical legs is the input itself";
}
