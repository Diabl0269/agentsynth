#include "ShortcutsSettingsTab.h"

ShortcutsSettingsTab::ShortcutsSettingsTab(ShortcutManager& sm)
    : shortcutManager(sm) {
    setWantsKeyboardFocus(true);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Keyboard Shortcuts", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));

    for (auto& actionId : shortcutManager.getActionIds()) {
        actionIds.add(actionId);

        auto descLabel = std::make_unique<juce::Label>();
        descLabel->setText(ShortcutManager::getActionDescription(actionId), juce::dontSendNotification);
        descLabel->setFont(juce::FontOptions(14.0f));
        addAndMakeVisible(*descLabel);
        descLabels.push_back(std::move(descLabel));

        auto bindButton = std::make_unique<juce::TextButton>();
        bindButton->setButtonText(ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding(actionId)));
        bindButton->setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
        bindButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        bindButton->setTooltip("Click, then press a key to rebind");
        int index = static_cast<int>(bindButtons.size());
        bindButton->onClick = [this, index] { startListening(index); };
        addAndMakeVisible(*bindButton);
        bindButtons.push_back(std::move(bindButton));
    }

    addAndMakeVisible(resetButton);
    resetButton.setButtonText("Reset to Defaults");
    resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc3333));
    resetButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    resetButton.setTooltip("Reset all keyboard shortcuts to their factory defaults");
    resetButton.onClick = [this] {
        auto options = juce::MessageBoxOptions()
                           .withIconType(juce::MessageBoxIconType::WarningIcon)
                           .withTitle("Reset Shortcuts")
                           .withMessage("Are you sure you want to reset all keyboard shortcuts to their defaults?")
                           .withButton("Reset")
                           .withButton("Cancel");
        juce::AlertWindow::showAsync(options, [this](int result) {
            if (result == 1) {
                shortcutManager.resetToDefaults();
                shortcutManager.saveToProperties();
                refreshBindingLabels();
            }
        });
    };

    addAndMakeVisible(exportButton);
    exportButton.setButtonText("Export...");
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    exportButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    exportButton.setTooltip("Export current shortcuts to a JSON file");
    exportButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Export Shortcuts", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File{}) {
                juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                for (auto& actionId : shortcutManager.getActionIds()) {
                    auto binding = shortcutManager.getBinding(actionId);
                    auto value = ShortcutManager::encodeKeyPress(binding);
                    obj->setProperty(actionId, value);
                }
                auto json = juce::JSON::toString(juce::var(obj.get()));
                file.replaceWithText(json);
            }
        });
    };

    addAndMakeVisible(importButton);
    importButton.setButtonText("Import...");
    importButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    importButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    importButton.setTooltip("Import shortcuts from a JSON file");
    importButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Import Shortcuts", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File{}) {
                auto json = juce::JSON::parse(file.loadFileAsString());
                if (auto* obj = json.getDynamicObject()) {
                    for (auto& actionId : shortcutManager.getActionIds()) {
                        if (obj->hasProperty(actionId)) {
                            auto value = obj->getProperty(actionId).toString();
                            auto kp = ShortcutManager::parseKeyPress(value);
                            if (kp.isValid())
                                shortcutManager.setBinding(actionId, kp);
                        }
                    }
                    shortcutManager.saveToProperties();
                    refreshBindingLabels();
                }
            }
        });
    };
}

void ShortcutsSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void ShortcutsSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(15);
    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);

    for (size_t i = 0; i < descLabels.size(); ++i) {
        auto row = bounds.removeFromTop(28);
        descLabels[i]->setBounds(row.removeFromLeft(180));
        bindButtons[i]->setBounds(row);
        bounds.removeFromTop(4);
    }

    bounds.removeFromTop(15);
    auto buttonRow = bounds.removeFromTop(28);
    exportButton.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(8);
    importButton.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(8);
    resetButton.setBounds(buttonRow.removeFromLeft(160));
}

bool ShortcutsSettingsTab::keyPressed(const juce::KeyPress& key) {
    if (listeningIndex < 0)
        return false;

    if (key == juce::KeyPress::escapeKey) {
        cancelListening();
        return true;
    }

    // Ignore modifier-only keypresses
    if (key.getKeyCode() == 0)
        return true;

    auto actionId = actionIds[listeningIndex];
    auto conflicting = shortcutManager.getConflictingAction(actionId, key);

    if (conflicting.isNotEmpty()) {
        // Swap: assign the old binding of this action to the conflicting action
        auto oldBinding = shortcutManager.getBinding(actionId);
        shortcutManager.setBinding(conflicting, oldBinding);
    }

    shortcutManager.setBinding(actionId, key);
    shortcutManager.saveToProperties();
    listeningIndex = -1;
    refreshBindingLabels();
    return true;
}

juce::String ShortcutsSettingsTab::getRowDescription(int index) const {
    if (index < 0 || index >= static_cast<int>(descLabels.size()))
        return {};
    return descLabels[static_cast<size_t>(index)]->getText();
}

juce::String ShortcutsSettingsTab::getRowBindingText(int index) const {
    if (index < 0 || index >= static_cast<int>(bindButtons.size()))
        return {};
    return bindButtons[static_cast<size_t>(index)]->getButtonText();
}

void ShortcutsSettingsTab::startListening(int index) {
    listeningIndex = index;
    bindButtons[static_cast<size_t>(index)]->setButtonText("Press a key...");
    bindButtons[static_cast<size_t>(index)]->setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
    grabKeyboardFocus();
}

void ShortcutsSettingsTab::cancelListening() {
    if (listeningIndex >= 0) {
        refreshBindingLabels();
        listeningIndex = -1;
    }
}

void ShortcutsSettingsTab::refreshBindingLabels() {
    for (size_t i = 0; i < bindButtons.size(); ++i) {
        auto actionId = actionIds[static_cast<int>(i)];
        bindButtons[i]->setButtonText(ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding(actionId)));
        bindButtons[i]->setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    }
}
