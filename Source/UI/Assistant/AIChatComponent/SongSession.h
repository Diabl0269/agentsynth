#pragma once

#include "AI/AIProvider.h"
#include "AI/SongSteps/SongSteps.h"
#include <juce_core/juce_core.h>

namespace synth {

// Everything the two song cards show, kept in the chat message (MessageData::song) and never in a card: the chat
// rebuilds its bubbles on a redraw, so a card is only a view of this.
struct SongSession {
    enum class Phase { ready, replanning, building, built, failed, dismissed };

    juce::String requestText;
    songsteps::PlanReply reply;
    int lengthIndex = 1; // 0 short (2 min), 1 medium (3 min), 2 long (4+)
    int detailIndex = 1; // 0 light, 1 full, 2 rich
    Phase phase = Phase::ready;

    juce::String notice; // one line under the buttons: the plan timed out, a re-plan failed
    int usesLeft = -1;   // -1 when the account is unknown
    int usesLimit = -1;

    songsteps::BuildModel model;
    bool listOpen = false;
    bool cancelled = false;
    juce::String failureText;
    juce::uint32 buildStartMs = 0;
    juce::uint32 buildEndMs = 0;
    AIProvider::RequestId requestId;
    bool fadeInPending = true; // the card that takes this phase fades in once

    static const char* lengthName(int index) {
        static const char* names[] = {"short", "medium", "long"};
        return names[juce::jlimit(0, 2, index)];
    }
    static const char* detailName(int index) {
        static const char* names[] = {"light", "full", "rich"};
        return names[juce::jlimit(0, 2, index)];
    }

    /** The segment a plan falls under, so the card opens showing what the plan really is. */
    static int lengthIndexFor(const songsteps::SongPlan& plan) {
        const double seconds = plan.seconds();
        return seconds <= 150.0 ? 0 : (seconds <= 210.0 ? 1 : 2);
    }
    static int detailIndexFor(const songsteps::SongPlan& plan) {
        if (plan.tracks.size() >= 6 || plan.laneCount >= 10)
            return 2;
        return (plan.tracks.size() <= 4 && plan.laneCount <= 4) ? 0 : 1;
    }

    juce::uint32 elapsedMs(juce::uint32 nowMs) const { return (buildEndMs != 0 ? buildEndMs : nowMs) - buildStartMs; }
    /** Takes a new plan; the choices follow the plan only the first time, so a re-plan keeps what the user picked. */
    void adoptPlan(const songsteps::PlanReply& newReply, bool followPlan) {
        if (&newReply != &reply) // juce::var frees its object when assigned to itself
            reply = newReply;
        if (followPlan) {
            lengthIndex = lengthIndexFor(reply.plan);
            detailIndex = detailIndexFor(reply.plan);
        }
    }
};

/** "0:34" for a duration in milliseconds. */
inline juce::String formatClock(juce::uint32 ms) {
    const int seconds = (int)(ms / 1000);
    return juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2);
}

} // namespace synth
