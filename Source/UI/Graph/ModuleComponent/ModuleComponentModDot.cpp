// ModuleComponentModDot.cpp -- a card's side of the mod dot (docs/modules/modulation.md#drag-to-knob-modulation):
// the press claim that routes the dot's mouse events to the canvas' ModDotController, and the transparent
// Tab-stop button each modulated knob or fader gets, centred on the dot (docs/layout/module-card.md#modulation-dot),
// and the Drum Kit's pad knobs as cable targets (a cable on one binds to the picked pad through a ModSlotHost slot).

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/ModSlotHost.h"
#include "Modules/ModuleBase.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardWidgets/CardControlGestures.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include <set>

// Wires the third card-control claim: a plain press on the landing dot of a knob that has at least one
// attenuverter routing is the mod-dot gesture. Cmd-press, and a dot with nothing to adjust, fall through
// to the cable pickup (wantsCablePickupGestureFor), exactly as before.
void ModuleComponent::wireModDotGesture(juce::Slider& knob, synth::ui::CardControlGestures& gestures,
                                        juce::RangedAudioParameter* param) {
    gestures.wantsModDotGesture = [this, param, &knob](const juce::MouseEvent& e) {
        const int destChannel = destChannelForKnob(param, knob);
        if (e.mods.isCommandDown() || firstAttenuverterForChannel(destChannel).uid == 0)
            return false;
        return destChannel >= 0 && getModTargetKnobAnchor(destChannel).has_value() &&
               pressIsOnLandingDot(param, knob, e);
    };
    gestures.onModDotGesture = [this, param, &knob](const juce::MouseEvent& e, int phase) {
        const int destChannel = destChannelForKnob(param, knob);
        owner.getModDot().routeGesture(nodeId, destChannel, knob, getModDotButton(destChannel), e, phase);
    };
    gestures.onModDotCancelled = [this] { owner.getModDot().cancel(); };
}

synth::ui::ModDotButton* ModuleComponent::getModDotButton(int destChannel) const {
    for (auto* button : modDotButtons_)
        if (button->getDestChannel() == destChannel && button->isVisible())
            return button;
    return nullptr;
}

void ModuleComponent::syncModDotButtons() {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return;
    std::set<int> live;
    for (const auto& target : mb->getModulationTargets()) {
        // The control the dot belongs to: the knob or fader, else (the Filter's and LFO's hidden Morph knobs) the
        // map slider that stands in for it.
        const int si = sliderIndexForModTarget(target);
        juce::Component* control = si >= 0 ? static_cast<juce::Component*>(sliders[si]) : nullptr;
        if (control == nullptr && morphCard_ != nullptr)
            control = morphCard_->stripControlFor(target.channelIndex);
        bool onPadKnob = false;
        if (control == nullptr) {
            control = padKnobForTarget(target).slider; // a pad cable's dot sits on the picked pad's knob only
            onPadKnob = control != nullptr;
        }
        const auto anchor = getModTargetKnobAnchor(target.channelIndex);
        if (control == nullptr || !anchor.has_value())
            continue;
        const int count = owner.getModDot().knobSourceCount(nodeId, target.channelIndex);
        if (count == 0)
            continue;
        live.insert(target.channelIndex);

        synth::ui::ModDotButton* button = nullptr;
        for (auto* b : modDotButtons_)
            if (b->getDestChannel() == target.channelIndex)
                button = b;
        if (button != nullptr && button->getKnob() != control) { // the card rebuilt its controls
            modDotButtons_.removeObject(button);
            button = nullptr;
        }
        if (button == nullptr) {
            const int channel = target.channelIndex;
            button = modDotButtons_.add(new synth::ui::ModDotButton(channel, *control));
            button->onStep = [this, channel](float delta) {
                if (auto* b = getModDotButton(channel); b != nullptr && b->getKnob() != nullptr)
                    owner.getModDot().step(nodeId, channel, *b->getKnob(), delta);
            };
            button->onActivate = [this, channel] {
                if (auto* b = getModDotButton(channel); b != nullptr && owner.getModDot().onModDotClicked)
                    owner.getModDot().onModDotClicked(nodeId, channel, *b);
            };
            addChildComponent(button);
        }

        // A pad knob's own title names the pad ("Level: Kick"), where the slot's target only has its number.
        const auto knobName =
            onPadKnob && control->getTitle().isNotEmpty() ? control->getTitle() : knobNameForModTarget(mb, target);
        const auto title = knobName + " modulation, " + juce::String(count) + (count == 1 ? " source" : " sources");
        if (button->getTitle() != title)
            button->setTitle(title);
        auto tip = "Modulation sources for " + knobName;
        if (owner.getDoubleClickPortDisconnectEnabled())
            tip += ". Double-click to remove";
        if (button->getTooltip() != tip)
            button->setTooltip(tip);
        const auto centre = anchor->roundToInt();
        button->setBounds(
            juce::Rectangle<int>(synth::ui::ModDotButton::kSize, synth::ui::ModDotButton::kSize).withCentre(centre));
        button->setDotOffset(*anchor - centre.toFloat());
        button->setVisible(true);
    }
    for (auto* button : modDotButtons_)
        if (live.count(button->getDestChannel()) == 0 && button->isVisible())
            button->setVisible(false);
}

// A double-click on a visible CV jack that drives a knob: the knob's own dot menu logic instead of disconnecting
// everything on the jack. False when the jack drives no modulated knob, so the plain disconnect applies.
bool ModuleComponent::handleModJackDoubleClick(int visibleInputIndex) {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return false;
    for (const auto& target : mb->getModulationTargets()) {
        if (mb->mapInputChannel(target.channelIndex).visibleJackIndex != visibleInputIndex)
            continue;
        if (synth::ui::knobModSources(owner, nodeId, target.channelIndex).empty())
            return false;
        auto* button = getModDotButton(target.channelIndex);
        owner.getModDot().dotDoubleClicked(nodeId, target.channelIndex,
                                           button != nullptr ? static_cast<juce::Component&>(*button) : *this);
        return true;
    }
    return false;
}

// A card's pad knobs as cable targets. The Drum Kit card shows one set of Level, Tune, Decay and Start knobs that
// re-bind to whichever pad is picked; the knob rings and offers the drop only while it shows the pad the slot is
// bound to, so picking another pad leaves the cable moving the first.
namespace {
// A control counts only while it is on the card: every ancestor up to the card is visible.
bool shownOn(const juce::Component& card, const juce::Component& control) {
    const juce::Component* c = &control;
    for (; c != nullptr && c != &card; c = c->getParentComponent())
        if (!c->isVisible())
            return false;
    return c == &card;
}
} // namespace

// The knob the slot channel of `target` rings: the registry entry of its parameter, when a pad knob shows it now.
ModuleComponent::PadKnob ModuleComponent::padKnobForTarget(const ModulationTarget& target) const {
    auto* host = dynamic_cast<const synth::ModSlotHost*>(module);
    if (host == nullptr || target.paramId.isEmpty() ||
        host->modChannelForParam(target.paramId) != target.channelIndex ||
        dynamic_cast<const synth::HostedPluginModule*>(module) != nullptr)
        return {};
    for (const auto& entry : midiLearnableRegistry_.entries()) {
        if (entry.hosted || entry.paramId != target.paramId)
            continue;
        auto* knob = dynamic_cast<juce::Slider*>(entry.component);
        if (knob == nullptr || !shownOn(*this, *knob))
            return {};
        return {knob, getLocalArea(knob, knob->getLocalBounds())};
    }
    return {};
}

// The pad knob under `localPoint`: its slot channel when its pad has one, else a stand-in naming the registry entry
// that the drop turns into a claim. A pad knob on a kit with every slot taken still answers, so the drop can say why it
// was refused.
std::optional<ModuleComponent::Port> ModuleComponent::padSlotPortForPoint(juce::Point<int> localPoint) const {
    auto* host = dynamic_cast<const synth::ModSlotHost*>(module);
    if (host == nullptr || dynamic_cast<const synth::HostedPluginModule*>(module) != nullptr)
        return std::nullopt;
    const auto& entries = midiLearnableRegistry_.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (entry.hosted || !DrumKitModule::isPadParamId(entry.paramId) || entry.component == nullptr ||
            !shownOn(*this, *entry.component))
            continue;
        const auto area = getLocalArea(entry.component, entry.component->getLocalBounds());
        if (!area.contains(localPoint))
            continue;
        const int channel = host->modChannelForParam(entry.paramId);
        return Port{area, channel >= 0 ? channel : kUnboundPadKnobChannelBase - (int)i, /*isInput*/ true,
                    /*isMidi*/ false};
    }
    return std::nullopt;
}
