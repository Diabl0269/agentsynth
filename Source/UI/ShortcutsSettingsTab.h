#pragma once

#include "../ShortcutManager.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The Settings "Keyboard Shortcuts" tab.
//
// Displays one row per registered action: description label on the left,
// rebind button on the right.  Clicking a button enters listening mode
// (button turns orange with "Press a key..."); pressing any key (except
// Escape) commits the new binding.  If the key is already used by another
// action the bindings are swapped.  Reset-to-defaults is guarded by a
// confirmation dialog.  JSON export/import round-trips all bindings via
// ShortcutManager::encodeKeyPress / parseKeyPress.
//
// NOTE: ShortcutsSettingsTab.cpp MUST be added to BOTH the app target AND
// the test target in CMakeLists.txt (consolidation pass).
class ShortcutsSettingsTab : public juce::Component {
public:
    explicit ShortcutsSettingsTab(ShortcutManager& shortcutManager);
    ~ShortcutsSettingsTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Testing hooks ---------------------------------------------------------
    // Returns the number of action rows (== ShortcutManager::getActionIds().size()).
    int getShortcutCount() const { return static_cast<int>(bindButtons.size()); }

    // Returns the description text of row [index].
    juce::String getRowDescription(int index) const;

    // Returns the binding display string currently shown on row [index].
    juce::String getRowBindingText(int index) const;

    // Programmatically starts listening mode on row [index] (used by tests
    // to simulate a button click without needing a mouse event).
    void startListeningForTest(int index) { startListening(index); }

private:
    void startListening(int index);
    void cancelListening();
    void refreshBindingLabels();

    ShortcutManager& shortcutManager;

    juce::Label titleLabel;
    juce::StringArray actionIds;
    std::vector<std::unique_ptr<juce::Label>> descLabels;
    std::vector<std::unique_ptr<juce::TextButton>> bindButtons;
    juce::TextButton resetButton;
    juce::TextButton exportButton;
    juce::TextButton importButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int listeningIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutsSettingsTab)
};
