// Concern: sizing AppUndoManager's snapshot steps (AppUndoManagerSnapshotSize.h). A step's size is about the length of
// its states written out as JSON (synth::estimateJsonSize), counted once, when the step is pushed, and kept: the
// history asks again when it drops the step past its budget. Writing each state out to measure it cost as much as the
// whole project per edit; counting skips every node object and cable list the graph snapshot cache already counted,
// so a step costs only what its edit changed.
#include "AppUndoManagerSnapshotSize.h"

#include "AudioEngine/GraphSnapshotCache.h"

namespace undo_size {

namespace {
KnownSizes* active = nullptr;
} // namespace

KnownSizes::KnownSizes(std::function<int(const void*)> lookup)
    : lookup_(std::move(lookup))
    , previous_(active) {
    active = this;
}

KnownSizes::~KnownSizes() { active = previous_; }

int sizedOnce(int& memo, std::initializer_list<const juce::var*> states) {
    if (memo < 0) {
        const std::function<int(const void*)> none;
        const auto& known = active != nullptr ? active->lookup_ : none;
        int total = 0;
        for (const auto* state : states)
            total += synth::estimateJsonSize(*state, known);
        memo = total;
    }
    return memo;
}

} // namespace undo_size
