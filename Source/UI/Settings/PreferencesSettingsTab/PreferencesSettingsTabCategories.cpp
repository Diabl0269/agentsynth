#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: the category picker under the title (which category's rows are laid out) and its
// change path, which also remembers the choice.

namespace {
constexpr PreferencesSettingsTab::Category kCategoriesInOrder[] = {
    PreferencesSettingsTab::Category::Graph,  PreferencesSettingsTab::Category::Timeline,
    PreferencesSettingsTab::Category::Files,  PreferencesSettingsTab::Category::Mixer,
    PreferencesSettingsTab::Category::Panels, PreferencesSettingsTab::Category::MidiRemote};
// "All" leads the drop-down even though it is the last enumerator.
constexpr PreferencesSettingsTab::Category kAllCategory = PreferencesSettingsTab::Category::All;

int comboIdFromCategory(PreferencesSettingsTab::Category category) { return static_cast<int>(category) + 1; }

// The selected category is remembered across Settings windows and launches under this key, as the
// stable name below (never the enum's integer, never the display text, so a reordered enum or a
// reworded entry cannot repoint a saved choice).
constexpr const char* kCategoryKey = "preferencesCategory";

juce::String persistedCategoryName(PreferencesSettingsTab::Category category) {
    using Category = PreferencesSettingsTab::Category;
    switch (category) {
    case Category::Graph:
        return "Graph";
    case Category::Timeline:
        return "Timeline";
    case Category::Files:
        return "Files";
    case Category::Mixer:
        return "Mixer";
    case Category::Panels:
        return "Panels";
    case Category::MidiRemote:
        return "MidiRemote";
    case Category::All:
        break;
    }
    return "All";
}

// A missing or unrecognised value is All, the view that shows every category.
PreferencesSettingsTab::Category categoryFromPersistedName(const juce::String& name) {
    for (auto category : kCategoriesInOrder)
        if (persistedCategoryName(category) == name)
            return category;
    return kAllCategory;
}
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
    selectedCategory = categoryFromPersistedName(appProperties.getUserSettings()->getValue(kCategoryKey));
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
        appProperties.getUserSettings()->setValue(kCategoryKey, persistedCategoryName(selectedCategory));
        appProperties.saveIfNeeded();
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
