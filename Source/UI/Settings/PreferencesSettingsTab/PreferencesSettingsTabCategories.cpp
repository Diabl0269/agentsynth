#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: the category picker under the title (which category's rows are laid out) and its
// change path.

namespace {
constexpr PreferencesSettingsTab::Category kCategoriesInOrder[] = {
    PreferencesSettingsTab::Category::Graph,  PreferencesSettingsTab::Category::Timeline,
    PreferencesSettingsTab::Category::Files,  PreferencesSettingsTab::Category::Mixer,
    PreferencesSettingsTab::Category::Panels, PreferencesSettingsTab::Category::MidiRemote};
// "All" leads the drop-down even though it is the last enumerator.
constexpr PreferencesSettingsTab::Category kAllCategory = PreferencesSettingsTab::Category::All;

int comboIdFromCategory(PreferencesSettingsTab::Category category) { return static_cast<int>(category) + 1; }
} // namespace

juce::String PreferencesSettingsTab::categoryName(Category category) {
    switch (category) {
    case Category::Graph:
        return "Graph";
    case Category::Timeline:
        return "Timeline";
    case Category::Files:
        return "Files & Autosave";
    case Category::Mixer:
        return "Mixer";
    case Category::Panels:
        return "Panels & Windows";
    case Category::MidiRemote:
        return "MIDI Remote";
    case Category::All:
        return "All";
    }
    return {};
}

// The picker is a plain juce::ComboBox (the same widget FeedbackSettingsTab's category picker
// uses). Its onChange is the ONE place the selection changes: it re-lays the rows and scrolls back
// to the top, so a category never opens half-way down the previous one's scroll offset.
void PreferencesSettingsTab::setupCategorySelector() {
    addAndMakeVisible(categoryCombo);
    categoryCombo.addItem(categoryName(kAllCategory), comboIdFromCategory(kAllCategory));
    for (auto category : kCategoriesInOrder)
        categoryCombo.addItem(categoryName(category), comboIdFromCategory(category));
    categoryCombo.setTitle("Preferences category");
    categoryCombo.setTooltip("Choose which group of preferences to show, or All for every group with collapsible "
                             "sections. Typing in the filter searches all groups.");
    categoryCombo.setSelectedId(comboIdFromCategory(selectedCategory), juce::dontSendNotification);
    categoryCombo.onChange = [this] {
        selectedCategory = static_cast<Category>(categoryCombo.getSelectedId() - 1);
        contentViewport.setViewPosition(0, 0);
        resized();
        repaint();
    };
    setupSectionControls();
    resized();
}

void PreferencesSettingsTab::setSelectedCategory(Category category) {
    categoryCombo.setSelectedId(comboIdFromCategory(category), juce::sendNotificationSync);
}
