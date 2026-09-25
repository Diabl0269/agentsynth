// PluginKnobPickerComponentScope.cpp -- the Preset combo (list/load/save/delete) and "Reset to
// automatic". The Apply-to combo's own onChange is wired inline in PluginKnobPickerComponent.cpp
// (buildChrome()) since it is a one-line call into applyCurrentLayout(); everything else scope- and
// preset-related that needs more than that lives here.
// See docs/control/plugin-card-layout.md#choosing-knobs.
#include "Plugin/Hosting/HostedPluginCardLayout.h"
#include "PluginKnobPickerComponent.h"

namespace synth::ui {

void PluginKnobPickerComponent::refreshPresetCombo() {
    presetCombo_.clear(juce::dontSendNotification);
    if (store_ == nullptr)
        return;
    int id = 1;
    for (const auto& name : store_->listPresets(identity_))
        presetCombo_.addItem(name, id++);
}

// Copies the preset into whichever scope "Apply to" currently names -- the design doc's own wording
// ("loading a preset copies it into whichever scope Apply to selects").
void PluginKnobPickerComponent::loadPreset(const juce::String& name) {
    if (store_ == nullptr)
        return;
    const auto result = store_->loadPreset(identity_, name);
    if (result.status != PluginCardLayoutStore::LoadStatus::Ok)
        return;
    partitionSlots(result.layout.slots);
    applyCurrentLayout();
    rebuildRows();
    resized();
}

void PluginKnobPickerComponent::promptSaveAsPreset() {
    auto* window = new juce::AlertWindow("Save Preset", "Preset name:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", {}, "Name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PluginKnobPickerComponent> safeThis(this);
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

// Saves the CURRENT working set (independent of "Apply to" -- a preset is a named layout, not tied
// to a scope) and fails silently for the reserved "default" name or an empty store, matching
// PluginCardLayoutStore::savePreset's own contract.
void PluginKnobPickerComponent::commitSaveAsPreset(const juce::String& name) {
    if (store_ == nullptr)
        return;
    CardLayout layout;
    layout.version = CardLayout::kCurrentVersion;
    layout.slots = workingSlots_;
    if (store_->savePreset(identity_, name, layout)) {
        refreshPresetCombo();
        presetCombo_.setText(name, juce::dontSendNotification);
    }
}

void PluginKnobPickerComponent::promptDeletePreset() {
    const auto name = presetCombo_.getText();
    if (name.isEmpty())
        return;

    juce::Component::SafePointer<PluginKnobPickerComponent> safeThis(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, "Delete Preset",
                                       "Delete the preset \"" + name + "\"?", "Delete", "Cancel", this,
                                       juce::ModalCallbackFunction::create([safeThis, name](int result) {
                                           if (result != 1)
                                               return;
                                           if (auto* self = safeThis.getComponent())
                                               self->commitDeletePreset(name);
                                       }));
}

void PluginKnobPickerComponent::commitDeletePreset(const juce::String& name) {
    if (store_ == nullptr)
        return;
    if (store_->deletePreset(identity_, name)) {
        refreshPresetCombo();
        presetCombo_.setText({}, juce::dontSendNotification);
    }
}

// Removes the CHOSEN scope's layout (This instance: the override; All instances: the stored default,
// which also clears this instance's override -- the same pairing applyCurrentLayout's "All instances"
// branch always keeps) so precedence falls through, then refreshes the picker to show whatever now
// resolves (the plugin's stored default, or the automatic set).
void PluginKnobPickerComponent::resetToAutomatic() {
    auto* module = module_.get();
    if (module == nullptr)
        return;

    const auto before = HostedPluginModule::makeCardLayoutPatch(module->getCardLayoutOverride());
    if (applyToAllInstances_ && store_ != nullptr)
        store_->clearDefault(identity_);
    module->setCardLayoutOverride(juce::var());
    const auto after = HostedPluginModule::makeCardLayoutPatch(module->getCardLayoutOverride());
    if (undoManager_ != nullptr)
        undoManager_->recordNodeExtraStateChange(graph_, nodeId_, before, after);

    const auto resolved = resolveHostedCardLayout(*module, store_);
    partitionSlots(resolved.layout.slots);
    rebuildRows();
    resized();
}

} // namespace synth::ui
