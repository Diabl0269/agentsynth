#include "ShortcutsSettingsTab.h"

namespace {
// Divider alpha under a section header. GENTLE on purpose: the row list is already visually grouped
// by the header text and the gaps, so the rule only has to hint at the boundary — at
// PreferencesSettingsTab's original 0.18 it read as a table border and chopped the list into boxes.
// Both tabs were softened together; keep them in the same ballpark.
constexpr float kDividerAlpha = 0.10f;

// Section header text alpha (muted, like the library sidebar's category labels) and the lift it
// gets on hover so it reads as clickable.
constexpr float kHeaderTextAlpha = 0.70f;
constexpr float kHeaderHoverTextAlpha = 1.0f;
constexpr float kTopStripTextAlpha = 0.65f;

constexpr int kChevronSize = 8;
constexpr int kHeaderTextIndent = 22; // leaves room for the chevron at x = 6
} // namespace

ShortcutsSettingsTab::ShortcutsSettingsTab(ShortcutManager& sm)
    : shortcutManager(sm) {
    setWantsKeyboardFocus(true);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Keyboard Shortcuts", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));

    // Search field, same shape as the module library's: single line, Escape clears rather than
    // bubbling, and the filter is applied on every keystroke (see setSearchText -> rebuildLayout).
    addAndMakeVisible(searchEditor);
    searchEditor.setMultiLine(false);
    searchEditor.setReturnKeyStartsNewLine(false);
    searchEditor.setEscapeAndReturnKeysConsumed(true);
    searchEditor.setSelectAllWhenFocused(true);
    searchEditor.setJustification(juce::Justification::centredLeft);
    searchEditor.setIndents(6, 0);
    searchEditor.setFont(juce::Font(juce::FontOptions(13.0f)));
    searchEditor.setTextToShowWhenEmpty("Search shortcuts...", findColour(juce::Label::textColourId).withAlpha(0.45f));
    searchEditor.setTooltip("Filter by action name or by the key it is bound to (e.g. \"Cmd\", \"Shift\", \"Left\").");
    searchEditor.onTextChange = [this] { rebuildLayout(); };
    searchEditor.onEscapeKey = [this] {
        if (searchEditor.getText().isNotEmpty())
            setSearchText({});
    };

    // The rows live inside a Viewport: the table is well over forty rows now, and a settings dialog
    // is a few hundred pixels tall. The title, the search field, the collapse-all strip and the
    // export/import/reset row all stay pinned outside it — the two controls that change WHICH rows
    // are on screen must never scroll out of reach.
    addAndMakeVisible(rowsViewport);
    rowsViewport.setViewedComponent(&rowsHost, false);
    rowsViewport.setScrollBarsShown(true, false);

    for (auto& actionId : shortcutManager.getActionIds()) {
        actionIds.add(actionId);

        auto descLabel = std::make_unique<juce::Label>();
        descLabel->setText(ShortcutManager::getActionDescription(actionId), juce::dontSendNotification);
        descLabel->setFont(juce::FontOptions(14.0f));
        // Children of the scrolled host, not of the tab: their bounds are in content space, and the
        // Viewport is what clips them.
        rowsHost.addAndMakeVisible(*descLabel);
        descLabels.push_back(std::move(descLabel));

        auto bindButton = std::make_unique<juce::TextButton>();
        bindButton->setButtonText(ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding(actionId)));
        bindButton->setTooltip("Click, then press a key to rebind");
        int index = static_cast<int>(bindButtons.size());
        bindButton->onClick = [this, index] { startListening(index); };
        rowsHost.addAndMakeVisible(*bindButton);
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

//==============================================================================
// Pure helpers
//==============================================================================

bool ShortcutsSettingsTab::rowMatchesQuery(const juce::String& query, const juce::String& description,
                                           const juce::String& bindingText) {
    const auto q = normalisedQuery(query);
    if (q.isEmpty())
        return true;
    return description.containsIgnoreCase(q) || bindingText.containsIgnoreCase(q);
}

//==============================================================================
// Layout
//==============================================================================

void ShortcutsSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(15);
    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(8);

    searchEditor.setBounds(bounds.removeFromTop(kSearchHeight));
    topStripBounds = bounds.removeFromTop(kTopStripHeight);

    // Pinned action row at the bottom, carved before the rows get the remainder.
    auto buttonRow = bounds.removeFromBottom(28);
    bounds.removeFromBottom(10);
    exportButton.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(8);
    importButton.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(8);
    resetButton.setBounds(buttonRow.removeFromLeft(160));

    rowsViewport.setBounds(bounds);
    rebuildLayout();
}

void ShortcutsSettingsTab::rebuildLayout() {
    layout.clear();

    const auto query = normalisedQuery(searchEditor.getText());
    const bool filtering = query.isNotEmpty();

    // Width the rows are laid out to: the viewport minus its scrollbar gutter, so a rebind button
    // never runs under the thumb. The gutter is reserved UNCONDITIONALLY rather than measured —
    // whether the bar is on screen depends on the content height this very pass is computing, so
    // asking would make the layout depend on its own output and shift the rows by a few pixels every
    // time a section folded.
    const int contentWidth = juce::jmax(0, rowsViewport.getWidth() - rowsViewport.getScrollBarThickness());

    // Every row starts hidden; the pass below shows the ones it lays out. A row left invisible is
    // one the fold or the filter dropped — invisible children neither paint nor hit-test, which is
    // what stops a collapsed section's buttons from still being clickable.
    for (size_t i = 0; i < descLabels.size(); ++i) {
        descLabels[i]->setVisible(false);
        bindButtons[i]->setVisible(false);
    }

    int y = 0;
    for (auto category : ShortcutManager::getCategoryOrder()) {
        // The ids of this category, and which of them survive the filter. Header text counts as a
        // match too, so typing "piano" reveals the whole Piano Roll block rather than nothing.
        const bool headerMatches = filtering && ShortcutManager::getCategoryName(category).containsIgnoreCase(query);
        std::vector<int> visibleRows;
        for (int i = 0; i < actionIds.size(); ++i) {
            if (ShortcutManager::getCategory(actionIds[i]) != category)
                continue;
            if (!filtering || headerMatches ||
                rowMatchesQuery(query, descLabels[(size_t)i]->getText(), bindButtons[(size_t)i]->getButtonText()))
                visibleRows.push_back(i);
        }

        const bool sectionHasMatch = !visibleRows.empty();
        if (!sectionIsVisible(filtering, sectionHasMatch))
            continue;

        layout.push_back({-1, category, {0, y, contentWidth, kSectionHeaderHeight}, true});
        y += kSectionHeaderHeight;
        // The divider sits in the gap below the header; paintRows draws it at the header's bottom
        // edge, so no layout height is reserved for the 1 px rule itself.
        y += kRowGap;

        if (sectionIsExpanded(filtering, sectionHasMatch, isSectionCollapsed(category))) {
            for (int index : visibleRows) {
                juce::Rectangle<int> row(0, y, contentWidth, kRowHeight);
                layout.push_back({index, category, row, false});

                auto rowArea = row;
                descLabels[(size_t)index]->setBounds(rowArea.removeFromLeft(kDescriptionWidth));
                bindButtons[(size_t)index]->setBounds(rowArea);
                descLabels[(size_t)index]->setVisible(true);
                bindButtons[(size_t)index]->setVisible(true);

                y += kRowHeight + kRowGap;
            }
        }

        y += kSectionGap;
    }

    rowsHost.setBounds(0, 0, juce::jmax(contentWidth, rowsViewport.getWidth()), juce::jmax(y, 1));
    rowsHost.repaint();
    repaint(topStripBounds);
}

//==============================================================================
// Paint
//==============================================================================

void ShortcutsSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));

    // The collapse-all strip. Right-aligned and drawn small, exactly as the library sidebar's is —
    // it is chrome, not a button, and should not compete with the section headers.
    const auto textColour = findColour(juce::Label::textColourId);
    g.setColour(textColour.withAlpha(topStripHovered ? kHeaderHoverTextAlpha : kTopStripTextAlpha));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(areAllSectionsCollapsed() ? "EXPAND ALL" : "COLLAPSE ALL", topStripBounds,
               juce::Justification::centredRight);
}

void ShortcutsSettingsTab::paintRows(juce::Graphics& g) {
    const auto textColour = findColour(juce::Label::textColourId);

    for (const auto& entry : layout) {
        if (!entry.isHeader)
            continue;

        const bool hot = hoveredHeader.has_value() && *hoveredHeader == entry.category;
        const bool collapsed = !isSearchActive() && isSectionCollapsed(entry.category);

        // Chevron drawn as a path rather than a ▾/▸ glyph — glyph coverage is not guaranteed across
        // the embedded typefaces (see the theming font limitation), the same reason
        // ModuleLibraryComponent draws its own.
        drawChevron(g,
                    juce::Rectangle<float>(
                        6.0f, (float)entry.bounds.getY() + (float)(kSectionHeaderHeight - kChevronSize) * 0.5f,
                        (float)kChevronSize, (float)kChevronSize),
                    collapsed, textColour.withAlpha(hot ? kHeaderHoverTextAlpha : kHeaderTextAlpha));

        g.setColour(textColour.withAlpha(hot ? kHeaderHoverTextAlpha : kHeaderTextAlpha));
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.drawText(ShortcutManager::getCategoryName(entry.category).toUpperCase(),
                   entry.bounds.withTrimmedLeft(kHeaderTextIndent), juce::Justification::centredLeft);

        // The gentle rule: one hairline under the header, at low alpha. It separates the sections
        // without boxing them in — see kDividerAlpha.
        g.setColour(textColour.withAlpha(kDividerAlpha));
        g.fillRect(entry.bounds.getX(), entry.bounds.getBottom(), entry.bounds.getWidth(), 1);
    }

    if (layout.empty() && isSearchActive()) {
        g.setColour(textColour.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        // rowsHost's bounds, not the tab's: this paints into the scrolled host.
        g.drawText("No matching shortcuts",
                   juce::Rectangle<int>(6, 0, juce::jmax(0, rowsHost.getWidth() - 12), kRowHeight),
                   juce::Justification::centredLeft);
    }
}

void ShortcutsSettingsTab::drawChevron(juce::Graphics& g, juce::Rectangle<float> area, bool collapsed,
                                       juce::Colour colour) {
    juce::Path p;
    p.addTriangle(area.getX(), area.getY(), area.getRight(), area.getY(), area.getCentreX(), area.getBottom());
    if (collapsed)
        p.applyTransform(
            juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, area.getCentreX(), area.getCentreY()));
    g.setColour(colour);
    g.fillPath(p);
}

//==============================================================================
// Mouse
//==============================================================================

void ShortcutsSettingsTab::mouseDown(const juce::MouseEvent& e) {
    if (isInTopStrip(e.getPosition()))
        toggleAllSections();
}

void ShortcutsSettingsTab::mouseMove(const juce::MouseEvent& e) {
    const bool nowHovered = isInTopStrip(e.getPosition());
    if (nowHovered == topStripHovered)
        return;
    topStripHovered = nowHovered;
    setMouseCursor(nowHovered ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint(topStripBounds);
}

void ShortcutsSettingsTab::mouseExit(const juce::MouseEvent&) {
    if (!topStripHovered)
        return;
    topStripHovered = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(topStripBounds);
}

void ShortcutsSettingsTab::rowsMouseDown(const juce::MouseEvent& e) {
    if (const auto category = sectionHeaderAt(e.getPosition()))
        toggleSection(*category);
}

void ShortcutsSettingsTab::rowsMouseMove(const juce::MouseEvent& e) {
    const auto category = sectionHeaderAt(e.getPosition());
    if (category == hoveredHeader)
        return; // repaint only on a real change, never per mouse-move
    hoveredHeader = category;
    rowsHost.setMouseCursor(category.has_value() ? juce::MouseCursor::PointingHandCursor
                                                 : juce::MouseCursor::NormalCursor);
    rowsHost.repaint();
}

void ShortcutsSettingsTab::rowsMouseExit() {
    if (!hoveredHeader.has_value())
        return;
    hoveredHeader.reset();
    rowsHost.setMouseCursor(juce::MouseCursor::NormalCursor);
    rowsHost.repaint();
}

std::optional<ShortcutCategory> ShortcutsSettingsTab::sectionHeaderAt(juce::Point<int> contentPos) const {
    for (const auto& entry : layout)
        if (entry.isHeader && entry.bounds.contains(contentPos))
            return entry.category;
    return std::nullopt;
}

//==============================================================================
// Search / collapse state
//==============================================================================

void ShortcutsSettingsTab::setSearchText(const juce::String& text) {
    if (searchEditor.getText() != text)
        searchEditor.setText(text, juce::dontSendNotification);
    rebuildLayout();
}

bool ShortcutsSettingsTab::isSectionCollapsed(ShortcutCategory category) const {
    return collapsedSections.find(category) != collapsedSections.end();
}

void ShortcutsSettingsTab::setSectionCollapsed(ShortcutCategory category, bool collapsed) {
    const bool changed =
        collapsed ? collapsedSections.insert(category).second : (collapsedSections.erase(category) > 0);
    if (!changed)
        return;
    rebuildLayout();
}

bool ShortcutsSettingsTab::areAllSectionsCollapsed() const {
    for (auto category : ShortcutManager::getCategoryOrder())
        if (!isSectionCollapsed(category))
            return false;
    return true;
}

void ShortcutsSettingsTab::setAllSectionsCollapsed(bool collapsed) {
    std::set<ShortcutCategory> next;
    if (collapsed)
        for (auto category : ShortcutManager::getCategoryOrder())
            next.insert(category);
    if (next == collapsedSections)
        return;
    collapsedSections = std::move(next);
    rebuildLayout();
}

bool ShortcutsSettingsTab::isRowVisible(int index) const {
    for (const auto& entry : layout)
        if (!entry.isHeader && entry.actionIndex == index)
            return true;
    return false;
}

bool ShortcutsSettingsTab::isSectionVisible(ShortcutCategory category) const {
    for (const auto& entry : layout)
        if (entry.isHeader && entry.category == category)
            return true;
    return false;
}

//==============================================================================
// Rebinding
//==============================================================================

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
        // Swap: assign the old binding of this action to the conflicting action. The conflict is
        // category-scoped now, so the action we swap with is always one from the SAME section —
        // which is what makes a swap comprehensible (both rows are on screen together) rather than
        // silently moving a piano-roll key because a graph action wanted it.
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
    if (index < 0 || index >= static_cast<int>(bindButtons.size()))
        return;
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
        bindButtons[i]->removeColour(juce::TextButton::buttonColourId);
    }
    // A rebind changes the text the filter matches against, so a row can enter or leave the current
    // search results as a direct result of it — re-run the pass rather than leaving a stale list.
    rebuildLayout();
}
