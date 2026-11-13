#include "ControlStyleCards.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/CardBody/ControlStyleNodeOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/Theme.h"

namespace synth {

juce::PropertiesFile* userSettingsOf(GraphEditor& editor) {
    auto* properties = editor.getModDot().host.appProperties;
    return properties != nullptr ? properties->getUserSettings() : nullptr;
}

theme::ControlStyleOverride effectiveControlStyle(GraphEditor& editor, const ModuleComponent& card) {
    const auto own = getControlStyleNodeOverride(editor.getAudioEngine().getGraph(), card.getNodeId());
    theme::ControlStyleOverride type;
    if (auto* settings = userSettingsOf(editor))
        type = theme::loadTypeControlStyle(*settings, AIStateMapper::getFactoryTypeName(card.getModule()));
    return {own.knob ? own.knob : type.knob, own.fader ? own.fader : type.fader};
}

namespace {
// Returns whether the property changed.
bool publish(juce::Component& card, const char* property, std::optional<theme::KnobStyle> style) {
    auto& properties = card.getProperties();
    const juce::var wanted = style ? juce::var((int)*style) : juce::var();
    if (properties[property] == wanted)
        return false;
    if (style)
        properties.set(property, wanted);
    else
        properties.remove(property);
    return true;
}
} // namespace

// The card's cached image holds its sliders, so a change repaints the card itself.
void refreshControlStyle(GraphEditor& editor, ModuleComponent& card) {
    const auto style = effectiveControlStyle(editor, card);
    const bool knobChanged = publish(card, theme::kKnobStyleProperty, style.knob);
    const bool faderChanged = publish(card, theme::kFaderStyleProperty, style.fader);
    if (knobChanged || faderChanged)
        card.repaint();
}

void refreshAllControlStyles(GraphEditor& editor) {
    for (auto* card : editor.getModuleComponents())
        if (card != nullptr)
            refreshControlStyle(editor, *card);
}

} // namespace synth
