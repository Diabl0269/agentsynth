#include "BounceExporter.h"

#include "BounceSession.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace synth {

juce::String fileExtensionFor(BounceFormat format) {
    switch (format) {
    case BounceFormat::Aiff:
        return "aiff";
    case BounceFormat::Flac:
        return "flac";
    case BounceFormat::Mp3:
        return "mp3";
    case BounceFormat::Wav:
        break;
    }
    return "wav";
}

// nullptr for Mp3 when `lameExecutable` is not an existing file: MP3 is encoded by the user's own
// `lame` (juce::LAMEEncoderAudioFormat, see LameLocator.h) and there is nothing to fall back to.
std::unique_ptr<juce::AudioFormat> createAudioFormatFor(BounceFormat format, const juce::File& lameExecutable) {
    switch (format) {
    case BounceFormat::Mp3:
        if (!lameExecutable.existsAsFile())
            return nullptr;
        return std::make_unique<juce::LAMEEncoderAudioFormat>(lameExecutable);
    case BounceFormat::Aiff:
        return std::make_unique<juce::AiffAudioFormat>();
    case BounceFormat::Flac:
        return std::make_unique<juce::FlacAudioFormat>();
    case BounceFormat::Wav:
        break;
    }
    return std::make_unique<juce::WavAudioFormat>();
}

// -1 for a bitrate that is not offered. The CBR entries of getQualityOptions() follow the ten VBR ones.
int lameQualityIndexForBitrate(int kbps) {
    const bool offered =
        std::find(std::begin(kMp3BitratesKbps), std::end(kMp3BitratesKbps), kbps) != std::end(kMp3BitratesKbps);
    if (!offered)
        return -1;
    // Look the bitrate up in JUCE's own list rather than hard-coding an offset into it.
    const auto wanted = juce::String(kbps) + " Kb/s CBR";
    juce::LAMEEncoderAudioFormat format{juce::File()};
    return format.getQualityOptions().indexOf(wanted);
}

// The one place the options map to juce::AudioFormat::createWriterFor's arguments. The stream is
// only taken over on success.
std::unique_ptr<juce::AudioFormatWriter> createBounceWriter(const BounceOptions& options, juce::OutputStream* stream,
                                                            unsigned int numChannels) {
    const auto audioFormat = createAudioFormatFor(options.format, options.lameExecutable);
    if (audioFormat == nullptr)
        return nullptr;
    const bool isMp3 = options.format == BounceFormat::Mp3;
    const int qualityIndex = isMp3 ? lameQualityIndexForBitrate(options.mp3BitrateKbps) : 0;
    if (qualityIndex < 0)
        return nullptr;
    return std::unique_ptr<juce::AudioFormatWriter>(audioFormat->createWriterFor(
        stream, options.sampleRate, numChannels, isMp3 ? 16 : options.bitDepth, {}, qualityIndex));
}

// MP3 is produced when the writer closes (lame runs then), and a failed run leaves the file empty
// without any error from JUCE.
bool encodedOutputIsMissing(const BounceOptions& options, const juce::File& file) {
    return options.format == BounceFormat::Mp3 && (!file.existsAsFile() || file.getSize() <= 0);
}

juce::String validateBounceOptions(const BounceOptions& options) {
    if (!(options.sampleRate > 0.0) || !std::isfinite(options.sampleRate))
        return "Sample rate must be a positive number.";
    if (options.blockSize <= 0)
        return "Block size must be at least 1 sample.";
    if (options.numChannels <= 0)
        return "A bounce needs at least one channel.";
    if (options.format == BounceFormat::Mp3) {
        if (!options.lameExecutable.existsAsFile())
            return "MP3 export needs the lame encoder, which was not found. Install it (e.g. brew install lame) or "
                   "export another format.";
        if (lameQualityIndexForBitrate(options.mp3BitrateKbps) < 0)
            return "MP3 bitrate must be 128, 192, 256 or 320 kbps.";
    } else if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32) {
        return "Bit depth must be 16, 24 or 32.";
    }
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
