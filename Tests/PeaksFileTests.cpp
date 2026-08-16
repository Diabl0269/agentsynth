// synth::PeaksFile — the single place that knows the ".agpk" waveform-peaks sidecar format
// (RecordTapModule delegates to it — see RecordTapModule.h's class comment). Three groups:
//
//   1. Format round-trip and rejection — write()/read() agree on shape and values; a missing,
//      too-short, bad-magic/version, or structurally-truncated file is rejected outright.
//   2. Accumulator — the incremental bucket math (addSamples/flushPartial/reset), including the
//      short-final-bucket rule, exercised directly and independent of any writer thread.
//   3. RecordTapModule writes byte-identical files: a real capture's .agpk, read back through
//      PeaksFile::read(), matches the same ramp math Tests/RecordTapTests.cpp pins against the
//      raw bytes.

#include "../Source/Modules/RecordTapModule.h"
#include "../Source/Timeline/PeaksFile.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

using synth::PeaksFile;

namespace {

struct ScopedTempFile {
    explicit ScopedTempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedTempFile() { file.deleteFile(); }

    juce::File file;
};

// Same ramp convention as RecordTapTests.cpp — exactly representable as a float, so a captured
// take's peaks are bit-exact and EXPECT_FLOAT_EQ can be plain equality; channel 1 negates channel
// 0 so a per-channel mix-up can't hide.
float rampSample(juce::int64 globalFrame, int channel) {
    const float value = (float)globalFrame / 32768.0f;
    return channel == 0 ? value : -value;
}

void fillWithRamp(juce::AudioBuffer<float>& buffer, juce::int64 firstFrame) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.getWritePointer(channel)[i] = rampSample(firstFrame + i, channel);
}

PeaksFile::Data makeSyntheticData(int bucketSize, int numChannels, int bucketCount) {
    PeaksFile::Data data;
    data.bucketSize = bucketSize;
    data.numChannels = numChannels;
    for (int bucket = 0; bucket < bucketCount; ++bucket)
        for (int channel = 0; channel < numChannels; ++channel) {
            const float base = (float)(bucket * 10 + channel);
            data.buckets.emplace_back(-base - 1.0f, base + 1.0f); // distinct min/max per slot
        }
    return data;
}

} // namespace

// ============================================================================
// 1. Format round-trip and rejection
// ============================================================================

TEST(PeaksFileTest, RoundTripThroughPeaksFile) {
    ScopedTempFile file("agentsynth_peaksfile_roundtrip.agpk");

    const auto written = makeSyntheticData(256, 2, 5);
    ASSERT_TRUE(PeaksFile::write(file.file, written));
    ASSERT_TRUE(file.file.existsAsFile());

    PeaksFile::Data read;
    ASSERT_TRUE(PeaksFile::read(file.file, read));
    EXPECT_EQ(read.bucketSize, written.bucketSize);
    EXPECT_EQ(read.numChannels, written.numChannels);
    ASSERT_EQ(read.buckets.size(), written.buckets.size());
    for (size_t i = 0; i < written.buckets.size(); ++i) {
        EXPECT_FLOAT_EQ(read.buckets[i].first, written.buckets[i].first) << "bucket slot " << i << " min";
        EXPECT_FLOAT_EQ(read.buckets[i].second, written.buckets[i].second) << "bucket slot " << i << " max";
    }
}

TEST(PeaksFileTest, RoundTripThroughRealRecordTapCapture) {
    ScopedTempFile wav("agentsynth_peaksfile_via_rectap.wav");
    ScopedTempFile peaks("agentsynth_peaksfile_via_rectap.agpk");

    constexpr double kSampleRate = 48000.0;
    constexpr int kFrames = 600; // 2 buckets of 256 + a 88-frame remainder

    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kFrames);
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));

    juce::AudioBuffer<float> buffer(2, kFrames);
    juce::MidiBuffer midi;
    fillWithRamp(buffer, 0);
    tap.processBlock(buffer, midi);

    const auto take = tap.stopCapture();
    ASSERT_TRUE(take.ok);
    ASSERT_EQ(take.lengthSamples, kFrames);

    PeaksFile::Data data;
    ASSERT_TRUE(PeaksFile::read(peaks.file, data)) << "RecordTapModule's own sidecar must parse as PeaksFile";
    EXPECT_EQ(data.bucketSize, RecordTapModule::kPeakBucketSize);
    EXPECT_EQ(data.numChannels, 2);

    const int expectedBuckets =
        (int)((kFrames + RecordTapModule::kPeakBucketSize - 1) / RecordTapModule::kPeakBucketSize);
    ASSERT_EQ((int)(data.buckets.size() / 2), expectedBuckets);

    // The ramp rises monotonically on ch0 and falls on ch1, so each bucket's extremes are its
    // first and last frame — same assertion shape as RecordTapTests.cpp's own byte-level check.
    for (int bucket = 0; bucket < expectedBuckets; ++bucket) {
        const int first = bucket * RecordTapModule::kPeakBucketSize;
        const int last = std::min(first + RecordTapModule::kPeakBucketSize, kFrames) - 1;
        const auto& ch0 = data.buckets[(size_t)bucket * 2 + 0];
        const auto& ch1 = data.buckets[(size_t)bucket * 2 + 1];
        EXPECT_FLOAT_EQ(ch0.first, rampSample(first, 0)) << "bucket " << bucket << " ch0 min";
        EXPECT_FLOAT_EQ(ch0.second, rampSample(last, 0)) << "bucket " << bucket << " ch0 max";
        EXPECT_FLOAT_EQ(ch1.first, rampSample(last, 1)) << "bucket " << bucket << " ch1 min";
        EXPECT_FLOAT_EQ(ch1.second, rampSample(first, 1)) << "bucket " << bucket << " ch1 max";
    }

    // Re-writing what PeaksFile just read reproduces the same bytes (a real round trip through
    // the class, not just through RecordTapModule). Compared as raw bytes, not text — this is a
    // binary format and loadFileAsString() would mangle it.
    ScopedTempFile rewritten("agentsynth_peaksfile_via_rectap_rewritten.agpk");
    ASSERT_TRUE(PeaksFile::write(rewritten.file, data));
    juce::MemoryBlock originalBytes, rewrittenBytes;
    ASSERT_TRUE(peaks.file.loadFileAsData(originalBytes));
    ASSERT_TRUE(rewritten.file.loadFileAsData(rewrittenBytes));
    EXPECT_TRUE(rewrittenBytes == originalBytes) << "re-serialising a parsed Data must reproduce the original bytes";
}

TEST(PeaksFileTest, ReadRejectsMissingFile) {
    PeaksFile::Data data;
    EXPECT_FALSE(PeaksFile::read(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("agentsynth_peaksfile_does_not_exist.agpk"),
                                 data));
}

TEST(PeaksFileTest, ReadRejectsTooShortForAHeader) {
    ScopedTempFile file("agentsynth_peaksfile_tooshort.agpk");
    { // 8 bytes: half the 16-byte header.
        std::unique_ptr<juce::FileOutputStream> stream(file.file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        stream->writeInt64(0);
    }

    PeaksFile::Data data;
    EXPECT_FALSE(PeaksFile::read(file.file, data));
}

TEST(PeaksFileTest, ReadRejectsBadMagic) {
    ScopedTempFile file("agentsynth_peaksfile_badmagic.agpk");
    {
        std::unique_ptr<juce::FileOutputStream> stream(file.file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        stream->writeInt(0xdeadbeef); // wrong magic
        stream->writeInt((int)PeaksFile::kVersion);
        stream->writeInt(256);
        stream->writeInt(2);
    }

    PeaksFile::Data data;
    EXPECT_FALSE(PeaksFile::read(file.file, data));
}

TEST(PeaksFileTest, ReadRejectsUnsupportedVersion) {
    ScopedTempFile file("agentsynth_peaksfile_badversion.agpk");
    {
        std::unique_ptr<juce::FileOutputStream> stream(file.file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        stream->writeInt((int)PeaksFile::kMagic);
        stream->writeInt((int)PeaksFile::kVersion + 1); // a future/unknown version
        stream->writeInt(256);
        stream->writeInt(2);
    }

    PeaksFile::Data data;
    EXPECT_FALSE(PeaksFile::read(file.file, data));
}

TEST(PeaksFileTest, ReadRejectsZeroBucketSizeOrChannels) {
    ScopedTempFile zeroBucket("agentsynth_peaksfile_zerobucket.agpk");
    {
        std::unique_ptr<juce::FileOutputStream> stream(zeroBucket.file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        stream->writeInt((int)PeaksFile::kMagic);
        stream->writeInt((int)PeaksFile::kVersion);
        stream->writeInt(0); // bucketSize
        stream->writeInt(2);
    }
    PeaksFile::Data data;
    EXPECT_FALSE(PeaksFile::read(zeroBucket.file, data));

    ScopedTempFile zeroChannels("agentsynth_peaksfile_zerochannels.agpk");
    {
        std::unique_ptr<juce::FileOutputStream> stream(zeroChannels.file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        stream->writeInt((int)PeaksFile::kMagic);
        stream->writeInt((int)PeaksFile::kVersion);
        stream->writeInt(256);
        stream->writeInt(0); // numChannels
    }
    EXPECT_FALSE(PeaksFile::read(zeroChannels.file, data));
}

TEST(PeaksFileTest, ReadRejectsTruncatedPayload) {
    // A valid header followed by a byte count that is not a whole number of complete (all-channel)
    // buckets — garbage/truncated, per the class comment.
    ScopedTempFile file("agentsynth_peaksfile_truncated.agpk");
    {
        std::unique_ptr<juce::FileOutputStream> stream(file.file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        stream->writeInt((int)PeaksFile::kMagic);
        stream->writeInt((int)PeaksFile::kVersion);
        stream->writeInt(256);
        stream->writeInt(2); // numChannels == 2, so a bucket is 4 floats (16 bytes)
        stream->writeFloat(0.0f);
        stream->writeFloat(1.0f);
        stream->writeFloat(2.0f);      // only 3 floats: one channel's pair plus a lone float — not a
                                       // whole bucket, and not even a whole number of pairs' worth here
        stream->writeByte((char)0xAB); // and a stray trailing byte on top, for good measure
    }

    PeaksFile::Data data;
    EXPECT_FALSE(PeaksFile::read(file.file, data));
}

TEST(PeaksFileTest, ReadRejectsFileOverSizeCap) {
    // A file structurally VALID in every other respect (real magic/version, whole (min,max)
    // buckets of zeros) but one bucket over PeaksFile::kMaxFileBytes: read() must reject it on
    // size alone, before ever loading or parsing the payload. Grown via setPosition/truncate
    // rather than written byte-by-byte so the test stays fast (a sparse file on disk).
    ScopedTempFile file("agentsynth_peaksfile_oversize.agpk");
    {
        std::unique_ptr<juce::FileOutputStream> stream(file.file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        stream->writeInt((int)PeaksFile::kMagic);
        stream->writeInt((int)PeaksFile::kVersion);
        stream->writeInt(256);
        stream->writeInt(2); // numChannels == 2: a bucket is 16 bytes
        stream->flush();
    }
    {
        // kMaxFileBytes plus one extra whole bucket (16 bytes), which is itself a multiple of 16 —
        // so the only thing wrong with this file is its size.
        std::unique_ptr<juce::FileOutputStream> stream(file.file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        stream->setPosition(PeaksFile::kMaxFileBytes + 16);
        stream->truncate();
    }
    ASSERT_GT(file.file.getSize(), PeaksFile::kMaxFileBytes);

    PeaksFile::Data data;
    EXPECT_FALSE(PeaksFile::read(file.file, data));
}

TEST(PeaksFileTest, WriteRejectsInvalidShape) {
    ScopedTempFile file("agentsynth_peaksfile_write_invalid.agpk");
    PeaksFile::Data data;
    data.bucketSize = 0;
    data.numChannels = 2;
    EXPECT_FALSE(PeaksFile::write(file.file, data));
    EXPECT_FALSE(file.file.existsAsFile()) << "a rejected write must not create a partial file";

    data.bucketSize = 256;
    data.numChannels = 0;
    EXPECT_FALSE(PeaksFile::write(file.file, data));
    EXPECT_FALSE(file.file.existsAsFile());
}

TEST(PeaksFileTest, WriteOfEmptyBucketsProducesJustAHeader) {
    // A take of zero samples: header only, no buckets — same contract RecordTapModule documents.
    ScopedTempFile file("agentsynth_peaksfile_empty.agpk");
    PeaksFile::Data data;
    data.bucketSize = 256;
    data.numChannels = 2;
    ASSERT_TRUE(PeaksFile::write(file.file, data));

    PeaksFile::Data read;
    ASSERT_TRUE(PeaksFile::read(file.file, read));
    EXPECT_EQ(read.bucketSize, 256);
    EXPECT_EQ(read.numChannels, 2);
    EXPECT_TRUE(read.buckets.empty());
}

// ============================================================================
// 2. Accumulator
// ============================================================================

TEST(PeaksFileAccumulatorTest, ProducesBucketsMatchingRampMath) {
    constexpr int kBucketSize = 256;
    constexpr int kChannels = 2;
    constexpr int kFrames = 600; // 2 full buckets + a 88-frame remainder

    PeaksFile::Accumulator accumulator(kBucketSize, kChannels);
    juce::AudioBuffer<float> buffer(kChannels, kFrames);
    fillWithRamp(buffer, 0);
    accumulator.addSamples(buffer, kFrames);
    accumulator.flushPartial();

    const auto& data = accumulator.getData();
    const int expectedBuckets = (kFrames + kBucketSize - 1) / kBucketSize;
    ASSERT_EQ((int)(data.buckets.size() / kChannels), expectedBuckets);

    for (int bucket = 0; bucket < expectedBuckets; ++bucket) {
        const int first = bucket * kBucketSize;
        const int last = std::min(first + kBucketSize, kFrames) - 1;
        EXPECT_FLOAT_EQ(data.buckets[(size_t)bucket * 2 + 0].first, rampSample(first, 0));
        EXPECT_FLOAT_EQ(data.buckets[(size_t)bucket * 2 + 0].second, rampSample(last, 0));
        EXPECT_FLOAT_EQ(data.buckets[(size_t)bucket * 2 + 1].first, rampSample(last, 1));
        EXPECT_FLOAT_EQ(data.buckets[(size_t)bucket * 2 + 1].second, rampSample(first, 1));
    }
}

TEST(PeaksFileAccumulatorTest, FlushPartialIsANoOpWithNothingAccumulated) {
    PeaksFile::Accumulator accumulator(256, 2);
    accumulator.flushPartial();
    EXPECT_TRUE(accumulator.getData().buckets.empty());

    juce::AudioBuffer<float> buffer(2, 256);
    fillWithRamp(buffer, 0);
    accumulator.addSamples(buffer, 256);
    ASSERT_EQ(accumulator.getData().buckets.size(), 2u); // exactly one full bucket, auto-flushed

    accumulator.flushPartial(); // nothing new since the auto-flush above
    EXPECT_EQ(accumulator.getData().buckets.size(), 2u);
}

TEST(PeaksFileAccumulatorTest, ResetClearsBucketsAndInProgressFill) {
    PeaksFile::Accumulator accumulator(256, 1);
    juce::AudioBuffer<float> buffer(1, 100);
    fillWithRamp(buffer, 0);
    accumulator.addSamples(buffer, 100);
    accumulator.flushPartial();
    ASSERT_FALSE(accumulator.getData().buckets.empty());

    accumulator.reset();
    EXPECT_TRUE(accumulator.getData().buckets.empty());
    EXPECT_EQ(accumulator.getData().bucketSize, 256) << "reset() keeps the same shape";
    EXPECT_EQ(accumulator.getData().numChannels, 1);

    // Accumulating fresh data after reset() starts a clean bucket, not one polluted by the
    // pre-reset partial fill.
    juce::AudioBuffer<float> flat(1, 50);
    for (int i = 0; i < 50; ++i)
        flat.getWritePointer(0)[i] = 0.5f;
    accumulator.addSamples(flat, 50);
    accumulator.flushPartial();
    ASSERT_EQ(accumulator.getData().buckets.size(), 1u);
    EXPECT_FLOAT_EQ(accumulator.getData().buckets[0].first, 0.5f);
    EXPECT_FLOAT_EQ(accumulator.getData().buckets[0].second, 0.5f);
}

// ============================================================================
// 3. RecordTapModule's refactor didn't change the bytes it writes
// ============================================================================

TEST(PeaksFileTest, RecordTapStillWritesIdenticalFormat) {
    ScopedTempFile wav("agentsynth_peaksfile_identical.wav");
    ScopedTempFile peaks("agentsynth_peaksfile_identical.agpk");

    constexpr double kSampleRate = 48000.0;
    constexpr int kBlockSize = 512;
    constexpr int kBlocks = 8;
    constexpr juce::int64 kExpectedFrames = (juce::int64)kBlocks * kBlockSize;

    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, kBlockSize);
    ASSERT_TRUE(tap.startCapture(wav.file, peaks.file, kSampleRate, 2));

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < kBlocks; ++block) {
        fillWithRamp(buffer, (juce::int64)block * kBlockSize);
        tap.processBlock(buffer, midi);
    }

    const auto take = tap.stopCapture();
    ASSERT_TRUE(take.ok);
    ASSERT_EQ(take.lengthSamples, kExpectedFrames);

    // Byte-for-byte reproduction, independent of PeaksFile's own read() — the same raw parse
    // RecordTapTests.cpp's readPeaks() does — pinned here too so a regression in EITHER the
    // module's use of PeaksFile or PeaksFile::write() itself fails this test.
    juce::MemoryBlock block;
    ASSERT_TRUE(peaks.file.loadFileAsData(block));
    ASSERT_GE(block.getSize(), 16u);
    const auto* bytes = static_cast<const juce::uint8*>(block.getData());
    const auto readU32 = [bytes](size_t offset) {
        return (juce::uint32)bytes[offset] | ((juce::uint32)bytes[offset + 1] << 8) |
               ((juce::uint32)bytes[offset + 2] << 16) | ((juce::uint32)bytes[offset + 3] << 24);
    };
    EXPECT_EQ(readU32(0), RecordTapModule::kPeaksMagic);
    EXPECT_EQ(readU32(4), RecordTapModule::kPeaksVersion);
    EXPECT_EQ(readU32(8), (juce::uint32)RecordTapModule::kPeakBucketSize);
    EXPECT_EQ(readU32(12), 2u);

    const juce::int64 expectedBuckets =
        (kExpectedFrames + RecordTapModule::kPeakBucketSize - 1) / RecordTapModule::kPeakBucketSize;
    EXPECT_EQ((juce::int64)block.getSize(), 16 + expectedBuckets * 2 /*channels*/ * 2 /*min,max*/ * 4 /*bytes*/);

    // And PeaksFile::read() agrees with that same raw parse.
    PeaksFile::Data data;
    ASSERT_TRUE(PeaksFile::read(peaks.file, data));
    EXPECT_EQ(data.bucketSize, (int)RecordTapModule::kPeakBucketSize);
    EXPECT_EQ(data.numChannels, 2);
    EXPECT_EQ((juce::int64)(data.buckets.size() / 2), expectedBuckets);
}
