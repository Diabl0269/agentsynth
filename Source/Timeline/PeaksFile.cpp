#include "PeaksFile.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace synth {

namespace {

constexpr size_t kHeaderBytes = 16;

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

} // namespace

//==============================================================================
bool PeaksFile::read(const juce::File& file, Data& out) {
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block))
        return false;
    if (block.getSize() < kHeaderBytes)
        return false;

    const auto* bytes = static_cast<const juce::uint8*>(block.getData());
    const std::uint32_t magic = readLittleEndianUInt32(bytes + 0);
    const std::uint32_t version = readLittleEndianUInt32(bytes + 4);
    const std::uint32_t bucketSize = readLittleEndianUInt32(bytes + 8);
    const std::uint32_t numChannels = readLittleEndianUInt32(bytes + 12);

    if (magic != kMagic || version != kVersion)
        return false;
    if (bucketSize == 0 || numChannels == 0)
        return false;

    const size_t payloadBytes = block.getSize() - kHeaderBytes;
    if (payloadBytes % 4 != 0)
        return false; // not a whole number of floats: garbage/truncated
    const size_t floatCount = payloadBytes / 4;
    if (floatCount % 2 != 0)
        return false; // not a whole number of (min,max) pairs
    const size_t pairCount = floatCount / 2;
    if (pairCount % numChannels != 0)
        return false; // not a whole number of complete (all-channel) buckets

    Data result;
    result.bucketSize = (int)bucketSize;
    result.numChannels = (int)numChannels;
    result.buckets.reserve(pairCount);
    for (size_t i = 0; i < pairCount; ++i) {
        const size_t offset = kHeaderBytes + i * 8;
        const float minValue = readLittleEndianFloat(bytes + offset);
        const float maxValue = readLittleEndianFloat(bytes + offset + 4);
        result.buckets.emplace_back(minValue, maxValue);
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
    writeLittleEndianUInt32(*stream, kVersion);
    writeLittleEndianUInt32(*stream, (std::uint32_t)data.bucketSize);
    writeLittleEndianUInt32(*stream, (std::uint32_t)data.numChannels);
    for (const auto& bucket : data.buckets) {
        writeLittleEndianFloat(*stream, bucket.first);
        writeLittleEndianFloat(*stream, bucket.second);
    }

    return stream->getStatus().wasOk();
}

//==============================================================================
PeaksFile::Accumulator::Accumulator(int bucketSize, int numChannels)
    : bucketSize_(juce::jmax(1, bucketSize))
    , numChannels_(juce::jmax(1, numChannels))
    , bucketMin_((size_t)numChannels_, std::numeric_limits<float>::max())
    , bucketMax_((size_t)numChannels_, std::numeric_limits<float>::lowest()) {
    data_.bucketSize = bucketSize_;
    data_.numChannels = numChannels_;
}

void PeaksFile::Accumulator::reset() {
    data_.buckets.clear();
    bucketFill_ = 0;
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
        bucketMin_[(size_t)channel] = std::numeric_limits<float>::max();
        bucketMax_[(size_t)channel] = std::numeric_limits<float>::lowest();
    }
    bucketFill_ = 0;
}

} // namespace synth
