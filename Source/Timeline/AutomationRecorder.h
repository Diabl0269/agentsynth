#pragma once

#include "TimelineDoc.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {

class TransportService; // Forward declaration (Source/Transport/TransportService.h)

// The set of parameters a user's hand is currently on, published to the audio thread.
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
 * @brief Captures parameter gestures into automation lanes. Every method here is MESSAGE THREAD
 *        ONLY; the parameter-listener callbacks are the one exception and run on any thread.
 *
 * The sibling of synth::MidiRecorder — same shape (arm, capture into a fixed ring, commit as one
 * undo step), different source: this reads juce::AudioProcessorParameter::Listener callbacks,
 * where a UI slider attachment's beginChangeGesture / setValueNotifyingHost / endChangeGesture
 * trio lands.
 *
 * -- The programmatic-write guard --
 * A preset load, an AI patch apply and an undo all push values into parameters with
 * setValueNotifyingHost, exactly like a knob drag does. If the recorder captured those, arming
 * record and loading a preset would silently overwrite every armed lane. So the primary guard is:
 * **a value change is only captured while a capture SPAN is open, and a span only ever opens from
 * a real gesture (Touch/Latch) or from the transport starting to play on a Write lane.** Nothing
 * that merely calls setValueNotifyingHost can open one.
 *
 * ScopedProgrammaticApply is the belt-and-braces second guard, for a writer that wraps its own
 * writes in gestures: while one is alive, even gestured events are dropped. MainComponent opens
 * one (via its ProgrammaticApplyScope) around preset load, New Patch, project open, the AI apply
 * span, and every undo/redo restore — see docs/architecture.md's "App wiring" section.
 *
 * -- Thread affinity --
 * A parameter listener is called on whatever thread wrote the parameter. Our own UI writes on the
 * message thread, but a host automating a HOSTED plugin's parameter calls straight from its AUDIO
 * thread (juce::AudioProcessorParameter::sendValueChangedMessageToListeners dispatches inline). So
 * ParamListener's two callbacks do nothing but read atomics, take the transport's seqlock snapshot,
 * denormalise through a pointer resolved at bind time, and push a POD Event into a ring. Nothing
 * else — no doc, no bindings, no spans, no allocation. All of that happens in drainEvents(), which
 * only update(), pollTransport() and the arming/detach paths call, on the message thread.
 *
 * Two rings, because juce::AbstractFifo is single-producer: one the message thread fills, one every
 * other thread fills (its pushes are serialised by a try-lock that never blocks). One consumer
 * drains both. A message-thread GESTURE edge drains inline, so a knob claims and commits with no
 * latency; value changes accumulate until the next drain, which is what bounds the ring.
 *
 * -- Re-entrancy --
 * A commit mutates the TimelineDoc, whose listeners drive AudioEngine::publishTimeline, whose
 * owner re-runs unbindAll() + bindLane() — all from INSIDE an inline drain, i.e. inside one of this
 * class's own parameter-listener callbacks. Two rules make that safe, and both are load-bearing:
 *   1. Capture state lives in `spans`, keyed by LaneId, NOT in `bindings`. Every commit path takes
 *      the pending commits out of the way BEFORE touching the doc, so a re-entrant
 *      unbindAll()/bindLane() finds nothing in flight to corrupt.
 *   2. unbindAll() moves detached listener objects to a graveyard drained only when no callback is
 *      on the stack. Destroying the listener whose method is currently executing is otherwise a
 *      use-after-free.
 *
 * -- No logging --
 * Not one line, anywhere in this file or its .cpp. A parameter drag produces a value change per
 * mouse move; the AIChatComponent Logger sink turns per-gesture logging into a multi-second UI
 * freeze (see CLAUDE.md).
 */
class AutomationRecorder {
public:
    AutomationRecorder();
    ~AutomationRecorder();

    // Capacity of each event ring. Parameter listeners are called on whatever thread wrote the
    // parameter — the message thread for our own UI, but a host is free to automate from its audio
    // thread — so the callback must be allocation-free, which is what the fixed ring buys. Overflow
    // drops the event and raises hadOverrun(); the take still commits, just thinner.
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
    // `param` is the base AudioProcessorParameter type, not RangedAudioParameter — a hosted
    // plugin's own parameter has no NormalisableRange (see HostedPluginModule.h /
    // Source/Timeline/AutomationBinding.h). Every method here that needs a denormalised value
    // (denormalisedValueOf) already branches on whether `param` is ALSO a RangedAudioParameter, so
    // one code path serves both kinds; this is all message-thread work, so that branch is a cheap
    // dynamic_cast, not something worth caching.
    //
    // `node` is held as a refcounted Node::Ptr for the same reason AutomationBindingTable does:
    // removeListener() in unbindAll()/the destructor must never touch a processor the graph has
    // already destroyed.
    void bindLane(LaneId laneId, juce::AudioProcessorParameter* param, juce::AudioProcessorGraph::Node::Ptr node);
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
    //
    // ANY THREAD. Both callbacks are allocation-free and lock-free and touch nothing but atomics,
    // the transport snapshot and one ring — see the thread-affinity note above. `ranged` is the
    // dynamic_cast done once here at bind time, so denormalising in the callback costs a branch.
    struct ParamListener : juce::AudioProcessorParameter::Listener {
        ParamListener(AutomationRecorder& r, std::int64_t lane, juce::AudioProcessorParameter* p) noexcept;

        void parameterValueChanged(int, float newValue) override;
        void parameterGestureChanged(int, bool starting) override;

        double denormalise(float normalised) const noexcept;

        AutomationRecorder& recorder;
        std::int64_t laneId = 0;
        juce::AudioProcessorParameter* param = nullptr;
        const juce::RangedAudioParameter* ranged = nullptr;
    };

    struct Binding {
        std::int64_t laneId = 0;
        juce::AudioProcessorParameter* param = nullptr; // widened from RangedAudioParameter*
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

    enum class EventKind : std::uint8_t { GestureStart, GestureEnd, Value };

    // What a listener callback pushes. Beat-stamped and denormalised at push time, not at drain
    // time: the lane stores denormalised values, and stamping in the callback means the ring never
    // has to be interpreted against a transport position or a binding table that moved since.
    // For a gesture start, `value` is the parameter's value as the hand landed on it.
    struct Event {
        std::int64_t laneId = 0;
        double beat = 0.0;
        double value = 0.0;
        EventKind kind = EventKind::Value;
    };

    // Slots a value event refuses to eat into, so a gesture edge always fits. A dropped value only
    // thins the take; a dropped gesture END would lose it entirely, because nothing else closes a
    // Touch span. Far more than the eight simultaneous gestures GestureClaims allows for.
    static constexpr int kGestureHeadroom = 64;

    // Fixed-capacity ring: one producer at a time plus one consumer, which is exactly what
    // juce::AbstractFifo requires. `producerLock` is only ever TRY-locked, so a push never blocks a
    // caller that may be an audio thread; a contended push drops the event like a full ring does.
    struct EventRing {
        explicit EventRing(int capacity);

        // ANY THREAD. Allocation-free, never blocks. False means dropped (full or contended).
        // `reserved` is the free space the push refuses to consume — see kGestureHeadroom.
        bool push(const Event& event, int reserved) noexcept;
        void reset() noexcept { fifo.reset(); }

        juce::AbstractFifo fifo;
        std::vector<Event> slots;
        juce::SpinLock producerLock;
    };

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

    // ANY THREAD. Routes to the ring owned by the calling thread's class.
    void postEvent(const Event& event) noexcept;
    // ANY THREAD. A plain id compare — juce::MessageManager::existsAndIsCurrentThread() takes a
    // mutex, and nothing on this path may block a caller that might be an audio thread.
    bool isOwnerThread() const noexcept;

    // MESSAGE THREAD. The whole capture state machine lives here: opening and closing spans,
    // reading record modes off the doc, and committing. Re-entrant calls are dropped — an inline
    // drain's own commit can reach setGlobalRecordEnable/detach, and a nested reader would corrupt
    // the fifo; the outer loop picks up whatever is left.
    void drainEvents();
    void drainOneRing(EventRing& ring);
    void applyEvent(const Event& event);
    void openGestureSpan(const Event& event);
    void closeGestureSpan(const Event& event);
    void appendValue(const Event& event);

    int claimSlotFor(const juce::AudioProcessorParameter* param) noexcept;
    void releaseSlot(int slot) noexcept;

    void purgeGraveyard();

    TimelineDoc* doc = nullptr;
    AppUndoManager* undo = nullptr;
    // Atomic because a listener callback on a foreign thread beat-stamps its events from the
    // transport's seqlock snapshot; every other read is message-thread.
    std::atomic<TransportService*> transport{nullptr};

    std::vector<Binding> bindings;
    std::vector<Span> spans;
    // Detached-but-not-yet-destroyed ParamListeners; drained only when callbackDepth == 0. Atomic
    // because a callback on a foreign thread bumps it; a graveyard entry has already been through
    // removeListener(), which juce serialises against dispatch, so a nonzero depth can only mean a
    // re-entrant callback on this thread — and skipping the purge for it is the whole point.
    std::vector<std::unique_ptr<ParamListener>> graveyard;
    std::atomic<int> callbackDepth{0};

    bool lastPlaying = false;
    bool draining = false;

    // Stamped by the message-thread entry points (attachTo/update), so a callback can tell which of
    // the two rings it owns without asking JUCE. Null until attachTo(), which runs long before any
    // binding exists to call back.
    std::atomic<juce::Thread::ThreadID> ownerThreadId{nullptr};

    AutomationRecordState audioState;
    std::atomic<int> suspendCount{0};
    std::atomic<bool> overrunFlag{false};

    // Same shape as MidiRecorder's ring: the listener callback writes, the message thread drains.
    // Split in two so each keeps a single producer — see the thread-affinity note above.
    EventRing messageEvents{kRingCapacity};
    EventRing foreignEvents{kRingCapacity};
    std::vector<Event> drainScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationRecorder)
};

} // namespace synth
