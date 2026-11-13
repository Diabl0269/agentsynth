// ModuleLibrarySearch.cpp -- the search box: query normalisation, live filtering of buildRows()'s section/child
// visibility, and the search field's theme colours.
#include "ModuleLibraryComponent.h"

namespace {
// What a row's query is matched against: its name, plus the other names a module goes by (a module row only;
// the painter highlights the visible name alone).
juce::String searchableText(const ModuleLibraryComponent::Entry& entry) {
    if (entry.kind != ModuleLibraryComponent::RowKind::Module)
        return entry.text;
    return entry.text + " " + synth::ui::moduleSearchAliases(entry.text);
}
} // namespace

void ModuleLibraryComponent::setSearchText(const juce::String& text) {
    if (searchEditor.getText() != text)
        searchEditor.setText(text, juce::dontSendNotification);
    applySearchQuery(text);
}

void ModuleLibraryComponent::applySearchQuery(const juce::String& text) {
    if (searchQuery == text)
        return;
    searchQuery = text;
    clampHoverToVisibleRow();
    clampKeyboardFocusToVisibleRow();
    // New filters should show the first match, not leave the view parked halfway down a list
    // that just shrank.
    scrollOffset = 0;
    updateScrollBar();
    repaint();
}

bool ModuleLibraryComponent::sectionVisibleInSearch(size_t headerIndex, size_t end) const {
    const auto q = normalisedSearchQuery(searchQuery);
    if (q.isEmpty())
        return false;
    if (synth::ui::searchMatches(entries[headerIndex].text, q))
        return true;
    for (size_t j = headerIndex + 1; j < end; ++j)
        if (synth::ui::searchMatches(searchableText(entries[j]), q))
            return true;
    return false;
}

bool ModuleLibraryComponent::childVisibleInSearch(const Entry& entry) const {
    const auto q = normalisedSearchQuery(searchQuery);
    return q.isNotEmpty() &&
           (synth::ui::searchMatches(searchableText(entry), q) || synth::ui::searchMatches(entry.section, q));
}

void ModuleLibraryComponent::applySearchEditorColours() {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    juce::Colour bg = juce::Colours::black.withAlpha(0.35f);
    juce::Colour text = juce::Colours::white;
    juce::Colour muted = juce::Colours::grey;
    juce::Colour outline = juce::Colours::grey.darker();
    if (lf != nullptr) {
        const auto& c = lf->getTheme().colors;
        bg = c.surface;
        text = c.textPrimary;
        muted = c.textMuted;
        outline = c.border;
    }
    searchEditor.setColour(juce::TextEditor::backgroundColourId, bg);
    searchEditor.setColour(juce::TextEditor::textColourId, text);
    searchEditor.setColour(juce::TextEditor::outlineColourId, outline);
    searchEditor.setColour(juce::TextEditor::focusedOutlineColourId, outline);
    searchEditor.setTextToShowWhenEmpty("Search modules...", muted);
}
