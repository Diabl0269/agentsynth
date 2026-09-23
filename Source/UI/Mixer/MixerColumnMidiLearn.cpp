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
// FRO253: Solo IS covered here too, but as a node command rather than a parameter --
// ChannelStripModule::soloed_ is engine state (an std::atomic<bool>), not a
// juce::RangedAudioParameter (Source/Modules/ChannelStripModule.h), so it has nothing for a
// Target::Parameter to point at. Its registry entry (isSolo=true, param=null) routes through
// MixerPanelComponent's onSoloMidiLearnRequested/onSoloMidiForgetRequested/onQuerySoloMidiMapping
// instead of GraphEditor's node-keyed parameter callbacks -- Solo has no graph parameter identity
// for those to key on, so it needs its own small seam rather than sharing MidiLearnController::
// arm()/forget()/queryMappings() (see MidiLearnController::armNodeCommand() and friends).

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

// FRO253: mirrors registerMidiLearnable() above for the Solo node command entry -- param stays
// null (isSolo=true is what mouseDown()/refreshMidiLearnBadges() key on instead).
void MixerColumnComponent::registerSoloMidiLearnable() {
    if (std::find(midiLearnListenerTargets_.begin(), midiLearnListenerTargets_.end(), &soloButton_) ==
        midiLearnListenerTargets_.end()) {
        soloButton_.addMouseListener(this, false);
        midiLearnListenerTargets_.push_back(&soloButton_);
    }

    MidiLearnableEntry e;
    e.component = &soloButton_;
    e.isSolo = true;
    e.targetName = "Solo";
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

    const MidiLearnableEntry* entry = nullptr;
    for (const auto& candidate : midiLearnableEntries_)
        if (candidate.component == e.eventComponent) {
            entry = &candidate;
            break;
        }
    if (entry == nullptr)
        return; // not a registered control

    if (entry->isSolo)
        showSoloMidiLearnMenu();
    else if (entry->param != nullptr)
        showParamMidiLearnMenu(*entry->param);
}

// Split out of mouseDown() above so each menu-building branch stays well under the function-size
// cap -- see the two callers for when each one applies.
void MixerColumnComponent::showParamMidiLearnMenu(juce::RangedAudioParameter& param) {
    if (graphEditor_ == nullptr || !graphEditor_->onMidiLearnRequested)
        return; // the MIDI Remote host isn't wired (headless build)

    juce::String label;
    if (graphEditor_->onQueryMidiMappingsForNode) {
        const auto mappings = graphEditor_->onQueryMidiMappingsForNode(nodeId_);
        const auto found = mappings.find(param.paramID);
        if (found != mappings.end())
            label = found->second;
    }

    // Same reasoning as ModuleComponent::appendMidiLearnMenuItems: the popup's actions run
    // asynchronously (showMenuAsync), so `this` must be re-checked rather than captured raw --
    // MixerPanelComponent::rebuild() can destroy this column between the click and the choice.
    juce::Component::SafePointer<MixerColumnComponent> safeThis(this);
    const juce::String paramId = param.paramID;
    GraphEditor* graphEditor = graphEditor_;

    synth::ui::midilearn::MenuContent content;
    content.targetName = param.getName(100);
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

// FRO253: Solo's own menu -- same shape as showParamMidiLearnMenu() above, but through
// MixerPanelComponent's onSoloMidiLearnRequested/onSoloMidiForgetRequested/onQuerySoloMidiMapping
// (set by MixerPanelComponent::rebuild(), forwarding MainComponent's single wiring) rather than
// GraphEditor's parameter-keyed callbacks -- Solo has no paramId for those to key on.
void MixerColumnComponent::showSoloMidiLearnMenu() {
    if (!onSoloMidiLearnRequested)
        return; // the MIDI Remote host isn't wired (headless build)

    const juce::String label = onQuerySoloMidiMapping ? onQuerySoloMidiMapping() : juce::String();

    juce::Component::SafePointer<MixerColumnComponent> safeThis(this);

    synth::ui::midilearn::MenuContent content;
    content.targetName = "Solo";
    content.mappingLabel = label;
    content.learn = [safeThis] {
        if (safeThis != nullptr && safeThis->onSoloMidiLearnRequested)
            safeThis->onSoloMidiLearnRequested();
    };
    content.forget = [safeThis] {
        if (safeThis != nullptr && safeThis->onSoloMidiForgetRequested)
            safeThis->onSoloMidiForgetRequested();
    };

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
                repaint(getLocalArea(e.component, e.component->getLocalBounds()).expanded(2));
                return;
            }
    };
    repaintFor(midiLearnArmedParamId_);
    midiLearnArmedParamId_ = paramId;
    midiLearnArmedSinceMs_ = juce::Time::getMillisecondCounterHiRes();
    repaintFor(midiLearnArmedParamId_);
}

// FRO253: mirrors setMidiLearnArmedParam() above for the Solo entry (isSolo, no paramId to key on).
void MixerColumnComponent::setMidiLearnArmedSolo(bool armed) {
    if (midiLearnArmedSolo_ == armed)
        return;
    midiLearnArmedSolo_ = armed;
    if (midiLearnArmedSolo_)
        midiLearnArmedSoloSinceMs_ = juce::Time::getMillisecondCounterHiRes();
    repaint(getLocalArea(&soloButton_, soloButton_.getLocalBounds()).expanded(2));
}

// ============================================================================
// Badges + armed outline (paint)
// ============================================================================

void MixerColumnComponent::refreshMidiLearnBadges() {
    // Solo's mapped flag comes from onQuerySoloMidiMapping (no GraphEditor callback to key on --
    // it has no paramId), so this runs regardless of whether the parameter callback below is wired.
    bool changed = false;
    for (auto& e : midiLearnableEntries_) {
        if (!e.isSolo)
            continue;
        const bool mapped = onQuerySoloMidiMapping && onQuerySoloMidiMapping().isNotEmpty();
        if (mapped != e.mapped) {
            e.mapped = mapped;
            changed = true;
        }
    }

    if (graphEditor_ != nullptr && graphEditor_->onQueryMidiMappingsForNode) {
        const auto mappings = graphEditor_->onQueryMidiMappingsForNode(nodeId_);
        for (auto& e : midiLearnableEntries_) {
            if (e.isSolo || e.param == nullptr)
                continue;
            const auto found = mappings.find(e.param->paramID);
            const bool mapped = found != mappings.end();
            if (mapped == e.mapped)
                continue;
            e.mapped = mapped;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

// FRO256: called from refreshMeter()'s existing 10 Hz tick -- paintMidiLearnArmedOutline() computes
// its alpha from wall time on every paint(), so the outline only visibly "breathes" if something
// keeps asking for a repaint while armed; nothing did before this (setMidiLearnArmedParam/
// setMidiLearnArmedSolo above each repaint exactly once, on the arm/clear edge). Confined to the
// armed control's own bounds, same as every other MIDI Learn repaint here -- never the whole
// column, and bounded overall by RemoteEngine's 10 s learn timeout, not by this tick
// (Source/UI/CLAUDE.md's no-unconditional-repaint rule: this is a no-op whenever nothing is armed).
void MixerColumnComponent::repaintArmedMidiLearnOutline() {
    if (midiLearnArmedSolo_) {
        repaint(getLocalArea(&soloButton_, soloButton_.getLocalBounds()).expanded(2));
        ++midiLearnArmedRepaintCount_;
    }
    if (midiLearnArmedParamId_.isEmpty())
        return;
    for (const auto& e : midiLearnableEntries_) {
        if (e.param == nullptr || e.param->paramID != midiLearnArmedParamId_)
            continue;
        repaint(getLocalArea(e.component, e.component->getLocalBounds()).expanded(2));
        ++midiLearnArmedRepaintCount_;
        break;
    }
}

void MixerColumnComponent::paintMidiLearnOverlays(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour badgeColour = lf != nullptr ? lf->getTheme().colors.midiMapped : juce::Colour(0xffB48EF5);
    const juce::Colour armedColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

    for (const auto& e : midiLearnableEntries_) {
        if (!e.mapped)
            continue;
        // FRO256: `e.component` is a direct child for the fader/pan/mute/solo entries but TWO
        // levels deep for a send-row knob (column -> sendList_ -> knob) -- getLocalArea(component,
        // component's own local bounds) walks the parent chain regardless of depth, unlike
        // component->getBounds() (one level: bounds in its IMMEDIATE parent's frame only). Passing
        // component->getParentComponent() as the source while still handing it component's own
        // local bounds (the pre-fix code) mixed two different coordinate frames and put every
        // badge/outline at this column's own top-left corner instead of on the control.
        const auto bounds = getLocalArea(e.component, e.component->getLocalBounds());
        synth::ui::midilearn::paintMidiMappedBadge(g, bounds, badgeColour);
    }

    if (midiLearnArmedSolo_) {
        const auto bounds = getLocalArea(&soloButton_, soloButton_.getLocalBounds());
        synth::ui::midilearn::paintMidiLearnArmedOutline(g, bounds, armedColour, midiLearnArmedSoloSinceMs_);
    }

    if (midiLearnArmedParamId_.isEmpty())
        return;

    for (const auto& e : midiLearnableEntries_) {
        if (e.param == nullptr || e.param->paramID != midiLearnArmedParamId_)
            continue;
        const auto bounds = getLocalArea(e.component, e.component->getLocalBounds());
        synth::ui::midilearn::paintMidiLearnArmedOutline(g, bounds, armedColour, midiLearnArmedSinceMs_);
        break;
    }
}

} // namespace synth::ui
