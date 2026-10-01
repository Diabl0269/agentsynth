// CardLayoutEditorComponentScope.cpp -- the Preset combo (list/load/save/delete) and Reset. The Apply-to
// combo's onChange is wired in buildScopeAndPresetControls(): switching it re-applies the working layout
// to the newly chosen scope at once. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutEditorComponent.h"

namespace synth::ui {

void CardLayoutEditorComponent::refreshPresetCombo() {
    presetCombo_.clear(juce::dontSendNotification);
    int id = 1;
    for (const auto& name : source_->listPresets())
        presetCombo_.addItem(name, id++);
}

// Loading copies the preset into whichever scope "Apply to" names.
void CardLayoutEditorComponent::loadPreset(const juce::String& name) {
    const auto preset = source_->loadPreset(name);
    if (!preset.has_value())
        return;
    model_.load(*preset);
    updateMissingLabel();
    applyCurrentLayout();
    rebuildRows();
    resized();
}

void CardLayoutEditorComponent::promptSaveAsPreset() {
    auto* window = new juce::AlertWindow("Save preset", "Preset name:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", {}, "Name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<CardLayoutEditorComponent> safeThis(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, window](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (result != 1)
                                    return;
                                const auto typed = owned->getTextEditorContents("name").trim();
                                if (typed.isEmpty())
                                    return;
                                if (auto* self = safeThis.getComponent())
                                    self->commitSaveAsPreset(typed);
                            }),
                            false);
}

// A preset is a named layout, not tied to a scope; the reserved "default" name fails in the store.
void CardLayoutEditorComponent::commitSaveAsPreset(const juce::String& name) {
    if (source_->savePreset(name, model_.toLayout())) {
        refreshPresetCombo();
        presetCombo_.setText(name, juce::dontSendNotification);
    }
}

void CardLayoutEditorComponent::promptDeletePreset() {
    const auto name = presetCombo_.getText();
    if (name.isEmpty())
        return;

    juce::Component::SafePointer<CardLayoutEditorComponent> safeThis(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, "Delete preset",
                                       "Delete the preset \"" + name + "\"?", "Delete", "Cancel", this,
                                       juce::ModalCallbackFunction::create([safeThis, name](int result) {
                                           if (result != 1)
                                               return;
                                           if (auto* self = safeThis.getComponent())
                                               self->commitDeletePreset(name);
                                       }));
}

void CardLayoutEditorComponent::commitDeletePreset(const juce::String& name) {
    if (source_->deletePreset(name)) {
        refreshPresetCombo();
        presetCombo_.setText({}, juce::dontSendNotification);
    }
}

// Removes the chosen scope's layout (for every module of the kind, the stored default as well as this
// module's own), then shows whatever the card now resolves to. The preset combo lets go of its pick, so
// choosing that preset again loads it again.
void CardLayoutEditorComponent::resetToDefault() {
    if (!source_->isAlive())
        return;
    presetCombo_.setSelectedId(0, juce::dontSendNotification);
    model_.load(source_->reset(applyToAll_));
    updateMissingLabel();
    rebuildRows();
    resized();
}

} // namespace synth::ui
