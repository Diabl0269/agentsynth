// Module-level tests for the mixer's Channel Strip and Master nodes (P9-2, docs/mixer/mixer.md#node-types).
//
//   • pan law        -- balance law, unity at centre, for both Mono and Stereo strips
//   • gain           -- dB fader, the -60 dB floor is silence
//   • bypass / mute  -- the root CLAUDE.md two-branch contract: bypass is dry (no gain/pan, a mono
//                        strip still feeds both legs), mute clears
//   • shape          -- fixed once written or once live; a later width change is refused
//   • ports          -- kRightBase split-block jack map, no inherited Dual I/O toggle
//   • Master         -- Direct summed into Mix BEFORE the fader; bypass is a unity sum that keeps
//                        Direct; mute clears
//
// Solo gating (the render-time gate both modules read off the playhead) lives in
// MixerSoloTests.cpp.

#include "Mixer/PeakMeterLatch.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr int kRight = ChannelStripModule::kRightBase;

void setParam(juce::AudioProcessor& processor, const juce::String& id, float value) {
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (ranged->getParameterID() == id) {
                ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
                return;
            }
    FAIL() << "no parameter " << id;
}

// A strip-sized buffer with a constant on the left input and (optionally) the right input, junk on
// the reserved channels so the hygiene clear is exercised on every test.
juce::AudioBuffer<float> stripInput(float left, float right, int numSamples = kBlockSize) {
    juce::AudioBuffer<float> buffer(ChannelStripModule::kNumOutputs, numSamples);
    buffer.clear();
    for (int i = 0; i < numSamples; ++i) {
        buffer.setSample(0, i, left);
        buffer.setSample(kRight, i, right);
        for (int ch = 1; ch < kRight; ++ch)
            buffer.setSample(ch, i, 9.0f);
    }
    return buffer;
}

// Processes enough blocks for any gain/pan ramp to settle, then one more into `buffer`.
void settleAndProcess(juce::AudioProcessor& processor, juce::AudioBuffer<float>& buffer) {
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) { // 40 * 64 samples > the 20 ms ramp at 48 kHz
        auto scratch = buffer;
        processor.processBlock(scratch, midi);
    }
    // FRO146: the meter latch keeps the MAX peak since a reader's last read, deliberately --
    // that's the whole point (a real over during the ramp above must not be lost). Drain every
    // reader here so a caller's own takeMeterPeak() after this function sees only the ONE real
    // block below, not history from the warm-up ramp.
    if (auto* strip = dynamic_cast<ChannelStripModule*>(&processor)) {
        strip->takeMeterPeak(synth::MeterReader::Mixer, 0);
        strip->takeMeterPeak(synth::MeterReader::Mixer, 1);
        strip->takeMeterPeak(synth::MeterReader::TrackHeader, 0);
        strip->takeMeterPeak(synth::MeterReader::TrackHeader, 1);
    } else if (auto* master = dynamic_cast<MasterModule*>(&processor)) {
        master->takeMeterPeak(synth::MeterReader::Mixer, 0);
        master->takeMeterPeak(synth::MeterReader::Mixer, 1);
        master->takeMeterPeak(synth::MeterReader::TrackHeader, 0);
        master->takeMeterPeak(synth::MeterReader::TrackHeader, 1);
    }
    processor.processBlock(buffer, midi);
}

void expectChannel(const juce::AudioBuffer<float>& buffer, int ch, float expected, const char* what) {
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        ASSERT_NEAR(buffer.getSample(ch, i), expected, 1.0e-5f) << what << " (ch " << ch << ", sample " << i << ")";
}

juce::AudioBuffer<float> masterInput(float mixL, float mixR, float directL, float directR) {
    juce::AudioBuffer<float> buffer(MasterModule::kNumInputs, kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(MasterModule::kMixLeft, i, mixL);
        buffer.setSample(MasterModule::kMixRight, i, mixR);
        buffer.setSample(MasterModule::kDirectLeft, i, directL);
        buffer.setSample(MasterModule::kDirectRight, i, directR);
    }
    return buffer;
}

} // namespace

// ============================================================================
// Pan law + gain
// ============================================================================

TEST(ChannelStripTest, StereoCentrePanIsUnityOnBothLegs) {
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);
    auto buffer = stripInput(0.5f, -0.25f);
    settleAndProcess(strip, buffer);
    expectChannel(buffer, 0, 0.5f, "left leg at centre pan");
    expectChannel(buffer, kRight, -0.25f, "right leg at centre pan");
}

TEST(ChannelStripTest, MonoCentrePanFeedsBothLegsAtUnity) {
    ChannelStripModule strip;
    ASSERT_TRUE(strip.setShape(ChannelStripModule::Shape::Mono));
    strip.prepareToPlay(kSampleRate, kBlockSize);
    auto buffer = stripInput(0.5f, 0.0f);
    settleAndProcess(strip, buffer);
    expectChannel(buffer, 0, 0.5f, "mono strip, left leg");
    expectChannel(buffer, kRight, 0.5f, "mono strip, right leg carries the same input");
}

TEST(ChannelStripTest, PanIsABalanceLawThatOnlyAttenuatesTheFarLeg) {
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);

    setParam(strip, "pan", -1.0f);
    auto hardLeft = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, hardLeft);
    expectChannel(hardLeft, 0, 0.5f, "hard left keeps the left leg at unity");
    expectChannel(hardLeft, kRight, 0.0f, "hard left silences the right leg");

    setParam(strip, "pan", 0.5f);
    auto halfRight = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, halfRight);
    expectChannel(halfRight, 0, 0.25f, "half right halves the left leg");
    expectChannel(halfRight, kRight, 0.5f, "half right keeps the right leg at unity");
}

TEST(ChannelStripTest, GainIsInDecibelsAndTheFloorIsSilence) {
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);

    setParam(strip, "gain", 6.0f);
    auto boosted = stripInput(0.25f, 0.25f);
    settleAndProcess(strip, boosted);
    const float plus6 = juce::Decibels::decibelsToGain(6.0f);
    expectChannel(boosted, 0, 0.25f * plus6, "+6 dB");
    expectChannel(boosted, kRight, 0.25f * plus6, "+6 dB");

    setParam(strip, "gain", ChannelStripModule::kMinGainDb);
    auto floored = stripInput(0.25f, 0.25f);
    settleAndProcess(strip, floored);
    expectChannel(floored, 0, 0.0f, "the fader floor is silence, not -60 dB");
    expectChannel(floored, kRight, 0.0f, "the fader floor is silence, not -60 dB");
}

TEST(ChannelStripTest, ReservedChannelsAreClearedEveryBlock) {
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);
    auto buffer = stripInput(0.5f, 0.5f); // junk on ch1..3
    settleAndProcess(strip, buffer);
    for (int ch = 1; ch < kRight; ++ch)
        expectChannel(buffer, ch, 0.0f, "reserved channel");
}

// ============================================================================
// Bypass / mute — two separate branches
// ============================================================================

TEST(ChannelStripTest, BypassIsDryIgnoringGainAndPan) {
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);
    setParam(strip, "gain", -20.0f);
    setParam(strip, "pan", -1.0f);
    strip.setBypassed(true);

    auto buffer = stripInput(0.5f, -0.25f);
    settleAndProcess(strip, buffer);
    expectChannel(buffer, 0, 0.5f, "bypassed: left untouched");
    expectChannel(buffer, kRight, -0.25f, "bypassed: right untouched");
}

TEST(ChannelStripTest, BypassedMonoStripStillFeedsBothLegs) {
    ChannelStripModule strip;
    ASSERT_TRUE(strip.setShape(ChannelStripModule::Shape::Mono));
    strip.prepareToPlay(kSampleRate, kBlockSize);
    strip.setBypassed(true);

    auto buffer = stripInput(0.5f, 7.0f); // junk on the hidden right INPUT
    settleAndProcess(strip, buffer);
    expectChannel(buffer, 0, 0.5f, "bypassed mono strip, left");
    expectChannel(buffer, kRight, 0.5f, "bypassed mono strip, right is the input, not the hidden jack's junk");
}

TEST(ChannelStripTest, MuteClears) {
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);
    strip.setMuted(true);

    auto buffer = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, buffer);
    expectChannel(buffer, 0, 0.0f, "muted");
    expectChannel(buffer, kRight, 0.0f, "muted");
    EXPECT_EQ(strip.takeMeterPeak(synth::MeterReader::Mixer, 0), 0.0f);
}

TEST(ChannelStripTest, MeterReportsTheLastBlocksPostFaderPeakPerReader) {
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);
    setParam(strip, "pan", 1.0f);
    auto buffer = stripInput(0.5f, -0.75f);
    settleAndProcess(strip, buffer);
    EXPECT_NEAR(strip.takeMeterPeak(synth::MeterReader::Mixer, 0), 0.0f, 1.0e-6f) << "hard right silences the left leg";
    EXPECT_NEAR(strip.takeMeterPeak(synth::MeterReader::Mixer, 1), 0.75f, 1.0e-6f);
    // FRO146: consuming (read-and-reset) but ONLY of the reader's own slot -- the Mixer reader's
    // second read (with no new block in between) sees 0, silence, exactly as a real per-block
    // store would once the peak is gone; the TrackHeader reader, which has never read yet, still
    // sees the full peak the Mixer reader already consumed.
    EXPECT_NEAR(strip.takeMeterPeak(synth::MeterReader::Mixer, 1), 0.0f, 1.0e-6f)
        << "a reader's own second read, with no new block, sees the slot it just reset";
    EXPECT_NEAR(strip.takeMeterPeak(synth::MeterReader::TrackHeader, 1), 0.75f, 1.0e-6f)
        << "a DIFFERENT reader that hasn't read yet still sees the same latched peak";
}

TEST(ChannelStripTest, MeterMissedOversRegressionAHotBlockBetweenTwoQuietBlocksIsStillReported) {
    // FRO146: the bug the latch replaces. The old scheme stored exactly one float per leg,
    // overwritten every block -- a hot block sandwiched between two quiet ones was silently gone
    // by the time a 10 Hz UI poll got around to reading it.
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);
    juce::MidiBuffer midi;

    auto quiet1 = stripInput(0.01f, 0.01f);
    settleAndProcess(strip, quiet1);
    strip.takeMeterPeak(synth::MeterReader::Mixer, 0); // drain the settling blocks
    strip.takeMeterPeak(synth::MeterReader::Mixer, 1);

    auto hot = stripInput(0.9f, 0.9f);
    strip.processBlock(hot, midi);
    auto quiet2 = stripInput(0.01f, 0.01f);
    strip.processBlock(quiet2, midi);

    EXPECT_NEAR(strip.takeMeterPeak(synth::MeterReader::Mixer, 0), 0.9f, 1.0e-2f)
        << "the hot block's peak must still be reported at the NEXT read, not overwritten by the "
           "quiet block that followed it";
}

// ============================================================================
// Shape: fixed at creation
// ============================================================================

TEST(ChannelStripTest, ShapeIsFixedOnceWritten) {
    ChannelStripModule strip;
    EXPECT_EQ(strip.getShape(), ChannelStripModule::Shape::Stereo) << "default";
    ASSERT_TRUE(strip.setShape(ChannelStripModule::Shape::Mono));
    EXPECT_FALSE(strip.setShape(ChannelStripModule::Shape::Stereo)) << "a later width change is refused";
    EXPECT_EQ(strip.getShape(), ChannelStripModule::Shape::Mono);
    EXPECT_TRUE(strip.setShape(ChannelStripModule::Shape::Mono)) << "re-asserting the same shape is fine";

    // The trusted extra-state path obeys the same lock.
    auto* state = new juce::DynamicObject();
    state->setProperty("shape", "stereo");
    strip.setExtraState(juce::var(state));
    EXPECT_EQ(strip.getShape(), ChannelStripModule::Shape::Mono);
}

TEST(ChannelStripTest, ShapeLocksOnceTheStripIsLive) {
    ChannelStripModule strip; // never explicitly shaped: defaults to Stereo
    strip.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_FALSE(strip.setShape(ChannelStripModule::Shape::Mono)) << "a live strip is never widened or narrowed";
    EXPECT_EQ(strip.getShape(), ChannelStripModule::Shape::Stereo);
}

TEST(ChannelStripTest, ExtraStateRoundTripsShapeAndSolo) {
    ChannelStripModule original;
    ASSERT_TRUE(original.setShape(ChannelStripModule::Shape::Mono));
    original.setSoloed(true);

    ChannelStripModule restored;
    restored.setExtraState(original.getExtraState());
    EXPECT_EQ(restored.getShape(), ChannelStripModule::Shape::Mono);
    EXPECT_TRUE(restored.isSoloed());
    EXPECT_EQ(restored.getParameters().size(), original.getParameters().size()) << "solo must never become a parameter";
}

// FRO225 (docs/mixer/panel.md): the strip's own user-given name -- empty (unset) is the default, and
// it round-trips through getExtraState()/setExtraState() exactly like shape/solo above, so it
// survives save/load and presets.
TEST(ChannelStripTest, StripNameIsUnsetByDefault) {
    ChannelStripModule strip;
    EXPECT_TRUE(strip.getStripName().isEmpty());
}

TEST(ChannelStripTest, ExtraStateRoundTripsStripName) {
    ChannelStripModule original;
    original.setStripName("Lead Vox");

    ChannelStripModule restored;
    restored.setExtraState(original.getExtraState());
    EXPECT_EQ(restored.getStripName(), "Lead Vox");
}

TEST(ChannelStripTest, ExtraStateWithNoNamePropertyLeavesTheNameUnset) {
    // A patch saved before FRO225 (or any state object that never had a "name" key) must not throw
    // -- setExtraState only ever WRITES the field when the property is present (same
    // hasProperty()-gated pattern as every other extra-state key on this class).
    ChannelStripModule strip;
    auto* state = new juce::DynamicObject();
    state->setProperty("shape", "mono");
    strip.setExtraState(juce::var(state));
    EXPECT_TRUE(strip.getStripName().isEmpty());
}

// ============================================================================
// Ports
// ============================================================================

TEST(ChannelStripTest, JackMapFollowsTheShapeWithTheRightLegOnKRightBase) {
    ChannelStripModule stereo;
    EXPECT_FALSE(stereo.hasDualIOParameter()) << "the (5,5) shape inherits no Dual I/O toggle";
    EXPECT_EQ(stereo.rightAudioLegChannel(), kRight);
    EXPECT_EQ(stereo.getVisibleInputPortCount(), 2);
    EXPECT_EQ(stereo.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(stereo.mapInputChannel(kRight).visibleJackIndex, 1);
    EXPECT_TRUE(stereo.mapInputChannel(kRight).isPolyGroupHead);
    EXPECT_FALSE(stereo.mapInputChannel(1).isPolyGroupHead) << "ch1 is reserved, never a jack";
    EXPECT_EQ(stereo.getJackTargets(1, /*isInput=*/true).front().rawHeadChannel, kRight);
    EXPECT_EQ(stereo.getJackTargets(1, /*isInput=*/false).front().rawHeadChannel, kRight);

    ChannelStripModule mono;
    ASSERT_TRUE(mono.setShape(ChannelStripModule::Shape::Mono));
    EXPECT_EQ(mono.getVisibleInputPortCount(), 1);
    EXPECT_EQ(mono.getVisibleOutputPortCount(), 2) << "strip output is always stereo";
    EXPECT_FALSE(mono.mapInputChannel(kRight).isPolyGroupHead) << "a mono strip has no right input jack";
    EXPECT_EQ(mono.getJackTargets(1, /*isInput=*/false).front().rawHeadChannel, kRight);
}

// ============================================================================
// Master
// ============================================================================

TEST(MasterModuleTest, DirectIsSummedIntoMixBeforeTheFader) {
    MasterModule master;
    master.prepareToPlay(kSampleRate, kBlockSize);
    setParam(master, "gain", -6.0f);

    auto buffer = masterInput(0.2f, 0.1f, 0.3f, 0.05f);
    settleAndProcess(master, buffer);
    const float g = juce::Decibels::decibelsToGain(-6.0f);
    expectChannel(buffer, 0, (0.2f + 0.3f) * g, "left = (Mix + Direct) * gain");
    expectChannel(buffer, 1, (0.1f + 0.05f) * g, "right = (Mix + Direct) * gain");
    expectChannel(buffer, MasterModule::kDirectLeft, 0.0f, "Direct channels are cleared after the sum");
    expectChannel(buffer, MasterModule::kDirectRight, 0.0f, "Direct channels are cleared after the sum");
    EXPECT_NEAR(master.takeMeterPeak(synth::MeterReader::Mixer, 0), (0.2f + 0.3f) * g, 1.0e-5f);
}

TEST(MasterModuleTest, BypassIsAUnitySumThatKeepsDirect) {
    MasterModule master;
    master.prepareToPlay(kSampleRate, kBlockSize);
    setParam(master, "gain", -20.0f);
    master.setBypassed(true);

    auto buffer = masterInput(0.2f, 0.1f, 0.3f, 0.05f);
    settleAndProcess(master, buffer);
    expectChannel(buffer, 0, 0.5f, "bypassed: unity Mix + Direct, fader ignored");
    expectChannel(buffer, 1, 0.15f, "bypassed: unity Mix + Direct, fader ignored");
}

TEST(MasterModuleTest, MuteClears) {
    MasterModule master;
    master.prepareToPlay(kSampleRate, kBlockSize);
    master.setMuted(true);

    auto buffer = masterInput(0.2f, 0.1f, 0.3f, 0.05f);
    settleAndProcess(master, buffer);
    for (int ch = 0; ch < MasterModule::kNumInputs; ++ch)
        expectChannel(buffer, ch, 0.0f, "muted");
}

TEST(MasterModuleTest, FourLabelledInputJacksAndNoDualIOToggle) {
    MasterModule master;
    EXPECT_FALSE(master.hasDualIOParameter()) << "opted out: its inputs are two stereo blocks, not an FX pair";
    EXPECT_EQ(master.getVisibleInputPortCount(), 4);
    EXPECT_EQ(master.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(master.getInputPortLabel(MasterModule::kDirectLeft), "Direct L");
    EXPECT_EQ(master.getInputPortLabel(MasterModule::kMixRight), "Mix R");
    EXPECT_EQ(master.getJackTargets(MasterModule::kDirectRight, /*isInput=*/true).front().rawHeadChannel,
              MasterModule::kDirectRight);
}
