#pragma once

#include "TimelineDoc.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace synth {

// The audio thread's view of the timeline (TL2-2): a flat, immutable, allocation-free-to-read
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
struct TimelineSnapshot {
    // Fixed string capacity, including the NUL. 63 usable bytes: a uuid is 36.
    static constexpr int kMaxStringBytes = 64;

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
        int firstPoint = 0; // range into points[]
        int numPoints = 0;
    };

    struct Point {
        double beat = 0.0;
        double value = 0.0;
        float tension = 0.0f;
        int curve = 0; // a synth::BreakpointCurve value
    };

    static_assert(std::is_trivially_copyable_v<TrackInfo>, "audio-thread PODs only — no juce::String, no vectors");
    static_assert(std::is_trivially_copyable_v<NoteEvent>, "audio-thread PODs only — no juce::String, no vectors");
    static_assert(std::is_trivially_copyable_v<LaneInfo>, "audio-thread PODs only — no juce::String, no vectors");
    static_assert(std::is_trivially_copyable_v<Point>, "audio-thread PODs only — no juce::String, no vectors");

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

} // namespace synth
