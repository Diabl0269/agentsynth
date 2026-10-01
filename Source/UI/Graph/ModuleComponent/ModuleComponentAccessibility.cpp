// ModuleComponentAccessibility.cpp
//
// What a screen reader hears on a card (docs/development/accessibility.md). The card is a group
// named after its title. Every knob, combo and toggle is named after its parameter and carries a
// tooltip naming it; a knob's spoken value is the parameter's own text (SliderParameterAttachment
// installs it as the slider's text function, which JUCE's slider handler reads), so VoiceOver says
// "Cutoff, 1.2 kHz". The painted jacks get invisible stand-ins named "Audio L input" and so on:
// they take no clicks and no focus, they only exist for the accessibility tree.

#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/ModuleBase.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include <algorithm>
#include <map>

using namespace detail;

namespace {

class CardAccessibilityHandler final : public synth::ui::TooltipHelpHandler {
public:
    explicit CardAccessibilityHandler(ModuleComponent& card)
        : TooltipHelpHandler(card, juce::AccessibilityRole::group)
        , card_(card) {}

    // The title the header shows (a rename included), never an upper-cased paint string.
    juce::String getTitle() const override { return card_.cardTitle(); }

private:
    ModuleComponent& card_;
};

// A jack's accessible stand-in: a named, click-through, unfocusable element over the painted dot.
class PortAccessible final : public juce::Component {
public:
    explicit PortAccessible(const juce::String& name) {
        setTitle(name);
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::staticText);
    }
};

bool isNamedControl(const juce::Component& c) {
    return dynamic_cast<const juce::Slider*>(&c) != nullptr || dynamic_cast<const juce::ComboBox*>(&c) != nullptr ||
           dynamic_cast<const juce::Button*>(&c) != nullptr || c.getWantsKeyboardFocus();
}

// Every control on the card, shown or not (a wavetable tab or a collapsed section may hide some
// now and show them later). Text editors are left alone: their name is their content.
void collectControls(juce::Component& parent, std::vector<juce::Component*>& out) {
    for (auto* child : parent.getChildren()) {
        if (dynamic_cast<juce::TextEditor*>(child) != nullptr)
            continue;
        if (isNamedControl(*child))
            out.push_back(child);
        else
            collectControls(*child, out);
    }
}

juce::String fallbackName(const juce::Component& c) {
    if (auto* b = dynamic_cast<const juce::Button*>(&c); b != nullptr && b->getButtonText().isNotEmpty())
        return b->getButtonText();
    if (c.getName().isNotEmpty())
        return c.getName();
    return c.getComponentID();
}

} // namespace

std::unique_ptr<juce::AccessibilityHandler> ModuleComponent::createAccessibilityHandler() {
    return std::make_unique<CardAccessibilityHandler>(*this);
}

// Runs once the card has built every control. Names come from the parameter where the control is
// bound to one, else from the control's own text or name. A tooltip is only added where the
// control has none, and the MIDI Learn registry is told, since it composes a mapped control's
// tooltip from the one the control had when it registered.
void ModuleComponent::applyControlAccessibility() {
    std::map<juce::Component*, juce::String> preferred;
    for (int i = 0; i < sliders.size(); ++i)
        if (i < sliderParams.size() && sliderParams[i] != nullptr)
            preferred[sliders[i]] = sliderParams[i]->getName(100);
    for (int i = 0; i < comboBoxes.size(); ++i) {
        if (i < comboParams.size() && comboParams[i] != nullptr)
            preferred[comboBoxes[i]] = comboParams[i]->getName(100);
        else if (i < comboLabels.size() && comboLabels[i] != nullptr)
            preferred[comboBoxes[i]] = comboLabels[i]->getText();
    }

    if (keyboardComponent != nullptr) {
        preferred[keyboardComponent.get()] = "Keyboard";
        // The keyboard's own octave-scroll arrows: the first scrolls down, the second up.
        int arrow = 0;
        for (auto* child : keyboardComponent->getChildren()) {
            if (auto* b = dynamic_cast<juce::Button*>(child)) {
                const juce::String name = arrow++ == 0 ? "Scroll keyboard down" : "Scroll keyboard up";
                b->setTitle(name);
                b->setTooltip(name);
            }
        }
    }

    std::vector<juce::Component*> controls;
    collectControls(*this, controls);
    for (auto* c : controls) {
        if (std::find(portAccessibles_.begin(), portAccessibles_.end(), c) != portAccessibles_.end())
            continue;
        const auto it = preferred.find(c);
        const auto name = it != preferred.end() && it->second.isNotEmpty() ? it->second : fallbackName(*c);
        if (name.isEmpty())
            continue;
        if (c->getTitle().isEmpty())
            c->setTitle(name);
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(c); tip != nullptr && tip->getTooltip().isEmpty()) {
            tip->setTooltip(name);
            midiLearnableRegistry_.setBaseTooltip(*c, name);
        }
    }
}

// Rebuilt only when the set of jacks changes; otherwise each stand-in just follows its jack. Macro
// port widgets are skipped: their tooltip already carries the port's name.
void ModuleComponent::syncPortAccessibility() {
    if (module == nullptr || isMacroPortType(getType(module)))
        return;

    std::vector<std::pair<juce::String, juce::Point<int>>> jacks;
    auto* mb = dynamic_cast<ModuleBase*>(module);
    const bool ioNode = dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module) != nullptr;
    auto ioLabel = [](int i) {
        return i == 0 ? juce::String("Left") : i == 1 ? juce::String("Right") : juce::String();
    };

    if (module->acceptsMidi())
        jacks.emplace_back("Midi input", getMidiPortCenter(false));
    for (int i : drawnInputJackIndices()) {
        auto label = mb != nullptr ? mb->getInputPortLabel(i) : ioNode ? ioLabel(i) : juce::String();
        if (label.isEmpty())
            label = "In " + juce::String(i);
        jacks.emplace_back(label + " input", getPortCenter(i, true));
    }
    if (module->producesMidi())
        jacks.emplace_back("Midi output", getMidiPortCenter(true));
    const int outs = mb != nullptr ? mb->getVisibleOutputPortCount() : module->getTotalNumOutputChannels();
    for (int i = 0; i < outs; ++i) {
        auto label = mb != nullptr ? mb->getOutputPortLabel(i) : ioNode ? ioLabel(i) : juce::String();
        if (label.isEmpty())
            label = "Out " + juce::String(i);
        jacks.emplace_back(label + " output", getPortCenter(i, false));
    }

    bool same = (int)jacks.size() == portAccessibles_.size();
    for (int i = 0; same && i < portAccessibles_.size(); ++i)
        same = portAccessibles_[i]->getTitle() == jacks[(size_t)i].first;
    if (!same) {
        portAccessibles_.clear();
        for (const auto& jack : jacks)
            addAndMakeVisible(portAccessibles_.add(new PortAccessible(jack.first)));
    }
    for (int i = 0; i < portAccessibles_.size(); ++i)
        portAccessibles_[i]->setBounds(juce::Rectangle<int>(12, 12).withCentre(jacks[(size_t)i].second));
}
