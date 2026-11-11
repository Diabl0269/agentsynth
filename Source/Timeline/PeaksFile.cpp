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

} // namespace synth
