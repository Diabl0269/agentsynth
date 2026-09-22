// Right-click MIDI Learn on a mixer column's controls (FRO133,
// docs/control/midi-remote-ui.md#right-click-midi-learn--coverage): the fader, pan, Mute and every
// send-row level knob are ordinary ChannelStripModule parameters, so this reuses the exact
// registry/menu/badge/armed-outline shape FRO130 built for the module card
// (Source/UI/Graph/ModuleComponent/ModuleComponentMidiLearn.cpp), via the surface-agnostic helpers
// in Source/UI/MidiRemote/MidiLearnMenu.h -- MixerColumnComponent keeps its OWN small registry
// (MidiLearnableEntry) rather than sharing ModuleComponent's, since the two components have no
// common base to hang a shared registry off (Source/UI/CLAUDE.md's "keep per-surface state next
// to the surface that owns its lifetime" reasoning).
//
// Learn/forget/query all route through the SAME GraphEditor-owned callbacks ModuleComponent
// already uses (owner_->onMidiLearnRequested etc., wired once in
// Source/MainComponent/MainComponentSetup.cpp) -- a mixer column's nodeId_ IS the same
// AudioProcessorGraph::NodeID as its strip's canvas ModuleComponent, so there is no second
// MidiLearnController entry point to add for parameter targets (unlike the transport bar's action
// targets, which have no node and need MidiLearnController::armAction() instead --
// TimelineTransportBarMidiLearn.cpp).
//
// Solo is deliberately NOT covered here: ChannelStripModule::soloed_ is engine state (an
// std::atomic<bool>), not a juce::RangedAudioParameter (Source/Modules/ChannelStripModule.h), so
// it has nothing for a Target::Parameter to point at. See the FRO133 follow-up ticket.

#include "MixerColumnComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <algorithm>

namespace synth::ui {

// ============================================================================
// Registration
// ============================================================================

void MixerColumnComponent::registerMidiLearnable(juce::Component& control, juce::RangedAudioParameter* param) {
    if (param == nullptr)
        return; // mirrors ModuleComponent::MidiLearnableRegistry::add's own no-op

    if (std::find(midiLearnListenerTargets_.begin(), midiLearnListenerTargets_.end(), &control) ==
        midiLearnListenerTargets_.end()) {
        control.addMouseListener(this, false);
        midiLearnListenerTargets_.push_back(&control);
    }

    MidiLearnableEntry e;
    e.component = &control;
    e.param = param;
    midiLearnableEntries_.push_back(e);
}

juce::RangedAudioParameter*
MixerColumnComponent::findMidiLearnableParamForTest(const juce::Component* component) const {
    for (const auto& e : midiLearnableEntries_)
        if (e.component == component)
            return e.param;
    return nullptr;
}

bool MixerColumnComponent::isMidiLearnBadgeMappedForTest(const juce::Component* component) const {
    for (const auto& e : midiLearnableEntries_)
        if (e.component == component)
            return e.mapped;
    return false;
}

// ============================================================================
// Right-click dispatch
// ============================================================================

void MixerColumnComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.eventComponent == this)
        return; // a click on the column's own background -- mouseUp() handles select-on-click

    if (!e.mods.isPopupMenu())
        return; // left-click on a registered control is the control's own business (drag, toggle, ...)

    juce::RangedAudioParameter* param = nullptr;
    for (const auto& entry : midiLearnableEntries_)
        if (entry.component == e.eventComponent) {
            param = entry.param;
            break;
        }
    if (param == nullptr || graphEditor_ == nullptr || !graphEditor_->onMidiLearnRequested)
        return; // not a registered control, or the MIDI Remote host isn't wired (headless build)

    juce::String label;
    if (graphEditor_->onQueryMidiMappingsForNode) {
        const auto mappings = graphEditor_->onQueryMidiMappingsForNode(nodeId_);
        const auto found = mappings.find(param->paramID);
        if (found != mappings.end())
            label = found->second;
    }

    // Same reasoning as ModuleComponent::appendMidiLearnMenuItems: the popup's actions run
    // asynchronously (showMenuAsync), so `this` must be re-checked rather than captured raw --
    // MixerPanelComponent::rebuild() can destroy this column between the click and the choice.
    juce::Component::SafePointer<MixerColumnComponent> safeThis(this);
    const juce::String paramId = param->paramID;
    GraphEditor* graphEditor = graphEditor_;

    synth::ui::midilearn::MenuContent content;
    content.targetName = param->getName(100);
    content.mappingLabel = label;
    content.learn = [safeThis, graphEditor, paramId] {
        if (safeThis != nullptr && graphEditor->onMidiLearnRequested)
            graphEditor->onMidiLearnRequested(safeThis->nodeId_, paramId);
    };
    content.forget = [safeThis, graphEditor, paramId] {
        if (safeThis != nullptr && graphEditor->onMidiForgetRequested)
            graphEditor->onMidiForgetRequested(safeThis->nodeId_, paramId);
    };
    if (graphEditor_->onEditMidiAssignmentRequested) {
        content.editAssignment = [safeThis, graphEditor, paramId] {
            if (safeThis != nullptr && graphEditor->onEditMidiAssignmentRequested)
                graphEditor->onEditMidiAssignmentRequested(safeThis->nodeId_, paramId);
        };
    }

    juce::PopupMenu menu;
    synth::ui::midilearn::appendMidiLearnMenuItems(menu, content);
    if (menu.getNumItems() == 0)
        return;
    showContextMenuHook_(menu);
}

// ============================================================================
// Armed state (the breathing outline)
// ============================================================================

void MixerColumnComponent::setMidiLearnArmedParam(const juce::String& paramId) {
    if (midiLearnArmedParamId_ == paramId)
        return;
    const auto repaintFor = [this](const juce::String& id) {
        if (id.isEmpty())
            return;
        for (const auto& e : midiLearnableEntries_)
            if (e.param != nullptr && e.param->paramID == id) {
                repaint(getLocalArea(e.component->getParentComponent(), e.component->getLocalBounds()).expanded(2));
                return;
            }
    };
    repaintFor(midiLearnArmedParamId_);
    midiLearnArmedParamId_ = paramId;
    midiLearnArmedSinceMs_ = juce::Time::getMillisecondCounterHiRes();
    repaintFor(midiLearnArmedParamId_);
}

// ============================================================================
// Badges + armed outline (paint)
// ============================================================================

void MixerColumnComponent::refreshMidiLearnBadges() {
    if (graphEditor_ == nullptr || !graphEditor_->onQueryMidiMappingsForNode)
        return;
    const auto mappings = graphEditor_->onQueryMidiMappingsForNode(nodeId_);
    bool changed = false;
    for (auto& e : midiLearnableEntries_) {
        const auto found = mappings.find(e.param->paramID);
        const bool mapped = found != mappings.end();
        if (mapped == e.mapped)
            continue;
        e.mapped = mapped;
        changed = true;
    }
    if (changed)
        repaint();
}

void MixerColumnComponent::paintMidiLearnOverlays(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour badgeColour = lf != nullptr ? lf->getTheme().colors.midiMapped : juce::Colour(0xffB48EF5);
    const juce::Colour armedColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

    for (const auto& e : midiLearnableEntries_) {
        if (!e.mapped)
            continue;
        const auto bounds = getLocalArea(e.component->getParentComponent(), e.component->getLocalBounds());
        synth::ui::midilearn::paintMidiMappedBadge(g, bounds, badgeColour);
    }

    if (midiLearnArmedParamId_.isEmpty())
        return;

    for (const auto& e : midiLearnableEntries_) {
        if (e.param == nullptr || e.param->paramID != midiLearnArmedParamId_)
            continue;
        const auto bounds = getLocalArea(e.component->getParentComponent(), e.component->getLocalBounds());
        synth::ui::midilearn::paintMidiLearnArmedOutline(g, bounds, armedColour, midiLearnArmedSinceMs_);
        break;
    }
}

} // namespace synth::ui
