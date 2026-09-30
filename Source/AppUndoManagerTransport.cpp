// Concern: AppUndoManager's transport (tempo, time signature, loop) undo step.
#include "AppUndoManager.h"
#include "Transport/TransportDoc.h"

namespace {

// Same firstPerform convention as MixerPanLawAction: the initial edit is already applied when the action is
// pushed, so the first perform() only registers it; undo applies `before`, redo applies `after`.
class TransportDocAction : public juce::UndoableAction {
public:
    TransportDocAction(std::function<void(const synth::TransportDoc&)> apply, const synth::TransportDoc& before,
                       const synth::TransportDoc& after)
        : apply(std::move(apply))
        , before(before)
        , after(after) {}

    bool perform() override {
        if (firstPerform) {
            firstPerform = false;
            return true;
        }
        apply(after);
        return true;
    }

    bool undo() override {
        apply(before);
        return true;
    }

    int getSizeInUnits() override { return 1; }

private:
    std::function<void(const synth::TransportDoc&)> apply;
    synth::TransportDoc before;
    synth::TransportDoc after;
    bool firstPerform = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportDocAction)
};

} // namespace

// A tempo, signature or loop edit changes what the saved project holds, so it goes through the undo manager:
// that bumps the edit serial, the one dirty-state funnel.
void AppUndoManager::recordTransportChange(std::function<void(const synth::TransportDoc&)> apply,
                                           const synth::TransportDoc& before, const synth::TransportDoc& after) {
    if (before == after)
        return;

    undoManager.beginNewTransaction();
    performAction(new TransportDocAction(std::move(apply), before, after));
}
