// The mixer's per-project pan law (see docs/mixer/mixer.md#pan-law).
//
//   • ModuleBase::panGains / panGainsCompensated -- the two laws' raw gain values, in isolation
//   • ChannelStripModule -- a MONO strip's own "pan" picks up the project law off its playhead;
//     a STEREO strip's "pan" always stays the balance law regardless of the project setting
//   • Send pan on a MONO strip follows the same rule as the strip's own pan
//
// ChannelStripTests.cpp covers the balance law itself (default state, no playhead installed) and
// every other Channel Strip behaviour; this file is deliberately narrow to the law choice.

#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "Transport/TransportService.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr int kRight = ChannelStripModule::kRightBase;
constexpr float kTolerance = 1.0e-5f;

void setParam(juce::AudioProcessor& processor, const juce::String& id, float value) {
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (ranged->getParameterID() == id) {
                ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
                return;
            }
    FAIL() << "no parameter " << id;
}

juce::AudioBuffer<float> stripInput(float left, float right, int numSamples = kBlockSize) {
    juce::AudioBuffer<float> buffer(ChannelStripModule::kNumOutputs, numSamples);
    buffer.clear();
    for (int i = 0; i < numSamples; ++i) {
        buffer.setSample(0, i, left);
        buffer.setSample(kRight, i, right);
    }
    return buffer;
}

// Processes enough blocks for the gain/pan ramp to settle, then one more into `buffer`.
void settleAndProcess(juce::AudioProcessor& processor, juce::AudioBuffer<float>& buffer) {
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) { // 40 * 64 samples > the 20 ms ramp at 48 kHz
        auto scratch = buffer;
        processor.processBlock(scratch, midi);
    }
    processor.processBlock(buffer, midi);
}

void expectChannel(const juce::AudioBuffer<float>& buffer, int ch, float expected, const char* what) {
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        ASSERT_NEAR(buffer.getSample(ch, i), expected, kTolerance) << what << " (ch " << ch << ")";
}

} // namespace

// ============================================================================
// ModuleBase::panGains / panGainsCompensated -- raw gain values, both laws
// ============================================================================

TEST(MixerPanLawTest, BalanceLawGainsAtCentreHardLHardRAndMid) {
    float l = 0.0f, r = 0.0f;

    ModuleBase::panGains(0.0f, l, r);
    EXPECT_NEAR(l, 1.0f, kTolerance);
    EXPECT_NEAR(r, 1.0f, kTolerance);

    ModuleBase::panGains(-1.0f, l, r);
    EXPECT_NEAR(l, 1.0f, kTolerance);
    EXPECT_NEAR(r, 0.0f, kTolerance);

    ModuleBase::panGains(1.0f, l, r);
    EXPECT_NEAR(l, 0.0f, kTolerance);
    EXPECT_NEAR(r, 1.0f, kTolerance);

    ModuleBase::panGains(0.5f, l, r);
    EXPECT_NEAR(l, 0.5f, kTolerance);
    EXPECT_NEAR(r, 1.0f, kTolerance);
}

TEST(MixerPanLawTest, CompensatedLawGainsAtCentreHardLHardRAndMid) {
    float l = 0.0f, r = 0.0f;
    constexpr float kSqrt2 = 1.41421356f;

    // Centre: unity on both legs (within float cosine/sine rounding -- the whole point is
    // that this is NOT the near/far attenuation the balance law gives).
    ModuleBase::panGainsCompensated(0.0f, l, r);
    EXPECT_NEAR(l, 1.0f, kTolerance);
    EXPECT_NEAR(r, 1.0f, kTolerance);

    // Hard left: the near leg at sqrt(2) (+3 dB), the far leg at zero.
    ModuleBase::panGainsCompensated(-1.0f, l, r);
    EXPECT_NEAR(l, kSqrt2, kTolerance);
    EXPECT_NEAR(r, 0.0f, kTolerance);

    // Hard right: mirrored.
    ModuleBase::panGainsCompensated(1.0f, l, r);
    EXPECT_NEAR(l, 0.0f, kTolerance);
    EXPECT_NEAR(r, kSqrt2, kTolerance);

    // A mid value: constant-power means l*l + r*r == 2 (each leg is sqrt(2) times a
    // cos/sin, so unlike the balance law's l + r == 1, the invariant carries the sqrt(2) along),
    // and the far (right) leg is louder than centre, not just less-attenuated.
    ModuleBase::panGainsCompensated(0.5f, l, r);
    EXPECT_NEAR(l * l + r * r, 2.0f, 1.0e-4f) << "constant power at a mid pan value";
    EXPECT_GT(r, 1.0f) << "compensated law raises the far leg above unity as pan moves away from centre";
    EXPECT_LT(l, 1.0f);
}

// ============================================================================
// ChannelStripModule -- law choice depends on SHAPE, never on send-mono
// ============================================================================

TEST(MixerPanLawTest, MonoStripFollowsProjectPanLawFromTransport) {
    synth::TransportService transport;
    transport.setMixerPanLawCompensatedForBlock(true);

    ChannelStripModule strip;
    ASSERT_TRUE(strip.setShape(ChannelStripModule::Shape::Mono));
    strip.setPlayHead(&transport);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    setParam(strip, "pan", -1.0f);
    auto hardLeft = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, hardLeft);
    // Compensated hard-left: left leg raised to 0.5 * sqrt(2), right leg silenced.
    expectChannel(hardLeft, 0, 0.5f * 1.41421356f, "mono strip, compensated hard left, left leg");
    expectChannel(hardLeft, kRight, 0.0f, "mono strip, compensated hard left, right leg");
}

TEST(MixerPanLawTest, StereoStripStaysBalanceLawEvenWhenProjectIsCompensated) {
    synth::TransportService transport;
    transport.setMixerPanLawCompensatedForBlock(true);

    ChannelStripModule strip; // default shape is Stereo
    strip.setPlayHead(&transport);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    setParam(strip, "pan", -1.0f);
    auto hardLeft = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, hardLeft);
    // Balance law, unaffected by the project's Compensated setting: left leg stays at unity,
    // right leg goes silent -- never sqrt(2).
    expectChannel(hardLeft, 0, 0.5f, "stereo strip's pan stays a balance control, left leg");
    expectChannel(hardLeft, kRight, 0.0f, "stereo strip's pan stays a balance control, right leg");
}

TEST(MixerPanLawTest, NoTransportFallsBackToBalanceLawEvenOnAMonoStrip) {
    // A bare module with no playhead installed (a foreign host, or a headless unit test that
    // never wires one) must render with the plain balance law (panGains).
    ChannelStripModule strip;
    ASSERT_TRUE(strip.setShape(ChannelStripModule::Shape::Mono));
    strip.prepareToPlay(kSampleRate, kBlockSize);

    setParam(strip, "pan", -1.0f);
    auto hardLeft = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, hardLeft);
    expectChannel(hardLeft, 0, 0.5f, "no transport, balance law, left leg");
    expectChannel(hardLeft, kRight, 0.0f, "no transport, balance law, right leg");
}

// ============================================================================
// Send pan on a MONO strip
// ============================================================================

TEST(MixerPanLawTest, SendPanOnMonoStripFollowsTheProjectPanLawToo) {
    synth::TransportService transport;
    transport.setMixerPanLawCompensatedForBlock(true);

    ChannelStripModule strip;
    ASSERT_TRUE(strip.setShape(ChannelStripModule::Shape::Mono));
    strip.setPlayHead(&transport);
    ASSERT_EQ(strip.addSend(), 0);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    setParam(strip, "pan", 0.0f); // strip's own pan stays centred -- only the send is panned
    setParam(strip, "send1Pan", -1.0f);
    auto buffer = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, buffer);

    const int sendL = ChannelStripModule::sendLeftChannel(0);
    const int sendR = ChannelStripModule::sendRightChannel(0);
    // Mono strip -> both legs tapped equal at 0.5 (centred own-pan) -> send panned hard left under
    // the Compensated law: left leg raised to sqrt(2), right leg silenced.
    expectChannel(buffer, sendL, 0.5f * 1.41421356f, "send pan on mono strip, compensated hard left, L");
    expectChannel(buffer, sendR, 0.0f, "send pan on mono strip, compensated hard left, R");
}

TEST(MixerPanLawTest, SendPanOnStereoStripStaysBalanceLaw) {
    synth::TransportService transport;
    transport.setMixerPanLawCompensatedForBlock(true);

    ChannelStripModule strip; // default shape is Stereo
    strip.setPlayHead(&transport);
    ASSERT_EQ(strip.addSend(), 0);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    setParam(strip, "send1Pan", -1.0f);
    auto buffer = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, buffer);

    const int sendL = ChannelStripModule::sendLeftChannel(0);
    const int sendR = ChannelStripModule::sendRightChannel(0);
    expectChannel(buffer, sendL, 0.5f, "send pan on stereo strip stays balance, L");
    expectChannel(buffer, sendR, 0.0f, "send pan on stereo strip stays balance, R");
}
