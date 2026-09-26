// Concern: FRO131 (docs/control/midi-remote-ui.md#the-midi-remote-panel) -- the panel's own
// selection state and the fan-out between its three regions and the live engine/store. The three
// regions themselves (ControllersListComponent, ControllerSurfaceComponent, ControlInspectorComponent)
// know nothing about RemoteEngine/MidiLearnController -- this is the one place that does.
#include "MidiRemotePanelComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/ContinuousTarget.h"
#include "MidiRemote/ControllerDetect.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "Modules/ChannelStripModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/AutomationBinding.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>
#include <juce_audio_devices/juce_audio_devices.h>

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

// A parameter target that no longer resolves (its module is gone, or a hosted plugin's parameter
// drifted away) is an orphan node: shown as "(missing module)", never silently re-bound.
bool parameterTargetResolves(AudioEngine& audioEngine, const synth::Target::Parameter& target) {
    auto* processor = resolveProcessor(audioEngine, target.nodeUuid);
    return processor != nullptr &&
           synth::resolveLaneParameter(processor, target.paramId, target.paramIndexHint).resolved();
}

} // namespace

MidiRemotePanelComponent::MidiRemotePanelComponent() {
    // Focus-region root (docs/control/shortcuts.md's "Focus regions": every region root calls
    // this so grabKeyboardFocus() lands deterministically here, not on whichever child JUCE would
    // otherwise pick by Y/X position).
    setWantsKeyboardFocus(true);
    addMouseListener(&focusOnClick_, true); // FRO273: any press inside routes Cmd+Z here

    addAndMakeVisible(controllersList_);
    addAndMakeVisible(toolbar_);
    addAndMakeVisible(controllerSurface_);
    addAndMakeVisible(inspector_);
    addChildComponent(orphanView_);

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
    controllersList_.onFeedbackOutputRequested = [this](const juce::String& profileId, const juce::String& identifier,
                                                        const juce::String& name) {
        std::optional<synth::ControllerProfile::Input> device;
        if (identifier.isNotEmpty())
            device = synth::ControllerProfile::Input{identifier, name};
        setFeedbackOutput(profileId, device);
    };
    // FRO139: the ONLY call to the live juce::MidiOutput enumeration -- ControllersListComponent
    // itself never calls it (see its own header comment on why: it crashed inside a headless test
    // process). Mirrors showAddControllerPopover()'s identical split for juce::MidiInput.
    controllersList_.queryFeedbackOutputs = [] {
        std::vector<ControllersListComponent::FeedbackDeviceOption> result;
        for (const auto& info : juce::MidiOutput::getAvailableDevices())
            result.push_back({info.identifier, info.name});
        return result;
    };

    controllersList_.onAddControllerRequested = [this](juce::Component& anchor) { showAddControllerPopover(anchor); };

    toolbar_.onDetectToggled = [this](bool on) { setDetectActive(on); };
    toolbar_.onAssignRequested = [this](juce::Component& anchor) { showAssignMenu(anchor); };
    toolbar_.onTemplatesRequested = [this](juce::Component& anchor) { showTemplatesMenu(anchor); };
    toolbar_.onMoreRequested = [this](juce::Component& anchor) { showMoreMenu(anchor); };

    controllerSurface_.onSelectionChanged = [this](const std::vector<juce::String>& ids) { selectControls(ids); };
    controllerSurface_.onControlsMoved =
        [this](const std::vector<synth::ui::ControllerSurfaceComponent::MovedCell>& moves) {
            handleControlsMoved(moves);
        };
    controllerSurface_.onDeleteControlsRequested = [this](const std::vector<juce::String>& controlIds) {
        handleDeleteControlsRequested(controlIds);
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
    inspector_.onLearnTargetRequested = [this](juce::Component& anchor) { showAssignMenu(anchor); };
    orphanView_.onRelinkRequested = [this](juce::Component& anchor) { showRelinkMenu(anchor); };
    orphanView_.onRecreateRequested = [this](juce::Component& anchor) { showRecreateMenu(anchor); };
}

MidiRemotePanelComponent::~MidiRemotePanelComponent() { removeMouseListener(&focusOnClick_); }

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
    const bool hosted = audioEngine_->isHosted();

    std::vector<ControllersListComponent::RowModel> rows;
    if (hosted && !hostMidiProfileExists())
        rows.push_back({kHostMidiProfileId, "Host MIDI", ControllersListComponent::RowState::present});
    for (const auto& profile : profiles) {
        const auto notHere =
            hosted ? ControllersListComponent::RowState::standaloneOnly : ControllersListComponent::RowState::absent;
        rows.push_back({profile.id, profile.name,
                        isProfilePresent(profile) ? ControllersListComponent::RowState::present : notHere,
                        profile.hasOutput, profile.output.identifier});
    }
    for (const auto& ref : doc_->controllers) {
        const bool hasLocalProfile =
            std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == ref.profileId; });
        if (!hasLocalProfile)
            rows.push_back({ref.profileId, ref.name, ControllersListComponent::RowState::orphan});
    }
    controllersList_.setRows(rows);
    controllersList_.setHosted(hosted); // the plugin build's live list is exactly Host MIDI

    const bool selectedIsHostMidiRow = hosted && selectedProfileId_ == kHostMidiProfileId;
    if (!selectedProfileId_.isEmpty() && findSelectedProfile() == nullptr && !isOrphanId(selectedProfileId_) &&
        !selectedIsHostMidiRow) {
        selectedProfileId_.clear();
        selectedControlId_.clear();
        setDetectActive(false);
    }
    toolbar_.setProfileSelected(isSelectedProfileUsable());
    refreshSurfaceForSelectedProfile();
    refreshInspectorForSelection();
    refreshUndoHint(); // every history change reaches here through onChanged -> scheduleLiveRefresh
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
        // FRO272: every profile bound to this device hears it, not just the first one found -- two
        // profiles can share an input (an imported copy, a file carried over from another machine),
        // and matching only the first left the other's surface frozen until a tab switch re-seeded it.
        const synth::ControllerProfile* profile = nullptr;
        for (const auto& candidate : profiles) {
            if (candidate.input.identifier != sourceKey)
                continue;
            profileLastActivityMs_[candidate.id] = static_cast<juce::int64>(now);
            if (candidate.id == selectedProfileId_)
                profile = &candidate;
        }
        if (profile == nullptr)
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
    if (profileId == kHostMidiProfileId && audioEngine_ != nullptr && audioEngine_->isHosted() &&
        !hostMidiProfileExists())
        createHostMidiProfile();
    if (profileId != selectedProfileId_) {
        setDetectActive(false); // Detect belongs to one controller
        encoderDetect_.cancel();
    }
    if (profileId != selectedProfileId_)
        orphanStatus_.clear();
    selectedProfileId_ = profileId;
    selectedControlId_.clear();
    selectedControlIds_.clear();
    controllersList_.setSelectedProfileId(profileId);
    toolbar_.setProfileSelected(isSelectedProfileUsable());
    refreshSurfaceForSelectedProfile();
    refreshInspectorForSelection();
}

void MidiRemotePanelComponent::selectControl(const juce::String& controlId) {
    controllerSurface_.setSelectedControlId(controlId); // fans back into selectControls() below
}

// FRO270: the surface's onSelectionChanged -- see the header's doc comment on why
// selectedControlId_ is kept as this set's single member only while size() == 1.
void MidiRemotePanelComponent::selectControls(const std::vector<juce::String>& controlIds) {
    selectedControlIds_ = controlIds;
    selectedControlId_ = controlIds.size() == 1u ? controlIds.front() : juce::String();
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
            if (moduleName.isEmpty() || !parameterTargetResolves(*audioEngine_, projectIt->target.parameter)) {
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
                        if (projectIt->specEncoding == synth::Encoding::abs7 ||
                            synth::isPairedEncoding(projectIt->specEncoding)) {
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
            // FRO236: profile->actions also carries continuous assignments now -- branch on kind.
            cell.assignmentLabel = actionIt->target.isContinuous()
                                       ? synth::continuousTargetDisplayName(actionIt->target.continuous.kind)
                                       : ShortcutManager::getActionDescription(actionIt->target.action.actionId);
        } else {
            cell.assignmentLabel = "-";
        }

        cells.push_back(cell);
    }
    controllerSurface_.setControls(profile->id, cells);
}

void MidiRemotePanelComponent::refreshInspectorForSelection() {
    const bool orphan = isOrphanSelected();
    orphanView_.setVisible(orphan);
    inspector_.setVisible(!orphan);
    if (orphan) {
        toolbar_.setControlSelected(false);
        juce::String name;
        for (const auto& ref : doc_->controllers)
            if (ref.profileId == selectedProfileId_)
                name = ref.name;
        orphanView_.setOrphan(name, learnController_->countProjectAssignmentsForProfile(selectedProfileId_),
                              !getRecreateInputs().empty(), audioEngine_ != nullptr && audioEngine_->isHosted());
        orphanView_.setStatusText(orphanStatus_);
        return;
    }

    const auto* profile = findSelectedProfile();
    ControlInspectorComponent::ControlModel model;
    // FRO270: 2+ selected -- "N controls selected", every per-control field disabled. selectedControlId_
    // is empty in this case (the header's own contract: it's the single-selection anchor only), so
    // this must be checked BEFORE the "nothing selected" empty state below, which would otherwise
    // look identical.
    if (selectedControlIds_.size() >= 2u) {
        model.selectedCount = static_cast<int>(selectedControlIds_.size());
        toolbar_.setControlSelected(false);
        inspector_.setControl(model);
        return;
    }

    if (profile == nullptr || selectedControlId_.isEmpty()) {
        toolbar_.setControlSelected(false);
        inspector_.setControl(model);
        return;
    }
    auto controlIt = std::find_if(profile->controls.begin(), profile->controls.end(),
                                  [&](const auto& c) { return c.id == selectedControlId_; });
    if (controlIt == profile->controls.end()) {
        toolbar_.setControlSelected(false);
        inspector_.setControl(model);
        return;
    }

    model.hasControl = true;
    model.control = *controlIt;
    toolbar_.setControlSelected(true);

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
                row.isOrphaned = moduleName.isEmpty() || (audioEngine_ != nullptr &&
                                                          !parameterTargetResolves(*audioEngine_, a.target.parameter));
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
        // FRO236: profile->actions also carries continuous assignments now -- branch on kind.
        row.drivesLabel = a.target.isContinuous() ? synth::continuousTargetDisplayName(a.target.continuous.kind)
                                                  : ShortcutManager::getActionDescription(a.target.action.actionId);
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
    learnController_->updateProfile(updated, "Rename controller");
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

void MidiRemotePanelComponent::setFeedbackOutput(const juce::String& profileId,
                                                 const std::optional<synth::ControllerProfile::Input>& device) {
    if (learnController_ == nullptr)
        return;
    const auto& profiles = learnController_->getProfiles();
    auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == profileId; });
    if (it == profiles.end())
        return;
    auto updated = *it;
    updated.hasOutput = device.has_value();
    updated.output = device.value_or(synth::ControllerProfile::Input{});
    // Saves, republishes to the engine (setProfiles clears feedback_) and records one history step.
    learnController_->updateProfile(updated, "Set feedback output");
    rebuildFromProfiles();
}

// FRO270: `moves` is one entry per moved control, whether the drag was a lone selection or a
// group -- one updateProfile() call either way, so one undo on the controller history restores
// every moved control's PREVIOUS position at once.
void MidiRemotePanelComponent::handleControlsMoved(
    const std::vector<synth::ui::ControllerSurfaceComponent::MovedCell>& moves) {
    const auto* profile = findSelectedProfile();
    if (profile == nullptr || learnController_ == nullptr || moves.empty())
        return;
    auto updated = *profile;
    bool anyFound = false;
    for (const auto& move : moves) {
        auto it = std::find_if(updated.controls.begin(), updated.controls.end(),
                               [&](const auto& c) { return c.id == move.controlId; });
        if (it == updated.controls.end())
            continue;
        it->layout.col = move.col;
        it->layout.row = move.row;
        anyFound = true;
    }
    if (!anyFound)
        return;
    learnController_->updateProfile(updated, moves.size() == 1u ? "Move control" : "Move controls");

    // Source/UI/CLAUDE.md's rebuild-mid-gesture rule: onControlsMoved fires from
    // ControllerSurfaceCell::onDragEnded, still on that cell's own mouseUp call stack -- rebuilding
    // the grid synchronously here (setControls() clears and reallocates every cell) would free the
    // very cell whose mouseUp is still executing. Defer to the next message-loop iteration instead,
    // same fix shape as a live-drag survivor elsewhere in this codebase. SafePointer guards against
    // the panel itself being torn down before the deferred call runs (dock closed mid-drag).
    // FRO270: no explicit re-selection call is needed here -- ControllerSurfaceComponent::setControls()
    // preserves the CURRENT selection across a same-profile rebuild on its own (the automatic
    // pruning FRO270 added), and re-applying just selectedControlId_ here would wrongly collapse a
    // still-live multi-selection down to empty (selectedControlId_ is only the single-selection
    // anchor -- see the header's doc comment).
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->refreshSurfaceForSelectedProfile();
    });
}

// FRO270: `controlIds` is every id to delete, whether the request came from a lone selection or a
// group -- confirmed ONCE (mirroring ControllersListComponent's own profile-delete confirm, the
// only existing confirm-before-delete in this panel) with the total assignment count across all of
// them, then MidiLearnController::deleteControls() in one call so a single undo on either history
// restores the whole group.
void MidiRemotePanelComponent::handleDeleteControlsRequested(const std::vector<juce::String>& controlIds) {
    const auto* profile = findSelectedProfile();
    if (profile == nullptr || learnController_ == nullptr || doc_ == nullptr || controlIds.empty())
        return;
    const juce::String profileId = profile->id;

    int assignmentCount = 0;
    for (const auto& controlId : controlIds) {
        for (const auto& a : doc_->assignments)
            if (a.control.profileId == profileId && a.control.controlId == controlId)
                ++assignmentCount;
        for (const auto& a : profile->actions)
            if (a.control.controlId == controlId)
                ++assignmentCount;
    }

    const juce::String message =
        controlIds.size() == 1u
            ? (assignmentCount == 0 ? juce::String("Delete this control?")
                                    : "Delete this control? This removes " + juce::String(assignmentCount) +
                                          (assignmentCount == 1 ? " assignment." : " assignments."))
            : "Delete " + juce::String((int)controlIds.size()) + " controls? This removes " +
                  juce::String(assignmentCount) + (assignmentCount == 1 ? " assignment." : " assignments.");

    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    showPrompt("Delete controls", message, true, [safeThis, profileId, controlIds](bool ok) {
        if (!ok || safeThis == nullptr || safeThis->learnController_ == nullptr)
            return;
        safeThis->learnController_->deleteControls(profileId, controlIds);
        for (const auto& id : controlIds)
            if (safeThis->selectedControlId_ == id)
                safeThis->selectedControlId_.clear();
        safeThis->selectedControlIds_.erase(
            std::remove_if(safeThis->selectedControlIds_.begin(), safeThis->selectedControlIds_.end(),
                           [&](const juce::String& id) {
                               return std::find(controlIds.begin(), controlIds.end(), id) != controlIds.end();
                           }),
            safeThis->selectedControlIds_.end());
        safeThis->refreshSurfaceForSelectedProfile();
        safeThis->refreshInspectorForSelection();
    });
}

void MidiRemotePanelComponent::handleForgetRequested(const juce::String& assignmentId) {
    // By assignment id (project or global), so it still works when the target's module is gone --
    // an orphaned assignment's only action.
    if (learnController_ == nullptr)
        return;
    learnController_->forgetAssignment(assignmentId);
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
    orphanView_.setBounds(inspector_.getBounds());
    toolbar_.setBounds(bounds.removeFromTop(toolbar_.getPreferredHeight()));
    controllerSurface_.setBounds(bounds);
}

} // namespace synth::ui
