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
//   MENU       appendMidiLearnMenuItems() builds the doc-exact block; showAutomateMenuForSlider
//              (sliders) and showMidiLearnOnlyMenu (everything else) both call it, then both route
//              through showContextMenuHook_ so a test can capture the result headlessly.
//   BADGES     refreshMidiLearnBadges(), called once per module from the existing gated 15 Hz
//              timerCallback (never a new timer -- Source/UI/CLAUDE.md's envelope-playhead
//              precedent), and paintMidiLearnOverlays(), called from paint().
//
// The armed-control "breathing outline" also lives in paintMidiLearnOverlays(): a plain
// elapsed-time sine computed at paint time, repainted only by the SAME gated 15 Hz tick while
// armed (setMidiLearnArmedParam), confined to repaint(controlBounds) -- never a new AnimationDriver
// or a third exception to the two-exception time-bounded-animation rule
// (docs/layout/animation.md#the-time-bounded-animation-rule). It is bounded overall by
// RemoteEngine's own 10 s learn timeout, which is what clears the armed param via
// setMidiLearnArmedParam({}) on cancel/bind/timeout.

#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <cmath>

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

    menu.addSeparator();

    if (label.isEmpty()) {
        menu.addItem("MIDI Learn '" + param->getName(100) + "'...", [safeThis, paramId] {
            if (safeThis != nullptr)
                safeThis->armMidiLearnFor(paramId);
        });
        return;
    }

    menu.addItem(-1, "MIDI: " + label, false, false); // disabled title row -- tells you what drives it

    if (owner.onEditMidiAssignmentRequested) {
        menu.addItem("Edit MIDI assignment...", [safeThis, paramId] {
            if (safeThis != nullptr && safeThis->owner.onEditMidiAssignmentRequested)
                safeThis->owner.onEditMidiAssignmentRequested(safeThis->nodeId, paramId);
        });
    }
    menu.addItem("MIDI Learn again...", [safeThis, paramId] {
        if (safeThis != nullptr)
            safeThis->armMidiLearnFor(paramId);
    });
    menu.addItem("Forget MIDI", [safeThis, paramId] {
        if (safeThis != nullptr)
            safeThis->forgetMidiFor(paramId);
    });
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

    constexpr int kBadgeDiameter = 6;
    for (const auto& e : midiLearnableRegistry_.entries()) {
        if (!e.mapped)
            continue;
        const auto bounds = e.component->getBounds();
        g.setColour(badgeColour);
        g.fillEllipse(static_cast<float>(bounds.getRight() - kBadgeDiameter), static_cast<float>(bounds.getY()),
                      static_cast<float>(kBadgeDiameter), static_cast<float>(kBadgeDiameter));
    }

    if (midiLearnArmedParamId_.isEmpty())
        return;

    for (const auto& e : midiLearnableRegistry_.entries()) {
        if (e.param == nullptr || e.param->paramID != midiLearnArmedParamId_)
            continue;
        // A thin breathing outline, alpha easing ~0.4..1.0 -- explicitly not a glow (Obsidian has
        // glow 0, and this must read the same in every theme;
        // docs/control/midi-remote-ui.md#the-learn-interaction). Time-bounded overall by
        // RemoteEngine's 10 s learn timeout (setMidiLearnArmedParam({}) on cancel/bind/timeout);
        // repainted only by the existing gated 15 Hz timerCallback while armed -- never a free-
        // running animation.
        const double elapsedSec = (juce::Time::getMillisecondCounterHiRes() - midiLearnArmedSinceMs_) / 1000.0;
        constexpr double kBreathPeriodSec = 1.2;
        const float phase = static_cast<float>(
            0.5 * (1.0 - std::cos(2.0 * juce::MathConstants<double>::pi * elapsedSec / kBreathPeriodSec)));
        const float alpha = 0.4f + 0.6f * phase;
        g.setColour(armedColour.withAlpha(alpha));
        g.drawRect(e.component->getBounds(), 1);
        break;
    }
}
