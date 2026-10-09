// Concern: AppUndoManager's mixer view (pin / hide) snapshot step.
#include "AppUndoManager.h"
#include "Mixer/MixerViewDoc.h"

namespace {

// Restores the MixerViewDoc from before/after snapshots; every var comes from the doc's own toVar(), so
// fromVar() must accept it. The initial edit is already applied when the action is pushed, so the first
// perform() only registers it.
class MixerViewSnapshotAction : public juce::UndoableAction {
public:
    MixerViewSnapshotAction(synth::MixerViewDoc& doc, const juce::var& beforeState, const juce::var& afterState,
                            std::function<void()> postRestore)
        : doc(doc)
        , beforeState(beforeState)
        , afterState(afterState)
        , postRestore(std::move(postRestore)) {}

    bool perform() override {
        if (firstPerform) {
            firstPerform = false;
            return true;
        }
        return restore(afterState);
    }

    bool undo() override { return restore(beforeState); }

    int getSizeInUnits() override {
        return static_cast<int>(juce::JSON::toString(beforeState).length() + juce::JSON::toString(afterState).length());
    }

private:
    bool restore(const juce::var& state) {
        const bool ok = doc.fromVar(state);
        jassert(ok);
        if (postRestore)
            postRestore();
        return ok;
    }

    synth::MixerViewDoc& doc;
    juce::var beforeState;
    juce::var afterState;
    std::function<void()> postRestore;
    bool firstPerform = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerViewSnapshotAction)
};

} // namespace

// A view edit changes what the saved project holds, so it goes through the undo manager like the pan law
// and macro collapse do: that is what marks the document unsaved (the edit serial is the one dirty-state
// funnel). Identical snapshots are a no-op edit and create no step.
bool AppUndoManager::recordMixerViewChange(synth::MixerViewDoc& doc, const juce::var& beforeJson,
                                           const juce::var& afterJson, std::function<void()> postRestore) {
    if (juce::JSON::toString(beforeJson) == juce::JSON::toString(afterJson))
        return false;

    beginTransaction();
    performAction(new MixerViewSnapshotAction(doc, beforeJson, afterJson, std::move(postRestore)));
    return true;
}
