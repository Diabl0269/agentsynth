// Concern: FRO240 (docs/control/midi-remote.md#replace-and-duplicate) -- retargeting a node's MIDI
// Remote project assignments onto a replacement node's uuid when "Replace with..." swaps it in
// (GraphEditor::replaceModule -> onModuleReplaced -> here). See MidiLearnController.h for the full
// contract, including why this mutates doc_ with no undo recording of its own.

#include "MidiRemote/MidiLearnController.h"

#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/AutomationBinding.h"

namespace synth::midi {

namespace {

// FRO253's own rule for which command a node supports: today only toggleSolo exists, and the only
// module that answers to it is a ChannelStripModule (MainComponentRemoteActionInvoker::
// invokeNodeCommand's own check) -- mirrored here rather than shared, since that invoker lives in
// the app layer and this is Core-adjacent (MidiLearnController links AppUI, not the reverse).
bool moduleSupportsNodeCommand(juce::AudioProcessor* processor, NodeCommandKind command) {
    if (command != NodeCommandKind::toggleSolo)
        return false; // the only member NodeCommandKind defines today
    return dynamic_cast<ChannelStripModule*>(processor) != nullptr;
}

} // namespace

bool MidiLearnController::retargetNode(const juce::String& oldNodeUuid, juce::AudioProcessorGraph::NodeID newNodeId) {
    if (oldNodeUuid.isEmpty())
        return false; // the replaced node never had MIDI Remote assignments pointing at it

    auto* newNode = engine_.getGraph().getNodeForId(newNodeId);
    if (newNode == nullptr)
        return false;

    // Assigned lazily -- and only once, below -- so a replace with nothing to retarget never
    // stamps a uuid onto a node that would otherwise go without one until its own first MIDI
    // Learn, same as ensureNodeUuid()'s every other caller.
    juce::String newNodeUuid;
    auto resolveNewNodeUuid = [this, newNodeId, &newNodeUuid] {
        if (newNodeUuid.isEmpty())
            newNodeUuid = ensureNodeUuid(newNodeId);
        return newNodeUuid;
    };

    bool changedAny = false;
    for (auto& assignment : doc_.assignments) {
        if (assignment.target.isParameter() && assignment.target.parameter.nodeUuid == oldNodeUuid) {
            const auto resolution =
                synth::resolveLaneParameter(newNode->getProcessor(), assignment.target.parameter.paramId, -1);
            if (!resolution.resolved())
                continue; // paramId doesn't exist on the new module -- leave it exactly as it was

            const auto uuid = resolveNewNodeUuid();
            if (uuid.isEmpty())
                continue; // new node isn't a real ModuleBase (shouldn't happen for a replace target)

            assignment.target.parameter.nodeUuid = uuid;
            assignment.target.parameter.paramIndexHint =
                synth::captureParamIndexHint(newNode->getProcessor(), assignment.target.parameter.paramId);
            changedAny = true;
        } else if (assignment.target.isNodeCommand() && assignment.target.nodeCommand.nodeUuid == oldNodeUuid) {
            if (!moduleSupportsNodeCommand(newNode->getProcessor(), assignment.target.nodeCommand.command))
                continue; // e.g. Solo on a strip replaced by a non-strip module -- leave as it was

            const auto uuid = resolveNewNodeUuid();
            if (uuid.isEmpty())
                continue;

            assignment.target.nodeCommand.nodeUuid = uuid;
            changedAny = true;
        }
    }

    // The doc mutation above is folded into the caller's own undo transaction (see this method's
    // header comment) -- but the ENGINE's live assignment cache is a separate copy
    // (RemoteEngine::setAssignments), so a retarget must republish it right away, same as every
    // other doc_ mutator here, or the just-replaced module keeps reading its OLD mapping (or
    // nothing at all) until an unrelated later graph change happens to reach reconcile().
    if (changedAny)
        publishAssignments();

    return changedAny;
}

} // namespace synth::midi
