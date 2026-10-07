// Concern: sizing AppUndoManager's snapshot steps (AppUndoManagerSnapshotSize.h). A step's size is the length of its
// states written out as JSON. The history asks it when the step is pushed, and again when the step is dropped past the
// size budget (every edit, once the budget is full); each answer used to write whole-project snapshots out afresh,
// right after the record call had written the same snapshots out to see whether anything changed. So the change check
// keeps its lengths for the steps pushed in the same call, and a step keeps its size once known.
#include "AppUndoManagerSnapshotSize.h"

namespace undo_size {

namespace {
MeasuredJson* active = nullptr;

// What a state is known by while a check is live: the object or array it holds (a snapshot var is never rebuilt while
// its step is pushed). A state holding neither is not remembered.
const void* identityOf(const juce::var& state) {
    if (auto* object = state.getDynamicObject())
        return object;
    return state.getArray();
}
} // namespace

MeasuredJson::MeasuredJson()
    : previous_(active) {
    active = this;
}

MeasuredJson::~MeasuredJson() { active = previous_; }

int MeasuredJson::measure(const juce::var& state, const juce::String& text) {
    if (const auto* id = identityOf(state))
        lengths_.emplace_back(id, text.length());
    return text.length();
}

bool MeasuredJson::differs(const juce::var& a, const juce::var& b) {
    const auto textA = juce::JSON::toString(a);
    const auto textB = juce::JSON::toString(b);
    measure(a, textA);
    measure(b, textB);
    return textA != textB;
}

int MeasuredJson::lengthOf(const juce::var& state) {
    const auto* id = identityOf(state);
    if (active == nullptr || id == nullptr)
        return -1;
    for (const auto& [known, length] : active->lengths_)
        if (known == id)
            return length;
    return -1;
}

int sizedOnce(int& memo, std::initializer_list<const juce::var*> states) {
    if (memo < 0) {
        int total = 0;
        for (const auto* state : states) {
            const int known = MeasuredJson::lengthOf(*state);
            total += known >= 0 ? known : juce::JSON::toString(*state).length();
        }
        memo = total;
    }
    return memo;
}

} // namespace undo_size
