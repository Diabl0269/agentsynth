#pragma once

#include <cstddef>
#include <functional>
#include <juce_core/juce_core.h>
#include <optional>
#include <string>
#include <vector>

// The client side of the server's song steps (POST /v1/song/plan and /v1/song/build, documented in the platform's
// the platform's song steps doc and in docs/ai/song-steps.md here). Pure data and parsing, no UI and no network: the
// plan the server prices, the NDJSON lines a build streams, and the model of a running build that the build card reads.
namespace synth::songsteps {

/** The kinds of step a build or an edit plan row can show. `mix` is reserved for a later optional step. */
enum class StepKind { plan, track, pattern, sections, automation, module, connect, settings, modulation, fix, mix };

juce::String stepKindToString(StepKind kind);
/** An unknown or empty name reads as `plan`, so a newer server's new kind still gets an icon. */
StepKind stepKindFromString(const juce::String& name);

enum class StepStatus { waiting, running, fixing, done, skipped };
juce::String stepStatusToString(StepStatus status);
StepStatus stepStatusFromString(const juce::String& name, bool& known);

struct StepInfo {
    juce::String id;
    StepKind kind = StepKind::plan;
    juce::String label;
};

struct PlanSection {
    juce::String name;
    int bars = 0;
    int energy = 1; // 1 (quiet) to 5 (full)
    juce::String chords;
    juce::String description;
};

struct PlanTrack {
    juce::String name;
    juce::String role;
    juce::String instrument;
    juce::String sound;
    juce::StringArray sections;
};

/** The server's SongPlanV1, read for display. `raw` is the object exactly as received: it is what goes back to
 *  /v1/song/build and as `previousPlan` to /v1/song/plan, never rebuilt from the fields below. */
struct SongPlan {
    juce::String title;
    juce::String key;
    int bpm = 0;
    std::vector<PlanSection> sections;
    std::vector<PlanTrack> tracks;
    int laneCount = 0;
    juce::String special;
    juce::var raw;

    int totalBars() const;
    /** 4/4 bars at `bpm`; 0 without a tempo. */
    double seconds() const;
    /** "3:20". */
    juce::String durationText() const;
};

/** A reply of /v1/song/plan. */
struct PlanReply {
    SongPlan plan;
    int units = 1; // what Build costs, in monthly uses
    double estimatedUsd = 0.0;
    std::vector<StepInfo> steps;
    juce::String token;
};

/** Reads the `data` of a plan reply (the JSON text the provider delivers). False, with `error` set, when it is not a
 *  usable plan (no sections or tracks, no token). */
bool parsePlanReply(const juce::String& dataJson, PlanReply& out, juce::String& error);
bool parseSongPlan(const juce::var& planVar, SongPlan& out);

/** "Dark drum and bass: 3:20, 174 BPM, 7 sections" and similar one-liners the card, the history and a screen reader
 *  share. */
juce::String planMetaText(const SongPlan& plan);

// -- the build stream ----------------------------------------------------------------------------------------------

enum class EventType { steps, step, ping, done, error };

struct StreamEvent {
    EventType type = EventType::ping;
    std::vector<StepInfo> steps; // steps
    juce::String stepId;         // step
    StepStatus status = StepStatus::running;
    int ms = -1;              // step: how long it took, when finished
    juce::String stepSummary; // step: the tooltip text
    juce::var envelope;       // done: the ProjectEditEnvelope
    juce::StringArray notes;  // done: what an optional step skipped
    int tracks = 0, sections = 0, bars = 0;
    double seconds = 0.0;  // done: the song summary
    juce::String message;  // error
    bool refunded = false; // error
};

/** One NDJSON line, or nullopt for a blank line, bad JSON or a line that is not a known event (skipped, never fatal).
 */
std::optional<StreamEvent> parseStreamLine(const juce::String& line);

/** Cuts a byte stream into lines however the network chunked it: a line split across reads is held until its newline
 *  arrives. Bytes, not text, so a multi-byte character cut by a chunk boundary stays whole. */
class LineSplitter {
public:
    using LineHandler = std::function<void(const juce::String& line)>;
    void push(const char* data, std::size_t size, const LineHandler& onLine);
    /** The last line when the stream ended without a final newline. */
    void finish(const LineHandler& onLine);

private:
    std::string pending;
};

// -- a running build -------------------------------------------------------------------------------------------------

struct StepState {
    StepInfo info;
    StepStatus status = StepStatus::waiting;
    int ms = -1;
    juce::String summary;
};

/** What the build card shows, driven by the stream's events. Pure: the card only reads it. */
class BuildModel {
public:
    /** Applies one event. Returns true when anything the card shows changed. */
    bool apply(const StreamEvent& event);

    const std::vector<StepState>& steps() const { return stepStates; }
    int total() const { return (int)stepStates.size(); }
    /** Steps that are over, finished or skipped. */
    int finishedCount() const;
    /** The step to show as "now": the first fixing one, else the first running one; -1 when none is. */
    int currentIndex() const;
    /** "step N of M": N is the finished count plus one, never past M. */
    int currentNumber() const;
    /** 0 to 1, finished steps over all steps (0 until the list is known). */
    float progress() const;
    bool isFixing() const {
        return currentIndex() >= 0 && stepStates[(size_t)currentIndex()].status == StepStatus::fixing;
    }
    bool hasSkipped() const;

    bool isDone() const { return finished && !failed; }
    bool isFailed() const { return failed; }
    bool isFinished() const { return finished; }
    juce::String failureMessage() const { return failure; }
    bool wasRefunded() const { return refunded; }
    const StreamEvent& doneEvent() const { return finalEvent; }

    /** Marks the build over without a terminal event (cancelled by the user, or the connection dropped). */
    void markFailed(const juce::String& message, bool refundedUses);
    void markDone(const StreamEvent& doneEventIn);

    /** One-line result: "12 steps, 7 sections, 3:20" (skipped steps are named). */
    juce::String resultLine() const;

private:
    std::vector<StepState> stepStates;
    bool finished = false;
    bool failed = false;
    bool refunded = false;
    juce::String failure;
    StreamEvent finalEvent;
};

} // namespace synth::songsteps
