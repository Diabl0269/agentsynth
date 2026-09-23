// Concern: FRO131 (docs/control/midi-remote-ui.md#the-midi-remote-panel) -- the panel's own
// selection state and the fan-out between its three regions and the live engine/store. The three
// regions themselves (ControllersListComponent, ControllerSurfaceComponent, ControlInspectorComponent)
// know nothing about RemoteEngine/MidiLearnController -- this is the one place that does.
#include "MidiRemotePanelComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/ControllerDetect.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "Modules/ChannelStripModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/AutomationBinding.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>

namespace synth::ui {

namespace {

juce::String resolveModuleName(AudioEngine& audioEngine, const juce::String& nodeUuid) {
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node->properties["uuid"].toString() == nodeUuid)
            return node->getProcessor() != nullptr ? node->getProcessor()->getName() : juce::String();
    }
    return {};
}

juce::AudioProcessor* resolveProcessor(AudioEngine& audioEngine, const juce::String& nodeUuid) {
    for (auto* node : audioEngine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == nodeUuid)
            return node->getProcessor();
    return nullptr;
}

} // namespace

MidiRemotePanelComponent::MidiRemotePanelComponent() {
    // Focus-region root (docs/control/shortcuts.md's "Focus regions": every region root calls
    // this so grabKeyboardFocus() lands deterministically here, not on whichever child JUCE would
    // otherwise pick by Y/X position).
    setWantsKeyboardFocus(true);

    addAndMakeVisible(controllersList_);
    addAndMakeVisible(toolbar_);
    addAndMakeVisible(controllerSurface_);
    addAndMakeVisible(inspector_);

    controllersList_.onSelectProfile = [this](const juce::String& profileId) { selectProfile(profileId); };
    controllersList_.onRenameRequested = [this](const juce::String& profileId, const juce::String& newName) {
        handleRenameRequested(profileId, newName);
    };
    controllersList_.onExportRequested = [this](const juce::String& profileId) { handleExportRequested(profileId); };
    controllersList_.countProjectAssignments = [this](const juce::String& profileId) {
        return learnController_ != nullptr ? learnController_->countProjectAssignmentsForProfile(profileId) : 0;
    };
    controllersList_.onDeleteConfirmed = [this](const juce::String& profileId) {
        handleDeleteProfileRequested(profileId);
    };

    controllersList_.onAddControllerRequested = [this](juce::Component& anchor) { showAddControllerPopover(anchor); };

    toolbar_.onDetectToggled = [this](bool on) { setDetectActive(on); };
    toolbar_.onTemplatesRequested = [this](juce::Component& anchor) { showTemplatesMenu(anchor); };
    toolbar_.onMoreRequested = [this](juce::Component& anchor) { showMoreMenu(anchor); };

    controllerSurface_.onSelectControl = [this](const juce::String& controlId) { selectControl(controlId); };
    controllerSurface_.onControlMoved = [this](const juce::String& controlId, int col, int row) {
        handleControlMoved(controlId, col, row);
    };
    controllerSurface_.onDeleteControlRequested = [this](const juce::String& controlId) {
        handleDeleteControlRequested(controlId);
    };

    inspector_.onLocateRequested = [this](const juce::String& nodeUuid) {
        if (onLocateNode)
            onLocateNode(nodeUuid);
    };
    inspector_.onAssignmentEdited = [this](const synth::Assignment& assignment) {
        if (learnController_ != nullptr)
            learnController_->updateAssignment(assignment);
        refreshInspectorForSelection();
    };
    inspector_.onForgetRequested = [this](const juce::String& assignmentId) { handleForgetRequested(assignmentId); };
    inspector_.onControlEdited = [this](const synth::Control& control) { handleControlEdited(control); };
    inspector_.onAutoDetectRequested = [this](const synth::Control& control) { beginEncoderAutoDetect(control); };
}

MidiRemotePanelComponent::~MidiRemotePanelComponent() = default;

void MidiRemotePanelComponent::configure(AudioEngine& audioEngine, synth::midi::RemoteEngine& remoteEngine,
                                         synth::midi::MidiLearnController& learnController,
                                         synth::MidiRemoteProjectDoc& doc, GraphEditor& graphEditor) {
    audioEngine_ = &audioEngine;
    remoteEngine_ = &remoteEngine;
    learnController_ = &learnController;
    doc_ = &doc;
    graphEditor_ = &graphEditor;
    rebuildFromProfiles();
}

void MidiRemotePanelComponent::rebuildFromProfiles() {
    if (learnController_ == nullptr || doc_ == nullptr || audioEngine_ == nullptr)
        return;

    const auto& profiles = learnController_->getProfiles();
    const auto openIdentifiers = audioEngine_->getOpenMidiInputIdentifiers();
    const bool hosted = audioEngine_->isHosted();

    std::vector<ControllersListComponent::RowModel> rows;
    for (const auto& profile : profiles) {
        const bool present = hosted ? profile.input.identifier == synth::midi::hostSourceKey()
                                    : std::find(openIdentifiers.begin(), openIdentifiers.end(),
                                                profile.input.identifier) != openIdentifiers.end();
        rows.push_back(
            {profile.id, profile.name,
             present ? ControllersListComponent::RowState::present : ControllersListComponent::RowState::absent});
    }
    for (const auto& ref : doc_->controllers) {
        const bool hasLocalProfile =
            std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == ref.profileId; });
        if (!hasLocalProfile)
            rows.push_back({ref.profileId, ref.name, ControllersListComponent::RowState::orphan});
    }
    controllersList_.setRows(rows);
    controllersList_.setAddControllerVisible(!hosted); // the plugin build's list is exactly Host MIDI

    if (!selectedProfileId_.isEmpty() && findSelectedProfile() == nullptr) {
        selectedProfileId_.clear();
        selectedControlId_.clear();
        setDetectActive(false);
    }
    toolbar_.setProfileSelected(findSelectedProfile() != nullptr);
    refreshSurfaceForSelectedProfile();
    refreshInspectorForSelection();
}

void MidiRemotePanelComponent::scheduleLiveRefresh() {
    if (liveRefreshPending_)
        return;
    liveRefreshPending_ = true;
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->liveRefreshPending_ = false;
        safeThis->rebuildFromProfiles();
    });
}

// ONE drainActivity() pass feeds everything: the Controllers list's dots, the surface's live
// widgets, Detect (FRO134) and encoder auto-detect (FRO134) -- a second drain would steal events
// from the first. Detect works on a copy of the selected profile and persists it once, after the
// drain, so a burst of new controls is one write and one grid rebuild.
void MidiRemotePanelComponent::refreshActivity() {
    if (remoteEngine_ == nullptr || learnController_ == nullptr)
        return;

    const auto& profiles = learnController_->getProfiles();
    const auto now = juce::Time::getMillisecondCounter();

    std::optional<synth::ControllerProfile> working;
    bool profileChanged = false;
    std::vector<juce::String> litControlIds;
    std::vector<synth::midi::RemoteEvent> selectedEvents;

    remoteEngine_->drainActivity([&](const juce::String& sourceKey, const synth::midi::RemoteEvent& event) {
        auto profileIt = std::find_if(profiles.begin(), profiles.end(),
                                      [&](const auto& p) { return p.input.identifier == sourceKey; });
        if (profileIt == profiles.end())
            return;
        const auto* profile = &*profileIt;

        profileLastActivityMs_[profile->id] = static_cast<juce::int64>(now);

        if (profile->id != selectedProfileId_)
            return;

        encoderDetect_.feed(event);
        selectedEvents.push_back(event);

        if (detect_.isActive()) {
            if (!working)
                working = *profile;
            const auto step = detect_.handleEvent(*working, event);
            profileChanged = profileChanged || step.controlAdded;
            if (step.litControlId.isNotEmpty())
                litControlIds.push_back(step.litControlId);
        }
        if (const auto* control = synth::midi::findControlForEvent(profile->controls, event))
            controllerSurface_.noteActivity(control->id, event.kind, event.value);
    });

    commitDetectStep(working, profileChanged, litControlIds, selectedEvents);

    for (const auto& profile : profiles) {
        const auto lastMs = profileLastActivityMs_.count(profile.id) ? profileLastActivityMs_[profile.id] : 0;
        const bool lit = lastMs != 0 && (static_cast<juce::int64>(now) - lastMs) < kActivityLitMs;
        auto& wasLit = profileActivityLit_[profile.id];
        if (wasLit != lit) {
            wasLit = lit;
            controllersList_.setActivityLit(profile.id, lit);
        }
    }
}

bool MidiRemotePanelComponent::selectAssignmentForParameter(const juce::String& nodeUuid, const juce::String& paramId) {
    if (doc_ == nullptr)
        return false;
    auto it = std::find_if(doc_->assignments.begin(), doc_->assignments.end(), [&](const synth::Assignment& a) {
        return a.target.isParameter() && a.target.parameter.nodeUuid == nodeUuid &&
               a.target.parameter.paramId == paramId;
    });
    if (it == doc_->assignments.end())
        return false;
    selectProfile(it->control.profileId);
    selectControl(it->control.controlId);
    return true;
}

void MidiRemotePanelComponent::selectProfile(const juce::String& profileId) {
    if (profileId != selectedProfileId_) {
        setDetectActive(false); // Detect belongs to one controller
        encoderDetect_.cancel();
    }
    selectedProfileId_ = profileId;
    selectedControlId_.clear();
    controllersList_.setSelectedProfileId(profileId);
    toolbar_.setProfileSelected(findSelectedProfile() != nullptr);
    refreshSurfaceForSelectedProfile();
    refreshInspectorForSelection();
}

void MidiRemotePanelComponent::selectControl(const juce::String& controlId) {
    selectedControlId_ = controlId;
    controllerSurface_.setSelectedControlId(controlId);
    refreshInspectorForSelection();
}

const synth::ControllerProfile* MidiRemotePanelComponent::findSelectedProfile() const {
    if (learnController_ == nullptr || selectedProfileId_.isEmpty())
        return nullptr;
    const auto& profiles = learnController_->getProfiles();
    auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == selectedProfileId_; });
    return it != profiles.end() ? &*it : nullptr;
}

void MidiRemotePanelComponent::refreshSurfaceForSelectedProfile() {
    const auto* profile = findSelectedProfile();
    if (profile == nullptr || doc_ == nullptr || audioEngine_ == nullptr) {
        controllerSurface_.setControls({}, {});
        return;
    }

    std::vector<ControllerSurfaceComponent::CellModel> cells;
    for (const auto& control : profile->controls) {
        ControllerSurfaceComponent::CellModel cell;
        cell.control = control;

        // FRO253's Target::Kind::nodeCommand (Solo mapping) can also live in doc_->assignments,
        // alongside parameter targets -- find whichever one this control has, if any, then branch
        // on .target.kind to read the right union member.
        auto projectIt =
            std::find_if(doc_->assignments.begin(), doc_->assignments.end(), [&](const synth::Assignment& a) {
                return a.control.profileId == profile->id && a.control.controlId == control.id;
            });
        auto actionIt = std::find_if(profile->actions.begin(), profile->actions.end(),
                                     [&](const synth::Assignment& a) { return a.control.controlId == control.id; });

        if (projectIt != doc_->assignments.end() && projectIt->target.isParameter()) {
            cell.isMapped = true;
            const juce::String moduleName = resolveModuleName(*audioEngine_, projectIt->target.parameter.nodeUuid);
            if (moduleName.isEmpty()) {
                cell.assignmentLabel = "(missing module)";
                cell.isWarning = true;
            } else {
                auto* processor = resolveProcessor(*audioEngine_, projectIt->target.parameter.nodeUuid);
                juce::String paramName;
                if (processor != nullptr) {
                    auto resolution = synth::resolveLaneParameter(processor, projectIt->target.parameter.paramId,
                                                                  projectIt->target.parameter.paramIndexHint);
                    if (resolution.resolved()) {
                        paramName = resolution.liveParameter()->getName(64);
                        const float paramValue = resolution.liveParameter()->getValue();
                        if (projectIt->specEncoding == synth::Encoding::abs7) {
                            // FRO262: Surface widgets show hardware position, not parameter position --
                            // activity events are raw 0..1 (RemoteEngineDecode.cpp's pushActivityOnly),
                            // only ever range-mapped on write (RemoteEngineInternal.h's mapThroughRange,
                            // applied in RemoteEngineApply.cpp). Invert that same [rangeMin,rangeMax] map
                            // to recover the hardware position this parameter's current value implies.
                            const double rangeMin = projectIt->range.min;
                            const double rangeMax = projectIt->range.max;
                            cell.initialValue =
                                !juce::approximatelyEqual(rangeMin, rangeMax)
                                    ? juce::jlimit(0.0f, 1.0f,
                                                   static_cast<float>((paramValue - rangeMin) / (rangeMax - rangeMin)))
                                    : 0.0f;
                        } else {
                            // Relative encodings never range-map (RemoteEngineApply.cpp adds event.value
                            // straight onto param->getValue()) -- the parameter's own value IS the
                            // position noteActivity() would reach.
                            cell.initialValue = paramValue;
                        }
                    }
                }
                cell.assignmentLabel = moduleName + juce::String::fromUTF8(" \xc2\xb7 ") +
                                       (paramName.isEmpty() ? projectIt->specControlName : paramName);
            }
        } else if (projectIt != doc_->assignments.end() && projectIt->target.isNodeCommand()) {
            cell.isMapped = true;
            const juce::String moduleName = resolveModuleName(*audioEngine_, projectIt->target.nodeCommand.nodeUuid);
            if (moduleName.isEmpty()) {
                cell.assignmentLabel = "(missing module)";
                cell.isWarning = true;
            } else {
                cell.assignmentLabel = moduleName + juce::String::fromUTF8(" \xc2\xb7 Solo");
                // FRO262: nodeCommand's only target today is Solo (FRO253) -- seed the cell from the
                // strip's actual current solo state rather than always showing "off".
                if (auto* processor = resolveProcessor(*audioEngine_, projectIt->target.nodeCommand.nodeUuid)) {
                    if (auto* strip = dynamic_cast<ChannelStripModule*>(processor))
                        cell.initialValue = strip->isSoloed() ? 1.0f : 0.0f;
                }
            }
        } else if (actionIt != profile->actions.end()) {
            cell.isMapped = true;
            cell.assignmentLabel = ShortcutManager::getActionDescription(actionIt->target.action.actionId);
        } else {
            cell.assignmentLabel = "-";
        }

        cells.push_back(cell);
    }
    controllerSurface_.setControls(profile->id, cells);
}

void MidiRemotePanelComponent::refreshInspectorForSelection() {
    const auto* profile = findSelectedProfile();
    ControlInspectorComponent::ControlModel model;

    if (profile == nullptr || selectedControlId_.isEmpty()) {
        inspector_.setControl(model);
        return;
    }
    auto controlIt = std::find_if(profile->controls.begin(), profile->controls.end(),
                                  [&](const auto& c) { return c.id == selectedControlId_; });
    if (controlIt == profile->controls.end()) {
        inspector_.setControl(model);
        return;
    }

    model.hasControl = true;
    model.control = *controlIt;

    if (doc_ != nullptr) {
        for (const auto& a : doc_->assignments) {
            if (a.control.profileId != profile->id || a.control.controlId != selectedControlId_)
                continue;
            // FRO253's Target::Kind::nodeCommand (Solo mapping) shares this list with parameter
            // targets -- read the right union member for whichever kind this row actually is.
            ControlInspectorComponent::AssignmentRowModel row;
            row.assignment = a;
            row.scopeLabel = "Project";
            if (a.target.isParameter()) {
                row.nodeUuid = a.target.parameter.nodeUuid;
                const juce::String moduleName =
                    audioEngine_ != nullptr ? resolveModuleName(*audioEngine_, row.nodeUuid) : "";
                row.isOrphaned = moduleName.isEmpty();
                row.drivesLabel = row.isOrphaned
                                      ? "(missing module)"
                                      : moduleName + juce::String::fromUTF8(" \xc2\xb7 ") + a.specControlName;
            } else if (a.target.isNodeCommand()) {
                row.nodeUuid = a.target.nodeCommand.nodeUuid;
                const juce::String moduleName =
                    audioEngine_ != nullptr ? resolveModuleName(*audioEngine_, row.nodeUuid) : "";
                row.isOrphaned = moduleName.isEmpty();
                row.drivesLabel =
                    row.isOrphaned ? "(missing module)" : moduleName + juce::String::fromUTF8(" \xc2\xb7 Solo");
            } else {
                continue; // not reachable today (doc_->assignments never holds an action target)
            }
            model.assignments.push_back(row);
        }
    }
    for (const auto& a : profile->actions) {
        if (a.control.controlId != selectedControlId_)
            continue;
        ControlInspectorComponent::AssignmentRowModel row;
        row.assignment = a;
        row.scopeLabel = "Global";
        row.drivesLabel = ShortcutManager::getActionDescription(a.target.action.actionId);
        model.assignments.push_back(row);
    }

    inspector_.setControl(model);
}

void MidiRemotePanelComponent::handleRenameRequested(const juce::String& profileId, const juce::String& newName) {
    if (learnController_ == nullptr)
        return;
    const auto& profiles = learnController_->getProfiles();
    auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == profileId; });
    if (it == profiles.end())
        return;
    auto updated = *it;
    updated.name = newName;
    learnController_->updateProfile(updated);
    rebuildFromProfiles();
}

void MidiRemotePanelComponent::handleExportRequested(const juce::String& profileId) {
    if (learnController_ == nullptr)
        return;
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export controller...", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                       juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync(flags, [this, chooser, profileId](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file != juce::File() && learnController_ != nullptr)
            learnController_->exportProfile(profileId, file);
    });
}

void MidiRemotePanelComponent::handleDeleteProfileRequested(const juce::String& profileId) {
    if (learnController_ == nullptr)
        return;
    learnController_->deleteProfile(profileId);
    if (selectedProfileId_ == profileId) {
        selectedProfileId_.clear();
        selectedControlId_.clear();
    }
    rebuildFromProfiles();
}

void MidiRemotePanelComponent::handleControlMoved(const juce::String& controlId, int col, int row) {
    const auto* profile = findSelectedProfile();
    if (profile == nullptr || learnController_ == nullptr)
        return;
    auto updated = *profile;
    auto it = std::find_if(updated.controls.begin(), updated.controls.end(),
                           [&](const auto& c) { return c.id == controlId; });
    if (it == updated.controls.end())
        return;
    it->layout.col = col;
    it->layout.row = row;
    learnController_->updateProfile(updated);

    // Source/UI/CLAUDE.md's rebuild-mid-gesture rule: onControlMoved fires from
    // ControllerSurfaceCell::onDragEnded, still on that cell's own mouseUp call stack -- rebuilding
    // the grid synchronously here (setControls() clears and reallocates every cell) would free the
    // very cell whose mouseUp is still executing. Defer to the next message-loop iteration instead,
    // same fix shape as a live-drag survivor elsewhere in this codebase. SafePointer guards against
    // the panel itself being torn down before the deferred call runs (dock closed mid-drag).
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->refreshSurfaceForSelectedProfile();
        safeThis->controllerSurface_.setSelectedControlId(safeThis->selectedControlId_);
    });
}

void MidiRemotePanelComponent::handleDeleteControlRequested(const juce::String& controlId) {
    const auto* profile = findSelectedProfile();
    if (profile == nullptr || learnController_ == nullptr)
        return;
    learnController_->deleteControl(profile->id, controlId);
    if (selectedControlId_ == controlId)
        selectedControlId_.clear();
    refreshSurfaceForSelectedProfile();
    refreshInspectorForSelection();
}

void MidiRemotePanelComponent::handleForgetRequested(const juce::String& assignmentId) {
    // Assignment ids aren't a MidiLearnController lookup key today -- forget() resolves a
    // PROJECT (parameter-target) assignment by node/paramId, forgetAction() resolves a GLOBAL
    // (action-target) one by actionId. Resolve here and forget via whichever existing API matches,
    // rather than adding a second removal path per scope.
    if (learnController_ == nullptr)
        return;

    if (doc_ != nullptr) {
        auto it = std::find_if(doc_->assignments.begin(), doc_->assignments.end(),
                               [&](const auto& a) { return a.id == assignmentId; });
        if (it != doc_->assignments.end()) {
            // FRO253's Target::Kind::nodeCommand (Solo mapping) shares this list with parameter
            // targets -- forget() resolves the former by (nodeId, paramId), forgetNodeCommand() the
            // latter by (nodeId, NodeCommandKind); guard the union read and call whichever matches.
            if (audioEngine_ != nullptr) {
                const juce::String targetUuid =
                    it->target.isParameter() ? it->target.parameter.nodeUuid : it->target.nodeCommand.nodeUuid;
                for (auto* node : audioEngine_->getGraph().getNodes()) {
                    if (node->properties["uuid"].toString() != targetUuid)
                        continue;
                    if (it->target.isParameter())
                        learnController_->forget(node->nodeID, it->target.parameter.paramId);
                    else if (it->target.isNodeCommand())
                        learnController_->forgetNodeCommand(node->nodeID, it->target.nodeCommand.command);
                    break;
                }
            }
            refreshSurfaceForSelectedProfile();
            refreshInspectorForSelection();
            return;
        }
    }

    if (const auto* profile = findSelectedProfile()) {
        auto it = std::find_if(profile->actions.begin(), profile->actions.end(),
                               [&](const auto& a) { return a.id == assignmentId; });
        if (it != profile->actions.end())
            learnController_->forgetAction(it->target.action.actionId);
    }
    refreshSurfaceForSelectedProfile();
    refreshInspectorForSelection();
}

// FRO134/FRO264: name / kind / encoding edits from the Inspector. Rebuilds synchronously -- this
// is the inspector's own combo/label callback, not a cell's mouse stack.
void MidiRemotePanelComponent::handleControlEdited(const synth::Control& control) {
    if (learnController_ == nullptr || selectedProfileId_.isEmpty())
        return;
    if (!learnController_->updateControl(selectedProfileId_, control))
        return;
    refreshSurfaceForSelectedProfile();
    refreshInspectorForSelection();
}

void MidiRemotePanelComponent::resized() {
    auto bounds = getLocalBounds();
    controllersList_.setBounds(bounds.removeFromLeft(kListWidth));
    inspector_.setBounds(bounds.removeFromRight(kInspectorWidth));
    toolbar_.setBounds(bounds.removeFromTop(toolbar_.getPreferredHeight()));
    controllerSurface_.setBounds(bounds);
}

} // namespace synth::ui
