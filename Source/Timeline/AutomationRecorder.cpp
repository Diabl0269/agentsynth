#include "AutomationRecorder.h"
#include "../AppUndoManager.h"
#include "../Transport/TransportService.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace synth {

namespace {

constexpr int kOff = static_cast<int>(LaneRecordMode::Off);
constexpr int kTouch = static_cast<int>(LaneRecordMode::Touch);
constexpr int kLatch = static_cast<int>(LaneRecordMode::Latch);
constexpr int kWrite = static_cast<int>(LaneRecordMode::Write);

bool capturesGestures(int mode) noexcept { return mode == kTouch || mode == kLatch || mode == kWrite; }

// `param` may be a hosted plugin's own parameter, which has no NormalisableRange (a
// juce::HostedAudioProcessorParameter is a sibling hierarchy to RangedAudioParameter — see
// HostedPluginModule.h). Message thread only, so the dynamic_cast here is cheap and not worth
// caching; the listener callbacks use ParamListener::denormalise, which caches it at bind time.
double denormalisedValueOf(const juce::AudioProcessorParameter* param) noexcept {
    if (param == nullptr)
        return 0.0;
    if (const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*>(param))
        return static_cast<double>(ranged->convertFrom0to1(param->getValue()));
    // A hosted parameter's native domain is always 0..1 (JUCE's own host contract), which is exactly
    // what such a lane's RangeSnapshot is {0, 1, default} to match — see AutomationBinding.h.
    return static_cast<double>(param->getValue());
}

} // namespace

// Balances the re-entrancy depth counter that keeps unbindAll() from destroying the very listener
// object whose callback is currently on the stack.
struct AutomationRecorder::CallbackScope {
    explicit CallbackScope(std::atomic<int>& depth) noexcept
        : d(depth) {
        d.fetch_add(1, std::memory_order_acq_rel);
    }
    ~CallbackScope() noexcept { d.fetch_sub(1, std::memory_order_acq_rel); }
    std::atomic<int>& d;
};

// ------------------------------------------------------------------ param listener --

AutomationRecorder::ParamListener::ParamListener(AutomationRecorder& r, std::int64_t lane,
                                                 juce::AudioProcessorParameter* p) noexcept
    : recorder(r)
    , laneId(lane)
    , param(p)
    , ranged(dynamic_cast<const juce::RangedAudioParameter*>(p)) {}

double AutomationRecorder::ParamListener::denormalise(float normalised) const noexcept {
    return ranged != nullptr ? static_cast<double>(ranged->convertFrom0to1(normalised))
                             : static_cast<double>(normalised);
}

void AutomationRecorder::ParamListener::parameterValueChanged(int, float newValue) {
    CallbackScope scope(recorder.callbackDepth);

    // THE programmatic-write guard, and it has to be evaluated HERE rather than at drain time: a
    // ScopedProgrammaticApply around a preset load is long gone by the time the drain runs.
    if (recorder.isSuspended() || !recorder.isGlobalRecordEnabled())
        return;

    auto* t = recorder.transport.load(std::memory_order_relaxed);
    if (t == nullptr)
        return;
    const auto position = t->getPositionSnapshot();
    if (!position.playing)
        return;

    Event event;
    event.kind = EventKind::Value;
    event.laneId = laneId;
    event.beat = position.ppq;
    event.value = denormalise(newValue);
    recorder.postEvent(event);
}

void AutomationRecorder::ParamListener::parameterGestureChanged(int, bool starting) {
    CallbackScope scope(recorder.callbackDepth);

    auto* t = recorder.transport.load(std::memory_order_relaxed);
    const auto position = t != nullptr ? t->getPositionSnapshot() : TransportService::PositionSnapshot{};

    if (starting) {
        if (recorder.isSuspended() || !recorder.isGlobalRecordEnabled() || !position.playing)
            return; // a knob turned against a stopped transport records nothing: no span to put it on
    }
    // A gesture END is always posted, suspended or not: it closes whatever span an earlier start
    // opened, and a start that was suppressed leaves nothing for it to find.

    Event event;
    event.kind = starting ? EventKind::GestureStart : EventKind::GestureEnd;
    event.laneId = laneId;
    event.beat = position.ppq;
    event.value = starting && param != nullptr ? denormalise(param->getValue()) : 0.0;
    recorder.postEvent(event);

    // A gesture edge decides a claim and, for Touch, a commit — both want to happen NOW so the
    // applier stops fighting the hand on the very next block. Only the owner's thread may do that
    // work; a foreign-thread edge waits for the next update().
    if (recorder.isOwnerThread())
        recorder.drainEvents();
}

// ----------------------------------------------------------------------- ring --

AutomationRecorder::EventRing::EventRing(int capacity)
    : fifo(capacity)
    , slots(static_cast<std::size_t>(capacity)) {}

bool AutomationRecorder::EventRing::push(const Event& event, int reserved) noexcept {
    const juce::SpinLock::ScopedTryLockType producer(producerLock);
    if (!producer.isLocked())
        return false; // another producer is mid-push; dropping beats blocking a possible audio thread

    if (fifo.getFreeSpace() <= reserved)
        return false;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 < 1)
        return false; // full: drop rather than allocate or block

    slots[static_cast<std::size_t>(start1)] = event;
    fifo.finishedWrite(1);
    return true;
}

AutomationRecorder::AutomationRecorder() = default;

AutomationRecorder::~AutomationRecorder() {
    // Deliberately NOT detach(): a destructor must not commit into a TimelineDoc / AppUndoManager
    // whose own destruction may already be under way. Listeners are removed (each binding holds a
    // refcounted Node::Ptr precisely so the processor owning `param` is still alive here) and any
    // in-flight capture is dropped.
    for (auto& binding : bindings)
        if (binding.param != nullptr && binding.listener != nullptr)
            binding.param->removeListener(binding.listener.get());
    bindings.clear();

    for (auto& span : spans)
        releaseSlot(span.claimSlot);
    spans.clear();
    graveyard.clear();
}

// ---------------------------------------------------------------------- wiring --

void AutomationRecorder::attachTo(TimelineDoc& docIn, AppUndoManager& undoIn, TransportService& transportIn) {
    doc = &docIn;
    undo = &undoIn;
    transport.store(&transportIn, std::memory_order_relaxed);
    ownerThreadId.store(juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);
    lastPlaying = transportIn.getPositionSnapshot().playing;
}

void AutomationRecorder::detach() {
    audioState.globalRecordEnable.store(false, std::memory_order_relaxed);
    // Bindings first: once every listener is gone nothing can post another event, so the drain and
    // the commit below see a settled ring.
    unbindAll();
    drainEvents();
    commitAll(currentPpq());
    doc = nullptr;
    undo = nullptr;
    transport.store(nullptr, std::memory_order_relaxed);
}

void AutomationRecorder::bindLane(LaneId laneId, juce::AudioProcessorParameter* param,
                                  juce::AudioProcessorGraph::Node::Ptr node) {
    purgeGraveyard();

    if (!laneId.isValid() || param == nullptr)
        return;
    if (findBinding(laneId.value) != nullptr)
        return; // one binding per lane: a re-bind of the same lane is the owner's job to sequence

    Binding binding;
    binding.laneId = laneId.value;
    binding.param = param;
    binding.node = std::move(node);
    binding.listener = std::make_unique<ParamListener>(*this, laneId.value, param);
    param->addListener(binding.listener.get());
    bindings.push_back(std::move(binding));

    // A span that survived a rebind with a hand still on the knob has to re-claim, against the NEW
    // parameter pointer — unbindAll() dropped the old claim because that pointer was about to
    // become unverifiable, and the applier matches claims by pointer identity.
    if (auto* span = findSpan(laneId.value); span != nullptr && span->gestureActive && span->claimSlot < 0)
        span->claimSlot = claimSlotFor(param);
}

void AutomationRecorder::unbindAll() {
    for (auto& binding : bindings) {
        if (binding.param != nullptr && binding.listener != nullptr)
            binding.param->removeListener(binding.listener.get());
        if (binding.listener != nullptr)
            graveyard.push_back(std::move(binding.listener));
    }
    bindings.clear();

    // Claims name a raw juce::AudioProcessorParameter*. Once the binding that vouched for that
    // pointer is gone, the applier must not keep skipping a parameter on its word — the spans
    // themselves survive (an open Latch/Write take is not the business of an unrelated republish),
    // they just stop claiming until bindLane() re-establishes them. The index MUST be cleared with
    // the slot: a stale one would later release whatever OTHER parameter has since taken that slot,
    // and it is also what tells bindLane() this span still needs to re-claim.
    for (auto& span : spans) {
        releaseSlot(span.claimSlot);
        span.claimSlot = -1;
    }

    purgeGraveyard();
}

void AutomationRecorder::purgeGraveyard() {
    if (callbackDepth.load(std::memory_order_acquire) == 0)
        graveyard.clear();
}

// ---------------------------------------------------------------------- arming --

void AutomationRecorder::setGlobalRecordEnable(bool enabled) {
    if (isGlobalRecordEnabled() == enabled)
        return;

    if (!enabled) {
        // Flag down FIRST so nothing a re-entrant publish triggers can re-open a span behind us.
        audioState.globalRecordEnable.store(false, std::memory_order_relaxed);
        drainEvents();
        commitAll(currentPpq());
        return;
    }

    // Fresh arm: drop anything stale so a take can never inherit the previous one's tail.
    for (auto& span : spans)
        releaseSlot(span.claimSlot);
    spans.clear();
    messageEvents.reset();
    foreignEvents.reset();
    overrunFlag.store(false, std::memory_order_relaxed);
    audioState.globalRecordEnable.store(true, std::memory_order_relaxed);

    // Arming while already rolling counts as a play-start for Write lanes.
    lastPlaying = false;
    pollTransport();
}

// --------------------------------------------------------------------- driving --

void AutomationRecorder::update() {
    ownerThreadId.store(juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);
    purgeGraveyard();
    drainEvents();
    pollTransport();
}

void AutomationRecorder::pollTransport() {
    auto* t = transport.load(std::memory_order_relaxed);
    if (t == nullptr)
        return;

    const auto position = t->getPositionSnapshot();
    const bool playing = position.playing;
    const bool wasPlaying = lastPlaying;
    lastPlaying = playing;

    if (playing) {
        // Idempotent, and called on every poll rather than only on the transition, so a lane
        // switched to Write mid-playback (or a global arm mid-playback) starts its span too.
        if (isGlobalRecordEnabled())
            openWriteSpans(position.ppq);
        return;
    }

    if (!wasPlaying)
        return;

    // playing -> stopped. Order matters and is not the obvious one: the spans are TAKEN first (so
    // nothing is still in flight), then Write drops to Touch, and only THEN is the doc data written.
    // recordTimelineChange's before/after snapshots are whole-document, and lane.recordMode is part
    // of a document — dropping the mode after the commit would bake "was Write" into the undo step,
    // so undoing the take would silently re-arm the lane. Doing it first makes the mode identical on
    // both sides of the step, which is what "a mode flip is not undoable" has to mean here.
    drainEvents();
    std::vector<PendingCommit> commits;
    takeSpans(position.ppq, [](const Span&) { return true; }, commits);
    autoDropWriteLanes();
    applyCommits(commits);
}

void AutomationRecorder::openWriteSpans(double ppq) {
    for (const auto& binding : bindings) {
        if (laneRecordMode(binding.laneId) != kWrite)
            continue;
        if (findSpan(binding.laneId) != nullptr)
            continue;

        Span span;
        span.laneId = binding.laneId;
        span.mode = kWrite;
        span.startBeat = ppq;
        span.entryValue = denormalisedValueOf(binding.param);
        spans.push_back(std::move(span));
    }
}

void AutomationRecorder::autoDropWriteLanes() {
    if (doc == nullptr)
        return;

    // Collected first: setLaneRecordMode is a mutation, and a Track*/AutomationLane* from getTracks()
    // is only valid until the next one.
    std::vector<LaneId> toDrop;
    for (const auto& track : doc->getTracks())
        for (const auto& lane : track.lanes)
            if (lane.recordMode == kWrite)
                toDrop.push_back(lane.id);

    for (const auto laneId : toDrop)
        doc->setLaneRecordMode(laneId, kTouch);
}

// -------------------------------------------------------------- parameter events --

bool AutomationRecorder::isOwnerThread() const noexcept {
    const auto owner = ownerThreadId.load(std::memory_order_relaxed);
    return owner != nullptr && owner == juce::Thread::getCurrentThreadId();
}

void AutomationRecorder::postEvent(const Event& event) noexcept {
    auto& ring = isOwnerThread() ? messageEvents : foreignEvents;
    const int reserved = event.kind == EventKind::Value ? kGestureHeadroom : 0;
    if (!ring.push(event, reserved))
        overrunFlag.store(true, std::memory_order_relaxed);
}

void AutomationRecorder::drainEvents() {
    if (draining)
        return; // a commit re-entered us; the loop we are already inside will pick the rest up
    draining = true;

    // Both rings, because either can be the one a commit's re-entrant republish filled.
    for (;;) {
        const bool hadMessage = messageEvents.fifo.getNumReady() > 0;
        const bool hadForeign = foreignEvents.fifo.getNumReady() > 0;
        if (!hadMessage && !hadForeign)
            break;
        drainOneRing(messageEvents);
        drainOneRing(foreignEvents);
    }

    draining = false;
}

void AutomationRecorder::drainOneRing(EventRing& ring) {
    for (;;) {
        const int numReady = ring.fifo.getNumReady();
        if (numReady <= 0)
            break;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        ring.fifo.prepareToRead(numReady, start1, size1, start2, size2);

        // Copied out, and the read closed, before anything is applied: applyEvent commits, which
        // mutates the doc, which re-enters this class — the fifo must not be mid-read across that.
        // `drainScratch` is a member so a long take costs no allocation; the re-entrancy guard in
        // drainEvents() is what makes reusing it safe.
        drainScratch.clear();
        drainScratch.reserve(static_cast<std::size_t>(size1 + size2));
        for (int i = 0; i < size1; ++i)
            drainScratch.push_back(ring.slots[static_cast<std::size_t>(start1 + i)]);
        for (int i = 0; i < size2; ++i)
            drainScratch.push_back(ring.slots[static_cast<std::size_t>(start2 + i)]);
        ring.fifo.finishedRead(size1 + size2);

        for (const auto& event : drainScratch)
            applyEvent(event);
    }
}

void AutomationRecorder::applyEvent(const Event& event) {
    switch (event.kind) {
    case EventKind::GestureStart:
        openGestureSpan(event);
        break;
    case EventKind::GestureEnd:
        closeGestureSpan(event);
        break;
    case EventKind::Value:
        appendValue(event);
        break;
    }
}

void AutomationRecorder::openGestureSpan(const Event& event) {
    const int mode = laneRecordMode(event.laneId);
    if (!capturesGestures(mode))
        return;

    // Only for the claim, and re-read here rather than carried in the event: a republish between the
    // push and this drain hands the lane a new parameter pointer, and the applier matches by pointer.
    const auto* binding = findBinding(event.laneId);
    if (binding == nullptr || binding->param == nullptr)
        return;
    auto* param = binding->param;

    auto* span = findSpan(event.laneId);
    if (span == nullptr) {
        Span fresh;
        fresh.laneId = event.laneId;
        fresh.mode = mode;
        fresh.startBeat = event.beat;
        fresh.entryValue = event.value;
        spans.push_back(std::move(fresh));
        span = &spans.back();
    }

    span->gestureActive = true;
    if (span->claimSlot < 0)
        span->claimSlot = claimSlotFor(param);
}

void AutomationRecorder::closeGestureSpan(const Event& event) {
    auto* span = findSpan(event.laneId);
    if (span == nullptr)
        return;

    span->gestureActive = false;
    releaseSlot(span->claimSlot);
    span->claimSlot = -1;

    // Touch commits the moment the hand leaves the knob; Latch and Write hold the span open until
    // the transport stops (or the global arm goes down).
    if (span->mode != kTouch)
        return;

    const std::int64_t laneId = event.laneId;
    std::vector<PendingCommit> commits;
    takeSpans(event.beat, [laneId](const Span& s) { return s.laneId == laneId; }, commits);
    applyCommits(commits);
}

void AutomationRecorder::appendValue(const Event& event) {
    auto* span = findSpan(event.laneId);
    if (span == nullptr) {
        // Only Write opens a span without a gesture — that is the whole difference between Write and
        // Latch. Anything else with no span open is a programmatic write, or a take that committed
        // between the push and this drain; either way it is dropped.
        if (laneRecordMode(event.laneId) != kWrite)
            return;

        Span fresh;
        fresh.laneId = event.laneId;
        fresh.mode = kWrite;
        fresh.startBeat = event.beat;
        fresh.entryValue = event.value;
        spans.push_back(std::move(fresh));
        span = &spans.back();
    }

    if (static_cast<int>(span->captured.size()) >= kRingCapacity) {
        overrunFlag.store(true, std::memory_order_relaxed);
        return; // a span cannot grow without bound however long it stays open
    }
    span->captured.push_back({event.beat, event.value});
}

// ------------------------------------------------------------------- committing --

template <typename Predicate>
void AutomationRecorder::takeSpans(double endBeat, Predicate shouldCommit, std::vector<PendingCommit>& out) {
    for (std::size_t i = 0; i < spans.size();) {
        if (!shouldCommit(spans[i])) {
            ++i;
            continue;
        }

        Span& span = spans[i];
        releaseSlot(span.claimSlot);

        PendingCommit commit;
        commit.laneId = span.laneId;
        commit.mode = span.mode;
        commit.startBeat = span.startBeat;
        commit.entryValue = span.entryValue;
        commit.captured = std::move(span.captured);
        commit.endBeat = std::max(endBeat, commit.startBeat);
        if (!commit.captured.empty())
            commit.endBeat = std::max(commit.endBeat, commit.captured.back().beat);

        out.push_back(std::move(commit));
        spans.erase(spans.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void AutomationRecorder::commitAll(double endBeat) {
    std::vector<PendingCommit> commits;
    takeSpans(endBeat, [](const Span&) { return true; }, commits);
    applyCommits(commits);
}

void AutomationRecorder::applyCommits(std::vector<PendingCommit>& commits) {
    if (commits.empty() || doc == nullptr || undo == nullptr)
        return;

    // Moved out before a single doc byte changes: the mutation notifies listeners, which is how the
    // owner ends up calling unbindAll()/bindLane() from inside this call. Nothing about the capture
    // state may still be live at that point.
    const std::vector<PendingCommit> batch = std::move(commits);
    commits.clear();

    // ONE undo step for the batch — a Touch gesture end is a batch of one, a stop is a batch of
    // every span that was open, and undoing a stop should not take N presses of Cmd+Z.
    undo->recordTimelineChange(*doc, [this, &batch] {
        for (const auto& commit : batch)
            applyOneCommit(commit);
    });
}

void AutomationRecorder::applyOneCommit(const PendingCommit& commit) {
    const LaneId laneId{commit.laneId};
    const auto* lane = doc->getLane(laneId);
    if (lane == nullptr)
        return; // the lane was deleted while the take was rolling

    // Everything read off `lane` has to be read NOW: the pointer dies at the first mutation below.
    const double rangeSpan = static_cast<double>(lane->range.maxValue) - static_cast<double>(lane->range.minValue);
    const double epsilon = (std::isfinite(rangeSpan) && rangeSpan > 0.0) ? rangeSpan * kThinningEpsilonFraction : 0.0;

    std::vector<double> doomedBeats;
    for (const auto& point : lane->points)
        if (point.beat >= commit.startBeat && point.beat <= commit.endBeat)
            doomedBeats.push_back(point.beat);

    // Capture order is chronological, but a locate or a loop wrap mid-take can hand back a beat that
    // moved backwards — sort so the thinner and the lane both see a monotonic run, and collapse
    // same-beat duplicates the way a second addBreakpoint at that beat would (last wins).
    std::vector<CapturedPoint> ordered = commit.captured;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const CapturedPoint& a, const CapturedPoint& b) { return a.beat < b.beat; });
    std::vector<CapturedPoint> deduped;
    deduped.reserve(ordered.size());
    for (const auto& point : ordered) {
        if (!deduped.empty() && deduped.back().beat == point.beat)
            deduped.back() = point;
        else
            deduped.push_back(point);
    }

    const std::vector<CapturedPoint> thinned = thinPoints(deduped, epsilon);
    const bool isTouch = commit.mode == kTouch;

    // An empty Touch gesture (armed, grabbed, let go without moving) changes nothing at all — no
    // points removed, no undo step. Write and Latch DO write their span flat, because "overwrite
    // from here to stop" is what the user asked for even if the knob never moved.
    if (isTouch && thinned.empty())
        return;

    for (const double beat : doomedBeats)
        doc->removeBreakpoint(laneId, beat);

    // The span anchor. Write/Latch anchor at the value the parameter held when the span opened, so
    // the overwrite starts from where playback actually was; Touch anchors at its first captured
    // value, so letting go leaves no step at the grab point. A thinned point landing on the same
    // beat replaces this anchor — addBreakpoint's same-beat rule, and the reason it goes in first.
    const double anchorValue = isTouch ? thinned.front().value : commit.entryValue;
    doc->addBreakpoint(laneId, commit.startBeat, anchorValue, 0.0f, static_cast<int>(BreakpointCurve::Linear));

    for (const auto& point : thinned)
        doc->addBreakpoint(laneId, point.beat, point.value, 0.0f, static_cast<int>(BreakpointCurve::Linear));

    // Write/Latch close their span with a terminal anchor, so the overwritten region really ends at
    // the value the take left behind instead of ramping into whatever breakpoint survived past it.
    // Touch does not: releasing the knob is exactly the point at which the underlying automation
    // should take back over.
    if (!isTouch) {
        const double lastBeat = thinned.empty() ? commit.startBeat : thinned.back().beat;
        if (commit.endBeat > lastBeat) {
            const double lastValue = thinned.empty() ? commit.entryValue : thinned.back().value;
            doc->addBreakpoint(laneId, commit.endBeat, lastValue, 0.0f, static_cast<int>(BreakpointCurve::Linear));
        }
    }
}

// ----------------------------------------------------------------------- thinning --

std::vector<AutomationRecorder::CapturedPoint> AutomationRecorder::thinPoints(const std::vector<CapturedPoint>& points,
                                                                              double epsilon) {
    const std::size_t count = points.size();
    if (count <= 2)
        return points;

    std::vector<bool> keep(count, false);
    keep.front() = true;
    keep.back() = true;

    // Explicit stack instead of recursion: a take can carry 16k points, and RDP's worst case
    // recurses once per point.
    std::vector<std::pair<std::size_t, std::size_t>> pending;
    pending.reserve(32);
    pending.emplace_back(std::size_t{0}, count - 1);

    while (!pending.empty()) {
        const auto [first, last] = pending.back();
        pending.pop_back();
        if (last <= first + 1)
            continue;

        const double firstBeat = points[first].beat;
        const double firstValue = points[first].value;
        const double beatSpan = points[last].beat - firstBeat;
        const double valueSpan = points[last].value - firstValue;

        double worst = -1.0;
        std::size_t worstIndex = first;
        for (std::size_t i = first + 1; i < last; ++i) {
            // Vertical distance from the chord. A zero-width chord (every point on the same beat)
            // degenerates to the distance from the first value, which is the right answer for it.
            const double onChord =
                (beatSpan != 0.0) ? firstValue + valueSpan * ((points[i].beat - firstBeat) / beatSpan) : firstValue;
            const double distance = std::abs(points[i].value - onChord);
            if (distance > worst) {
                worst = distance;
                worstIndex = i;
            }
        }

        if (worst > epsilon) {
            keep[worstIndex] = true;
            pending.emplace_back(first, worstIndex);
            pending.emplace_back(worstIndex, last);
        }
    }

    std::vector<CapturedPoint> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        if (keep[i])
            out.push_back(points[i]);
    return out;
}

// ------------------------------------------------------------------------ claims --

int AutomationRecorder::claimSlotFor(const juce::AudioProcessorParameter* param) noexcept {
    if (param == nullptr)
        return -1;
    for (int i = 0; i < GestureClaims::kMaxSimultaneous; ++i) {
        if (audioState.claims.params[static_cast<std::size_t>(i)].load(std::memory_order_relaxed) == nullptr) {
            audioState.claims.params[static_cast<std::size_t>(i)].store(param, std::memory_order_relaxed);
            return i;
        }
    }
    return -1; // out of slots: the lane keeps playing back rather than the recorder growing a container
}

void AutomationRecorder::releaseSlot(int slot) noexcept {
    if (slot < 0 || slot >= GestureClaims::kMaxSimultaneous)
        return;
    audioState.claims.params[static_cast<std::size_t>(slot)].store(nullptr, std::memory_order_relaxed);
}

// ----------------------------------------------------------------------- lookups --

const AutomationRecorder::Binding* AutomationRecorder::findBinding(std::int64_t laneId) const noexcept {
    for (const auto& binding : bindings)
        if (binding.laneId == laneId)
            return &binding;
    return nullptr;
}

AutomationRecorder::Span* AutomationRecorder::findSpan(std::int64_t laneId) noexcept {
    for (auto& span : spans)
        if (span.laneId == laneId)
            return &span;
    return nullptr;
}

int AutomationRecorder::laneRecordMode(std::int64_t laneId) const {
    if (doc == nullptr)
        return kOff;
    const auto* lane = doc->getLane(LaneId{laneId});
    return lane != nullptr ? lane->recordMode : kOff;
}

double AutomationRecorder::currentPpq() const noexcept {
    auto* t = transport.load(std::memory_order_relaxed);
    return t != nullptr ? t->getPositionSnapshot().ppq : 0.0;
}

} // namespace synth
