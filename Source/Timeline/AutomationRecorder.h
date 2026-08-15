#pragma once

#include "TimelineDoc.h"
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {

class TransportService; // Forward declaration (Source/Transport/TransportService.h)

// TL4-4: the set of parameters a user's hand is currently on, published to the audio thread.
//
// Fixed slots and nothing but atomics, because AutomationApplier::applyBlock reads this on every
// block: a claimed parameter is one the applier must NOT write, so the knob the user is turning
// doesn't fight the playhead. The message thread (AutomationRecorder) is the only writer, and it
// only ever stores a parameter pointer or a null — there is no ABA problem to solve, so the loads
// are relaxed and the scan is branch-free enough to be free at eight slots.
//
// Eight simultaneous gestures is well past what a mouse, a trackpad and a hardware controller can
// produce together; a ninth simply doesn't get a slot, and the applier keeps playing that lane
// back. Failing that way round is deliberate — the alternative (an unbounded structure) would mean
// allocating on the message thread and reading a container the audio thread can see resize.
struct GestureClaims {
    static constexpr int kMaxSimultaneous = 8;

    std::array<std::atomic<const juce::AudioProcessorParameter*>, kMaxSimultaneous> params{};

    // AUDIO THREAD (and message thread). Allocation-free, lock-free, no logging.
    bool isClaimed(const juce::AudioProcessorParameter* param) const noexcept {
        if (param == nullptr)
            return false;
        for (const auto& slot : params)
            if (slot.load(std::memory_order_relaxed) == param)
                return true;
        return false;
    }
};

// The whole of AutomationRecorder the audio thread is allowed to see. Handed to the applier by
// pointer (see AudioEngine::setAutomationRecorder) so the applier never touches the recorder's
// message-thread state — the doc, the undo manager, the capture buffers — at all.
struct AutomationRecordState {
    GestureClaims claims;
    // The global R/W arm. Off means the recorder captures nothing AND every Write lane plays back
    // like a Read lane, which is what makes "disarm" a complete off switch rather than a half one.
    std::atomic<bool> globalRecordEnable{false};
};

/**
 * @brief TL4-4: captures parameter gestures into automation lanes. MESSAGE THREAD ONLY.
 *
 * The sibling of synth::MidiRecorder — same shape (arm, capture into a fixed ring, commit as one
 * undo step), different source: where MidiRecorder reads the audio thread's MIDI buffer, this reads
 * juce::AudioProcessorParameter::Listener callbacks, which is where a UI slider attachment's
 * beginChangeGesture / setValueNotifyingHost / endChangeGesture trio lands.
 *
 * -- The programmatic-write guard --------------------------------------------------------------
 *
 * A preset load, an AI patch apply and an undo all push values into parameters with
 * setValueNotifyingHost, exactly like a knob drag does. If the recorder captured those, arming
 * record and loading a preset would silently overwrite every armed lane with the preset's values.
 *
 * So the primary guard is: **a value change is only captured while a capture SPAN is open, and a
 * span only ever opens from a real gesture (Touch/Latch) or from the transport starting to play on
 * a Write lane.** Nothing that merely calls setValueNotifyingHost can open one. That single rule
 * covers every programmatic writer we have without any of them having to know this class exists.
 *
 * ScopedProgrammaticApply is the belt-and-braces second guard, for a future path that wraps its
 * writes in gestures (a UI control being driven programmatically, a macro-recall animating knobs):
 * while one is alive, even gestured events are dropped. Intended call sites — to be wired when the
 * app-level owner lands in TL5 — are PresetManager's load, AIStateMapper::applyJSONToGraph on the
 * apply path, and AppUndoManager's undo/redo restore.
 *
 * -- Re-entrancy -----------------------------------------------------------------------------------
 *
 * A commit mutates the TimelineDoc, whose listeners drive AudioEngine::publishTimeline, whose owner
 * re-runs unbindAll() + bindLane(). All of that can therefore run INSIDE one of this class's own
 * parameter-listener callbacks. Two design rules make that safe, and both are load-bearing:
 *
 *   1. Capture state lives in `spans`, keyed by LaneId, NOT in `bindings`. Every commit path resets
 *      the span state and then *takes* the pending commits out of the way BEFORE touching the doc,
 *      so a re-entrant unbindAll()/bindLane() finds nothing in flight to corrupt — and an open
 *      Latch/Write span survives a rebind instead of being silently dropped by an unrelated publish.
 *   2. unbindAll() does not destroy the per-parameter listener objects it just detached; it moves
 *      them to a graveyard that is only drained when no callback is on the stack. Destroying the
 *      listener whose method is currently executing is otherwise a use-after-free.
 *
 * -- No logging ------------------------------------------------------------------------------------
 *
 * Not one line, anywhere in this file or its .cpp. A parameter drag produces a value change per
 * mouse move; the AIChatComponent Logger sink turns per-gesture logging into a multi-second UI
 * freeze (see CLAUDE.md).
 */
class AutomationRecorder {
public:
    AutomationRecorder();
    ~AutomationRecorder();

    // Capacity of the {binding, beat, value} event ring. Parameter listeners are called on whatever
    // thread wrote the parameter — the message thread for our own UI, but a host is free to
    // automate from its audio thread — so the callback must be allocation-free, which is what the
    // fixed ring buys. Overflow drops the event and raises hadOverrun(); the take still commits.
    static constexpr int kRingCapacity = 16384;

    // RDP thinning tolerance, as a fraction of the lane's range span. 0.2% of a 20 Hz..20 kHz
    // cutoff lane is 40 Hz, which is inaudible at the top of the sweep and a fraction of a semitone
    // at the bottom — small enough to preserve gesture shape, large enough that a mouse drag's
    // hundreds of samples collapse to the handful of points a human would have drawn.
    static constexpr double kThinningEpsilonFraction = 0.002;

    // -- Wiring ----------------------------------------------------------------
    // Borrowed references, all three. They must outlive this recorder (or detach() must run first).
    void attachTo(TimelineDoc& doc, AppUndoManager& undo, TransportService& transport);
    // Commits anything open, drops every binding, and forgets the three references.
    void detach();

    // Registers this recorder as a listener on `param`, tying it to `laneId`. The OWNER rebuilds the
    // whole table after every AudioEngine::publishTimeline, exactly the way the applier's binding
    // table is rebuilt, because a lane's parameter is only resolvable against the current graph.
    //
    // `node` is held as a refcounted Node::Ptr for the same reason AutomationBindingTable does:
    // removeListener() in unbindAll()/the destructor must never touch a processor the graph has
    // already destroyed.
    void bindLane(LaneId laneId, juce::RangedAudioParameter* param, juce::AudioProcessorGraph::Node::Ptr node);
    void unbindAll();
    int getNumBindings() const noexcept { return static_cast<int>(bindings.size()); }

    // -- Arming ----------------------------------------------------------------
    // The global R/W switch. Turning it OFF commits every open span first, so disarming mid-take
    // keeps what was played rather than discarding it.
    void setGlobalRecordEnable(bool enabled);
    bool isGlobalRecordEnabled() const noexcept {
        return audioState.globalRecordEnable.load(std::memory_order_relaxed);
    }

    // -- Driving ---------------------------------------------------------------
    // MESSAGE THREAD. Polls the transport and drains the ring; a UI timer owns the call in the real
    // app, tests call it directly. Two transitions matter:
    //   stopped -> playing : opens a capture span on every armed Write lane (Write overwrites from
    //                        play to stop whether or not anything is touched).
    //   playing -> stopped : commits every open Latch/Write span, then AUTO-DROPS every Write lane
    //                        in the document to Touch — Cubase's rule, and the thing that stops a
    //                        forgotten Write mode from wiping the lane on the next playback.
    // The auto-drop is a plain setLaneRecordMode, deliberately NOT part of the commit's undo step:
    // undoing the take restores the data, never the arming.
    void update();

    // -- Audio-thread view ------------------------------------------------------
    const AutomationRecordState& getAudioState() const noexcept { return audioState; }

    // True if the ring overflowed since the last setGlobalRecordEnable(true). The take still
    // commits, just thinner than it was played.
    bool hadOverrun() const noexcept { return overrunFlag.load(std::memory_order_relaxed); }

    /**
     * @brief RAII suspension of ALL capture, gestured or not — the belt-and-braces programmatic
     *        write guard. Re-entrant (a counter, not a flag).
     */
    struct ScopedProgrammaticApply {
        explicit ScopedProgrammaticApply(AutomationRecorder& r) noexcept
            : recorder(r) {
            recorder.suspendCount.fetch_add(1, std::memory_order_relaxed);
        }
        ~ScopedProgrammaticApply() noexcept { recorder.suspendCount.fetch_sub(1, std::memory_order_relaxed); }

        ScopedProgrammaticApply(const ScopedProgrammaticApply&) = delete;
        ScopedProgrammaticApply& operator=(const ScopedProgrammaticApply&) = delete;

        AutomationRecorder& recorder;
    };

    bool isSuspended() const noexcept { return suspendCount.load(std::memory_order_relaxed) > 0; }

    // One captured {beat, denormalised value} pair. Exposed only because thinPoints() is worth
    // testing on its own — nothing else about the capture buffers is public.
    struct CapturedPoint {
        double beat = 0.0;
        double value = 0.0;
    };

    // Ramer-Douglas-Peucker, ITERATIVE (an explicit index stack, no recursion — a 16k-point take
    // that happens to be pathologically ordered must not blow the stack). Distance is VERTICAL, in
    // value units, not perpendicular: beats and parameter values have unrelated units, so a
    // perpendicular metric would silently change meaning with the lane's range and make `epsilon`
    // impossible to state as "0.2% of the range". Endpoints are always kept, order is preserved,
    // and a kept point keeps its EXACT captured value — corners of a gesture survive verbatim.
    static std::vector<CapturedPoint> thinPoints(const std::vector<CapturedPoint>& points, double epsilon);

private:
    // One parameter listener per binding. NOT the recorder itself: juce hands the callback only the
    // parameter's index WITHIN ITS OWN PROCESSOR, so two lanes bound to index 3 on two different
    // modules would be indistinguishable. A per-binding forwarder carries the identity instead.
    struct ParamListener : juce::AudioProcessorParameter::Listener {
        ParamListener(AutomationRecorder& r, std::int64_t lane) noexcept
            : recorder(r)
            , laneId(lane) {}

        void parameterValueChanged(int, float newValue) override { recorder.handleValueChanged(laneId, newValue); }
        void parameterGestureChanged(int, bool starting) override { recorder.handleGesture(laneId, starting); }

        AutomationRecorder& recorder;
        std::int64_t laneId;
    };

    struct Binding {
        std::int64_t laneId = 0;
        juce::RangedAudioParameter* param = nullptr;
        juce::AudioProcessorGraph::Node::Ptr node;
        std::unique_ptr<ParamListener> listener;
    };

    // RAII balance for callbackDepth — see purgeGraveyard() and the re-entrancy note above.
    struct CallbackScope;

    // An open capture span. Lives independently of `bindings` (see the re-entrancy note above) and
    // is erased when it commits.
    struct Span {
        std::int64_t laneId = 0;
        int mode = static_cast<int>(LaneRecordMode::Read); // the mode the span was opened in
        bool gestureActive = false;
        double startBeat = 0.0;
        double entryValue = 0.0; // the parameter's denormalised value when the span opened
        int claimSlot = -1;
        std::vector<CapturedPoint> captured;
    };

    // Everything one committing span needs, copied out before the doc is touched.
    struct PendingCommit {
        std::int64_t laneId = 0;
        int mode = static_cast<int>(LaneRecordMode::Read);
        double startBeat = 0.0;
        double endBeat = 0.0;
        double entryValue = 0.0;
        std::vector<CapturedPoint> captured;
    };

    // Denormalised at push time, not at drain time: the lane stores denormalised values, and doing
    // the conversion in the callback means the ring never has to be interpreted against a binding
    // table that may have been rebuilt since.
    struct Event {
        std::int64_t laneId = 0;
        double beat = 0.0;
        double value = 0.0;
    };

    // Called by ParamListener. Never mutate `bindings` from inside these without going through the
    // graveyard — see the class comment.
    void handleGesture(std::int64_t laneId, bool starting);
    void handleValueChanged(std::int64_t laneId, float normalised);

    const Binding* findBinding(std::int64_t laneId) const noexcept;
    Span* findSpan(std::int64_t laneId) noexcept;
    int laneRecordMode(std::int64_t laneId) const;
    double currentPpq() const noexcept;

    void pollTransport();
    void openWriteSpans(double ppq);
    // Moves every span matching the predicate into `out` and resets its claim. Never touches the doc.
    template <typename Predicate>
    void takeSpans(double endBeat, Predicate shouldCommit, std::vector<PendingCommit>& out);
    // The only doc-mutating path. One AppUndoManager::recordTimelineChange for the whole batch.
    void applyCommits(std::vector<PendingCommit>& commits);
    void applyOneCommit(const PendingCommit& commit);
    void commitAll(double endBeat);
    void autoDropWriteLanes();

    void drainRing();
    void pushEvent(const Event& event) noexcept;

    int claimSlotFor(const juce::AudioProcessorParameter* param) noexcept;
    void releaseSlot(int slot) noexcept;

    void purgeGraveyard();

    TimelineDoc* doc = nullptr;
    AppUndoManager* undo = nullptr;
    TransportService* transport = nullptr;

    std::vector<Binding> bindings;
    std::vector<Span> spans;
    // Detached-but-not-yet-destroyed ParamListeners; drained only when callbackDepth == 0.
    std::vector<std::unique_ptr<ParamListener>> graveyard;
    int callbackDepth = 0;

    bool lastPlaying = false;

    AutomationRecordState audioState;
    std::atomic<int> suspendCount{0};
    std::atomic<bool> overrunFlag{false};

    // Fixed-capacity SPSC ring, same shape as MidiRecorder's: the listener callback writes, the
    // message thread drains. Sized for a long take at mouse-move rate.
    juce::AbstractFifo ring{kRingCapacity};
    std::vector<Event> ringSlots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationRecorder)
};

} // namespace synth
