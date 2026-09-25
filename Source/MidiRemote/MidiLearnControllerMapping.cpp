// Concern: FRO135 mapping assistant -- assigning a chosen control to a target from the panel,
// Forget by assignment id, and the orphan-controller repairs Re-link / Recreate
// (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn, #controllers-list-left).
// The pick-target session lives in MidiLearnControllerPick.cpp.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/ContinuousTarget.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemote/MidiRemoteMapping.h"
#include "Modules/ModuleBase.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/AutomationBinding.h"
#include "UI/Chrome/StatusBarComponent.h"
#include <algorithm>

namespace synth::midi {

namespace {

bool sameProjectTarget(const synth::Target& a, const synth::Target& b) {
    if (a.isParameter() && b.isParameter())
        return a.parameter.nodeUuid == b.parameter.nodeUuid && a.parameter.paramId == b.parameter.paramId;
    if (a.isNodeCommand() && b.isNodeCommand())
        return a.nodeCommand.nodeUuid == b.nodeCommand.nodeUuid && a.nodeCommand.command == b.nodeCommand.command;
    return false;
}

} // namespace

// A profile edit or a project edit, depending on the target: an action assignment is GLOBAL (the
// profile's `actions`, not undoable -- docs/control/midi-remote.md#undo), a parameter or node
// command is PROJECT scope (undoable via recordMidiRemoteChange). Assigning replaces whatever the
// target was mapped to before AND whatever the control drove in the same scope (one project
// assignment and one global assignment per control for now).
AssignStatus MidiLearnController::assignControl(const juce::String& profileId, const juce::String& controlId,
                                                const PickTarget& pick) {
    const auto* profile = findProfile(profileId);
    if (profile == nullptr)
        return AssignStatus::unknownControl;
    const auto controlIt = std::find_if(profile->controls.begin(), profile->controls.end(),
                                        [&](const Control& c) { return c.id == controlId; });
    if (controlIt == profile->controls.end())
        return AssignStatus::unknownControl;
    const Control control = *controlIt;

    synth::Target target;
    juce::String targetName;
    if (pick.kind == PickTarget::Kind::action) {
        if (AppCommands::getCommandForAction(pick.actionId) == AppCommands::kNoCommand)
            return AssignStatus::invalidAction;
        target.kind = synth::Target::Kind::action;
        target.action.actionId = pick.actionId;
        targetName = ShortcutManager::getActionDescription(pick.actionId);
    } else if (pick.kind == PickTarget::Kind::continuous) {
        target.kind = synth::Target::Kind::continuous;
        target.continuous.kind = pick.continuous;
        targetName = synth::continuousTargetDisplayName(pick.continuous);
    } else {
        const juce::String uuid = ensureNodeUuid(pick.nodeId);
        if (uuid.isEmpty())
            return AssignStatus::unresolvedTarget;
        if (pick.kind == PickTarget::Kind::nodeCommand) {
            target.kind = synth::Target::Kind::nodeCommand;
            target.nodeCommand.nodeUuid = uuid;
            target.nodeCommand.command = pick.command;
            targetName = "Solo";
        } else {
            juce::RangedAudioParameter* param = nullptr;
            juce::AudioProcessor* processor = nullptr;
            if (auto* node = engine_.getGraph().getNodeForId(pick.nodeId)) {
                processor = node->getProcessor();
                for (auto* p : processor->getParameters())
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
                        ranged && ranged->paramID == pick.paramId)
                        param = ranged;
            }

            // FRO137: not one of this node's own RangedAudioParameters -- fall back to the same
            // hosted-plugin resolution an automation lane uses
            // (docs/control/plugin-card-layout.md#interaction-with-midi-remote-and-automation), so
            // the pick-target overlay/panel can also assign a plugin-card knob.
            juce::AudioProcessorParameter* hostedParam = nullptr;
            int paramIndexHint = -1;
            if (param == nullptr && processor != nullptr) {
                const auto resolution = synth::resolveLaneParameter(processor, pick.paramId, -1);
                hostedParam = resolution.liveParameter();
                if (hostedParam != nullptr)
                    paramIndexHint = synth::captureParamIndexHint(processor, pick.paramId);
            }

            if (param == nullptr && hostedParam == nullptr)
                return AssignStatus::unresolvedTarget;
            target.kind = synth::Target::Kind::parameter;
            target.parameter.nodeUuid = uuid;
            target.parameter.paramId = pick.paramId;
            target.parameter.paramIndexHint = paramIndexHint;
            targetName = param != nullptr ? param->getName(100) : hostedParam->getName(100);
        }
    }

    const Assignment assignment = makeAssignmentForControl(*profile, control, target);

    // FRO236: a continuous target is GLOBAL, exactly like an action (docs/control/midi-remote.md#continuous-targets
    // -- it means the same thing in every project), so it shares this branch and mirrors the action
    // rule verbatim, just keyed on ContinuousTargetKind instead of an actionId.
    if (target.isAction() || target.isContinuous()) {
        ControllerProfile updated = *profile;
        auto& actions = updated.actions;
        actions.erase(std::remove_if(actions.begin(), actions.end(),
                                     [&](const Assignment& a) {
                                         // One global assignment per control, whatever its kind.
                                         if (a.control.controlId == controlId)
                                             return true;
                                         if (target.isAction())
                                             return a.target.isAction() && a.target.action.actionId == pick.actionId;
                                         return a.target.isContinuous() && a.target.continuous.kind == pick.continuous;
                                     }),
                      actions.end());
        actions.push_back(assignment);
        updateProfile(updated); // saves, republishes to the engine and notifies onChanged
    } else {
        const juce::var beforeJson = doc_.toVar();
        auto& assignments = doc_.assignments;
        assignments.erase(std::remove_if(assignments.begin(), assignments.end(),
                                         [&](const Assignment& a) {
                                             return sameProjectTarget(a.target, target) ||
                                                    (a.control.profileId == profileId &&
                                                     a.control.controlId == controlId);
                                         }),
                          assignments.end());
        assignments.push_back(assignment);
        const bool controllerKnown = std::any_of(doc_.controllers.begin(), doc_.controllers.end(),
                                                 [&](const auto& ref) { return ref.profileId == profileId; });
        if (!controllerKnown)
            doc_.controllers.push_back({profileId, profile->name});
        const juce::var afterJson = doc_.toVar();
        undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
        publishAssignments();
    }

    statusBar_.showMessage("'" + control.name + "' now drives " + targetName);
    return AssignStatus::assigned;
}

// By assignment id, so it works for an orphaned target too (the node is gone, so forget(nodeId,
// paramId) has nothing to resolve) -- "Forget" is the only action an orphan node offers.
bool MidiLearnController::forgetAssignment(const juce::String& assignmentId) {
    auto& assignments = doc_.assignments;
    const auto it =
        std::find_if(assignments.begin(), assignments.end(), [&](const Assignment& a) { return a.id == assignmentId; });
    if (it != assignments.end()) {
        const juce::var beforeJson = doc_.toVar();
        assignments.erase(it);
        const juce::var afterJson = doc_.toVar();
        undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
        publishAssignments();
        statusBar_.showMessage("MIDI mapping removed");
        return true;
    }

    for (auto& profile : profiles_) {
        auto& actions = profile.actions;
        const auto sizeBefore = actions.size();
        actions.erase(
            std::remove_if(actions.begin(), actions.end(), [&](const Assignment& a) { return a.id == assignmentId; }),
            actions.end());
        if (actions.size() == sizeBefore)
            continue;
        profileStore_.save(profile);
        remoteEngine_.setProfiles(profiles_);
        statusBar_.showMessage("MIDI mapping removed");
        if (onChanged)
            onChanged();
        return true;
    }
    return false;
}

RelinkOutcome MidiLearnController::relinkController(const juce::String& orphanProfileId,
                                                    const juce::String& targetProfileId) {
    RelinkOutcome outcome;
    const auto* target = findProfile(targetProfileId);
    if (target == nullptr || orphanProfileId == targetProfileId || findProfile(orphanProfileId) != nullptr)
        return outcome;
    outcome.ok = true;

    const juce::var beforeJson = doc_.toVar();
    const auto result = relinkAssignments(doc_.assignments, orphanProfileId, *target);
    outcome.matched = result.matched;
    outcome.unmatched = result.unmatched;
    if (result.matched == 0)
        return outcome; // nothing changed, nothing to record

    if (std::none_of(doc_.controllers.begin(), doc_.controllers.end(),
                     [&](const auto& ref) { return ref.profileId == targetProfileId; }))
        doc_.controllers.push_back({targetProfileId, target->name});
    if (result.unmatched == 0)
        doc_.controllers.erase(std::remove_if(doc_.controllers.begin(), doc_.controllers.end(),
                                              [&](const auto& ref) { return ref.profileId == orphanProfileId; }),
                               doc_.controllers.end());

    const juce::var afterJson = doc_.toVar();
    undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
    publishAssignments();

    juce::String message = "Re-linked " + juce::String(result.matched) + " of " +
                           juce::String(result.matched + result.unmatched) + " assignments to " + target->name;
    if (result.unmatched > 0)
        message << "; " << result.unmatched << " stay orphaned (no control with the same message)";
    statusBar_.showMessage(message);
    return outcome;
}

// The profile half (a new controller file) is not undoable, like every profile edit; the project
// half -- assignments repointed at the new profile, the orphan reference swapped for it -- is one
// undo step, and undoing it puts the orphan back while the minted profile stays.
juce::String MidiLearnController::recreateController(const juce::String& orphanProfileId,
                                                     const ControllerProfile::Input& device) {
    if (findProfile(orphanProfileId) != nullptr)
        return {};
    const auto refIt = std::find_if(doc_.controllers.begin(), doc_.controllers.end(),
                                    [&](const auto& ref) { return ref.profileId == orphanProfileId; });
    if (refIt == doc_.controllers.end())
        return {};

    const juce::var beforeJson = doc_.toVar();
    const auto profile = recreateProfileFromAssignments(doc_.assignments, orphanProfileId, refIt->name, device);
    if (!addProfile(profile)) {
        return {};
    }
    refIt->profileId = profile.id;

    const juce::var afterJson = doc_.toVar();
    undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
    publishAssignments();
    statusBar_.showMessage("Recreated " + profile.name + " with " +
                           juce::String(static_cast<int>(profile.controls.size())) + " controls");
    return profile.id;
}

} // namespace synth::midi
