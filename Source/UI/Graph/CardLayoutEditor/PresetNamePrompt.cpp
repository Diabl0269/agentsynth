// PresetNamePrompt.cpp -- the Save preset window.
#include "PresetNamePrompt.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

void promptForPresetName(std::function<void(const juce::String& name)> onName) {
    auto* window = new juce::AlertWindow("Save preset", "Preset name:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", {}, "Name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    synth::ui::PopupMotion::attach(*window);
    window->enterModalState(true, juce::ModalCallbackFunction::create([window, onName = std::move(onName)](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                const auto typed = owned->getTextEditorContents("name").trim();
                                if (result == 1 && typed.isNotEmpty() && onName)
                                    onName(typed);
                            }),
                            false);
}

} // namespace synth::ui
