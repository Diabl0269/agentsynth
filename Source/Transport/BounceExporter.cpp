#include "BounceExporter.h"

#include "BounceSession.h"
#include <cmath>
#include <limits>

namespace synth {

juce::String fileExtensionFor(BounceFormat format) {
    switch (format) {
    case BounceFormat::Aiff:
        return "aiff";
    case BounceFormat::Flac:
        return "flac";
    case BounceFormat::Wav:
        break;
    }
    return "wav";
}

std::unique_ptr<juce::AudioFormat> createAudioFormatFor(BounceFormat format) {
    switch (format) {
    case BounceFormat::Aiff:
        return std::make_unique<juce::AiffAudioFormat>();
    case BounceFormat::Flac:
        return std::make_unique<juce::FlacAudioFormat>();
    case BounceFormat::Wav:
        break;
    }
    return std::make_unique<juce::WavAudioFormat>();
}

juce::String validateBounceOptions(const BounceOptions& options) {
    if (!(options.sampleRate > 0.0) || !std::isfinite(options.sampleRate))
        return "Sample rate must be a positive number.";
    if (options.blockSize <= 0)
        return "Block size must be at least 1 sample.";
    if (options.numChannels <= 0)
        return "A bounce needs at least one channel.";
    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32)
        return "Bit depth must be 16, 24 or 32.";
    if (options.format == BounceFormat::Aiff && options.bitDepth == 32)
        return "AIFF has no 32-bit float variant - choose 16 or 24 bit, or export WAV instead.";
    if (options.format == BounceFormat::Flac && options.bitDepth == 32)
        return "FLAC has no 32-bit float variant - choose 16 or 24 bit, or export WAV instead.";
    if (!std::isfinite(options.startBeat) || !std::isfinite(options.endBeat))
        return "The bounce range must be finite.";
    if (options.startBeat < 0.0)
        return "The bounce range must start at or after beat 0.";
    if (!(options.endBeat > options.startBeat))
        return "The bounce range must end after it starts.";
    if (!std::isfinite(options.tailSeconds) || options.tailSeconds < 0.0)
        return "Tail length must be zero or more seconds.";
    return {};
}

// bounce() is BounceSession driven to completion in one call, with no chunking - see
// BounceSession.h for why the choreography lives there instead of here. BounceRunner
// (Source/Transport/BounceRunner.h) is the other driver, for a caller that wants to interleave
// the render with a UI progress tick instead of blocking for the whole take.
BounceResult BounceExporter::bounce(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                                    const ProgressCallback& progress) {
    BounceSession session(engine, outFile, options, progress);
    constexpr int kUnbounded = std::numeric_limits<int>::max();
    while (!session.isRangeDone())
        session.stepRange(kUnbounded);
    while (!session.isTailDone())
        session.stepTail(kUnbounded);
    return session.finish();
}

} // namespace synth
