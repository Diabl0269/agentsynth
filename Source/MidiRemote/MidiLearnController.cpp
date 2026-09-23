// Arm/cancel/bind lifecycle for a module-card MIDI Learn (docs/control/midi-remote-ui.md#the-learn-interaction,
// docs/control/midi-remote.md#learn-what-does-the-first-message-mean). See MidiLearnController.h for why this
// is a standalone collaborator rather than more MainComponent methods.

#include "MidiRemote/MidiLearnController.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiRemoteLearnBinder.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "Modules/ModuleBase.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelineTransportBar.h"
#include <algorithm>

namespace synth::midi {

MidiLearnController::MidiLearnController(AudioEngine& engine, GraphEditor& graphEditor, RemoteEngine& remoteEngine,
                                         synth::MidiRemoteProjectDoc& doc, AppUndoManager& undo,
                                         StatusBarComponent& statusBar, ControllerProfileStore profileStore)
    : engine_(engine)
    , graphEditor_(graphEditor)
    , remoteEngine_(remoteEngine)
    , doc_(doc)
    , undo_(undo)
    , statusBar_(statusBar)
    , profileStore_(std::move(profileStore)) {
    profiles_ = profileStore_.loadAll().profiles;
    remoteEngine_.onLearned = [this](const LearnResult& result) { handleLearned(result); };
}

MidiLearnController::~MidiLearnController() { juce::Desktop::getInstance().removeGlobalMouseListener(&watcher_); }

bool MidiLearnController::isArmed() const noexcept { return remoteEngine_.isLearnArmed(); }

// A device opened after startup (or a hosted build's host-MIDI source) must be visible to the
// engine before a learn can hear it -- wireMidiRemoteEngine() only ever ran this once, at launch.
void MidiLearnController::refreshSources() {
    auto sources = engine_.getOpenMidiInputIdentifiers();
    if (engine_.isHosted())
        sources.push_back(hostSourceKey());
    remoteEngine_.setSources(sources);
}

juce::String MidiLearnController::resolveNodeUuid(juce::AudioProcessorGraph::NodeID nodeId) const {
    auto* node = engine_.getGraph().getNodeForId(nodeId);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

// Same ensure-uuid idiom as MainComponent::automateParameter/createTrackInNode -- mirrored into the
// processor in the same breath (ModuleBase::setNodeUuid's contract: written once, never rewritten).
juce::String MidiLearnController::ensureNodeUuid(juce::AudioProcessorGraph::NodeID nodeId) const {
    auto* node = engine_.getGraph().getNodeForId(nodeId);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr)
        return {};

    juce::String uuid = node->properties["uuid"].toString();
    if (uuid.isEmpty()) {
        uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        module->setNodeUuid(uuid);
    }
    return uuid;
}

void MidiLearnController::arm(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) {
    const juce::String uuid = ensureNodeUuid(nodeId);
    if (uuid.isEmpty())
        return;

    bool buttonLike = false;
    if (auto* node = engine_.getGraph().getNodeForId(nodeId)) {
        for (auto* p : node->getProcessor()->getParameters()) {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
            if (ranged != nullptr && ranged->paramID == paramId) {
                buttonLike = dynamic_cast<juce::AudioParameterBool*>(ranged) != nullptr;
                break;
            }
        }
    }

    endArmedUi(); // armLearn() below replaces any pending learn regardless, but its UI must follow

    refreshSources();

    LearnRequest request;
    request.target.kind = synth::Target::Kind::parameter;
    request.target.parameter.nodeUuid = uuid;
    request.target.parameter.paramId = paramId;
    request.buttonLike = buttonLike;
    remoteEngine_.armLearn(request);

    graphEditor_.setMidiLearnArmed(nodeId, paramId);
    if (mixerPanel_ != nullptr)
        mixerPanel_->setMidiLearnArmed(nodeId, paramId);
    statusBar_.showStickyMessage("Move a control on your MIDI device to map it... (Esc to cancel)");

    watcher_.onAnyClick = [this] { cancelArmed(); };
    watcher_.stillArmed = [this] { return remoteEngine_.isLearnArmed(); };
    watcher_.onExternallyCancelled = [this] {
        endArmedUi();
        statusBar_.showMessage("MIDI Learn timed out");
    };
    juce::Desktop::getInstance().addGlobalMouseListener(&watcher_);
    watcher_.start();
}

// FRO133 (docs/control/midi-remote.md#action-targets): the same arm/watch/cancel machinery as
// arm() above, minus the node/uuid resolution -- an action target has no graph node. Only one
// learn is ever armed at a time regardless of kind (docs/control/midi-remote-ui.md#the-learn-interaction),
// so this replaces any pending parameter learn exactly as arm() replaces a pending action learn.
void MidiLearnController::armAction(const juce::String& actionId) {
    endArmedUi();

    refreshSources();

    LearnRequest request;
    request.target.kind = synth::Target::Kind::action;
    request.target.action.actionId = actionId;
    request.buttonLike = true; // every action target is a button (docs/control/midi-remote.md#action-targets)
    remoteEngine_.armLearn(request);

    if (transportBar_ != nullptr)
        transportBar_->setMidiLearnArmedAction(actionId);
    statusBar_.showStickyMessage("Move a control on your MIDI device to map it... (Esc to cancel)");

    watcher_.onAnyClick = [this] { cancelArmed(); };
    watcher_.stillArmed = [this] { return remoteEngine_.isLearnArmed(); };
    watcher_.onExternallyCancelled = [this] {
        endArmedUi();
        statusBar_.showMessage("MIDI Learn timed out");
    };
    juce::Desktop::getInstance().addGlobalMouseListener(&watcher_);
    watcher_.start();
}

void MidiLearnController::cancelArmed() {
    if (!remoteEngine_.isLearnArmed())
        return;
    remoteEngine_.cancelLearn();
    endArmedUi();
    statusBar_.showMessage("MIDI Learn cancelled");
}

void MidiLearnController::endArmedUi() {
    watcher_.stop();
    juce::Desktop::getInstance().removeGlobalMouseListener(&watcher_);
    graphEditor_.clearMidiLearnArmed();
    if (mixerPanel_ != nullptr)
        mixerPanel_->clearMidiLearnArmed();
    if (transportBar_ != nullptr)
        transportBar_->clearMidiLearnArmedAction();
    statusBar_.clearMessage();
}

juce::String MidiLearnController::deviceNameForSourceKey(const juce::String& sourceKey) const {
    if (sourceKey == hostSourceKey())
        return "Host MIDI";
    for (const auto& info : juce::MidiInput::getAvailableDevices())
        if (info.identifier == sourceKey)
            return info.name;
    return sourceKey; // the device vanished mid-learn -- better than an empty label
}

const ControllerProfile* MidiLearnController::findProfile(const juce::String& id) const {
    for (const auto& p : profiles_)
        if (p.id == id)
            return &p;
    return nullptr;
}

// A learn that also auto-creates a profile/control persists that half to disk unconditionally and
// OUTSIDE the undo step -- docs/control/midi-remote.md#undo: "the profile stays" even if the
// project-doc half is later undone.
void MidiLearnController::handleLearned(const LearnResult& result) {
    const juce::String deviceName = deviceNameForSourceKey(result.sourceKey);
    auto outcome = bindLearnResult(result, deviceName, profiles_);

    if (result.target.isAction()) {
        handleLearnedAction(result, outcome);
        return;
    }

    if (outcome.profileIsNew || outcome.controlIsNew) {
        profileStore_.save(outcome.profile);
        auto existing = std::find_if(profiles_.begin(), profiles_.end(),
                                     [&](const ControllerProfile& p) { return p.id == outcome.profile.id; });
        if (existing != profiles_.end())
            *existing = outcome.profile;
        else
            profiles_.push_back(outcome.profile);
        remoteEngine_.setProfiles(profiles_);
    }

    const juce::var beforeJson = doc_.toVar();

    // "MIDI Learn again..." on an already-mapped target replaces its assignment, never duplicates it.
    auto& assignments = doc_.assignments;
    assignments.erase(
        std::remove_if(assignments.begin(), assignments.end(),
                       [&](const synth::Assignment& a) {
                           return a.target.isParameter() &&
                                  a.target.parameter.nodeUuid == outcome.assignment.target.parameter.nodeUuid &&
                                  a.target.parameter.paramId == outcome.assignment.target.parameter.paramId;
                       }),
        assignments.end());
    assignments.push_back(outcome.assignment);

    const bool controllerKnown = std::any_of(doc_.controllers.begin(), doc_.controllers.end(),
                                             [&](const auto& ref) { return ref.profileId == outcome.profile.id; });
    if (!controllerKnown)
        doc_.controllers.push_back({outcome.profile.id, outcome.profile.name});

    const juce::var afterJson = doc_.toVar();
    undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
    publishAssignments();

    endArmedUi();
    statusBar_.showMessage("Mapped to " + outcome.assignment.specControlName + " on " + deviceName);
}

// FRO133: an action assignment is a GLOBAL setting stored on the profile itself
// (ControllerProfile::actions, docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project),
// never the project doc, and -- like every other profile edit -- is NOT undoable
// (docs/control/midi-remote.md#undo: "Profile edits ... global settings, not undoable"). Unlike
// the parameter path above, the profile is saved UNCONDITIONALLY here: even an existing
// control's re-learn changes the profile (a new/replaced action assignment in its `actions`
// list), not just a brand-new profile/control.
void MidiLearnController::handleLearnedAction(const LearnResult& result, LearnBindOutcome& outcome) {
    auto& actions = outcome.profile.actions;
    actions.erase(std::remove_if(actions.begin(), actions.end(),
                                 [&](const synth::Assignment& a) {
                                     return a.target.isAction() &&
                                            a.target.action.actionId == result.target.action.actionId;
                                 }),
                  actions.end());
    actions.push_back(outcome.assignment);

    profileStore_.save(outcome.profile);
    auto existing = std::find_if(profiles_.begin(), profiles_.end(),
                                 [&](const ControllerProfile& p) { return p.id == outcome.profile.id; });
    if (existing != profiles_.end())
        *existing = outcome.profile;
    else
        profiles_.push_back(outcome.profile);
    remoteEngine_.setProfiles(profiles_);

    const juce::String deviceName = deviceNameForSourceKey(result.sourceKey);
    endArmedUi();
    statusBar_.showMessage("Mapped to " + outcome.assignment.specControlName + " on " + deviceName);
}

void MidiLearnController::forget(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) {
    const juce::String uuid = resolveNodeUuid(nodeId);
    if (uuid.isEmpty())
        return;

    const juce::var beforeJson = doc_.toVar();
    auto& assignments = doc_.assignments;
    const auto sizeBefore = assignments.size();
    assignments.erase(std::remove_if(assignments.begin(), assignments.end(),
                                     [&](const synth::Assignment& a) {
                                         return a.target.isParameter() && a.target.parameter.nodeUuid == uuid &&
                                                a.target.parameter.paramId == paramId;
                                     }),
                      assignments.end());
    if (assignments.size() == sizeBefore)
        return; // nothing was mapped

    const juce::var afterJson = doc_.toVar();
    undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
    publishAssignments();
    statusBar_.showMessage("MIDI mapping removed");
}

std::map<juce::String, juce::String>
MidiLearnController::queryMappings(juce::AudioProcessorGraph::NodeID nodeId) const {
    std::map<juce::String, juce::String> result;
    const juce::String uuid = resolveNodeUuid(nodeId);
    if (uuid.isEmpty())
        return result;

    for (const auto& a : doc_.assignments) {
        if (!a.target.isParameter() || a.target.parameter.nodeUuid != uuid)
            continue;
        juce::String label = a.specControlName;
        if (const auto* profile = findProfile(a.control.profileId))
            label << " on " << profile->name;
        result[a.target.parameter.paramId] = label;
    }
    return result;
}

// FRO133: mirrors forget() above but mutates a profile's `actions` list rather than doc_ -- an
// action assignment is global, so this searches every profile (in practice at most one carries
// any given actionId, since armAction() replaces rather than duplicates) rather than resolving a
// single node.
void MidiLearnController::forgetAction(const juce::String& actionId) {
    bool removedAny = false;
    for (auto& profile : profiles_) {
        auto& actions = profile.actions;
        const auto sizeBefore = actions.size();
        actions.erase(std::remove_if(actions.begin(), actions.end(),
                                     [&](const synth::Assignment& a) {
                                         return a.target.isAction() && a.target.action.actionId == actionId;
                                     }),
                      actions.end());
        if (actions.size() == sizeBefore)
            continue;
        removedAny = true;
        profileStore_.save(profile);
    }
    if (!removedAny)
        return; // nothing was mapped

    remoteEngine_.setProfiles(profiles_);
    statusBar_.showMessage("MIDI mapping removed");
}

std::map<juce::String, juce::String> MidiLearnController::queryActionMappings() const {
    std::map<juce::String, juce::String> result;
    for (const auto& profile : profiles_) {
        for (const auto& a : profile.actions) {
            if (!a.target.isAction())
                continue;
            result[a.target.action.actionId] = a.specControlName + " on " + profile.name;
        }
    }
    return result;
}

void MidiLearnController::publishAssignments() { remoteEngine_.setAssignments(doc_.assignments); }

bool MidiLearnController::updateProfile(const ControllerProfile& profile) {
    auto existing = std::find_if(profiles_.begin(), profiles_.end(),
                                 [&](const ControllerProfile& p) { return p.id == profile.id; });
    if (existing == profiles_.end())
        return false;
    *existing = profile;
    profileStore_.save(profile);
    remoteEngine_.setProfiles(profiles_);
    return true;
}

int MidiLearnController::countProjectAssignmentsForProfile(const juce::String& profileId) const {
    return static_cast<int>(
        std::count_if(doc_.assignments.begin(), doc_.assignments.end(),
                      [&](const synth::Assignment& a) { return a.control.profileId == profileId; }));
}

bool MidiLearnController::deleteProfile(const juce::String& profileId) {
    auto existing =
        std::find_if(profiles_.begin(), profiles_.end(), [&](const ControllerProfile& p) { return p.id == profileId; });
    if (existing == profiles_.end())
        return false;
    profileStore_.deleteProfile(profileId);
    profiles_.erase(existing);
    remoteEngine_.setProfiles(profiles_);
    return true;
}

bool MidiLearnController::deleteControl(const juce::String& profileId, const juce::String& controlId) {
    auto profileIt =
        std::find_if(profiles_.begin(), profiles_.end(), [&](const ControllerProfile& p) { return p.id == profileId; });
    if (profileIt == profiles_.end())
        return false;

    auto& controls = profileIt->controls;
    const auto controlIt =
        std::find_if(controls.begin(), controls.end(), [&](const Control& c) { return c.id == controlId; });
    if (controlIt == controls.end())
        return false;
    controls.erase(controlIt);

    // Not undoable, same as every other profile edit -- drop any global action assignment on it too.
    auto& actions = profileIt->actions;
    actions.erase(std::remove_if(actions.begin(), actions.end(),
                                 [&](const synth::Assignment& a) { return a.control.controlId == controlId; }),
                  actions.end());
    profileStore_.save(*profileIt);
    remoteEngine_.setProfiles(profiles_);

    // The project half IS undoable, mirroring forget()'s own before/after-JSON snapshot.
    const juce::var beforeJson = doc_.toVar();
    auto& assignments = doc_.assignments;
    const auto sizeBefore = assignments.size();
    assignments.erase(std::remove_if(assignments.begin(), assignments.end(),
                                     [&](const synth::Assignment& a) { return a.control.controlId == controlId; }),
                      assignments.end());
    if (assignments.size() != sizeBefore) {
        const juce::var afterJson = doc_.toVar();
        undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
        publishAssignments();
    }
    return true;
}

bool MidiLearnController::updateAssignment(const Assignment& updated) {
    if (!updated.target.isParameter())
        return false;

    auto it = std::find_if(doc_.assignments.begin(), doc_.assignments.end(),
                           [&](const synth::Assignment& a) { return a.id == updated.id; });
    if (it == doc_.assignments.end())
        return false;

    const juce::var beforeJson = doc_.toVar();
    *it = updated;
    const juce::var afterJson = doc_.toVar();
    undo_.recordMidiRemoteChange(doc_, beforeJson, afterJson, [this] { publishAssignments(); });
    publishAssignments();
    return true;
}

} // namespace synth::midi
