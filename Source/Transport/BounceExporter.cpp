#include "BounceExporter.h"

#include "../AudioEngine.h"
#include "OfflineTransportDriver.h"
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace synth {

namespace {

// A safety cap for the range render, sized off the exact block count the tempo implies. Under a
// constant tempo the render lands on the expected count exactly; this only exists so a future
// tempo-map bug fails after a bounded amount of work instead of grinding to the driver's own
// 2^18-block backstop.
constexpr juce::int64 kMaxRangeBlocks = 1 << 20;

BounceResult failure(juce::String message) {
    BounceResult result;
    result.ok = false;
    result.message = std::move(message);
    return result;
}

// Everything that can be rejected before a single sample is rendered, or a single file touched.
juce::String validate(const BounceOptions& options) {
    if (!(options.sampleRate > 0.0) || !std::isfinite(options.sampleRate))
        return "Sample rate must be a positive number.";
    if (options.blockSize <= 0)
        return "Block size must be at least 1 sample.";
    if (options.numChannels <= 0)
        return "A bounce needs at least one channel.";
    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32)
        return "Bit depth must be 16, 24 or 32.";
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

} // namespace

BounceResult BounceExporter::bounce(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                                    const ProgressCallback& progress) {
    if (const auto problem = validate(options); problem.isNotEmpty())
        return failure(problem);
    if (outFile == juce::File())
        return failure("No output file was given.");

    auto& transport = engine.getTransport();
    auto& graph = engine.getGraph();

    // ---- Everything that has to go back afterwards, read before anything is disturbed ----
    const auto before = transport.getPositionSnapshot();
    const double previousSampleRate = graph.getSampleRate();
    const int previousBlockSize = graph.getBlockSize();
    const int previousInputChannels = graph.getTotalNumInputChannels();
    const int previousOutputChannels = graph.getTotalNumOutputChannels();

    // Take the graph off the device (Standalone with a live device); a Hosted engine reports false
    // and is used as-is. Everything below now owns the graph exclusively — which is precisely the
    // precondition OfflineTransportDriver asserts on.
    const bool deviceWasAttached = engine.suspendDeviceCallback();

    // Constructing the driver re-prepares the whole graph at the render format.
    OfflineTransportDriver driver(engine, options.sampleRate, options.blockSize, options.numChannels);

    // Runs on every exit path, including the early failures below. Transport commands only take
    // effect on a tick, and nothing is going to tick this engine again before the caller looks at
    // it, so the restore renders one throwaway block to make them real — that is what lets a caller
    // read the restored playhead position straight after bounce() returns.
    const auto restore = [&] {
        transport.stop();
        transport.setLoop(before.loopStartPpq, before.loopEndPpq, before.looping);
        transport.locateBeat(before.ppq);
        driver.streamBlocks(1, {});

        if (deviceWasAttached)
            engine.resumeDeviceCallback(); // re-prepares at the DEVICE's rate; see AudioEngine.h
        else if (previousSampleRate > 0.0 && previousBlockSize > 0)
            engine.prepareForHost(previousSampleRate, previousBlockSize, previousInputChannels, previousOutputChannels);
    };

    // ---- The writer, on a temp file beside the target ----
    // juce::TemporaryFile only computes a name here; nothing is created until the stream opens, so
    // an unwritable or nonexistent destination directory fails right here rather than half way
    // through a render.
    juce::TemporaryFile temporary(outFile);
    std::unique_ptr<juce::FileOutputStream> stream(temporary.getFile().createOutputStream());
    if (stream == nullptr || stream->failedToOpen()) {
        restore();
        return failure("Could not open \"" + outFile.getFullPathName() + "\" for writing.");
    }

    juce::WavAudioFormat wavFormat;
    // bitDepth 32 makes this an IEEE-float WAV (juce::WavAudioFormatWriter sets
    // usesFloatingPointData for 32-bit), so a float bounce is a bit-exact copy of what the graph
    // produced, with no clipping and no dither decision to make.
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        stream.get(), options.sampleRate, (unsigned int)options.numChannels, options.bitDepth, {}, 0));
    if (writer == nullptr) {
        restore();
        return failure("Could not create a WAV writer for the requested format.");
    }
    stream.release(); // the writer owns the stream from here

    // ---- How long this is expected to be, for the progress fraction ----
    const double bpm = before.bpm > 0.0 ? before.bpm : 120.0;
    const double rangeSamples = (options.endBeat - options.startBeat) * 60.0 * options.sampleRate / bpm;
    const juce::int64 expectedRangeBlocks = (juce::int64)std::ceil(rangeSamples / (double)options.blockSize);
    const juce::int64 tailBlocks =
        (juce::int64)std::ceil(options.tailSeconds * options.sampleRate / (double)options.blockSize);
    const juce::int64 expectedTotalSamples = (expectedRangeBlocks + tailBlocks) * (juce::int64)options.blockSize;

    juce::int64 samplesWritten = 0;
    bool cancelled = false;
    bool writeFailed = false;

    // The one place audio leaves the render. `block` is the driver's scratch — valid for this call
    // only, which is exactly as long as writeFromAudioSampleBuffer needs it.
    const auto streamToWriter = [&](const juce::AudioBuffer<float>& block, const BlockTimeInfo&) {
        if (cancelled || writeFailed)
            return;

        if (!writer->writeFromAudioSampleBuffer(block, 0, block.getNumSamples())) {
            writeFailed = true;
            transport.stop(); // gets us out of the render loop; see below
            return;
        }

        samplesWritten += block.getNumSamples();

        if (progress) {
            const double fraction =
                expectedTotalSamples > 0 ? juce::jmin(1.0, (double)samplesWritten / (double)expectedTotalSamples) : 1.0;
            if (!progress(fraction)) {
                cancelled = true;
                // A stopped transport reaches no beat, so streamToBeat gives up one block later.
                // Posting the stop IS the cancellation mechanism — the render loop has no flag to
                // poll, and adding one would give it two ways to end instead of one.
                transport.stop();
            }
        }
    };

    // ---- Choreography ----
    // All four commands drain, in this order, at the top of the first tick below: stop whatever was
    // playing, drop the loop for the duration, locate to the range start, play.
    transport.stop();
    transport.setLoop(before.loopStartPpq, before.loopEndPpq, false);
    transport.locateBeat(options.startBeat);
    transport.play();

    // One block on its own first, on purpose. streamToBeat decides whether the target is even ahead
    // of the playhead by reading the CROSS-THREAD POSITION SNAPSHOT, which the locate above has not
    // reached yet — bouncing bars 1-2 while the playhead sits at bar 40 would otherwise bail out
    // instantly with an empty file. This block is not a throwaway: a command takes effect at sample
    // 0 of the block that drains it, so this IS the first block of the range.
    driver.streamBlocks(1, streamToWriter);

    if (!cancelled && !writeFailed) {
        const int maxBlocks = (int)juce::jlimit<juce::int64>(1, kMaxRangeBlocks, expectedRangeBlocks * 2 + 64);
        driver.streamToBeat(options.endBeat, streamToWriter, maxBlocks);
    }

    // ---- Tail ----
    // Rendered with the transport STOPPED, so clips and sequencers fall silent while the FX ring
    // out. That is the difference between a tail and just bouncing a longer range.
    if (!cancelled && !writeFailed && tailBlocks > 0) {
        transport.stop();
        driver.streamBlocks((int)juce::jmin<juce::int64>(tailBlocks, kMaxRangeBlocks), streamToWriter);
    }

    // Flush and close before the file is moved or inspected.
    writer.reset();

    restore();

    BounceResult result;
    result.samplesWritten = samplesWritten;

    if (cancelled) {
        // The temp file dies with `temporary`; the target was never touched, so a cancelled
        // re-bounce leaves a previous export exactly as it was.
        result.message = "Bounce cancelled.";
        return result;
    }

    if (writeFailed) {
        result.message = "Failed while writing to \"" + outFile.getFullPathName() + "\".";
        return result;
    }

    if (!temporary.overwriteTargetFileWithTemporary()) {
        result.message = "Could not move the rendered audio into \"" + outFile.getFullPathName() + "\".";
        return result;
    }

    if (progress)
        progress(1.0);

    result.ok = true;
    result.message = "Bounced " + juce::String(samplesWritten) + " samples to \"" + outFile.getFileName() + "\".";
    return result;
}

} // namespace synth
