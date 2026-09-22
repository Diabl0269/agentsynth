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

void MidiLearnController::publishAssignments() { remoteEngine_.setAssignments(doc_.assignments); }

} // namespace synth::midi
