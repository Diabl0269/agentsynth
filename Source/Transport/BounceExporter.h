#pragma once

#include <functional>
#include <juce_core/juce_core.h>

class AudioEngine;

namespace synth {

// Offline bounce/export (TL4-6): renders a beat range of the CURRENT patch to a WAV file, faster
// than realtime, through exactly the same graph the app plays through.
//
// "The same graph" is the whole point — there is no second, offline-only signal path to keep in
// sync. The render loop is synth::OfflineTransportDriver, the one every timeline test already
// renders through; the transport is the app's own transport, driven through its normal
// stop/locate/play commands; the modules are the live nodes. What differs from playback is only
// that nobody is waiting for a device: blocks are produced as fast as the CPU can produce them.
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
    int bitDepth = 24; // 16 / 24 = integer PCM, 32 = IEEE float
    int numChannels = 2;
};

struct BounceResult {
    bool ok = false;
    juce::String message; // always set: the reason on failure, a one-line summary on success
    juce::int64 samplesWritten = 0;
};

class BounceExporter {
public:
    // Return false from a progress callback to cancel. It is called once per written block with the
    // fraction of the expected total (range + tail) written so far, monotonically non-decreasing,
    // and once more with exactly 1.0 on success.
    using ProgressCallback = std::function<bool(double)>;

    // MESSAGE THREAD, BLOCKING. Renders [startBeat, endBeat] + tail and streams every block
    // straight into the WAV writer, so peak memory is one block regardless of how long the take is.
    //
    // Choreography, in order:
    //   1. If the engine is Standalone with a live device, its audio callback is DETACHED for the
    //      duration (AudioEngine::suspendDeviceCallback) — playback stops, and nothing else clocks
    //      the graph while the render owns it. A Hosted engine is used as-is; its host is expected
    //      to be quiescent, and there is no portable way for us to make it so.
    //   2. The engine is re-prepared at the render format (OfflineTransportDriver's constructor).
    //   3. The transport is stopped, UNLOOPED (a bounce renders the range linearly — a live loop
    //      would make "render until endBeat" unreachable), located to startBeat and played.
    //   4. Blocks are rendered and written until the transport's end position reaches endBeat,
    //      then the transport is stopped and the tail is rendered and written.
    //   5. The transport is put back: loop state restored, playhead located to where it was, and
    //      left STOPPED — bouncing from a playing transport leaves you stopped at the prior
    //      position, which is what every DAW does and what makes the position restore observable.
    //      The engine's prepare state is restored too (the device's rate on re-attach, or the
    //      previous hosted format).
    //
    // Atomicity: the render is written to a sibling temp file and moved into place only on success,
    // so a failed or cancelled bounce never leaves a truncated file — and never touches a file that
    // was already there.
    //
    // Determinism: two bounces of the same project produce byte-identical files. Nothing on the
    // render path reads the wall clock (that was the point of driving the graph from the transport
    // rather than from a device), voice allocation is deterministic, and the block boundaries are a
    // function of the options alone. The qualifier is "the same project", not "the same session":
    // bouncing twice in a row from one live engine starts the second render from whatever DSP state
    // the first left behind (free-running oscillator phase, un-decayed reverb), because
    // prepareToPlay resets rates and ramps but not phase. Reload the project and the bytes match.
    //
    // Non-render-safe modules: every offline block is handed an EMPTY juce::MidiBuffer, and the
    // engine's MIDI collector is never drained offline (no device callback is running to drain it).
    // Live-input modules therefore contribute silence to a bounce by definition, not by accident —
    // External MIDI, and any future hardware-input module, have nothing to emit. The one hole worth
    // naming: ExternalMidiModule keeps its own collector, fed straight from
    // AudioEngine::handleIncomingMidiMessage, so a note physically played on a MIDI keyboard while
    // a bounce is running can still land in the file. Don't play while you bounce.
    //
    // The metronome (TL5-6, not yet built) must be summed AFTER the graph, which is what keeps it
    // out of a bounce by construction rather than by a flag anyone has to remember to check.
    //
    // In a SYNTH_ENABLE_TIMELINE=0 build the transport never ticks, so a bounce renders one block
    // plus the tail and stops — the feature has no meaning without the timeline compiled in.
    static BounceResult bounce(AudioEngine& engine, const juce::File& outFile, const BounceOptions& options,
                               const ProgressCallback& progress = {});

    BounceExporter() = delete;
};

} // namespace synth
