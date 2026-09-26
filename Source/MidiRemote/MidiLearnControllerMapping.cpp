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
// profile's `actions`, a controller-history step -- docs/control/midi-remote.md#undo), a parameter
// or node command is PROJECT scope (undoable via recordMidiRemoteChange). Assigning replaces whatever the
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
    } else if (pick.kind == PickTarget::Kind::page) {
        // FRO142 (docs/control/midi-remote.md#pages): engine-internal, GLOBAL like an action --
        // never resolved through ShortcutManager/ActionCommandLookup (PageCommand's own comment).
        target.kind = synth::Target::Kind::page;
        target.page.command = pick.pageCommand;
        target.page.page = pick.pageNumber;
        targetName = pick.pageCommand == synth::PageCommand::next       ? juce::String("Next page")
                     : pick.pageCommand == synth::PageCommand::previous ? juce::String("Previous page")
                                                                        : "Page " + juce::String(pick.pageNumber);
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

    // FRO142 (docs/control/midi-remote.md#pages): a PROJECT-scope assignment (parameter/nodeCommand)
    // created while the profile's page N is active belongs to page N; a GLOBAL target
    // (action/continuous) ignores Assignment::page entirely, so it stays at the default (1).
    const bool projectScoped = target.isParameter() || target.isNodeCommand();
    const int assignmentPage = projectScoped ? remoteEngine_.getActivePage(profileId) : 1;
    const Assignment assignment = makeAssignmentForControl(*profile, control, target, assignmentPage);

    // FRO236: a continuous target is GLOBAL, exactly like an action (docs/control/midi-remote.md#continuous-targets
    // -- it means the same thing in every project), so it shares this branch and mirrors the action
    // rule verbatim, just keyed on ContinuousTargetKind instead of an actionId. FRO142: a page target
    // joins them -- it too is engine-internal and active on every page (PageCommand's own comment).
    if (target.isAction() || target.isContinuous() || target.isPage()) {
        ControllerProfile updated = *profile;
        auto& actions = updated.actions;
        actions.erase(std::remove_if(actions.begin(), actions.end(),
                                     [&](const Assignment& a) {
                                         // One global assignment per control, whatever its kind.
                                         if (a.control.controlId == controlId)
                                             return true;
                                         if (target.isAction())
                                             return a.target.isAction() && a.target.action.actionId == pick.actionId;
                                         if (target.isContinuous())
                                             return a.target.isContinuous() &&
                                                    a.target.continuous.kind == pick.continuous;
                                         return a.target.isPage() && a.target.page.command == pick.pageCommand &&
                                                (pick.pageCommand != synth::PageCommand::go ||
                                                 a.target.page.page == pick.pageNumber);
                                     }),
                      actions.end());
        actions.push_back(assignment);
        updateProfile(updated, "Assign control"); // saves, republishes, records and notifies onChanged
    } else {
        const juce::var beforeJson = doc_.toVar();
        auto& assignments = doc_.assignments;
        // FRO142 (docs/control/midi-remote.md#pages): scoped to the SAME page as the new assignment
        // -- a mapping on page 1 must survive assigning that control (or that target) again on
        // page 2; each page owns its own "one assignment per control/target" rule independently.
        assignments.erase(std::remove_if(assignments.begin(), assignments.end(),
                                         [&](const Assignment& a) {
                                             if (a.page != assignment.page)
                                                 return false;
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
        auto before = std::optional<ControllerProfile>(profile);
        auto& actions = profile.actions;
        const auto sizeBefore = actions.size();
        actions.erase(
            std::remove_if(actions.begin(), actions.end(), [&](const Assignment& a) { return a.id == assignmentId; }),
            actions.end());
        if (actions.size() == sizeBefore)
            continue;
        profileStore_.save(profile);
        remoteEngine_.setProfiles(profiles_);
        recordProfileEdit("Forget action", profile.id, std::move(before));
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

// Split across the two histories like deleteControl() (docs/control/midi-remote.md#undo): the
// profile half (a new controller file) is one controller-history step; the project half --
// assignments repointed at the new profile, the orphan reference swapped for it -- is one
// AppUndoManager step, and undoing it puts the orphan back while the minted profile stays.
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
    if (!addProfile(profile, "Recreate controller")) {
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

// FRO142 (docs/control/midi-remote.md#pages): see MidiLearnController.h's own doc comment on why
// this widens by the EFFECTIVE count (getEffectivePageCount), not profile.pageCount alone -- a
// project assignment may already reference a page beyond what the profile itself declares, and
// "+" must always reveal a genuinely new, empty page.
bool MidiLearnController::addPage(const juce::String& profileId) {
    const auto* profile = findProfile(profileId);
    if (profile == nullptr)
        return false;
    const int effective = remoteEngine_.getEffectivePageCount(profileId);
    if (effective >= 16)
        return false;

    ControllerProfile updated = *profile;
    updated.pageCount = effective + 1;
    if (!updateProfile(updated, "Add page"))
        return false;
    remoteEngine_.setActivePage(profileId, updated.pageCount);
    statusBar_.showMessage("Added page " + juce::String(updated.pageCount));
    return true;
}

bool MidiLearnController::deletePage(const juce::String& profileId, int page) {
    const auto* profile = findProfile(profileId);
    if (profile == nullptr || page <= 1 || page > remoteEngine_.getEffectivePageCount(profileId))
        return false;

    // The project half: every assignment THIS page owns is gone, and every higher page shifts down
    // by one so no page number is ever skipped -- one AppUndoManager step, same as any other
    // project-doc mutation in this file.
    const juce::var beforeJson = doc_.toVar();
    auto& assignments = doc_.assignments;
    assignments.erase(
        std::remove_if(assignments.begin(), assignments.end(),
                       [&](const Assignment& a) { return a.control.profileId == profileId && a.page == page; }),
        assignments.end());
    for (auto& a : assignments)
        if (a.control.profileId == profileId && a.page > page)
            --a.page;
    const juce::var afterJson = doc_.toVar();
    undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
    publishAssignments();

    // The profile half: pageCount shrinks by one too, whenever the deleted page was within it --
    // deleting the profile's OWN last declared page (rather than one only assignments pushed the
    // effective count up to) must not leave a phantom empty page at the end. A separate controller-
    // history step, same split recreateController() already uses for its own project+profile pair.
    if (profile->pageCount >= page) {
        ControllerProfile updated = *profile;
        updated.pageCount = juce::jmax(1, updated.pageCount - 1);
        updateProfile(updated, "Delete page");
    }

    if (remoteEngine_.getActivePage(profileId) >= page)
        remoteEngine_.setActivePage(profileId, juce::jmax(1, remoteEngine_.getActivePage(profileId) - 1));

    statusBar_.showMessage("Deleted page " + juce::String(page));
    return true;
}

} // namespace synth::midi
