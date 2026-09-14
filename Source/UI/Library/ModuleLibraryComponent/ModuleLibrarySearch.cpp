// ModuleLibrarySearch.cpp -- the search box: query normalisation/matching, highlight-span
// computation, live filtering of buildRows()'s section/child visibility, and the search field's
// theme colours.
#include "ModuleLibraryComponent.h"

bool ModuleLibraryComponent::textMatchesQuery(const juce::String& text, const juce::String& query) {
    const auto q = normalisedSearchQuery(query);
    return q.isNotEmpty() && text.containsIgnoreCase(q);
}

std::vector<ModuleLibraryComponent::HighlightSpan>
ModuleLibraryComponent::highlightSpansFor(const juce::String& text, const juce::String& query) {
    std::vector<HighlightSpan> spans;
    const auto q = normalisedSearchQuery(query);
    if (q.isEmpty() || text.isEmpty())
        return spans;
    const int qLen = q.length();
    int from = 0;
    while (from + qLen <= text.length()) {
        const int hit = text.indexOfIgnoreCase(from, q);
        if (hit < 0)
            break;
        spans.push_back({hit, qLen});
        from = hit + qLen;
    }
    return spans;
}

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
    if (textMatchesQuery(entries[headerIndex].text, q))
        return true;
    for (size_t j = headerIndex + 1; j < end; ++j)
        if (textMatchesQuery(entries[j].text, q))
            return true;
    return false;
}

bool ModuleLibraryComponent::childVisibleInSearch(const Entry& entry) const {
    const auto q = normalisedSearchQuery(searchQuery);
    return textMatchesQuery(entry.text, q) || textMatchesQuery(entry.section, q);
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
