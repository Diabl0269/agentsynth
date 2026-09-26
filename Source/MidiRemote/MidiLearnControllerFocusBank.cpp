// Concern: FRO141 (docs/control/midi-remote.md#focus-bank) -- transient bindings for the controls
// marked "follow selection" (Control::focusBank), rebuilt whenever the canvas selection changes.
// Polled from focusBankWatcher_'s 200 ms tick (MidiLearnController.h), a SEPARATE UiWatcher instance
// from the one arm()/armAction()/armNodeCommand() use for a learn's silent timeout, so this keeps
// running whether or not a learn is in progress.

#include "MidiRemote/MidiLearnController.h"

#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiRemoteMapping.h"
#include "Timeline/AutomationBinding.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/PickTargetOverlay/PickCandidate.h"

#include <algorithm>

namespace synth::midi {

namespace {

juce::String focusAssignmentId(const juce::String& profileId, const juce::String& controlId) {
    return "focus:" + profileId + ":" + controlId;
}

} // namespace

std::vector<const Control*> MidiLearnController::collectFocusBankControls(const ControllerProfile& profile) const {
    std::vector<const Control*> result;
    for (const auto& control : profile.controls)
        if (control.focusBank)
            result.push_back(&control);
    // docs/control/midi-remote.md#focus-bank: bank order is layout order, row then col.
    std::stable_sort(result.begin(), result.end(), [](const Control* a, const Control* b) {
        if (a->layout.row != b->layout.row)
            return a->layout.row < b->layout.row;
        return a->layout.col < b->layout.col;
    });
    return result;
}

// FRO141: exactly one selected module and no pick-target session binds the bank; anything else
// (none selected, several selected, or a pick in progress) leaves the CURRENT binding alone while
// a pick is active -- see the .h's own comment on focusBankWatcher_ -- but clears it in every other
// case. Only rebuilds when the selection or the set of bank controls actually changed since the last poll: republishing
// on every 200 ms tick would bump the engine's snapshot generation and needlessly end any in-flight gesture on an
// unrelated assignment (RemoteEngineReconcile.cpp's gesture cleanup).
void MidiLearnController::pollFocusBankSelection() {
    if (isPickingTarget())
        return;

    const auto selected = graphEditor_.getSelectedNodes();
    const bool haveSelection = selected.size() == 1;
    const auto newSelection = haveSelection ? selected.front() : juce::AudioProcessorGraph::NodeID();

    // focusBankSignature_: which controls are in each profile's bank ("profileId:controlId,..."). A
    // change (the inspector's "Follow selection" toggle, undo/redo, a profile added or removed)
    // rebinds even though the canvas selection itself didn't move.
    juce::String signature;
    for (const auto& profile : profiles_)
        for (const auto* control : collectFocusBankControls(profile))
            signature << profile.id << ':' << control->id << ',';

    const bool sameSelection =
        haveSelection == focusBankHasSelection_ && (!haveSelection || newSelection == focusBankSelectedNode_);
    if (sameSelection && signature == focusBankSignature_)
        return;

    focusBankSignature_ = signature;

    focusBankHasSelection_ = haveSelection;
    focusBankSelectedNode_ = newSelection;
    rebuildFocusBankAssignments();
}

void MidiLearnController::rebuildFocusBankAssignments() {
    std::vector<Assignment> transient;

    if (focusBankHasSelection_) {
        auto* node = engine_.getGraph().getNodeForId(focusBankSelectedNode_);
        ModuleComponent* moduleComponent = nullptr;
        for (auto* module : graphEditor_.getModuleComponents())
            if (module != nullptr && module->getNodeId() == focusBankSelectedNode_)
                moduleComponent = module;

        if (node != nullptr && moduleComponent != nullptr) {
            std::vector<synth::ui::PickCandidate> candidates;
            moduleComponent->collectPickCandidates(candidates);
            // Registry order (collectPickCandidates' own contract) IS on-card order; keep only a
            // parameter candidate whose control is actually showing. isShowing() also needs a
            // native peer, which a headless test host never has (PickTargetOverlay.cpp's own
            // comment); this checks the candidate's own isVisible() flag instead of walking the
            // whole ancestor chain -- addAndMakeVisible() sets it on every learnable control at
            // creation, and a card's own internal show/hide toggles (a collapsed section, a hidden
            // tab) already clear it directly on that control, so it needs no ancestor walk to be
            // meaningful.
            std::vector<synth::ui::PickCandidate> eligible;
            for (auto& candidate : candidates) {
                if (candidate.target.kind != PickTarget::Kind::parameter)
                    continue;
                auto* component = candidate.component.getComponent();
                if (component == nullptr || !component->isVisible())
                    continue;
                eligible.push_back(candidate);
            }

            const juce::String uuid = ensureNodeUuid(focusBankSelectedNode_);
            if (uuid.isNotEmpty()) {
                auto* processor = node->getProcessor();
                for (const auto& profile : profiles_) {
                    const auto bankControls = collectFocusBankControls(profile);
                    for (std::size_t i = 0; i < bankControls.size() && i < eligible.size(); ++i) {
                        synth::Target target;
                        target.kind = synth::Target::Kind::parameter;
                        target.parameter.nodeUuid = uuid;
                        target.parameter.paramId = eligible[i].target.paramId;
                        // FRO137: same hosted-plugin-card fallback arm() resolves -- harmless to
                        // compute unconditionally, since a built-in AudioParameter's resolution
                        // never consults this hint.
                        target.parameter.paramIndexHint =
                            synth::captureParamIndexHint(processor, eligible[i].target.paramId);

                        // takeover stays useDefault -- getDefaultTakeover() is Scale unless the
                        // user has changed Preferences' default.
                        Assignment a = makeAssignmentForControl(profile, *bankControls[i], target);
                        a.id = focusAssignmentId(profile.id, bankControls[i]->id);
                        transient.push_back(std::move(a));
                    }
                }
            }
        }
    }

    remoteEngine_.setTransientAssignments(std::move(transient));
    // FRO141: resolve against the live graph right away -- mirrors publishAssignments()'s own
    // comment on why setAssignments()'s own graph==nullptr rebuild isn't enough for a brand-new
    // assignment id (a freshly selected module's parameters would otherwise stay unresolved until
    // some unrelated graph change happened to reach MainComponent's reconcile funnel).
    remoteEngine_.reconcile(engine_.getGraph());
    // The MIDI Remote panel redraws on onChanged (FRO263): without it a bank cell kept showing
    // "Follows selection" after a rebind until something else refreshed the panel.
    if (onChanged)
        onChanged();
}

} // namespace synth::midi
