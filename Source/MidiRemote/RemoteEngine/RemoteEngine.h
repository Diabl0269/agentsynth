#pragma once

// synth::midi::RemoteEngine — the MIDI Remote engine (docs/midi_remote.md §6).
//
// Three threads meet here and the split is the whole design:
//
//   MIDI / audio thread   handleMessage(): look the message up in an immutable snapshot, decode it,
//                         push one POD onto this source's lock-free lane, answer consumed/not.
//                         NOTHING ELSE. No lock, no allocation, no logging, no juce::String
//                         construction, no AsyncUpdater (triggerAsyncUpdate takes a CriticalSection
//                         and allocates — see the note on the drain timer below).
//   message thread        drain(): apply events to parameters exactly as a mouse would
//                         (beginChangeGesture / setValueNotifyingHost / endChangeGesture), invoke
//                         action commands, resolve learns, free retired snapshots.
//   message thread        setProfiles / setAssignments / setSources / reconcile: rebuild the
//                         snapshot and publish it by atomic pointer swap.
//
// LIFETIME: the app layer must clear AudioEngine's RemoteMessageSink and let
// AudioEngine::setRemoteMessageSink(nullptr) return before destroying the engine. That call drains
// BOTH halves of the handshake -- drainAudioCallbacks() for renderNextBlock's render passes, and
// drainRemoteSinkCalls() for the two sink reads that happen OUTSIDE a render pass (the standalone
// MIDI driver thread and the hosted pre-render sink loop, FRO197) -- so once it returns, no reader
// can run again, which is what makes SnapshotPublisher's destructor safe.
//
// NOT YET REACHABLE BY A USER: nothing in the shipped UI can create an assignment until the
// right-click MIDI Learn ticket lands. The engine is wired end to end and covered by tests that
// drive AudioEngine::handleIncomingMidiMessage for real.

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "MidiRemote/RemoteEngine/RemoteMappingSnapshot.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "MidiRemote/RemoteModel.h"

#include <array>
#include <atomic>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <map>
#include <memory>
#include <vector>

namespace synth::midi {

/** endChangeGesture fires this long after the last event for an assignment, so one slow knob sweep
 *  is one undo step and one automation touch (docs/midi_remote.md §4.2). */
inline constexpr double kGestureIdleMs = 250.0;
/** A learn binds the message key seen most often in this window after the first eligible message
 *  (docs/midi_remote.md §4.5). */
inline constexpr double kLearnSettleMs = 300.0;
/** A learn with no eligible message for this long cancels itself. */
inline constexpr double kLearnTimeoutMs = 10000.0;
/** Drain rate. One frame of latency on a knob; see §4.2 on why that is the right trade. */
inline constexpr int kDrainHz = 60;
/** One hardware tick of a relative encoder moves a parameter by this much (docs/midi_remote.md
 *  §6 "relative -> signed delta x sensitivity"). */
inline constexpr float kRelativeSensitivity = 1.0f / 127.0f;

/** How the engine reaches an action target. Implemented in the app layer over
 *  juce::ApplicationCommandManager — Core never sees MainComponent. */
class RemoteActionInvoker {
public:
    virtual ~RemoteActionInvoker() = default;
    /** MESSAGE THREAD. Invoke synchronously, as a menu item or a keypress would. */
    virtual void invokeRemoteCommand(juce::CommandID commandId) = 0;
};

/** Resolves a ShortcutManager action id to its juce::CommandID. Injected for the same reason. */
using ActionCommandLookup = std::function<juce::CommandID(const juce::String& actionId)>;

/** What a learn is armed on, and what it produced. */
struct LearnRequest {
    Target target;
    /** Bool parameters and actions prefer note-on / CC-0-or-127 and infer buttonMode from what the
     *  hardware does on release (docs/midi_remote.md §4.5). */
    bool buttonLike = false;
};

struct LearnResult {
    juce::String sourceKey;
    MessageSpec spec;
    Encoding encoding = Encoding::abs7;
    ButtonMode buttonMode = ButtonMode::momentary;
    Target target;
};

class RemoteEngine final
    : public RemoteMessageSink
    , private juce::Timer {
public:
    RemoteEngine();
    ~RemoteEngine() override;

    // ---- MIDI / audio thread -------------------------------------------------------------------

    bool handleMessage(const juce::String& sourceKey, const juce::MidiMessage& message) noexcept override;

    // ---- Message thread: inputs ----------------------------------------------------------------

    /** Every source the engine may hear from: each open juce::MidiInput's identifier, plus
     *  hostSourceKey() in HostMode::Hosted. A key first seen here is given a lane for the engine's
     *  lifetime. A message from a key that is not in the current snapshot is ignored (never
     *  consumed) — that only happens in the window between a device opening and this call. */
    void setSources(const std::vector<juce::String>& sourceKeys);

    /** The global controller profiles (surfaces, passMapped, and action assignments). */
    void setProfiles(std::vector<ControllerProfile> profiles);

    /** The project's parameter assignments (the "midiRemote" project key). */
    void setAssignments(std::vector<Assignment> assignments);

    /** Preferences' default takeover, used by assignments set to Takeover::useDefault. Scale until
     *  the Preferences UI ships. */
    void setDefaultTakeover(Takeover takeover);

    void setActionInvoker(RemoteActionInvoker* invoker) noexcept { actionInvoker_ = invoker; }

    /** Returns true while something *other* than this engine holds the parameter — a real mouse
     *  drag, via AutomationRecorder's gesture claim. The engine then yields exactly as a second
     *  mouse would (docs/midi_remote.md §4.2). Injected so Core keeps no AutomationRecorder
     *  dependency. */
    void setParameterClaimedPredicate(std::function<bool(const juce::AudioProcessorParameter*)> pred) {
        isClaimedByOther_ = std::move(pred);
    }
    void setActionCommandLookup(ActionCommandLookup lookup) { actionLookup_ = std::move(lookup); }

    /** Test seam: milliseconds, monotonic. Defaults to juce::Time::getMillisecondCounterHiRes, so
     *  tests drive the 250 ms gesture idle and the 300 ms settle window without sleeping. */
    void setClock(std::function<double()> clock);

    // ---- Message thread: the graph -------------------------------------------------------------

    /** Rebuild every slot's live target through synth::resolveLaneParameter and republish. Called
     *  from MainComponent's reconcile funnel after any graph change; orphans what no longer
     *  resolves and never silently rebinds
     * (docs/architecture/app-wiring.md#app-wiring--who-owns-the-timeline-and-every-hook-that-keeps-it-in-step). */
    void reconcile(juce::AudioProcessorGraph& graph);

    // ---- Message thread: learn -----------------------------------------------------------------

    void armLearn(const LearnRequest& request);
    void cancelLearn();
    bool isLearnArmed() const noexcept { return learnToken_.load(std::memory_order_seq_cst) != 0; }
    /** Fired on the message thread from drain() when a learn settles. */
    std::function<void(const LearnResult&)> onLearned;

    // ---- Message thread: the drain -------------------------------------------------------------

    /** Apply every queued event, expire idle gestures, settle a pending learn, free retired
     *  snapshots. The timer calls this at kDrainHz; tests call it directly. */
    void drain();

    /** Live activity for the panel's surface: every decoded event, assigned or not. Drained
     *  separately from the apply path so the panel can never steal an event. */
    void drainActivity(const std::function<void(const juce::String& sourceKey, const RemoteEvent&)>& fn);

    // ---- Diagnostics (tests) -------------------------------------------------------------------

    const SnapshotPublisher& publisher() const noexcept { return publisher_; }
    int activeGestureCount() const noexcept;

private:
    void timerCallback() override;

    // Snapshot construction (RemoteEngineReconcile.cpp).
    void rebuildAndPublish(juce::AudioProcessorGraph* graph);
    int laneIndexFor(const juce::String& sourceKey);
    /** True when there is something worth ticking for: any slot, or an armed learn. Nothing wakes
     *  the message thread from the MIDI path, so the timer — not an AsyncUpdater — is what makes an
     *  idle app respond within a frame. Both conditions are message-thread facts. */
    void updateTimerState();

    // Apply (RemoteEngineApply.cpp).
    void applyEvent(const RemoteMappingSnapshot& snapshot, const RemoteEvent& event);
    void applyToParameter(const RemoteMappingSnapshot::Slot& slot, const RemoteEvent& event);
    void applyToAction(const RemoteMappingSnapshot::Slot& slot, const RemoteEvent& event);
    void expireIdleGestures();
    void endAllGestures();

    // Learn (RemoteEngineLearn.cpp).
    void noteLearnCandidate(const juce::String& sourceKey, const RemoteEvent& event);
    void settleLearnIfDue();

    /** Per-assignment apply state. Message thread only; keyed by assignment id so an in-flight
     *  sweep survives a republish. */
    struct GestureState {
        bool gestureActive = false;
        double lastEventMs = 0.0;
        float lastValue = 0.0f;
        /** Pick-up: false until the hardware has crossed the parameter's value. Scale: unused. */
        bool takeoverEngaged = false;
        bool toggleOn = false;
        juce::AudioProcessorParameter* param = nullptr;
    };

    /** One in-flight learn's tally. Message thread only — the MIDI path only ever pushes a
     *  learnCandidate event, per the §4.4 tripwire. */
    struct LearnTally {
        juce::String sourceKey;
        MessageSpec spec;
        int count = 0;
        bool sawRelease = false;
        float minValue = 1.0f;
        float maxValue = 0.0f;
    };

    SnapshotPublisher publisher_;
    std::array<std::unique_ptr<SourceLane>, kMaxRemoteSources> lanes_;
    std::map<juce::String, int> laneIndexByKey_; // message thread only; never recycles an index
    int nextLaneIndex_ = 0;
    /** The last setSources() argument, verbatim — rebuildAndPublish's source list is exactly this,
     *  not the cumulative union laneIndexByKey_ holds (that never shrinks by design). */
    std::vector<juce::String> sourceKeys_;

    /** Bumped on every publish; stamped into every RemoteEvent so a stale queued index is
     *  discarded rather than applied to a shifted table. Starts at 1 (0 means "no snapshot"). */
    std::uint32_t snapshotGeneration_ = 0;
    std::vector<ControllerProfile> profiles_;
    std::vector<Assignment> assignments_;
    Takeover defaultTakeover_ = Takeover::scale;

    std::map<juce::String, GestureState> gestures_;

    /** 0 == disarmed. The only learn state the MIDI path reads, and it is one word. */
    std::atomic<std::uint32_t> learnToken_{0};
    std::uint32_t nextLearnToken_ = 1;
    LearnRequest learnRequest_;
    std::vector<LearnTally> learnTallies_;
    double learnFirstEventMs_ = 0.0;
    double learnArmedMs_ = 0.0;

    RemoteActionInvoker* actionInvoker_ = nullptr;
    ActionCommandLookup actionLookup_;
    std::function<bool(const juce::AudioProcessorParameter*)> isClaimedByOther_;
    std::function<double()> clock_;

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the learn-armed check runs on the MIDI path");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteEngine)
};

} // namespace synth::midi
