// Lifecycle: construction/destruction, the source/profile/assignment/clock setters, lane
// bookkeeping, the drain timer's gating, and the two drain entry points (drain() applies events;
// drainActivity() mirrors every decoded event for the panel). See RemoteEngine.h's class comment
// for the three-thread split this file is one corner of.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"

#include <juce_core/juce_core.h>

namespace synth::midi {

const juce::String& hostSourceKey() noexcept {
    static const juce::String key("host");
    return key;
}

RemoteEngine::RemoteEngine() {
    // All kMaxRemoteSources lanes exist for the engine's whole life -- a lane index, once handed
    // out by laneIndexFor(), must always dereference to a real SourceLane (RemoteEvent.h's file
    // comment: an index is assigned once and never recycled).
    for (auto& lane : lanes_)
        lane = std::make_unique<SourceLane>();

    clock_ = [] { return juce::Time::getMillisecondCounterHiRes(); };
}

RemoteEngine::~RemoteEngine() {
    stopTimer();
    endAllGestures();
}

void RemoteEngine::setSources(const std::vector<juce::String>& sourceKeys) {
    sourceKeys_ = sourceKeys;
    for (const auto& key : sourceKeys)
        laneIndexFor(key); // ensure every current source has a lane assigned, in first-seen order
    rebuildAndPublish(nullptr);
}

void RemoteEngine::setProfiles(std::vector<ControllerProfile> profiles) {
    profiles_ = std::move(profiles);
    // FRO139: a profile's output device may just have changed (or the profile may have gained/lost
    // one) -- every "what did I last send" / cooldown fact feedback_ holds was computed against the
    // OLD output, so it's simplest and safest to forget all of it and let the next drain re-send
    // from scratch, rather than try to diff which assignments' profiles actually changed. Unlike
    // rebuildAndPublish's own per-publish feedback_ cleanup (RemoteEngineReconcile.cpp), this is not
    // "drop what's gone" -- it's "drop everything", because rebuildAndPublish runs after every graph
    // change too and must NOT do this (that would resend on every module you add).
    feedback_.clear();
    rebuildAndPublish(nullptr);
}

void RemoteEngine::setAssignments(std::vector<Assignment> assignments) {
    assignments_ = std::move(assignments);
    rebuildAndPublish(nullptr);
}

void RemoteEngine::setDefaultTakeover(Takeover takeover) {
    // Called on every settings-file write (a drag elsewhere in the app writes at frame rate), so an
    // unchanged value must not republish the snapshot; useDefault is meaningless as a default.
    if (takeover == Takeover::useDefault || takeover == defaultTakeover_)
        return;
    defaultTakeover_ = takeover;
    rebuildAndPublish(nullptr);
}

// FRO142 (docs/control/midi-remote.md#pages): getEffectivePageCount is a floor widened by whatever
// the CURRENT project assignments actually reference, so deleting the assignments on a profile's
// highest page (or loading a project that never used it) can shrink what this reports -- there is
// no ratchet here, unlike ControllerProfile::pageCount itself, which only ever grows through the
// UI's "+" button.
int RemoteEngine::getEffectivePageCount(const juce::String& profileId) const {
    int count = 1;
    for (const auto& profile : profiles_)
        if (profile.id == profileId)
            count = juce::jmax(count, profile.pageCount);
    for (const auto& assignment : assignments_)
        if (assignment.control.profileId == profileId)
            count = juce::jmax(count, assignment.page);
    return count;
}

// FRO142 (docs/control/midi-remote.md#pages): a page applies to PROJECT assignments only -- a
// GLOBAL profile action is active on every page (that is what keeps a page-switch button itself
// reachable from every page). A missing map entry means page 1, which is also what a profile the
// caller has never heard of gets -- there is no separate "unknown profile" answer to give.
int RemoteEngine::getActivePage(const juce::String& profileId) const {
    const auto found = activePages_.find(profileId);
    return found != activePages_.end() ? found->second : 1;
}

// Republishes if it actually changes (a setAssignments-style rebuild, not a graph reconcile) and
// re-sends feedback for the newly active page (see the .cpp comment on resendFeedback() below).
void RemoteEngine::setActivePage(const juce::String& profileId, int page) {
    const int clamped = juce::jlimit(1, getEffectivePageCount(profileId), page);
    if (clamped == getActivePage(profileId))
        return; // no-op: matches setDefaultTakeover's own "unchanged value never republishes" rule
    activePages_[profileId] = clamped;
    rebuildAndPublish(nullptr);
    // FRO142 (docs/control/midi-remote.md#pages): re-send feedback for the newly active page's
    // assignments -- resendFeedback() clears every cooldown/last-sent fact, so the very next drain
    // re-sends every mapped parameter's CURRENT value even where it hasn't moved, exactly the
    // "reopened device list" case this already existed for (FRO139).
    resendFeedback();
    if (onActivePageChanged)
        onActivePageChanged(profileId, clamped);
}

// Forgets every profile's active page (back to 1 for all). Call only from the project-load path --
// setAssignments() itself is ALSO the ordinary-edit path (Learn, Forget, undo/redo) and must not
// jump the user back to page 1 on every such edit, which is why this is a separate explicit call
// rather than something setAssignments() does itself.
void RemoteEngine::resetActivePages() { activePages_.clear(); }

void RemoteEngine::setClock(std::function<double()> clock) { clock_ = std::move(clock); }

void RemoteEngine::setFeedbackSink(RemoteFeedbackSink* sink) noexcept { feedbackSink_ = sink; }

void RemoteEngine::resendFeedback() { feedback_.clear(); }

int RemoteEngine::laneIndexFor(const juce::String& sourceKey) {
    const auto found = laneIndexByKey_.find(sourceKey);
    if (found != laneIndexByKey_.end())
        return found->second;
    if (nextLaneIndex_ >= kMaxRemoteSources)
        return -1; // a 17th controller is simply not routed -- see RemoteEvent.h's file comment
    const int index = nextLaneIndex_++;
    laneIndexByKey_.emplace(sourceKey, index);
    return index;
}

void RemoteEngine::updateTimerState() {
    const auto* snap = publisher_.live();
    const bool hasWork = (snap != nullptr && !snap->isEmpty()) || isLearnArmed();
    if (hasWork) {
        if (!isTimerRunning())
            startTimerHz(kDrainHz);
    } else {
        stopTimer();
    }
}

void RemoteEngine::timerCallback() { drain(); }

void RemoteEngine::drain() {
    const auto* snap = publisher_.live();
    if (snap == nullptr) {
        publisher_.collectRetired();
        return;
    }

    for (auto& lane : lanes_) {
        lane->events.drain([this, snap](const RemoteEvent& event) {
            // A republish between the MIDI-thread push and this drain invalidates the queued
            // slot/source indices: they were resolved against a table that no longer exists, and
            // reinterpreting them here would move the wrong parameter. Drop them.
            if (event.generation != snap->generation)
                return;
            if (event.kind == RemoteEventKind::learnCandidate) {
                const juce::String sourceKey = static_cast<std::size_t>(event.sourceIndex) < snap->sources.size()
                                                   ? snap->sources[event.sourceIndex].key
                                                   : juce::String();
                noteLearnCandidate(sourceKey, event);
            } else {
                applyEvent(*snap, event);
            }
        });
    }

    settleLearnIfDue();
    expireIdleGestures();
    sendFeedback(*snap);
    publisher_.collectRetired();
    updateTimerState();
}

void RemoteEngine::drainActivity(const std::function<void(const juce::String&, const RemoteEvent&)>& fn) {
    const auto* snap = publisher_.live();
    for (auto& lane : lanes_) {
        lane->activity.drain([&](const RemoteEvent& event) {
            const bool haveSourceKey =
                snap != nullptr && static_cast<std::size_t>(event.sourceIndex) < snap->sources.size();
            const juce::String sourceKey = haveSourceKey ? snap->sources[event.sourceIndex].key : juce::String();
            fn(sourceKey, event);
        });
    }
}

int RemoteEngine::activeGestureCount() const noexcept {
    int count = 0;
    for (const auto& entry : gestures_)
        if (entry.second.gestureActive)
            ++count;
    return count;
}

} // namespace synth::midi
