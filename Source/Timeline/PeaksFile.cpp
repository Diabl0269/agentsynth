#include "PeaksFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace synth {

namespace {

constexpr size_t kHeaderBytes = 16;

// Version 2 adds `rms` per bucket slot (root-mean-square of the bucket's samples: a sine of amplitude A reads
// A/sqrt(2), a square wave A), drawn as the darker body inside the min/max outline. Version 1 has none: it reads fine,
// `Data::rms` stays empty and the waveform draws as a plain outline. write() emits version 2 when `Data::rms` has one
// value per slot, else version 1.

// Little-endian codecs, matching RecordTapModule's original ones bit-for-bit (see PeaksFile.h's
// class comment — the format is unchanged, only where the code lives has moved).
void writeLittleEndianUInt32(juce::OutputStream& stream, std::uint32_t value) {
    stream.writeByte((char)(value & 0xffu));
    stream.writeByte((char)((value >> 8) & 0xffu));
    stream.writeByte((char)((value >> 16) & 0xffu));
    stream.writeByte((char)((value >> 24) & 0xffu));
}

void writeLittleEndianFloat(juce::OutputStream& stream, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float is not 32 bits");
    std::memcpy(&bits, &value, sizeof(bits));
    writeLittleEndianUInt32(stream, bits);
}

std::uint32_t readLittleEndianUInt32(const juce::uint8* bytes) {
    return (std::uint32_t)bytes[0] | ((std::uint32_t)bytes[1] << 8) | ((std::uint32_t)bytes[2] << 16) |
           ((std::uint32_t)bytes[3] << 24);
}

float readLittleEndianFloat(const juce::uint8* bytes) {
    const std::uint32_t bits = readLittleEndianUInt32(bytes);
    float value = 0.0f;
    static_assert(sizeof(bits) == sizeof(value), "float is not 32 bits");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The buckets [first, last] column `x` of `numColumns` covers out of `count`, partial edge buckets included.
std::pair<std::int64_t, std::int64_t> columnSpan(std::int64_t count, std::int64_t numColumns, std::int64_t x) {
    const std::int64_t first = x * count / numColumns;
    return {first, std::max(first, ((x + 1) * count + numColumns - 1) / numColumns - 1)};
}

// Folds one bucket's channels into a single (min,max): min of mins, max of maxes (the same fold aggregateToColumns
// uses).
std::pair<float, float> foldBucket(const std::vector<std::pair<float, float>>& buckets, int numChannels,
                                   std::int64_t bucketIndex) {
    float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
    for (int ch = 0; ch < numChannels; ++ch) {
        const auto& pair = buckets[(std::size_t)(bucketIndex * numChannels + ch)];
        lo = std::min(lo, pair.first);
        hi = std::max(hi, pair.second);
    }
    return {lo, hi};
}

// Where column `x` of `numColumns` sits among `count` buckets (count < numColumns): between the anchor columns of two
// neighbouring buckets, with the fraction `t` from the first to the second. Bucket b is anchored at the column nearest
// its centre, (b + 0.5) * numColumns / count - 0.5, so that column carries the bucket's own min/max exactly and no
// loud bucket is ever drawn lower than it is; columns before the first anchor and after the last hold the end value.
struct BucketBlend {
    std::int64_t i0, i1;
    float t;
};
std::int64_t anchorColumn(std::int64_t count, std::int64_t numColumns, std::int64_t bucket) {
    const double centre = ((double)bucket + 0.5) * (double)numColumns / (double)count - 0.5;
    return std::clamp<std::int64_t>((std::int64_t)std::llround(centre), 0, numColumns - 1);
}
// `next` is the bucket whose anchor is the first one past column `x` (advanced by the caller, columns ascending).
BucketBlend blendFor(std::int64_t count, std::int64_t numColumns, std::int64_t x, std::int64_t& next) {
    while (next < count && anchorColumn(count, numColumns, next) <= x)
        ++next;
    if (next == 0)
        return {0, 0, 0.0f};
    if (next >= count)
        return {count - 1, count - 1, 0.0f};
    const std::int64_t a0 = anchorColumn(count, numColumns, next - 1);
    const std::int64_t a1 = anchorColumn(count, numColumns, next);
    return {next - 1, next, (float)(x - a0) / (float)(a1 - a0)};
}

float blendValues(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

//==============================================================================
bool PeaksFile::writeForAudioFile(const juce::File& audioFile, const juce::File& peaksFile, int bucketSize) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(audioFile));
    if (reader == nullptr || reader->numChannels == 0)
        return false;

    constexpr int kChunkFrames = 16384;
    const int channels = (int)reader->numChannels;
    Accumulator accumulator(bucketSize, channels);
    juce::AudioBuffer<float> chunk(channels, kChunkFrames);
    for (juce::int64 position = 0; position < reader->lengthInSamples; position += kChunkFrames) {
        const int frames = (int)juce::jmin<juce::int64>(kChunkFrames, reader->lengthInSamples - position);
        if (!reader->read(&chunk, 0, frames, position, true, true))
            return false;
        accumulator.addSamples(chunk, frames);
    }
    accumulator.flushPartial();
    return write(peaksFile, accumulator.getData());
}

bool PeaksFile::read(const juce::File& file, Data& out) {
    if (!file.existsAsFile())
        return false;
    // Checked against the file's size on disk, before a single byte is loaded: an oversized
    // sidecar is rejected without ever allocating a buffer for it.
    if (file.getSize() > kMaxFileBytes)
        return false;

    juce::MemoryBlock block;
    if (!file.loadFileAsData(block))
        return false;
    if (block.getSize() < kHeaderBytes)
        return false;

    const auto* bytes = static_cast<const juce::uint8*>(block.getData());
    const std::uint32_t magic = readLittleEndianUInt32(bytes + 0);
    const std::uint32_t version = readLittleEndianUInt32(bytes + 4);
    const std::uint32_t bucketSize = readLittleEndianUInt32(bytes + 8);
    const std::uint32_t numChannels = readLittleEndianUInt32(bytes + 12);

    if (magic != kMagic || (version != kVersion && version != kVersionNoRms))
        return false;
    const bool hasRms = version == kVersion;
    const size_t slotBytes = hasRms ? 12 : 8;
    if (bucketSize == 0 || numChannels == 0)
        return false;

    const size_t payloadBytes = block.getSize() - kHeaderBytes;
    if (payloadBytes % slotBytes != 0)
        return false; // not a whole number of (min,max[,rms]) slots: garbage/truncated
    const size_t pairCount = payloadBytes / slotBytes;
    if (pairCount % numChannels != 0)
        return false; // not a whole number of complete (all-channel) buckets

    Data result;
    result.bucketSize = (int)bucketSize;
    result.numChannels = (int)numChannels;
    result.buckets.reserve(pairCount);
    if (hasRms)
        result.rms.reserve(pairCount);
    for (size_t i = 0; i < pairCount; ++i) {
        const size_t offset = kHeaderBytes + i * slotBytes;
        const float minValue = readLittleEndianFloat(bytes + offset);
        const float maxValue = readLittleEndianFloat(bytes + offset + 4);
        result.buckets.emplace_back(minValue, maxValue);
        if (hasRms)
            result.rms.push_back(readLittleEndianFloat(bytes + offset + 8));
    }

    out = std::move(result);
    return true;
}

bool PeaksFile::write(const juce::File& file, const Data& data) {
    if (data.bucketSize <= 0 || data.numChannels <= 0)
        return false;

    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    stream->setPosition(0);
    stream->truncate();

    writeLittleEndianUInt32(*stream, kMagic);
    const bool withRms = data.rms.size() == data.buckets.size();
    writeLittleEndianUInt32(*stream, withRms ? kVersion : kVersionNoRms);
    writeLittleEndianUInt32(*stream, (std::uint32_t)data.bucketSize);
    writeLittleEndianUInt32(*stream, (std::uint32_t)data.numChannels);
    for (std::size_t i = 0; i < data.buckets.size(); ++i) {
        writeLittleEndianFloat(*stream, data.buckets[i].first);
        writeLittleEndianFloat(*stream, data.buckets[i].second);
        if (withRms)
            writeLittleEndianFloat(*stream, data.rms[i]);
    }

    return stream->getStatus().wasOk();
}

//==============================================================================
PeaksFile::Accumulator::Accumulator(int bucketSize, int numChannels)
    : bucketSize_(juce::jmax(1, bucketSize))
    , numChannels_(juce::jmax(1, numChannels))
    , bucketMin_((size_t)numChannels_, std::numeric_limits<float>::max())
    , bucketMax_((size_t)numChannels_, std::numeric_limits<float>::lowest())
    , bucketSumSquares_((size_t)numChannels_, 0.0) {
    data_.bucketSize = bucketSize_;
    data_.numChannels = numChannels_;
}

void PeaksFile::Accumulator::reset() {
    data_.buckets.clear();
    data_.rms.clear();
    bucketFill_ = 0;
    std::fill(bucketSumSquares_.begin(), bucketSumSquares_.end(), 0.0);
    std::fill(bucketMin_.begin(), bucketMin_.end(), std::numeric_limits<float>::max());
    std::fill(bucketMax_.begin(), bucketMax_.end(), std::numeric_limits<float>::lowest());
}

void PeaksFile::Accumulator::addSamples(const juce::AudioBuffer<float>& chunk, int numFrames) {
    const int channels = juce::jmin(numChannels_, chunk.getNumChannels());

    for (int frame = 0; frame < numFrames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const float sample = chunk.getReadPointer(channel)[frame];
            bucketMin_[(size_t)channel] = std::min(bucketMin_[(size_t)channel], sample);
            bucketMax_[(size_t)channel] = std::max(bucketMax_[(size_t)channel], sample);
            bucketSumSquares_[(size_t)channel] += (double)sample * (double)sample;
        }
        if (++bucketFill_ >= bucketSize_)
            flushPartial();
    }
}

void PeaksFile::Accumulator::flushPartial() {
    if (bucketFill_ <= 0)
        return; // nothing accumulated: never emit an empty bucket

    for (int channel = 0; channel < numChannels_; ++channel) {
        data_.buckets.emplace_back(bucketMin_[(size_t)channel], bucketMax_[(size_t)channel]);
        data_.rms.push_back((float)std::sqrt(bucketSumSquares_[(size_t)channel] / (double)bucketFill_));
        bucketSumSquares_[(size_t)channel] = 0.0;
        bucketMin_[(size_t)channel] = std::numeric_limits<float>::max();
        bucketMax_[(size_t)channel] = std::numeric_limits<float>::lowest();
    }
    bucketFill_ = 0;
}

// Reduces `bucketCount` buckets (starting at bucket `firstBucket`, all channels folded: min of mins, max of
// maxes) to exactly `numColumns` (min,max) pairs for drawing. Column `c` covers the span
// `[c * bucketCount / numColumns, (c + 1) * bucketCount / numColumns)` and aggregates EVERY bucket that span
// touches, partial edge buckets included, so no bucket's extreme is ever skipped at any zoom. With fewer buckets
// than columns a bucket repeats across the columns it covers. The count is clamped to the buckets that exist;
// returns an empty vector when there is nothing to reduce. Pure: the one downsampler every peaks painter uses.
std::vector<std::pair<float, float>> PeaksFile::aggregateToColumns(const std::vector<std::pair<float, float>>& buckets,
                                                                   int numChannels, int firstBucket, int bucketCount,
                                                                   int numColumns) {
    std::vector<std::pair<float, float>> columns;
    if (numChannels <= 0 || numColumns <= 0 || firstBucket < 0)
        return columns;
    const std::int64_t available = (std::int64_t)(buckets.size() / (std::size_t)numChannels) - firstBucket;
    const std::int64_t count = std::min<std::int64_t>(bucketCount, available);
    if (count <= 0)
        return columns;

    columns.reserve((std::size_t)numColumns);
    for (std::int64_t x = 0; x < numColumns; ++x) {
        const auto [first, last] = columnSpan(count, numColumns, x);
        float lo = 0.0f, hi = 0.0f;
        bool any = false;
        for (std::int64_t b = first; b <= last && b < count; ++b) {
            for (int ch = 0; ch < numChannels; ++ch) {
                const auto& pair = buckets[(std::size_t)((firstBucket + b) * numChannels + ch)];
                lo = any ? std::min(lo, pair.first) : pair.first;
                hi = any ? std::max(hi, pair.second) : pair.second;
                any = true;
            }
        }
        columns.emplace_back(lo, hi);
    }
    return columns;
}

// The RMS twin of aggregateToColumns: column `c` is the root-mean-square of the per-bucket RMS values (all channels) of
// every bucket its span touches, so a loud bucket raises the body and a quiet stretch lowers it. Empty `rms` -> empty.
std::vector<float> PeaksFile::aggregateRmsToColumns(const std::vector<float>& rms, int numChannels, int firstBucket,
                                                    int bucketCount, int numColumns) {
    std::vector<float> columns;
    if (rms.empty() || numChannels <= 0 || numColumns <= 0 || firstBucket < 0)
        return columns;
    const std::int64_t available = (std::int64_t)(rms.size() / (std::size_t)numChannels) - firstBucket;
    const std::int64_t count = std::min<std::int64_t>(bucketCount, available);
    if (count <= 0)
        return columns;

    columns.reserve((std::size_t)numColumns);
    for (std::int64_t x = 0; x < numColumns; ++x) {
        const auto [first, last] = columnSpan(count, numColumns, x);
        double sumSquares = 0.0;
        std::int64_t n = 0;
        for (std::int64_t b = first; b <= last && b < count; ++b)
            for (int ch = 0; ch < numChannels; ++ch, ++n) {
                const double v = rms[(std::size_t)((firstBucket + b) * numChannels + ch)];
                sumSquares += v * v;
            }
        columns.push_back(n > 0 ? (float)std::sqrt(sumSquares / (double)n) : 0.0f);
    }
    return columns;
}

// With at least as many buckets as columns this IS aggregateToColumns. With fewer (zoomed in past one bucket per
// pixel) a column is the straight line between the two bucket centres around it, so a changing wave reads as a
// continuous outline rather than one flat step per bucket (the column nearest each bucket centre keeps that bucket's
// exact min/max, so no peak is lowered). Channels fold per bucket first.
std::vector<std::pair<float, float>> PeaksFile::smoothToColumns(const std::vector<std::pair<float, float>>& buckets,
                                                                int numChannels, int firstBucket, int bucketCount,
                                                                int numColumns) {
    if (numChannels <= 0 || numColumns <= 0 || firstBucket < 0)
        return {};
    const std::int64_t available = (std::int64_t)(buckets.size() / (std::size_t)numChannels) - firstBucket;
    const std::int64_t count = std::min<std::int64_t>(bucketCount, available);
    if (count <= 0)
        return {};
    if (count >= numColumns)
        return aggregateToColumns(buckets, numChannels, firstBucket, bucketCount, numColumns);

    std::vector<std::pair<float, float>> folded;
    folded.reserve((std::size_t)count);
    for (std::int64_t b = 0; b < count; ++b)
        folded.push_back(foldBucket(buckets, numChannels, firstBucket + b));

    std::vector<std::pair<float, float>> columns;
    columns.reserve((std::size_t)numColumns);
    std::int64_t next = 0;
    for (std::int64_t x = 0; x < numColumns; ++x) {
        const auto [i0, i1, t] = blendFor(count, numColumns, x, next);
        columns.emplace_back(blendValues(folded[(std::size_t)i0].first, folded[(std::size_t)i1].first, t),
                             blendValues(folded[(std::size_t)i0].second, folded[(std::size_t)i1].second, t));
    }
    return columns;
}

// The RMS twin of smoothToColumns: the per-bucket RMS (channels folded as aggregateRmsToColumns does) blended between
// bucket centres when there are fewer buckets than columns, aggregateRmsToColumns otherwise.
std::vector<float> PeaksFile::smoothRmsToColumns(const std::vector<float>& rms, int numChannels, int firstBucket,
                                                 int bucketCount, int numColumns) {
    if (rms.empty() || numChannels <= 0 || numColumns <= 0 || firstBucket < 0)
        return {};
    const std::int64_t available = (std::int64_t)(rms.size() / (std::size_t)numChannels) - firstBucket;
    const std::int64_t count = std::min<std::int64_t>(bucketCount, available);
    if (count <= 0)
        return {};
    if (count >= numColumns)
        return aggregateRmsToColumns(rms, numChannels, firstBucket, bucketCount, numColumns);

    std::vector<float> folded((std::size_t)count, 0.0f);
    for (std::int64_t b = 0; b < count; ++b) {
        double sumSquares = 0.0;
        for (int ch = 0; ch < numChannels; ++ch) {
            const double v = rms[(std::size_t)((firstBucket + b) * numChannels + ch)];
            sumSquares += v * v;
        }
        folded[(std::size_t)b] = (float)std::sqrt(sumSquares / (double)numChannels);
    }
    std::vector<float> columns;
    columns.reserve((std::size_t)numColumns);
    std::int64_t next = 0;
    for (std::int64_t x = 0; x < numColumns; ++x) {
        const auto [i0, i1, t] = blendFor(count, numColumns, x, next);
        columns.push_back(blendValues(folded[(std::size_t)i0], folded[(std::size_t)i1], t));
    }
    return columns;
}

// Exact per-column min/max/RMS from raw samples, the zoomed-in twin of the peaks columns. Column `x` covers the file
// samples [first + x * spc, first + (x + 1) * spc] (the closing edge sample included, so neighbouring columns share a
// sample and the outline has no gap). Channels fold exactly as peaks do: min of mins and max of maxes over every
// channel; RMS is the root of the mean square over every channel and frame. Samples outside `block` read as silence.
PeaksFile::SampleColumns PeaksFile::sampleColumns(const juce::AudioBuffer<float>& block, std::int64_t blockStart,
                                                  double firstColumnStartSample, double samplesPerColumn,
                                                  int numColumns) {
    SampleColumns out;
    const int channels = block.getNumChannels();
    const int frames = block.getNumSamples();
    if (channels <= 0 || numColumns <= 0 || samplesPerColumn <= 0.0)
        return out;
    out.minMax.reserve((std::size_t)numColumns);
    out.rms.reserve((std::size_t)numColumns);
    for (int x = 0; x < numColumns; ++x) {
        const double s0 = firstColumnStartSample + (double)x * samplesPerColumn;
        const std::int64_t i0 = std::max<std::int64_t>(0, (std::int64_t)std::floor(s0) - blockStart);
        const std::int64_t i1 =
            std::min<std::int64_t>(frames - 1, (std::int64_t)std::ceil(s0 + samplesPerColumn) - blockStart);
        if (i1 < i0) {
            out.minMax.emplace_back(0.0f, 0.0f);
            out.rms.push_back(0.0f);
            continue;
        }
        float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
        double sumSquares = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            const float* data = block.getReadPointer(ch);
            for (std::int64_t i = i0; i <= i1; ++i) {
                const float v = data[i];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                sumSquares += (double)v * (double)v;
            }
        }
        out.minMax.emplace_back(lo, hi);
        out.rms.push_back((float)std::sqrt(sumSquares / (double)((i1 - i0 + 1) * channels)));
    }
    return out;
}

// One read of `numSamples` frames from `startSample`, all channels, silence past the end of the file.
bool PeaksFile::readAudioSamples(const juce::File& audioFile, std::int64_t startSample, int numSamples,
                                 juce::AudioBuffer<float>& out) {
    if (startSample < 0 || numSamples <= 0)
        return false;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(audioFile));
    if (reader == nullptr || reader->numChannels == 0)
        return false;
    out.setSize((int)reader->numChannels, numSamples);
    out.clear();
    const std::int64_t available = reader->lengthInSamples - startSample;
    if (available <= 0)
        return true;
    const int frames = (int)std::min<std::int64_t>(numSamples, available);
    return reader->read(&out, 0, frames, startSample, true, true);
}

} // namespace synth
