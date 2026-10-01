#pragma once

// Shared fixtures for Tests/UI/Graph/CardBody/CardBody*Tests.cpp. Header-only; not registered in
// Tests/CMakeLists.txt.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <map>

namespace cardbody_test {

using NodeID = juce::AudioProcessorGraph::NodeID;

/** The automatic layout written out as a stored layout, `widgets` choosing a widget per paramId and
 *  `hidden` moved to the More row. */
inline synth::CardLayout automaticLayoutWith(juce::AudioProcessor& module,
                                             const std::map<juce::String, synth::CardWidget>& widgets,
                                             const juce::StringArray& hidden = {}) {
    const auto plan = synth::CardBodyPlan::forModule(module, std::nullopt);
    synth::CardSection section;
    section.id = "main";
    for (int index : plan.sections.front().items) {
        const auto& item = plan.items[(size_t)index];
        if (item.kind == synth::CardBodyItem::Kind::View) {
            synth::CardViewItem view;
            view.view = item.view;
            section.items.emplace_back(view);
        } else {
            synth::CardParamItem param;
            param.paramId = item.param->paramID;
            if (const auto it = widgets.find(param.paramId); it != widgets.end())
                param.widget = it->second;
            section.items.emplace_back(param);
        }
    }
    synth::CardLayout layout;
    layout.sections = {section};
    layout.hidden = hidden;
    return layout;
}

/** The automatic layout written out as a stored layout, with `hidden` moved to the More row. */
inline synth::CardLayout automaticLayoutHiding(juce::AudioProcessor& module, const juce::StringArray& hidden) {
    return automaticLayoutWith(module, {}, hidden);
}

/** A synthesized mouse event on `comp` at `local`. */
inline juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<float> local, juce::ModifierKeys mods,
                                int clicks = 1) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), local, juce::Time::getCurrentTime(),
                            clicks, false);
}

/** Right-clicks `child` on `card` the way a real click is dispatched (the child's own handler, then the
 *  card as its mouse listener) and returns the menu the card built. */
inline juce::PopupMenu rightClickOnCard(ModuleComponent& card, juce::Component& child) {
    juce::PopupMenu captured;
    card.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
    const auto down = mouseAt(child, child.getLocalBounds().getCentre().toFloat(), mods);
    child.mouseDown(down);
    card.mouseDown(down);
    child.mouseUp(down);
    card.setShowContextMenuHookForTest(nullptr);
    return captured;
}

/** The item titled `text` in `menu`, or null. */
inline const juce::PopupMenu::Item* menuItem(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

/** A canvas with real graph nodes and the cards updateComponents() builds for them. */
struct CardCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    CardCanvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(2400, 1800);
    }

    NodeID add(std::unique_ptr<juce::AudioProcessor> processor, int x, int y,
               std::optional<synth::CardLayout> layout = std::nullopt) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", x);
        node->properties.set("y", y);
        node->properties.set("uuid", juce::Uuid().toDashedString());
        if (layout.has_value())
            synth::setCardLayoutOverride(engine.getGraph(), nullptr, node->nodeID, layout);
        return node->nodeID;
    }

    ModuleComponent* card(NodeID id) {
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == id)
                return comp;
        return nullptr;
    }

    juce::AudioProcessor* processor(NodeID id) {
        auto* node = engine.getGraph().getNodeForId(id);
        return node != nullptr ? node->getProcessor() : nullptr;
    }
};

/** Every module type the library offers. */
inline juce::StringArray libraryTypes() {
    ModuleLibraryComponent library;
    return library.getDraggableModuleNames();
}

} // namespace cardbody_test
