// CardLayoutOnCardEditorScope.cpp -- the edit bar's Apply to and Preset menus: which cards a write changes,
// saving the layout as a named preset, loading one, and resetting to the default.
// docs/layout/module-card-layout.md#editing-a-layout.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "CardLayoutOnCardEditor.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardLayoutEditor/PresetNamePrompt.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <algorithm>

namespace synth::ui {

// The cards on the canvas of this module's type: what Apply to all changes.
int CardLayoutOnCardEditor::cardsOfType() const {
    auto* editor = graphEditor_.getComponent();
    if (editor == nullptr || source_ == nullptr)
        return 1;
    auto& graph = editor->getAudioEngine().getGraph();
    int count = 0;
    for (auto* card : editor->getModuleComponents()) {
        auto* node = card != nullptr ? graph.getNodeForId(card->getNodeId()) : nullptr;
        if (node != nullptr && AIStateMapper::getFactoryTypeName(node->getProcessor()) == source_->moduleType())
            ++count;
    }
    return std::max(count, 1);
}

// "This module" and "All Filter modules", the chosen one ticked, and a note of how many cards a write changes.
juce::PopupMenu CardLayoutOnCardEditor::buildApplyToMenu() const {
    juce::PopupMenu menu;
    if (source_ == nullptr)
        return menu;
    juce::Component::SafePointer<CardLayoutOnCardEditor> self(const_cast<CardLayoutOnCardEditor*>(this));
    const auto choose = [self](bool all) {
        return [self, all] {
            if (self != nullptr)
                self->chooseScope(all);
        };
    };
    menu.addItem(source_->thisScopeText(), true, !applyToAll_, choose(false));
    menu.addItem(source_->allScopeText(), true, applyToAll_, choose(true));
    menu.addSeparator();
    const int cards = applyToAll_ ? cardsOfType() : 1;
    menu.addItem("Changes " + juce::String(cards) + (cards == 1 ? " card" : " cards"), false, false, nullptr);
    return menu;
}

// Choosing a scope writes the layout being edited to it at once, and every later write follows it.
void CardLayoutOnCardEditor::chooseScope(bool allOfType) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr || allOfType == applyToAll_)
        return;
    flushNudge();
    applyToAll_ = allOfType;
    writeLayout(body->explicitLayout());
    announce(allOfType ? "Applying to " + source_->allScopeText() : "Applying to this module");
}

// "Save as...", "Reset to default", then the saved presets with a tick on the one in use.
juce::PopupMenu CardLayoutOnCardEditor::buildPresetMenu() const {
    juce::PopupMenu menu;
    if (source_ == nullptr)
        return menu;
    juce::Component::SafePointer<CardLayoutOnCardEditor> self(const_cast<CardLayoutOnCardEditor*>(this));
    const bool store = source_->hasPresets();
    menu.addItem("Save as...", store, false, [self] {
        if (self == nullptr)
            return;
        promptForPresetName([self](const juce::String& name) {
            if (self != nullptr)
                self->savePreset(name);
        });
    });
    menu.addItem(source_->resetText(), true, false, [self] {
        if (self != nullptr)
            self->resetLayout();
    });
    const auto presets = source_->listPresets();
    if (presets.isEmpty())
        return menu;
    menu.addSeparator();
    const auto current = source_->currentLayout();
    for (const auto& name : presets) {
        const bool inUse = source_->loadPreset(name) == current;
        menu.addItem(name, true, inUse, [self, name] {
            if (self != nullptr)
                self->loadPreset(name);
        });
    }
    return menu;
}

void CardLayoutOnCardEditor::loadPreset(const juce::String& name) {
    if (closed_ || source_ == nullptr)
        return;
    if (const auto preset = source_->loadPreset(name)) {
        flushNudge();
        writeLayout(*preset);
    }
}

// A preset is the layout the card draws now, named; the reserved name "default" is refused by the store.
void CardLayoutOnCardEditor::savePreset(const juce::String& name) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr)
        return;
    flushNudge();
    announce(source_->savePreset(name, body->explicitLayout()) ? "Saved preset " + name
                                                               : "Could not save the preset " + name);
}

// Removes the chosen scope's layout, so the card goes back to whatever it resolves to without it.
void CardLayoutOnCardEditor::resetLayout() {
    if (closed_ || source_ == nullptr)
        return;
    flushNudge();
    writing_ = true;
    source_->reset(applyToAll_);
    writing_ = false;
    syncToCard();
}

} // namespace synth::ui
