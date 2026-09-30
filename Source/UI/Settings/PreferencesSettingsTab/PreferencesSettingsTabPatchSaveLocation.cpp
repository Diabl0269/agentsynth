#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: the "Patch save location" preference (Files & Autosave): per project / shared folder /
// custom folder. The resolution itself lives in synth::PatchSaveLocation; this unit is the row.

namespace {
constexpr int kPatchSaveComboIdBase = 1; // combo id = PatchSaveMode + 1 (0 means "nothing selected")
} // namespace

synth::PatchSaveMode PreferencesSettingsTab::getPatchSaveMode() const {
    return static_cast<synth::PatchSaveMode>(patchSaveCombo.getSelectedId() - kPatchSaveComboIdBase);
}

void PreferencesSettingsTab::setPatchSaveMode(synth::PatchSaveMode mode) {
    patchSaveCombo.setSelectedId(static_cast<int>(mode) + kPatchSaveComboIdBase, juce::dontSendNotification);
    persistPatchSaveMode(mode);
}

void PreferencesSettingsTab::persistPatchSaveMode(synth::PatchSaveMode mode) {
    appProperties.getUserSettings()->setValue(synth::PatchSaveLocation::kModeKey,
                                              synth::PatchSaveLocation::modeToString(mode));
    appProperties.getUserSettings()->saveIfNeeded();
    patchSaveChooseButton.setEnabled(mode == synth::PatchSaveMode::Custom);
    updatePatchSaveHint();
    // No live push: MainComponent resolves the folder from the settings file each time a dialog opens.
}

juce::File PreferencesSettingsTab::getPatchSaveCustomFolder() const {
    const auto path = appProperties.getUserSettings()->getValue(synth::PatchSaveLocation::kCustomDirKey);
    return path.isEmpty() ? juce::File() : juce::File(path);
}

void PreferencesSettingsTab::setPatchSaveCustomFolder(const juce::File& folder) {
    appProperties.getUserSettings()->setValue(synth::PatchSaveLocation::kCustomDirKey, folder.getFullPathName());
    appProperties.getUserSettings()->saveIfNeeded();
    updatePatchSaveHint();
}

void PreferencesSettingsTab::updatePatchSaveHint() {
    juce::String text;
    switch (getPatchSaveMode()) {
    case synth::PatchSaveMode::Global:
        text = "One shared Patches folder (in your AgentSynth folder) for every project.";
        break;
    case synth::PatchSaveMode::Custom: {
        const auto folder = getPatchSaveCustomFolder();
        if (folder == juce::File())
            text = "Click Choose... to pick a folder. Until you do, the shared Patches folder is used.";
        else if (!folder.isDirectory())
            text = "\"" + folder.getFullPathName() + "\" no longer exists, so the shared Patches folder is used.";
        else
            text = folder.getFullPathName();
        break;
    }
    case synth::PatchSaveMode::PerProject:
    default:
        text = "A Patches folder inside the open project. Until the project is saved, the shared Patches folder "
               "is used.";
        break;
    }
    patchSaveHint.setText(text, juce::dontSendNotification);
}

void PreferencesSettingsTab::launchPatchSaveFolderChooser() {
    const auto start = getPatchSaveCustomFolder().isDirectory() ? getPatchSaveCustomFolder() : juce::File();
    patchSaveChooser = std::make_unique<juce::FileChooser>("Choose the patch folder", start);
    juce::Component::SafePointer<PreferencesSettingsTab> safeThis(this);
    patchSaveChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [safeThis](const juce::FileChooser& fc) {
                                      auto* self = safeThis.getComponent();
                                      const auto folder = fc.getResult();
                                      if (self != nullptr && folder != juce::File())
                                          self->setPatchSaveCustomFolder(folder);
                                  });
}

void PreferencesSettingsTab::setupPatchSaveLocationControls() {
    contentHost.addAndMakeVisible(patchSaveLabel);
    patchSaveLabel.setText("Patch save location:", juce::dontSendNotification);
    patchSaveLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(patchSaveCombo);
    patchSaveCombo.addItem("With each project", static_cast<int>(synth::PatchSaveMode::PerProject) + 1);
    patchSaveCombo.addItem("One shared patches folder", static_cast<int>(synth::PatchSaveMode::Global) + 1);
    patchSaveCombo.addItem("A folder I choose", static_cast<int>(synth::PatchSaveMode::Custom) + 1);
    patchSaveCombo.setTooltip("Where the patch save and open dialogs start: a Patches folder inside the open "
                              "project, one shared Patches folder, or a folder you pick.");
    // Read back through the setter, like the sibling groups: one code path, idempotent write.
    setPatchSaveMode(synth::PatchSaveLocation::modeFromString(
        appProperties.getUserSettings()->getValue(synth::PatchSaveLocation::kModeKey, "project")));
    patchSaveCombo.onChange = [this] { persistPatchSaveMode(getPatchSaveMode()); };

    contentHost.addAndMakeVisible(patchSaveChooseButton);
    patchSaveChooseButton.setTooltip("Pick the folder patches are saved to.");
    patchSaveChooseButton.onClick = [this] { launchPatchSaveFolderChooser(); };

    contentHost.addAndMakeVisible(patchSaveHint);
    styleMutedHintLabel(patchSaveHint);
    updatePatchSaveHint();
    setupCategorySelector(); // Chained here, the constructor is baselined
}

void PreferencesSettingsTab::layoutPatchSaveLocationGroup(int& y, int contentWidth, bool& pendingDivider,
                                                          const GroupMatchFn& groupMatches,
                                                          const SetVisibleFn& setGroupVisible,
                                                          const BeginGroupFn& beginGroup) {
    enterCategory(Category::Files, y);
    const std::initializer_list<juce::Component*> comps = {&patchSaveLabel, &patchSaveCombo, &patchSaveChooseButton,
                                                           &patchSaveHint};
    const bool visible = groupMatches(comps);
    setGroupVisible(comps, visible);
    beginGroup(visible);
    if (visible) {
        juce::Rectangle<int> row(0, y, contentWidth, 24);
        patchSaveLabel.setBounds(row.removeFromLeft(140));
        row.removeFromLeft(4);
        patchSaveCombo.setBounds(row.removeFromLeft(200));
        row.removeFromLeft(8);
        patchSaveChooseButton.setBounds(row.removeFromLeft(90));
        y += 24;
        patchSaveHint.setBounds(0, y, contentWidth, kHintHeight);
        y += kHintHeight;
    }
    pendingDivider = pendingDivider || visible;
}
