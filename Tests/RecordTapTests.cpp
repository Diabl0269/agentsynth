// RecordTapTests.cpp
//
// The "Rec Tap" module — a transparent stereo pass-through that copies what flows through it
// into a WAV plus a peaks sidecar, on a background thread.
//
// Two groups:
//   1. The module in isolation — pass-through, capture correctness, the .agpk format, overrun
//      behaviour, and the start/stop lifecycle. Driven directly (no engine, no device), the same
//      way MidiRecorderTests drives synth::MidiRecorder.
//   2. Registration — the internal-only checklist Track In established: absent from the library,
//      a pinned size estimate, a Utility cable colour. Gated #if SYNTH_ENABLE_TIMELINE, because the
//      factory entry is (the class itself always compiles).
//
//   3. The record-to-clip FLOW through MainComponent — arming an Audio track, the auto-spliced
//      master tap, the committed clip and its assetRef, and the MIDI path staying unaffected.
//      Gated too, for the same reason.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/RecordTapModule.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/CableColour.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "MainComponent.h"
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct ScopedTempFile {
    explicit ScopedTempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedTempFile() { file.deleteFile(); }

    juce::File file;
};

/** The take signal: a ramp that is exactly representable as a float (a power-of-two divisor), so a
 *  32-bit float WAV round-trips it BIT-EXACTLY and the assertions can use EXPECT_FLOAT_EQ's
 *  strictest form — plain equality. Channel 1 is the negation of channel 0, which makes a
 *  per-channel peak mix-up impossible to miss. */
float rampSample(juce::int64 globalFrame, int channel) {
    const float value = (float)globalFrame / 32768.0f;
    return channel == 0 ? value : -value;
}

void fillWithRamp(juce::AudioBuffer<float>& buffer, juce::int64 firstFrame) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.getWritePointer(channel)[i] = rampSample(firstFrame + i, channel);
}

/** Opens a take with JUCE's own WAV reader — the point is that some other tool can read what the
 *  module wrote, so nothing here shares code with the writer. */
struct WavContents {
    bool ok = false;
    double sampleRate = 0.0;
    int bitsPerSample = 0;
    int numChannels = 0;
    bool usesFloatingPointData = false;
    juce::int64 lengthInSamples = 0;
    juce::AudioBuffer<float> audio;
};

WavContents readWav(const juce::File& file) {
    WavContents out;
    if (!file.existsAsFile())
        return out;

    auto input = file.createInputStream();
    if (input == nullptr)
        return out;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(input.release(), /*deleteStreamIfOpeningFails=*/true));
    if (reader == nullptr)
        return out;

    out.sampleRate = reader->sampleRate;
    out.bitsPerSample = (int)reader->bitsPerSample;
    out.numChannels = (int)reader->numChannels;
    out.usesFloatingPointData = reader->usesFloatingPointData;
    out.lengthInSamples = reader->lengthInSamples;

    out.audio.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
    out.audio.clear();
    if (reader->lengthInSamples > 0)
        reader->read(&out.audio, 0, (int)reader->lengthInSamples, 0, true, true);
    out.ok = true;
    return out;
}

/** The .agpk sidecar, parsed straight from the bytes the header documents — deliberately NOT
 *  through any shared helper, so a change to the format has to be made in two places. */
struct PeaksContents {
    bool ok = false;
    juce::uint32 magic = 0;
    juce::uint32 version = 0;
    juce::uint32 bucketSize = 0;
    juce::uint32 numChannels = 0;
    std::vector<float> values; // per bucket, per channel: min, max
};

PeaksContents readPeaks(const juce::File& file) {
    PeaksContents out;
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block) || block.getSize() < 16)
        return out;

    const auto* bytes = static_cast<const juce::uint8*>(block.getData());
    const auto readU32 = [bytes](size_t offset) {
        return (juce::uint32)bytes[offset] | ((juce::uint32)bytes[offset + 1] << 8) |
               ((juce::uint32)bytes[offset + 2] << 16) | ((juce::uint32)bytes[offset + 3] << 24);
    };

    out.magic = readU32(0);
    out.version = readU32(4);
    out.bucketSize = readU32(8);
    out.numChannels = readU32(12);

    for (size_t offset = 16; offset + 4 <= block.getSize(); offset += 4) {
        const juce::uint32 bits = readU32(offset);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        out.values.push_back(value);
    }
    out.ok = true;
    return out;
}

} // namespace

// ============================================================================
// 1. Pass-through is unconditional
// ============================================================================

TEST(RecordTapTest, PassThroughAlways) {
    ScopedTempFile wav("agentsynth_rectap_passthrough.wav");
    ScopedTempFile peaks("agentsynth_rectap_passthrough.agpk");

    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::AudioBuffer<float> expected(2, kBlockSize);
    juce::MidiBuffer midi;

    // Disarmed.
    fillWithRamp(buffer, 0);
    expected.makeCopyOf(buffer);
    tap.processBlock(buffer, midi);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < kBlockSize; ++i)
            ASSERT_EQ(buffer.getReadPointer(channel)[i], expected.getReadPointer(channel)[i])
                << "disarmed pass-through altered ch" << channel << " sample " << i;

    // Armed: the tap copies the block out, it must not change it on the way past.
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));
    for (int block = 0; block < 4; ++block) {
        fillWithRamp(buffer, (juce::int64)block * kBlockSize);
        expected.makeCopyOf(buffer);
        tap.processBlock(buffer, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < kBlockSize; ++i)
                ASSERT_EQ(buffer.getReadPointer(channel)[i], expected.getReadPointer(channel)[i])
                    << "armed pass-through altered ch" << channel << " sample " << i;
    }
    tap.stopCapture();
}

// ============================================================================
// 2. The take is exactly what went in, and the peaks describe it
// ============================================================================

TEST(RecordTapTest, CaptureWritesExactWav) {
    ScopedTempFile wav("agentsynth_rectap_take.wav");
    ScopedTempFile peaks("agentsynth_rectap_take.agpk");

    constexpr int kBlocks = 8;
    constexpr juce::int64 kExpectedFrames = (juce::int64)kBlocks * kBlockSize; // 4096

    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kBlockSize);
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));
    EXPECT_TRUE(tap.isCapturing());

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < kBlocks; ++block) {
        fillWithRamp(buffer, (juce::int64)block * kBlockSize);
        tap.processBlock(buffer, midi);
    }

    const auto take = tap.stopCapture();
    EXPECT_FALSE(tap.isCapturing());
    ASSERT_TRUE(take.ok);
    EXPECT_FALSE(take.overran) << "a 4096-frame take must fit the default ring with room to spare";
    EXPECT_EQ(take.lengthSamples, kExpectedFrames);

    // ---- the WAV ----
    const auto contents = readWav(wav.file);
    ASSERT_TRUE(contents.ok) << "the take did not parse as a WAV";
    EXPECT_DOUBLE_EQ(contents.sampleRate, kSampleRate);
    EXPECT_EQ(contents.bitsPerSample, 32);
    EXPECT_TRUE(contents.usesFloatingPointData) << "32-bit must mean IEEE float, not 32-bit PCM";
    EXPECT_EQ(contents.numChannels, 2);
    ASSERT_EQ(contents.lengthInSamples, kExpectedFrames);

    for (int channel = 0; channel < 2; ++channel)
        for (juce::int64 frame = 0; frame < kExpectedFrames; ++frame)
            ASSERT_EQ(contents.audio.getReadPointer(channel)[frame], rampSample(frame, channel))
                << "float WAV round trip is not bit-exact at ch" << channel << " frame " << frame;

    // ---- the peaks sidecar ----
    const auto parsed = readPeaks(peaks.file);
    ASSERT_TRUE(parsed.ok) << "no peaks file was written";
    EXPECT_EQ(parsed.magic, RecordTapModule::kPeaksMagic);
    EXPECT_EQ(parsed.version, RecordTapModule::kPeaksVersion);
    EXPECT_EQ(parsed.bucketSize, (juce::uint32)RecordTapModule::kPeakBucketSize);
    EXPECT_EQ(parsed.numChannels, 2u);

    const juce::int64 expectedBuckets =
        (kExpectedFrames + RecordTapModule::kPeakBucketSize - 1) / RecordTapModule::kPeakBucketSize;
    ASSERT_EQ((juce::int64)parsed.values.size(), expectedBuckets * 2 /*channels*/ * 2 /*min,max*/);

    // The signal rises monotonically on ch0 and falls on ch1, so each bucket's extremes are its
    // first and last frame.
    for (juce::int64 bucket = 0; bucket < expectedBuckets; ++bucket) {
        const juce::int64 first = bucket * RecordTapModule::kPeakBucketSize;
        const juce::int64 last = std::min(first + RecordTapModule::kPeakBucketSize, kExpectedFrames) - 1;
        const size_t base = (size_t)bucket * 4;

        EXPECT_EQ(parsed.values[base + 0], rampSample(first, 0)) << "bucket " << bucket << " ch0 min";
        EXPECT_EQ(parsed.values[base + 1], rampSample(last, 0)) << "bucket " << bucket << " ch0 max";
        EXPECT_EQ(parsed.values[base + 2], rampSample(last, 1)) << "bucket " << bucket << " ch1 min";
        EXPECT_EQ(parsed.values[base + 3], rampSample(first, 1)) << "bucket " << bucket << " ch1 max";
    }
}

// A take whose length is not a whole number of buckets: the last bucket is SHORT, never padded, and
// covers only the frames that exist.
TEST(RecordTapTest, PartialFinalPeakBucketCoversOnlyTheFramesThatExist) {
    ScopedTempFile wav("agentsynth_rectap_partial.wav");
    ScopedTempFile peaks("agentsynth_rectap_partial.agpk");

    constexpr int kFrames = 300; // 1 full bucket of 256 + a 44-frame remainder

    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kFrames);
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));

    juce::AudioBuffer<float> buffer(2, kFrames);
    juce::MidiBuffer midi;
    fillWithRamp(buffer, 0);
    tap.processBlock(buffer, midi);

    const auto take = tap.stopCapture();
    ASSERT_TRUE(take.ok);
    EXPECT_EQ(take.lengthSamples, kFrames);

    const auto parsed = readPeaks(peaks.file);
    ASSERT_TRUE(parsed.ok);
    ASSERT_EQ(parsed.values.size(), 2u * 2u * 2u) << "expected exactly ceil(300/256) = 2 buckets";
    // The short bucket runs [256, 300).
    EXPECT_EQ(parsed.values[4], rampSample(256, 0));
    EXPECT_EQ(parsed.values[5], rampSample(299, 0));
}

// ============================================================================
// 3. Overrun: drop, flag, keep going
// ============================================================================

TEST(RecordTapTest, OverrunSetsFlagAndKeepsAudioThreadClean) {
    ScopedTempFile wav("agentsynth_rectap_overrun.wav");
    ScopedTempFile peaks("agentsynth_rectap_overrun.agpk");

    // A 64-frame ring against 512-frame blocks: the very first push cannot fit, whatever the writer
    // thread is doing, so the overrun is deterministic rather than a race the test hopes to win.
    RecordTapModule tap(64);
    tap.prepareToPlay(kSampleRate, kBlockSize);
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::AudioBuffer<float> expected(2, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 4; ++block) {
        fillWithRamp(buffer, (juce::int64)block * kBlockSize);
        expected.makeCopyOf(buffer);
        tap.processBlock(buffer, midi);
        // Dropping samples must never change the audio flowing through — the whole point of a tap.
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < kBlockSize; ++i)
                ASSERT_EQ(buffer.getReadPointer(channel)[i], expected.getReadPointer(channel)[i]);
    }
    EXPECT_TRUE(tap.hadOverrun());

    // Truncated, but finalised: the WAV still parses and matches what the counter reports.
    const auto take = tap.stopCapture();
    EXPECT_TRUE(take.ok);
    EXPECT_TRUE(take.overran);
    EXPECT_GT(take.lengthSamples, 0);
    EXPECT_LT(take.lengthSamples, (juce::int64)4 * kBlockSize) << "an overrun take must be SHORT";

    const auto contents = readWav(wav.file);
    ASSERT_TRUE(contents.ok);
    EXPECT_EQ(contents.lengthInSamples, take.lengthSamples);
    EXPECT_TRUE(readPeaks(peaks.file).ok);
}

// ============================================================================
// 4. Bypass
// ============================================================================

TEST(RecordTapTest, BypassPassesDryAndStopsCapturePushes) {
    ScopedTempFile wav("agentsynth_rectap_bypass.wav");
    ScopedTempFile peaks("agentsynth_rectap_bypass.agpk");

    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kBlockSize);
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::AudioBuffer<float> expected(2, kBlockSize);
    juce::MidiBuffer midi;

    tap.setBypassed(true);
    for (int block = 0; block < 4; ++block) {
        fillWithRamp(buffer, (juce::int64)block * kBlockSize);
        expected.makeCopyOf(buffer);
        tap.processBlock(buffer, midi);
        // Bypass on a module WITH a dry path passes the signal through — it does not clear.
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < kBlockSize; ++i)
                ASSERT_EQ(buffer.getReadPointer(channel)[i], expected.getReadPointer(channel)[i])
                    << "bypass must pass the dry signal, not mute it";
    }
    EXPECT_EQ(tap.getCapturedSamples(), 0) << "a bypassed tap is out of the chain: it records nothing";

    // Un-bypassing resumes capture on the very next block.
    tap.setBypassed(false);
    fillWithRamp(buffer, 0);
    tap.processBlock(buffer, midi);
    EXPECT_EQ(tap.getCapturedSamples(), (juce::int64)kBlockSize);

    const auto take = tap.stopCapture();
    EXPECT_TRUE(take.ok);
    EXPECT_EQ(take.lengthSamples, (juce::int64)kBlockSize);
}

// ============================================================================
// 4b. A take never spans a sample-rate change
// ============================================================================

TEST(RecordTapTest, PrepareAtANewRateStopsCaptureAtTheBoundary) {
    ScopedTempFile wav("agentsynth_rectap_ratechange.wav");
    ScopedTempFile peaks("agentsynth_rectap_ratechange.agpk");

    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kBlockSize);
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::AudioBuffer<float> expected(2, kBlockSize);
    juce::MidiBuffer midi;

    constexpr int kBlocksBeforeChange = 3;
    for (int block = 0; block < kBlocksBeforeChange; ++block) {
        fillWithRamp(buffer, (juce::int64)block * kBlockSize);
        tap.processBlock(buffer, midi);
    }
    // A re-prepare at the SAME rate is just a graph rebuild (a node added, a connection changed) and
    // must not touch the take.
    tap.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_FALSE(tap.wasCaptureHaltedByFormatChange());
    ASSERT_EQ(tap.getCapturedSamples(), (juce::int64)kBlocksBeforeChange * kBlockSize);

    // THE CHANGE: the device switches to 96 kHz mid-take. The WAV header already says 48 kHz.
    tap.prepareToPlay(96000.0, kBlockSize);
    EXPECT_TRUE(tap.wasCaptureHaltedByFormatChange());
    EXPECT_TRUE(tap.isCapturing()) << "the take is still the message thread's to commit — only the "
                                      "PUSH stops at the boundary";

    for (int block = kBlocksBeforeChange; block < kBlocksBeforeChange + 5; ++block) {
        fillWithRamp(buffer, (juce::int64)block * kBlockSize);
        expected.makeCopyOf(buffer);
        tap.processBlock(buffer, midi);
        // Halted or not, a tap is transparent: the audio flowing through is untouched.
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < kBlockSize; ++i)
                ASSERT_EQ(buffer.getReadPointer(channel)[i], expected.getReadPointer(channel)[i]);
    }
    EXPECT_EQ(tap.getCapturedSamples(), (juce::int64)kBlocksBeforeChange * kBlockSize)
        << "not one block rendered at the new rate may be pushed into a take armed at the old one";

    const auto take = tap.stopCapture();
    EXPECT_TRUE(take.ok) << "the ordinary commit path must still finalise the short take";
    EXPECT_EQ(take.lengthSamples, (juce::int64)kBlocksBeforeChange * kBlockSize);

    const auto contents = readWav(wav.file);
    ASSERT_TRUE(contents.ok);
    EXPECT_DOUBLE_EQ(contents.sampleRate, kSampleRate);
    EXPECT_EQ(contents.lengthInSamples, (juce::int64)kBlocksBeforeChange * kBlockSize);
    // Every frame in the file is a pre-change one, in order: the ramp continues past this point, so
    // a post-change block landing in the file would show up as a value that is out of range for it.
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < (int)contents.lengthInSamples; ++i)
            ASSERT_EQ(contents.audio.getReadPointer(channel)[i], rampSample(i, channel))
                << "channel " << channel << ", frame " << i;

    // Re-arming clears the latch: the next take is a fresh one at the new rate.
    ScopedTempFile wav2("agentsynth_rectap_ratechange2.wav");
    ScopedTempFile peaks2("agentsynth_rectap_ratechange2.agpk");
    ASSERT_TRUE(tap.startCapture(wav2.file, peaks2.file, 96000.0, 2));
    EXPECT_FALSE(tap.wasCaptureHaltedByFormatChange());
    fillWithRamp(buffer, 0);
    tap.processBlock(buffer, midi);
    EXPECT_EQ(tap.getCapturedSamples(), (juce::int64)kBlockSize);
    tap.stopCapture();
}

// ============================================================================
// 5. Lifecycle
// ============================================================================

TEST(RecordTapTest, StopWithoutStartIsSafe) {
    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kBlockSize);

    EXPECT_FALSE(tap.isCapturing());
    const auto first = tap.stopCapture();
    EXPECT_FALSE(first.ok);
    EXPECT_EQ(first.lengthSamples, 0);
    EXPECT_FALSE(first.overran);

    // Processing while disarmed is fine and records nothing.
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    fillWithRamp(buffer, 0);
    tap.processBlock(buffer, midi);
    EXPECT_EQ(tap.getCapturedSamples(), 0);

    // A second stop is just as inert.
    EXPECT_FALSE(tap.stopCapture().ok);
}

TEST(RecordTapTest, DoubleStartRejected) {
    ScopedTempFile wav("agentsynth_rectap_double.wav");
    ScopedTempFile peaks("agentsynth_rectap_double.agpk");
    ScopedTempFile otherWav("agentsynth_rectap_double_other.wav");
    ScopedTempFile otherPeaks("agentsynth_rectap_double_other.agpk");

    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kBlockSize);
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));

    // The second call must change nothing: two takes interleaved into one file is not a state this
    // module can be in.
    EXPECT_FALSE(tap.startCapture(otherWav.file, otherPeaks.file, kSampleRate, 2));

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    fillWithRamp(buffer, 0);
    tap.processBlock(buffer, midi);

    const auto take = tap.stopCapture();
    EXPECT_TRUE(take.ok);
    EXPECT_EQ(take.lengthSamples, (juce::int64)kBlockSize);
    EXPECT_TRUE(wav.file.existsAsFile());
    EXPECT_FALSE(otherWav.file.existsAsFile()) << "the rejected start must not have touched a file";

    // Rejected formats, none of which arms anything.
    RecordTapModule second;
    ScopedTempFile badWav("agentsynth_rectap_bad.wav");
    ScopedTempFile badPeaks("agentsynth_rectap_bad.agpk");
    EXPECT_FALSE(second.startCapture(badWav.file, badPeaks.file, 0.0, 2));
    EXPECT_FALSE(second.startCapture(badWav.file, badPeaks.file, kSampleRate, 0));
    EXPECT_FALSE(second.startCapture(badWav.file, badPeaks.file, kSampleRate, 3));
    EXPECT_FALSE(second.isCapturing());
}

// A take is finalised even if the module is destroyed with one still running — the destructor
// stops the capture rather than abandoning a half-written WAV.
TEST(RecordTapTest, DestructorFinalisesAnInFlightTake) {
    ScopedTempFile wav("agentsynth_rectap_dtor.wav");
    ScopedTempFile peaks("agentsynth_rectap_dtor.agpk");

    {
        RecordTapModule tap;
        tap.prepareToPlay(kSampleRate, kBlockSize);
        ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));

        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer midi;
        fillWithRamp(buffer, 0);
        tap.processBlock(buffer, midi);
    }

    const auto contents = readWav(wav.file);
    ASSERT_TRUE(contents.ok);
    EXPECT_EQ(contents.lengthInSamples, (juce::int64)kBlockSize);
    EXPECT_TRUE(readPeaks(peaks.file).ok);
}

// ============================================================================
// 6. Registration — the internal-only checklist Track In established
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

TEST(RecordTapTest, RegisteredButInternalOnly) {
    auto processor = synth::AIStateMapper::createModule("Rec Tap");
    ASSERT_NE(processor, nullptr) << "Rec Tap must be in the factory so a saved patch round-trips it";
    auto* tap = dynamic_cast<RecordTapModule*>(processor.get());
    ASSERT_NE(tap, nullptr);
    EXPECT_EQ(tap->getModuleType(), ModuleType::RecordTap);
    EXPECT_EQ(tap->getTotalNumInputChannels(), RecordTapModule::kNumChannels);
    EXPECT_EQ(tap->getTotalNumOutputChannels(), RecordTapModule::kNumChannels);
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(tap), "Rec Tap");

    // Never offered to a model. (The full golden lives in AIStateMapperTests.)
    EXPECT_FALSE(synth::AIStateMapper::authorableModuleTypes().contains("Rec Tap"));

    // Plumbing, like the device tap: Utility.
    EXPECT_EQ(synth::ui::categoryFor(ModuleType::RecordTap), synth::ui::ModuleCategory::Utility);
}

TEST(RecordTapTest, AbsentFromTheLibraryWithAPinnedSizeEstimate) {
    ModuleLibraryComponent library;
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Rec Tap"))
        << "Rec Tap is internal-only and must stay out of the module library";

    // The estimate is still queried programmatically (the record flow places the node), and a stale
    // one misplaces the card — the same assertion ModuleComponentTests makes for Track In.
    auto processor = synth::AIStateMapper::createModule("Rec Tap");
    ASSERT_NE(processor, nullptr);

    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

    const auto estimate = GraphEditor::estimateModuleSize("Rec Tap");
    EXPECT_EQ(estimate.x, comp.getWidth());
    EXPECT_EQ(estimate.y, comp.getHeight());
}

// ============================================================================
// 7. The record-to-clip flow (MainComponent)
// ============================================================================

namespace {

// Same minimal pattern as MainComponentTests.cpp's MockProvider.
class MockProviderRT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockRT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

RecordTapModule* findTapIn(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor()))
                return tap;
    return nullptr;
}

juce::AudioProcessorGraph::Node* findNodeNamed(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node;
    return nullptr;
}

int countTaps(juce::AudioProcessorGraph& graph) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node != nullptr && dynamic_cast<RecordTapModule*>(node->getProcessor()) != nullptr)
            ++count;
    return count;
}

} // namespace

class RecordFlowTest : public ::testing::Test {
protected:
    // The delegating MainComponent ctor reads/writes the shared on-disk "Agent Synth" settings, so
    // the keys this flow depends on are pinned before AND after every test — same hygiene as
    // TimelinePanelTests.cpp / TimelineTransportBarTests.cpp.
    void resetKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("timelinePanelVisible", "0");
            s->setValue("timelineCountInBars", 0); // no pre-roll: the punch is "now"
            s->saveIfNeeded();
        }
    }

    void SetUp() override {
        resetKeys();
        bundleDir =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_rectap_flow.agsproj");
        bundleDir.deleteRecursively();
    }
    void TearDown() override {
        resetKeys();
        bundleDir.deleteRecursively();
    }

    // Nothing here wants a real device clocking the graph: the transport is ticked by hand and the
    // tap is driven directly, exactly like TimelineTransportBarTests.cpp's own fixture.
    static void quiesceEngine(MainComponent& mc) { mc.getAudioEngine().suspendDeviceCallback(); }

    juce::File bundleDir;
};

TEST_F(RecordFlowTest, RecordFlowCreatesAudioClip) {
    MainComponent mc(std::make_unique<MockProviderRT>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& bar = mc.getTimelinePanel().getTransportBar();
    auto& transport = mc.getAudioEngine().getTransport();

    // A saved project, so the take lands inside the bundle rather than in app data.
    mc.saveProjectForTest(bundleDir);
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));

    // The default patch's master bus, before the splice.
    auto* outputNode = findNodeNamed(graph, "Audio Output");
    ASSERT_NE(outputNode, nullptr);
    auto* reverbNode = findNodeNamed(graph, "Reverb");
    ASSERT_NE(reverbNode, nullptr);
    ASSERT_TRUE(graph.isConnected({{reverbNode->nodeID, 0}, {outputNode->nodeID, 0}}));
    ASSERT_EQ(countTaps(graph), 0);

    // A clean stack, so "one undo step" below is unambiguous.
    mc.getUndoManager().clearUndoHistory();

    // ---- Record on ----
    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());

    // The tap exists, exactly once, spliced BETWEEN the reverb and the output.
    ASSERT_EQ(countTaps(graph), 1);
    auto* tap = findTapIn(graph);
    ASSERT_NE(tap, nullptr);
    auto* tapNode = findNodeNamed(graph, "Rec Tap");
    ASSERT_NE(tapNode, nullptr);
    for (int channel = 0; channel < RecordTapModule::kNumChannels; ++channel) {
        EXPECT_TRUE(graph.isConnected({{reverbNode->nodeID, channel}, {tapNode->nodeID, channel}}))
            << "the master feed must be re-routed into the tap on ch" << channel;
        EXPECT_TRUE(graph.isConnected({{tapNode->nodeID, channel}, {outputNode->nodeID, channel}}))
            << "the tap must feed the output on ch" << channel;
        EXPECT_FALSE(graph.isConnected({{reverbNode->nodeID, channel}, {outputNode->nodeID, channel}}))
            << "the original direct connection must be gone on ch" << channel;
    }
    // The splice is ONE undo step (a graph snapshot; the timeline is untouched until the commit).
    EXPECT_TRUE(mc.getUndoManager().canUndo());

    // ---- Roll ----
    // The capture is armed AT THE CLICK, not on a later poll tick — a poll could cost
    // a take up to ~100 ms of head. Where the take lands is decided at commit time instead, from the
    // tap's own sample anchor (see Tests/LatencyAlignmentTests.cpp for the placement arithmetic;
    // this tap is driven by hand, outside the graph, so it has no transport to anchor against and
    // the commit falls back to the punch — which is what the clip assertions below expect).
    EXPECT_TRUE(tap->isCapturing()) << "capture must start at Record-on";
    transport.tick(512); // drains the play() posted by "record implies roll"
    ASSERT_TRUE(transport.getPositionSnapshot().playing);
    mc.timerCallback();
    ASSERT_TRUE(tap->isCapturing());

    constexpr int kRolledBlocks = 8;
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < kRolledBlocks; ++block) {
        fillWithRamp(buffer, (juce::int64)block * kBlockSize);
        tap->processBlock(buffer, midi);
        transport.tick(kBlockSize);
    }

    const auto snapshot = transport.getPositionSnapshot();
    const double expectedLengthBeats =
        (double)(kRolledBlocks * kBlockSize) * snapshot.bpm / (60.0 * snapshot.sampleRate);

    // ---- Record off ----
    bar.getRecordButton().onClick();
    EXPECT_FALSE(bar.isRecordingForTest());
    EXPECT_FALSE(tap->isCapturing());

    ASSERT_NE(doc.getTrack(track), nullptr);
    ASSERT_EQ(doc.getTrack(track)->clips.size(), 1u) << "exactly one clip per take";
    const auto& clip = doc.getTrack(track)->clips[0];
    EXPECT_EQ(clip.assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(clip.startBeat, 0.0);
    EXPECT_NEAR(clip.lengthBeats, expectedLengthBeats, 1.0e-9);
    EXPECT_DOUBLE_EQ(clip.sourceStartSeconds, 0.0);

    const auto takeFile = bundleDir.getChildFile("Audio").getChildFile("take-1.wav");
    EXPECT_TRUE(takeFile.existsAsFile()) << "the take must live inside the bundle";
    EXPECT_TRUE(bundleDir.getChildFile("Peaks").getChildFile("take-1.agpk").existsAsFile());
    EXPECT_EQ(readWav(takeFile).lengthInSamples, (juce::int64)(kRolledBlocks * kBlockSize));

    // ---- A second take gets its own number, and re-uses the tap already spliced in ----
    transport.stop();
    transport.tick(kBlockSize);
    mc.timerCallback();

    bar.getRecordButton().onClick();
    EXPECT_EQ(countTaps(graph), 1) << "the master tap is found, not created a second time";
    transport.tick(kBlockSize);
    mc.timerCallback();
    ASSERT_TRUE(tap->isCapturing());
    fillWithRamp(buffer, 0);
    tap->processBlock(buffer, midi);
    transport.tick(kBlockSize);
    bar.getRecordButton().onClick();

    ASSERT_EQ(doc.getTrack(track)->clips.size(), 2u);
    EXPECT_EQ(doc.getTrack(track)->clips[1].assetRef, "Audio/take-2.wav");
    EXPECT_TRUE(bundleDir.getChildFile("Audio").getChildFile("take-2.wav").existsAsFile());

    // ---- Undo removes the clip; the FILE is never auto-deleted ----
    mc.simulateUndoClick();
    ASSERT_NE(doc.getTrack(track), nullptr);
    EXPECT_EQ(doc.getTrack(track)->clips.size(), 1u);
    EXPECT_TRUE(bundleDir.getChildFile("Audio").getChildFile("take-2.wav").existsAsFile())
        << "undo removes the clip, not the recording — a separate clean pass owns orphaned files";
}

// ============================================================================
// 8. The MIDI path is unaffected
// ============================================================================

TEST_F(RecordFlowTest, MidiArmedPathUnchanged) {
    MainComponent mc(std::make_unique<MockProviderRT>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& bar = mc.getTimelinePanel().getTransportBar();
    auto& transport = mc.getAudioEngine().getTransport();
    auto& recorder = mc.getMidiRecorderForTest();

    const auto track = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));

    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());
    EXPECT_TRUE(recorder.isRecording());
    EXPECT_EQ(countTaps(graph), 0) << "a MIDI take must not splice an audio tap into the patch";

    transport.tick(512);
    mc.timerCallback();
    EXPECT_EQ(countTaps(graph), 0) << "and the poll must not create one either";

    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 10);
        recorder.captureBlock(midi, transport.tick(512));
    }
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 20);
        recorder.captureBlock(midi, transport.tick(512));
    }

    bar.getRecordButton().onClick();
    EXPECT_FALSE(recorder.isRecording());
    ASSERT_EQ(doc.getTrack(track)->clips.size(), 1u);
    // A MIDI clip carries notes and NO asset — the audio fields stay at their defaults.
    EXPECT_FALSE(doc.getTrack(track)->clips[0].notes.empty());
    EXPECT_TRUE(doc.getTrack(track)->clips[0].assetRef.isEmpty());
}

// An Audio track armed in a patch with no master bus is refused, and refusing must have no side
// effects at all — no tap, no transport, no file.
TEST_F(RecordFlowTest, AudioRecordWithoutAnAudioOutputIsRefused) {
    MainComponent mc(std::make_unique<MockProviderRT>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& graph = mc.getAudioEngine().getGraph();
    if (auto* outputNode = findNodeNamed(graph, "Audio Output"))
        graph.removeNode(outputNode->nodeID);

    auto& doc = mc.getTimelineDoc();
    const auto track = doc.addTrack(synth::TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));

    auto& bar = mc.getTimelinePanel().getTransportBar();
    bar.getRecordButton().onClick();

    EXPECT_FALSE(bar.isRecordingForTest());
    EXPECT_EQ(countTaps(graph), 0);
    EXPECT_EQ(mc.getStatusBar().getTransientMessageForTest(), "Can't record audio: no Audio Output in the patch");

    mc.getAudioEngine().getTransport().tick(512);
    EXPECT_FALSE(mc.getAudioEngine().getTransport().getPositionSnapshot().playing)
        << "a rejected record request must not start the transport";
    EXPECT_TRUE(doc.getTrack(track)->clips.empty());
}

#endif // SYNTH_ENABLE_TIMELINE
