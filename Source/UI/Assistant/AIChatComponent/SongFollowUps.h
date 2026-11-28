#pragma once

#include "AI/SongSteps/SongSteps.h"
#include <vector>

namespace synth::songsteps {

// The follow-up suggestions under a built song card: what a person might say next, offered as chips that put the text
// in the chat box (never sent for them: an edit costs a use). Always three, in sentence case and plain words: a change
// request, a change to the key or the arrangement, and a question. None names a song word, so a message typed from one
// is read as a change to this song and never as a request for a new one (AIChatComponent::shouldPlanSong). Pure and
// deterministic: the same plan always gives the same three.
inline std::vector<juce::String> songFollowUps(const SongPlan& plan) {
    return {"Make it darker", plan.key.isNotEmpty() ? "Change the key" : "Add another instrument",
            plan.bpm > 0 ? "Why did you pick this tempo?" : "Why did you pick these instruments?"};
}

} // namespace synth::songsteps
