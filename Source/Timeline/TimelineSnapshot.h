#pragma once

#include "TimelineDoc.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace synth {

// The audio thread's view of the timeline: a flat, immutable, allocation-free-to-read
// projection of TimelineDoc.
//
// The doc is a tree of juce::Strings and nested std::vectors that the message thread mutates in
// place — none of which the audio thread may touch. buildFrom() flattens it once, on the message
// thread, into four contiguous arrays of trivially-copyable PODs plus per-track index ranges.
// After that the snapshot is READ-ONLY: the audio thread only indexes into it, and publication /
// reclamation is TimelineSnapshotExchange's job.
//
// Flatten policy (pinned by TimelineSnapshotTests, and load-bearing for every consumer):
//   - Note beats become ABSOLUTE. The doc stores them clip-relative (so moving a clip never
//     rewrites its notes); the snapshot stores clipStart + noteStart, because the audio thread
//     compares against the transport's ppq position and must not have to find the owning clip.
//   - Notes are CLIPPED to their clip's window. A note starting at or after the clip end is
//     dropped entirely; one overhanging the end is truncated so endBeat == clip end.
//   - All clips of a track merge into ONE per-track run, sorted by startBeat. The doc keeps clips
//     sorted by (startBeat, id) and notes within a clip sorted by (startBeat, pitch), so each
//     clip's contribution arrives already sorted and the merge is a stable merge of sorted runs —
//     never a full sort, however many clips a track has. Overlapping clips are legal in the model,
//     which is exactly why the runs have to be merged rather than concatenated.
//   - Strings are copied into fixed char buffers and TRUNCATED at kMaxStringBytes - 1 bytes, always
//     NUL-terminated. uuids are 36 chars and paramIds are short, so this never bites in practice;
//     the audio thread does strcmp against a char array and never touches a juce::String (whose
//     copy is refcounted and therefore not audio-safe).
//   - MUTED content is EXCLUDED here, at flatten time, not skipped downstream. A muted clip
//     contributes nothing at all (no note events, no AudioClipInfo entry — it is as if it were not
//     in the document), and a muted note inside an unmuted clip is dropped from the run. Doing it
//     once here is what keeps every consumer — TimelineMidiSource, the AudioClipStreamer's
//     assignment table, the offline bounce — free of any notion of mute: they cannot forget to
//     honour a flag they never see. Track-level mute is the deliberate exception and stays a
//     FIELD on TrackInfo, because solo/mute interact per block and a soloed-elsewhere track has to
//     keep its notes flattened to be un-silenced later.
//   - An AUDIO track contributes an audioClips run and NO notes; a MIDI track contributes
//     notes and NO audio clips. One Clip type covers both kinds in the doc (see synth::Clip), and
//     the track's TrackKind is what decides which half is flattened — so a stray note on an audio
//     track, or a stray assetRef on a MIDI clip, is inert rather than half-played.
struct TimelineSnapshot {
    // Fixed string capacity, including the NUL. 63 usable bytes: a uuid is 36.
    static constexpr int kMaxStringBytes = 64;

    // Capacity of an audio clip's asset reference, including the NUL. Deliberately much
    // larger than kMaxStringBytes — an assetRef is a bundle-relative PATH ("Audio/take-12.wav"),
    // not an identifier, and truncating one silently would point the streamer at a different file
    // (or at nothing) rather than merely at a name that fails to match.
    static constexpr int kMaxAssetRefBytes = 256;

    struct TrackInfo {
        std::int64_t trackId = 0;
        int kind = 0; // a synth::TrackKind value
        bool muted = false;
        bool soloed = false;
        bool armed = false;
        char bindingUuid[kMaxStringBytes] = {}; // NUL-terminated; empty means "unbound"
        int firstNote = 0;                      // range into notes[]
        int numNotes = 0;
        int firstLane = 0; // range into lanes[]
        int numLanes = 0;
        int firstAudioClip = 0; // range into audioClips[]
        int numAudioClips = 0;
    };

    struct NoteEvent {
        double startBeat = 0.0; // absolute (clip start + clip-relative note start)
        double endBeat = 0.0;   // absolute, clamped to the owning clip's end
        int pitch = 0;
        int velocity = 0;
        int channel = 1;
    };

    struct LaneInfo {
        std::int64_t laneId = 0;
        char nodeUuid[kMaxStringBytes] = {};
        char paramId[kMaxStringBytes] = {};
        // The lane's RangeSnapshot, copied verbatim: point values are denormalised, so a consumer
        // needs the range to normalise them for the target parameter.
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        // A synth::LaneRecordMode value, copied verbatim from the lane. The applier reads it
        // per block to decide whether this lane may write at all (Off / Write-while-recording) or
        // must yield to a hand on the knob (Touch / Latch) — see AutomationApplier::applyBlock.
        int recordMode = static_cast<int>(LaneRecordMode::Read);
        // Copied verbatim from AutomationLane::paramIndexHint (-1 = none). Message-thread-only
        // consumers (AudioEngine::publishTimeline's binding build) read this straight off the
        // snapshot rather than cross-referencing the doc, so the flattened lane and its hint always
        // travel together.
        int paramIndexHint = -1;
        int firstPoint = 0; // range into points[]
        int numPoints = 0;
    };

    struct Point {
        double beat = 0.0;
        double value = 0.0;
        float tension = 0.0f;
        int curve = 0; // a synth::BreakpointCurve value
    };

    // One audio clip on an Audio-kind track, in the form the audio thread needs it.
    //
    // `clipId` is the doc's own ClipId value and is the IDENTITY synth::AudioClipStreamer keys its
    // stream pool on — it is stable for the clip's lifetime and never reused, so a stream stays
    // bound to its clip across moves, trims and gain changes (all of which republish the snapshot).
    //
    // `gainLinear` is the clip's gainDb already converted, ONCE, here on the message thread: the
    // audio thread must not run a pow() per clip per block, and the conversion has a floor (see
    // buildFrom) rather than being an unbounded exponential of an unbounded field.
    struct AudioClipInfo {
        std::int64_t clipId = 0;
        double startBeat = 0.0; // absolute, timeline beats
        double lengthBeats = 0.0;
        char assetRef[kMaxAssetRefBytes] = {}; // NUL-terminated, bundle-relative; empty == inert
        float gainLinear = 1.0f;
        double fadeInBeats = 0.0;
        double fadeOutBeats = 0.0;
        double sourceStartSeconds = 0.0; // where inside the asset this clip starts reading
    };

    static_assert(std::is_trivially_copyable_v<TrackInfo>, "audio-thread PODs only - no juce::String, no vectors");
    static_assert(std::is_trivially_copyable_v<NoteEvent>, "audio-thread PODs only - no juce::String, no vectors");
    static_assert(std::is_trivially_copyable_v<LaneInfo>, "audio-thread PODs only - no juce::String, no vectors");
    static_assert(std::is_trivially_copyable_v<Point>, "audio-thread PODs only - no juce::String, no vectors");
    static_assert(std::is_trivially_copyable_v<AudioClipInfo>, "audio-thread PODs only - no juce::String, no vectors");

    TimelineSnapshot();
    ~TimelineSnapshot();

    // Non-copyable and non-movable: instances are handed around as unique_ptr and counted by
    // liveInstanceCount(), which a silent copy would falsify.
    TimelineSnapshot(const TimelineSnapshot&) = delete;
    TimelineSnapshot& operator=(const TimelineSnapshot&) = delete;

    std::vector<TrackInfo> tracks;
    std::vector<NoteEvent> notes; // per-track runs, absolute beats, each run sorted by startBeat
    std::vector<LaneInfo> lanes;  // per-track runs
    std::vector<Point> points;    // per-lane runs, sorted by beat
    // Per-track runs, sorted by startBeat. Only Audio-kind tracks contribute.
    std::vector<AudioClipInfo> audioClips;

    // True when at least one MIDI track has soloed set. Solo is a document-wide predicate ("is
    // anything soloed?" decides whether a non-soloed track is silent), so it is computed ONCE in
    // buildFrom rather than rescanned per track per block by every consumer. Audio tracks are not
    // counted: nothing renders them yet, and a soloed one must not silence the MIDI tracks.
    bool anySoloed = false;

    // The doc revision this was built from — consumers use it to tell "same content" from "new
    // content" without diffing.
    std::int64_t revision = 0;

    // Message thread only: allocates, walks juce::Strings, and reads the doc.
    static std::unique_ptr<TimelineSnapshot> buildFrom(const TimelineDoc& doc);

    // Every index range in bounds and every run correctly sorted. Cheap enough for tests and the
    // stress harness to call per block; not called on the real audio path.
    bool selfCheck() const noexcept;

    // Live-instance counter, maintained by the ctor/dtor. Always on (one relaxed atomic per
    // construction, which only ever happens on the message thread) because it is what lets tests
    // and the stress harness assert that reclamation actually frees — a leak here is a slow,
    // invisible growth of audio-thread-visible memory.
    static std::atomic<int>& liveInstanceCount() noexcept;
};

// Timeline beat -> FILE FRAME for one audio clip. The ONE mapping playback and offline stream
// priming share: frames are file frames advanced at the ENGINE's rate (this build does not
// resample), and the clip's trim is in seconds because it indexes a recording, which does not move
// when the tempo map does. Never negative — a beat before the clip starts reads its first frame.
inline std::int64_t sourceFrameForClipBeat(double clipStartBeat, double sourceStartSeconds, double beat, double bpm,
                                           double sampleRate) noexcept {
    const double secondsPerBeat = (bpm > 0.0) ? (60.0 / bpm) : 0.5;
    const double frames = ((beat - clipStartBeat) * secondsPerBeat + sourceStartSeconds) * sampleRate;
    return frames > 0.0 ? (std::int64_t)std::llround(frames) : 0;
}

} // namespace synth
