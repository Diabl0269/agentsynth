#pragma once

#include <functional>
#include <juce_core/juce_core.h>

class AudioEngine;

namespace synth {

// Offline bounce/export: renders a beat range of the CURRENT patch to a WAV file, faster
// than realtime, through exactly the same graph the app plays through.
//
// "The same graph" is the whole point — there is no second, offline-only signal path to keep in
// sync. The render loop is synth::OfflineTransportDriver, the one every timeline test already
// renders through; the transport is the app's own transport, driven through its normal
// stop/locate/play commands; the modules are the live nodes. What differs from playback is only
// that nobody is waiting for a device: blocks are produced as fast as the CPU can produce them.
enum class BounceFormat { Wav, Aiff };

struct BounceOptions {
    // The range to render, in beats, at the transport's current tempo. Half-open in intent but
    // whole-block in practice: the render stops on the first block whose END position reaches
    // endBeat, so the file is ceil(rangeSamples / blockSize) whole blocks long and may contain up
    // to one block of audio past endBeat.
    double startBeat = 0.0;
    double endBeat = 8.0;

    // Extra render past endBeat, with the TRANSPORT STOPPED — time-locked sources (clips,
    // sequencers, the timeline) go quiet while reverb and delay tails ring out. Rounded up to
    // whole blocks. 0 means the file ends exactly at the range.
    double tailSeconds = 0.0;

    // The render format. sampleRate is free to differ from the device's — the engine is re-prepared
    // at this rate for the duration and put back afterwards.
    double sampleRate = 44100.0;
    int blockSize = 512;
    int bitDepth = 24; // 16 / 24 = integer PCM, 32 = IEEE float (WAV only - see `format`)
    int numChannels = 2;

    // AIFF has no IEEE-float variant in juce::AiffAudioFormat, so bitDepth 32 is valid only when
    // format is Wav - validate() rejects the combination rather than silently downgrading it.
    BounceFormat format = BounceFormat::Wav;
};

struct BounceResult {
    bool ok = false;
    juce::String message; // always set: the reason on failure, a one-line summary on success
    juce::int64 samplesWritten = 0;

    // Blocks rendered before a Track Audio clip's ring could serve them, i.e. blocks where a
    // streamed clip contributed silence it should not have. Zero on a healthy machine — a bounce
    // waits for the prefetch thread rather than racing it — and non-zero only when that wait timed
    // out. Reported rather than swallowed: a short bounce is visible, a dropped clip is not.
    int streamDropouts = 0;
};

class BounceExporter {
public:
    // Return false from a progress callback to cancel. It is called once per written block with the
    // fraction of the expected total (range + tail) written so far, monotonically non-decreasing,
    // and once more with exactly 1.0 on success.
    using ProgressCallback = std::function<bool(double)>;

    // MESSAGE THREAD, BLOCKING. Renders [startBeat, endBeat] + tail and streams every block
    // straight into the WAV writer, so peak memory is one block regardless of how long the take is.
    // A live device callback is detached for the duration; the transport is unlooped (a live loop
    // would make "render until endBeat" unreachable), located to startBeat, played until endBeat,
    // then stopped for the tail. Afterwards loop state and playhead are restored and left STOPPED,
    // and the engine's prepare state (device rate or prior hosted format) is restored too.
    //
    // Atomicity: written to a sibling temp file, moved into place only on success — a failed or
    // cancelled bounce never leaves a truncated file and never touches a pre-existing one.
    //
    // Determinism: two bounces of the same project produce byte-identical files (no wall-clock
    // reads, deterministic voice allocation, block boundaries a pure function of the options). Not
    // true across two bounces from one live engine without a reload — prepareToPlay resets rates
    // and ramps but not phase, so DSP state carries over from the first render.
    //
    // Non-render-safe modules: every offline block gets an EMPTY juce::MidiBuffer and the engine's
    // MIDI collector is never drained offline, so live-input modules contribute silence by
    // definition. Exception: ExternalMidiModule keeps its own collector fed straight from
    // AudioEngine::handleIncomingMidiMessage, so a note played on a physical keyboard during a
    // bounce can still land in the file. Don't play while you bounce.
    //
    // Streamed audio clips: a bounce renders as fast as the CPU allows, which is far faster than
    // AudioClipStreamer's prefetch thread refills a ring — and re-preparing the engine at the render
    // format invalidates every ring first, so without a handshake the head of every Track Audio clip
    // is silence. Each block therefore waits on AudioClipStreamer::waitUntilPrimed before it is
    // rendered; a wait that times out is counted in BounceResult::streamDropouts and never passes
    // silently.
    //
    // The metronome is summed AFTER the graph in AudioEngine::renderPass, so a bounce captures it
    // in the post-graph buffer it renders. bounce() forces it off explicitly (user toggle and
    // count-in forced-on flag) for the duration and restores it via an RAII guard — see
    // MetronomeForceOffGuard in BounceExporter.cpp.
    static BounceResult bounce(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                               const ProgressCallback& progress = {});

    BounceExporter() = delete;
};

} // namespace synth
