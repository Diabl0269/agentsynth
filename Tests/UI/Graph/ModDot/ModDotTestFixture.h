#pragma once

// Shared fixture for the mod dot's tests: a real LFO -> attenuverter -> VCA.gain routing on a GraphEditor, the VCA
// card showing "gain" as a knob, and helpers that deliver real mouse events to it. Header-only; not compiled on its
// own and not registered in Tests/CMakeLists.txt.

#include "../ModuleComponent/ModuleComponentTestFixture.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModDot/ModDotPopover.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

// Button::triggerClick() posts the click as a command message, so it takes effect after one dispatch pass.
void clickNow(juce::Button& button) {
    button.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}

juce::Slider* findKnob(ModuleComponent& card, const juce::String& componentId) {
    for (auto* child : card.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == componentId)
                return s;
    return nullptr;
}

juce::AudioParameterFloat* amountParam(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"))
                           : nullptr;
}

void setAmount(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id, float value) {
    if (auto* p = amountParam(graph, id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

void showGainAsKnob(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID vcaId) {
    synth::CardParamItem gain;
    gain.paramId = "gain";
    gain.widget = synth::CardWidget::Knob;
    synth::CardSection section;
    section.id = "main";
    section.items.emplace_back(gain);
    synth::CardLayout layout;
    layout.sections = {section};
    synth::setCardLayoutOverride(graph, nullptr, vcaId, layout);
}

const auto kPlain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
const auto kCmd = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);

struct Fixture {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID lfoId, vcaId, attenId;
    ModuleComponent* vcaCard = nullptr;
    juce::Slider* gainKnob = nullptr;
    int gainChannel = -1;
    // The panel a click on the dot opened: a real callout needs a display, so the launcher keeps it instead.
    std::unique_ptr<juce::Component> held;

    explicit Fixture(bool modulated = true) {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto& graph = engine.getGraph();
        auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
        auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
        lfoId = lfoNode->nodeID;
        vcaId = vcaNode->nodeID;
        showGainAsKnob(graph, vcaId);
        for (const auto& t : dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->getModulationTargets())
            if (t.paramId == "gain")
                gainChannel = t.channelIndex;
        if (modulated) {
            attenId = engine.addModRouting(lfoId, 0, vcaId, gainChannel);
            setAmount(graph, attenId, 0.5f);
        }
        editor->getModDot().popoverLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&) {
            held = std::move(content);
        };
        refresh();
        vcaCard = findModuleComp(*editor, vcaNode->getProcessor());
        gainKnob = vcaCard != nullptr ? findKnob(*vcaCard, "Gain") : nullptr;
    }

    ~Fixture() {
        held.reset();
        engine.shutdown();
    }

    synth::ui::ModDotPopover* popover() { return dynamic_cast<synth::ui::ModDotPopover*>(held.get()); }

    // A plain click on the dot, as the user makes it: a press that moves under 3 px.
    synth::ui::ModDotPopover* clickDot() {
        const auto dot = dotInKnob();
        pressDragRelease(dot, dot);
        return popover();
    }

    void refresh() {
        editor->updateComponents();
        sizeModuleComponents(*editor);
        editor->timerCallback();
    }

    float amount(juce::AudioProcessorGraph::NodeID id) { return amountParam(engine.getGraph(), id)->get(); }

    // The landing dot, in the knob's own frame -- where a real press on it arrives.
    juce::Point<int> dotInKnob() const {
        const auto anchor = vcaCard->getModTargetKnobAnchor(gainChannel);
        return gainKnob->getLocalPoint(vcaCard, anchor->roundToInt());
    }

    void pressDragRelease(juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys mods = kPlain) {
        gainKnob->mouseDown(makeModuleClickWithMods(*gainKnob, from, mods));
        gainKnob->mouseDrag(makeModuleClickWithMods(*gainKnob, to, mods));
        gainKnob->mouseUp(makeModuleClickWithMods(*gainKnob, to, mods));
    }

    // A second LFO routed onto the same knob; returns its attenuverter.
    juce::AudioProcessorGraph::NodeID addSecondLfo() {
        auto node = engine.getGraph().addNode(std::make_unique<LFOModule>());
        const auto atten = engine.addModRouting(node->nodeID, 0, vcaId, gainChannel);
        setAmount(engine.getGraph(), atten, -0.25f);
        refresh();
        return atten;
    }
};

} // namespace
