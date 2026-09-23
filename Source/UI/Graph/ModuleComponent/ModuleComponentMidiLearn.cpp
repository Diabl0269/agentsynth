// Right-click MIDI Learn on a module-card control (FRO130,
// docs/control/midi-remote-ui.md#right-click-midi-learn--coverage /
// docs/control/midi-remote-ui.md#the-learn-interaction). Three concerns live here:
//
//   REGISTRY   ModuleComponent::MidiLearnableRegistry (declared in ModuleComponent.h, defined
//              below) -- every control createControls()/the header-button block registered via
//              registerMidiLearnable(), and the ONE identity lookup mouseDown() uses for a
//              right-click on anything other than a generic slider (which is already matched
//              against `sliders` for the "Automate" item; see showAutomateMenuForSlider in
//              ModuleComponentInteraction.cpp).
//   MENU       appendMidiLearnMenuItems() builds the doc-exact block by calling the surface-
//              agnostic synth::ui::midilearn::appendMidiLearnMenuItems() (FRO133,
//              Source/UI/MidiRemote/MidiLearnMenu.h) with this card's own callbacks;
//              showAutomateMenuForSlider (sliders) and showMidiLearnOnlyMenu (everything else)
//              both call it, then both route through showContextMenuHook_ so a test can capture
//              the result headlessly.
//   BADGES     refreshMidiLearnBadges(), called once per module from the existing gated 15 Hz
//              timerCallback (never a new timer -- Source/UI/CLAUDE.md's envelope-playhead
//              precedent), and paintMidiLearnOverlays(), called from paint() -- both now painted
//              via the shared synth::ui::midilearn paint helpers.
//
// The armed-control "breathing outline" also lives in paintMidiLearnOverlays(): repainted only by
// the SAME gated 15 Hz tick while armed (setMidiLearnArmedParam), confined to
// repaint(controlBounds) -- never a new AnimationDriver or a third exception to the two-exception
// time-bounded-animation rule (docs/layout/animation.md#the-time-bounded-animation-rule). It is
// bounded overall by RemoteEngine's own 10 s learn timeout, which is what clears the armed param
// via setMidiLearnArmedParam({}) on cancel/bind/timeout.

#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

// ============================================================================
// ModuleComponent::MidiLearnableRegistry
// ============================================================================

void ModuleComponent::MidiLearnableRegistry::add(juce::Component& component, juce::RangedAudioParameter* param) {
    if (param == nullptr)
        return; // mirrors sliderParams' own "a control built without a real parameter" no-op
    Entry e;
    e.component = &component;
    e.param = param;
    if (auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*>(&component))
        e.baseTooltip = tooltipClient->getTooltip();
    entries_.push_back(std::move(e));
}

juce::RangedAudioParameter* ModuleComponent::MidiLearnableRegistry::find(const juce::Component* component) const {
    for (const auto& e : entries_)
        if (e.component == component)
            return e.param;
    return nullptr;
}

bool ModuleComponent::MidiLearnableRegistry::refreshBadges(
    const std::function<juce::String(const juce::String&)>& mappingLabelFor) {
    bool changed = false;
    for (auto& e : entries_) {
        const juce::String label = mappingLabelFor(e.param->paramID);
        const bool mapped = label.isNotEmpty();
        const juce::String tooltip = mapped ? "MIDI: " + label : juce::String();
        if (mapped == e.mapped && tooltip == e.tooltip)
            continue;
        e.mapped = mapped;
        e.tooltip = tooltip;
        if (auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*>(e.component)) {
            tooltipClient->setTooltip(mapped ? (e.baseTooltip.isNotEmpty() ? e.baseTooltip + " - " + tooltip : tooltip)
                                             : e.baseTooltip);
        }
        changed = true;
    }
    return changed;
}

// ============================================================================
// Registration
// ============================================================================

void ModuleComponent::registerMidiLearnable(juce::Component& control, juce::RangedAudioParameter* param) {
    midiLearnableRegistry_.add(control, param);
}

// The pick-target overlay's view of this card (FRO135): every registered control with its parameter,
// so the overlay never needs to know what a card is.
void ModuleComponent::collectPickCandidates(std::vector<synth::ui::PickCandidate>& out) const {
    for (const auto& e : midiLearnableRegistry_.entries())
        if (e.param != nullptr)
            out.push_back({e.component, synth::midi::PickTarget::parameter(nodeId, e.param->paramID)});
}

// ============================================================================
// Menu
// ============================================================================

void ModuleComponent::showMidiLearnOnlyMenu(juce::RangedAudioParameter* param) {
    if (param == nullptr)
        return;
    juce::PopupMenu menu;
    appendMidiLearnMenuItems(menu, param);
    if (menu.getNumItems() == 0)
        return; // no host wired (headless build, or GraphEditor's callbacks never set) -- nothing to show
    showContextMenuHook_(menu);
}

void ModuleComponent::appendMidiLearnMenuItems(juce::PopupMenu& menu, juce::RangedAudioParameter* param) {
    if (param == nullptr || module == nullptr || !owner.onMidiLearnRequested)
        return; // headless build, mid-teardown, or MainComponent never wired the MIDI Remote host

    juce::String label;
    if (owner.onQueryMidiMappingsForNode) {
        const auto mappings = owner.onQueryMidiMappingsForNode(nodeId);
        const auto found = mappings.find(param->paramID);
        if (found != mappings.end())
            label = found->second;
    }

    // The popup's actions run asynchronously (showMenuAsync), so `this` must be re-checked rather
    // than captured raw -- same reasoning as showAutomateMenuForSlider's own comment.
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    const juce::String paramId = param->paramID;

    synth::ui::midilearn::MenuContent content;
    content.targetName = param->getName(100);
    content.mappingLabel = label;
    content.learn = [safeThis, paramId] {
        if (safeThis != nullptr)
            safeThis->armMidiLearnFor(paramId);
    };
    content.forget = [safeThis, paramId] {
        if (safeThis != nullptr)
            safeThis->forgetMidiFor(paramId);
    };
    if (owner.onEditMidiAssignmentRequested) {
        content.editAssignment = [safeThis, paramId] {
            if (safeThis != nullptr && safeThis->owner.onEditMidiAssignmentRequested)
                safeThis->owner.onEditMidiAssignmentRequested(safeThis->nodeId, paramId);
        };
    }
    synth::ui::midilearn::appendMidiLearnMenuItems(menu, content);
}

void ModuleComponent::armMidiLearnFor(const juce::String& paramId) {
    if (owner.onMidiLearnRequested)
        owner.onMidiLearnRequested(nodeId, paramId);
}

void ModuleComponent::forgetMidiFor(const juce::String& paramId) {
    if (owner.onMidiForgetRequested)
        owner.onMidiForgetRequested(nodeId, paramId);
}

// ============================================================================
// Armed state (the breathing outline)
// ============================================================================

void ModuleComponent::setMidiLearnArmedParam(const juce::String& paramId) {
    if (midiLearnArmedParamId_ == paramId)
        return;
    // Repaint whatever WAS armed (to clear its outline) as well as whatever now is.
    const auto clearBounds = [this](const juce::String& id) {
        if (id.isEmpty())
            return;
        for (const auto& e : midiLearnableRegistry_.entries())
            if (e.param != nullptr && e.param->paramID == id) {
                repaint(e.component->getBounds().expanded(2));
                return;
            }
    };
    clearBounds(midiLearnArmedParamId_);
    midiLearnArmedParamId_ = paramId;
    midiLearnArmedSinceMs_ = juce::Time::getMillisecondCounterHiRes();
    clearBounds(midiLearnArmedParamId_);
}

// ============================================================================
// Badges + armed outline (paint)
// ============================================================================

void ModuleComponent::refreshMidiLearnBadges() {
    if (module == nullptr || !owner.onQueryMidiMappingsForNode)
        return;
    const auto mappings = owner.onQueryMidiMappingsForNode(nodeId);
    const bool changed = midiLearnableRegistry_.refreshBadges([&mappings](const juce::String& paramId) {
        const auto found = mappings.find(paramId);
        return found != mappings.end() ? found->second : juce::String();
    });
    if (changed)
        repaint();
}

void ModuleComponent::paintMidiLearnOverlays(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour badgeColour = lf != nullptr ? lf->getTheme().colors.midiMapped : juce::Colour(0xffB48EF5);
    const juce::Colour armedColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

    for (const auto& e : midiLearnableRegistry_.entries()) {
        if (e.mapped)
            synth::ui::midilearn::paintMidiMappedBadge(g, e.component->getBounds(), badgeColour);
    }

    if (midiLearnArmedParamId_.isEmpty())
        return;

    for (const auto& e : midiLearnableRegistry_.entries()) {
        if (e.param == nullptr || e.param->paramID != midiLearnArmedParamId_)
            continue;
        // Time-bounded overall by RemoteEngine's 10 s learn timeout (setMidiLearnArmedParam({})
        // on cancel/bind/timeout); repainted only by the existing gated 15 Hz timerCallback while
        // armed -- never a free-running animation (docs/control/midi-remote-ui.md#the-learn-interaction).
        synth::ui::midilearn::paintMidiLearnArmedOutline(g, e.component->getBounds(), armedColour,
                                                         midiLearnArmedSinceMs_);
        break;
    }
}
