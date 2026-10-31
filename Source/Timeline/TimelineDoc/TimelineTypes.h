#pragma once

#include <cstdint>
#include <juce_core/juce_core.h>

// The timeline's ids, enums and small value types, for code that names them without needing the
// document model. TimelineDoc.h includes this.
namespace synth {

namespace detail {

// Strongly-typed timeline id. Tagged so a ClipId can never be passed where a TrackId is
// expected: the whole point is that ids are opaque handles the doc hands out, not indices.
// Ids are assigned monotonically per doc, never reused within a doc's lifetime (removing a
// track does not free its id), and survive a toVar/fromVar round trip — the doc persists its
// next-id counters, so anything holding an id across a save/load still resolves.
// value == 0 is the "invalid / not found" sentinel every failing factory returns.
template <typename Tag>
struct TimelineId {
    std::int64_t value = 0;

    constexpr bool isValid() const noexcept { return value > 0; }

    constexpr bool operator==(const TimelineId& other) const noexcept { return value == other.value; }
    constexpr bool operator!=(const TimelineId& other) const noexcept { return value != other.value; }
    constexpr bool operator<(const TimelineId& other) const noexcept { return value < other.value; }
    constexpr bool operator>(const TimelineId& other) const noexcept { return other.value < value; }
    constexpr bool operator<=(const TimelineId& other) const noexcept { return !(other.value < value); }
    constexpr bool operator>=(const TimelineId& other) const noexcept { return !(value < other.value); }
};

struct TrackIdTag {};
struct ClipIdTag {};
struct LaneIdTag {};
struct NoteIdTag {};
struct MarkerIdTag {};

} // namespace detail

using TrackId = detail::TimelineId<detail::TrackIdTag>;
using ClipId = detail::TimelineId<detail::ClipIdTag>;
using LaneId = detail::TimelineId<detail::LaneIdTag>;
using NoteId = detail::TimelineId<detail::NoteIdTag>;
using MarkerId = detail::TimelineId<detail::MarkerIdTag>;

// Serialised as an int, so these numbers are format. Values 3..15 are reserved for future
// kinds; a file carrying one is rejected by fromVar rather than coerced into a kind this
// build understands.
enum class TrackKind : int {
    Midi = 0,
    Audio = 1,      // Track Audio clip: docs/modules/modules.md#track-audio-module-timeline-audio-source-hidden
    Automation = 2, // holds lanes no single track owns; always last ("Unassigned automation")
};

static_assert(static_cast<int>(TrackKind::Midi) == 0, "TrackKind is serialised as an int - renumbering breaks files");
static_assert(static_cast<int>(TrackKind::Audio) == 1, "TrackKind is serialised as an int - renumbering breaks files");
static_assert(static_cast<int>(TrackKind::Automation) == 2,
              "TrackKind is serialised as an int - renumbering breaks files");

// Interpolation from a breakpoint to the next one. Serialised as an int (same contract as
// TrackKind: the numbers are format).
enum class BreakpointCurve : int {
    Hold = 0,
    Linear = 1,
    Bezier = 2, // reserved — reads/writes the `tension` field, no evaluator yet
};

static_assert(static_cast<int>(BreakpointCurve::Hold) == 0,
              "BreakpointCurve is serialised as an int - renumbering breaks files");
static_assert(static_cast<int>(BreakpointCurve::Linear) == 1,
              "BreakpointCurve is serialised as an int - renumbering breaks files");
static_assert(static_cast<int>(BreakpointCurve::Bezier) == 2,
              "BreakpointCurve is serialised as an int - renumbering breaks files");

// Per-lane automation record mode. Serialised as an int (same contract as TrackKind and
// BreakpointCurve: the numbers are format), and flattened into TimelineSnapshot::LaneInfo so the
// audio thread's applier can honour it without touching the doc.
//
// Semantics (the applier implements the audio half, synth::AutomationRecorder the capture half):
//   Off   — the lane is inert. Automation never plays back and nothing is ever recorded into it.
//   Read  — playback only. The default, and what a lane with no stored mode loads as.
//   Touch — plays back, but the user's hand wins WHILE a parameter gesture is in flight: the
//           applier skips a claimed parameter, and the gesture is captured and committed when the
//           user lets go. Let go and playback resumes immediately.
//   Latch — like Touch, except the capture span stays open after the gesture ends and only closes
//           on stop (or when global record is disabled) — the last value the user set "latches".
//   Write — while the transport is playing and global record is enabled, the lane does not play
//           back at all and its whole play-to-stop span is overwritten. With global record OFF a
//           Write lane behaves exactly like Read. Auto-drops to Touch on stop (Cubase behaviour).
enum class LaneRecordMode : int {
    Off = 0,
    Read = 1,
    Touch = 2,
    Latch = 3,
    Write = 4,
};

static_assert(static_cast<int>(LaneRecordMode::Off) == 0,
              "LaneRecordMode is serialised as an int - renumbering breaks files");
static_assert(static_cast<int>(LaneRecordMode::Read) == 1,
              "LaneRecordMode is serialised as an int - renumbering breaks files");
static_assert(static_cast<int>(LaneRecordMode::Touch) == 2,
              "LaneRecordMode is serialised as an int - renumbering breaks files");
static_assert(static_cast<int>(LaneRecordMode::Latch) == 3,
              "LaneRecordMode is serialised as an int - renumbering breaks files");
static_assert(static_cast<int>(LaneRecordMode::Write) == 4,
              "LaneRecordMode is serialised as an int - renumbering breaks files");

// Where an Audio track records from: nothing, a mono or stereo pair of input channels, the whole mix, or another
// track's sound (its instrument through its mixer channel).
struct AudioInputChoice {
    enum class Kind { None, Device, WholeMix, Track };
    static constexpr int kMaxChannel = 8; // highest 1-based input channel a choice may name
    Kind kind = Kind::None;
    int firstChannel = 0;    // 0-based; Device only
    int numChannels = 1;     // 1 or 2; Device only
    juce::String deviceName; // the input device at pick time, to name it while it is not connected
    TrackId sourceTrack;     // Track only; may name a track deleted since (the choice then reads "track removed")
    juce::String inputName;  // the user's name for the input on that interface; non-empty = found by name
    bool operator==(const AudioInputChoice& other) const noexcept {
        return kind == other.kind && firstChannel == other.firstChannel && numChannels == other.numChannels &&
               deviceName == other.deviceName && sourceTrack == other.sourceTrack && inputName == other.inputName;
    }
    bool operator!=(const AudioInputChoice& other) const noexcept { return !(*this == other); }
    bool isValid() const noexcept {
        if (kind == Kind::Track)
            return sourceTrack.isValid();
        return kind != Kind::Device || (firstChannel >= 0 && (numChannels == 1 || numChannels == 2) &&
                                        firstChannel + numChannels <= kMaxChannel);
    }
};

// Whether an Audio track plays its own input live: Auto while it is armed and the transport is stopped or recording,
// On always, Off never (synth::monitorHearsInput in Timeline/TrackMonitoring.h is the rule).
enum class MonitorMode { Auto, On, Off };

} // namespace synth
