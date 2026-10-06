// ModuleComponentModDot.cpp -- a card's side of the mod dot (docs/modules/modulation.md#drag-to-knob-modulation):
// the press claim that routes the dot's mouse events to the canvas' ModDotController, and the transparent
// Tab-stop button each modulated knob or fader gets, centred on the dot (docs/layout/module-card.md#modulation-dot).

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardWidgets/CardControlGestures.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include <set>

// Wires the third card-control claim: a plain press on the landing dot of a knob that has at least one
// attenuverter routing is the mod-dot gesture. Cmd-press, and a dot with nothing to adjust, fall through
// to the cable pickup (wantsCablePickupGestureFor), exactly as before.
void ModuleComponent::wireModDotGesture(juce::Slider& knob, synth::ui::CardControlGestures& gestures,
                                        juce::RangedAudioParameter* param) {
    gestures.wantsModDotGesture = [this, param, &knob](const juce::MouseEvent& e) {
        if (e.mods.isCommandDown() || firstAttenuverterForParam(param).uid == 0)
            return false;
        const int destChannel = destChannelForBoundParam(param);
        return destChannel >= 0 && getModTargetKnobAnchor(destChannel).has_value() &&
               pressIsOnLandingDot(param, knob, e);
    };
    gestures.onModDotGesture = [this, param, &knob](const juce::MouseEvent& e, int phase) {
        auto& controller = owner.getModDot();
        if (phase == 0) {
            const int destChannel = destChannelForBoundParam(param);
            auto* button = getModDotButton(destChannel);
            controller.pressed(nodeId, destChannel, knob,
                               button != nullptr ? static_cast<juce::Component&>(*button) : knob, e);
        } else if (phase == 1) {
            controller.dragged(e);
        } else {
            controller.released(e);
        }
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
        const int si = sliderIndexForModTarget(target);
        const auto anchor = getModTargetKnobAnchor(target.channelIndex);
        if (si < 0 || !anchor.has_value())
            continue;
        const int count = owner.getModDot().knobSourceCount(nodeId, target.channelIndex);
        if (count == 0)
            continue;
        live.insert(target.channelIndex);

        synth::ui::ModDotButton* button = nullptr;
        for (auto* b : modDotButtons_)
            if (b->getDestChannel() == target.channelIndex)
                button = b;
        if (button != nullptr && button->getKnob() != sliders[si]) { // the card rebuilt its controls
            modDotButtons_.removeObject(button);
            button = nullptr;
        }
        if (button == nullptr) {
            const int channel = target.channelIndex;
            button = modDotButtons_.add(new synth::ui::ModDotButton(channel, *sliders[si]));
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

        const auto knobName = knobNameForModTarget(mb, target);
        const auto title = knobName + " modulation, " + juce::String(count) + (count == 1 ? " source" : " sources");
        if (button->getTitle() != title)
            button->setTitle(title);
        auto tip = "Modulation sources for " + knobName;
        if (owner.getDoubleClickPortDisconnectEnabled())
            tip += ". Double-click to remove";
        if (button->getTooltip() != tip)
            button->setTooltip(tip);
        button->setBounds(juce::Rectangle<int>(synth::ui::ModDotButton::kSize, synth::ui::ModDotButton::kSize)
                              .withCentre(anchor->roundToInt()));
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
