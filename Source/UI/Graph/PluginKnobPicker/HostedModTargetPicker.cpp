// HostedModTargetPicker.cpp -- modulating a plugin parameter that is not on the card: a cable released on the card's
// body opens a search over all of the plugin's parameters, and the pick becomes a knob on the card with the cable on
// it. docs/control/plugin-card-layout.md#modulating-plugin-parameters.
#include "HostedModTargetPicker.h"
#include "AudioEngine/AudioEngine.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardLayoutEditor/HostedCardLayoutSource.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/PortPanel/PortConnector.h"
#include <unordered_map>

namespace synth::ui {

namespace {
HostedPluginModule* hostedAt(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId) {
    auto* node = editor.getAudioEngine().getGraph().getNodeForId(nodeId);
    return node != nullptr ? dynamic_cast<HostedPluginModule*>(node->getProcessor()) : nullptr;
}
} // namespace

// The card's own knobs come first: they are what the person sees and most likely means. A modulated parameter
// says so in its detail line, so a second cable onto it is a deliberate choice.
std::vector<ModMatrixPicker::Item> hostedModTargetItems(HostedPluginModule& module, GraphEditor& editor,
                                                        juce::AudioProcessorGraph::NodeID nodeId) {
    const HostedCardLayoutSource source(module, editor.getPluginCardLayoutStore(), editor.getAudioEngine().getGraph(),
                                        nodeId, editor.getUndoManager());
    const auto states = source.parameterStates();
    std::vector<ModMatrixPicker::Item> onCard, rest;
    for (const auto& param : module.getInstanceParameters()) {
        ModMatrixPicker::Item item;
        item.id = param.index + 1;
        item.text = param.displayName;
        if (module.modChannelForParam(param.paramId) >= 0)
            item.detail = "Modulated";
        const auto state = states.find(param.paramId);
        const bool shown = state != states.end() && state->second == HostedCardLayoutSource::ParameterState::OnCard;
        item.category = shown ? "On the card" : "All parameters";
        (shown ? onCard : rest).push_back(std::move(item));
    }
    onCard.insert(onCard.end(), std::make_move_iterator(rest.begin()), std::make_move_iterator(rest.end()));
    return onCard;
}

void showHostedModTargetPicker(GraphEditor& editor, ModuleComponent& card, juce::AudioProcessorGraph::NodeID sourceId,
                               int sourceJack, juce::Point<int> screenPos) {
    const auto nodeId = card.getNodeId();
    auto* module = hostedAt(editor, nodeId);
    if (module == nullptr || !module->hasInstance())
        return;
    juce::Component::SafePointer<GraphEditor> safeEditor(&editor);
    // Ids as they were when the list opened: a plugin swapped underneath the open list refuses the pick by id (no such
    // parameter) rather than taking whichever parameter now has that index.
    std::unordered_map<int, juce::String> paramIdByItem;
    for (const auto& param : module->getInstanceParameters())
        paramIdByItem[param.index + 1] = param.paramId;
    auto picker = std::make_unique<ModMatrixPicker>(
        "parameter", hostedModTargetItems(*module, editor, nodeId), 0,
        [safeEditor, nodeId, sourceId, sourceJack, paramIdByItem = std::move(paramIdByItem)](int id) {
            const auto paramId = paramIdByItem.find(id);
            if (safeEditor != nullptr && paramId != paramIdByItem.end())
                applyHostedModTarget(*safeEditor, nodeId, sourceId, sourceJack, paramId->second);
        });
    const auto title = "Modulate a " + card.cardTitle() + " parameter";
    picker->setAccessibleNames(title, "Search " + card.cardTitle() + " parameters to modulate");
    picker->setSearchPlaceholder("Search parameters...");
    if (auto& hook = test_hooks::hostedModTargetPickerHookForTest()) {
        hook(std::move(picker));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(picker), juce::Rectangle<int>(screenPos, screenPos).expanded(4),
                                           nullptr);
}

// The knob, its slot and the cable are one gesture and so one undo step: the source writes with no undo manager of
// its own, and the connection does not record either, so the outer snapshot holds all three.
bool applyHostedModTarget(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId,
                          juce::AudioProcessorGraph::NodeID sourceId, int sourceJack, const juce::String& paramId) {
    auto* module = hostedAt(editor, nodeId);
    if (module == nullptr || !module->hasInstance())
        return false;
    auto& graph = editor.getAudioEngine().getGraph();
    HostedCardLayoutSource source(*module, editor.getPluginCardLayoutStore(), graph, nodeId, /*undoManager=*/nullptr);
    const auto state = source.parameterState(paramId);
    if (state == HostedCardLayoutSource::ParameterState::Unknown)
        return false;

    bool connected = false;
    const auto gesture = [&] {
        if (state == HostedCardLayoutSource::ParameterState::Available)
            source.showParameter(paramId);
        const int channel = module->bindModSlot(paramId);
        if (channel < 0) {
            if (editor.onStatusMessage)
                editor.onStatusMessage("This plugin already has " + juce::String(HostedPluginModule::kMaxModSlots) +
                                       " modulated parameters");
            return;
        }
        connected = PortConnector::connectJacks(editor, sourceId, sourceJack, nodeId,
                                                module->mapInputChannel(channel).visibleJackIndex, /*isMidi=*/false,
                                                /*recordUndo=*/false)
                        .connected;
    };
    if (auto* undo = editor.getUndoManager())
        undo->recordGraphAndMacroChange(graph, editor.getMacros(), gesture);
    else
        gesture();
    return connected;
}

namespace test_hooks {
std::function<void(std::unique_ptr<ModMatrixPicker>)>& hostedModTargetPickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
