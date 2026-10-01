// CardLayoutEditorComponent.cpp -- construction, chrome, paint/resized. The row list and its edits are
// CardLayoutEditorComponentRows.cpp, the keys CardLayoutEditorComponentKeyboard.cpp, the reorder drag
// CardLayoutEditorComponentDrag.cpp, Apply to / presets / reset CardLayoutEditorComponentScope.cpp, the
// test seams CardLayoutEditorComponentTestSeams.cpp. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutEditorComponent.h"
#include "CardLayoutEditorRow.h"
#include "UI/Layout/DialogKeyboard.h"

namespace synth::ui {

namespace {
constexpr int kMargin = 10;
constexpr int kRowGap = 6;
constexpr int kControlHeight = 24;
} // namespace

CardLayoutEditorComponent::CardLayoutEditorComponent(std::unique_ptr<CardLayoutEditorSource> source,
                                                     const ShortcutManager* shortcuts)
    : source_(std::move(source))
    , shortcuts_(shortcuts)
    , params_(source_->parameters())
    , model_(params_, source_->hiddenRows(), source_->supportsGroups()) {
    model_.load(source_->currentLayout());
    setTitle(source_->title());
    buildChrome();
    refreshPresetCombo();
    rebuildRows();
    setSize(kWidth, kHeight);
}

// The source's destructor ends the session (a built-in module records its one undo step there).
CardLayoutEditorComponent::~CardLayoutEditorComponent() = default;

void CardLayoutEditorComponent::buildChrome() {
    titleLabel_.setText(source_->title(), juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    buildScopeAndPresetControls();

    searchEditor_.setComponentID("knobPickerSearch");
    searchEditor_.setTitle("Search controls");
    searchEditor_.setDescription("Search controls");
    searchEditor_.setTextToShowWhenEmpty("Search controls...", juce::Colours::grey);
    searchEditor_.onTextChange = [this] { rebuildRows(); };
    removeHiddenTabStops(searchEditor_);
    bubbleEscapeToParents(searchEditor_);
    addAndMakeVisible(searchEditor_);

    addGroupButton_.setComponentID("cardLayoutAddGroup");
    addGroupButton_.setTitle("Add group");
    addGroupButton_.setTooltip("Add a titled group at the end of the card; drag controls into it");
    addGroupButton_.onClick = [this] { addGroup(); };
    addChildComponent(addGroupButton_);
    addGroupButton_.setVisible(source_->supportsGroups());

    missingLabel_.setJustificationType(juce::Justification::centredLeft);
    missingLabel_.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(missingLabel_);
    updateMissingLabel();

    rowsContent_.setInterceptsMouseClicks(true, true);
    rowsViewport_.setViewedComponent(&rowsContent_, false);
    rowsViewport_.setScrollBarsShown(true, false);
    rowsViewport_.setWantsKeyboardFocus(false);
    addAndMakeVisible(rowsViewport_);
}

void CardLayoutEditorComponent::buildScopeAndPresetControls() {
    addAndMakeVisible(applyToLabel_);
    applyToCombo_.setComponentID("knobPickerApplyTo");
    applyToCombo_.setTitle("Apply to");
    applyToCombo_.setTooltip("Edit this card only, or the default for every card of this kind");
    applyToCombo_.addItem(source_->thisScopeText(), 1);
    applyToCombo_.addItem(source_->allScopeText(), 2);
    applyToCombo_.setSelectedId(1, juce::dontSendNotification);
    applyToCombo_.onChange = [this] {
        applyToAll_ = applyToCombo_.getSelectedId() == 2;
        applyCurrentLayout();
    };
    addAndMakeVisible(applyToCombo_);

    addAndMakeVisible(presetLabel_);
    presetCombo_.setComponentID("knobPickerPreset");
    presetCombo_.setTitle("Presets");
    presetCombo_.setTooltip("Load a saved layout into the scope Apply to names");
    presetCombo_.setTextWhenNothingSelected("Presets...");
    presetCombo_.onChange = [this] {
        const auto name = presetCombo_.getText();
        if (name.isNotEmpty())
            loadPreset(name);
    };
    addAndMakeVisible(presetCombo_);

    saveAsButton_.setComponentID("knobPickerSaveAs");
    saveAsButton_.setTooltip("Save this layout as a named preset");
    saveAsButton_.onClick = [this] { promptSaveAsPreset(); };
    addAndMakeVisible(saveAsButton_);

    deleteButton_.setComponentID("knobPickerDeletePreset");
    deleteButton_.setTooltip("Delete the chosen preset");
    deleteButton_.onClick = [this] { promptDeletePreset(); };
    addAndMakeVisible(deleteButton_);

    const bool presets = source_->hasPresets();
    presetCombo_.setEnabled(presets);
    saveAsButton_.setEnabled(presets);
    deleteButton_.setEnabled(presets);

    resetButton_.setButtonText(source_->resetText());
    resetButton_.setComponentID("knobPickerResetToAutomatic");
    resetButton_.setTooltip(source_->resetTooltip());
    resetButton_.onClick = [this] { resetToDefault(); };
    addAndMakeVisible(resetButton_);
}

void CardLayoutEditorComponent::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

int CardLayoutEditorComponent::layoutExtraControls(juce::Rectangle<int>) { return 0; }

void CardLayoutEditorComponent::resized() {
    auto area = getLocalBounds().reduced(kMargin);

    titleLabel_.setBounds(area.removeFromTop(kControlHeight + 4));
    area.removeFromTop(kRowGap);

    auto scopeRow = area.removeFromTop(kControlHeight);
    applyToLabel_.setBounds(scopeRow.removeFromLeft(60));
    applyToCombo_.setBounds(scopeRow.removeFromLeft(230));
    area.removeFromTop(kRowGap);

    auto presetRow = area.removeFromTop(kControlHeight);
    presetLabel_.setBounds(presetRow.removeFromLeft(46));
    presetCombo_.setBounds(presetRow.removeFromLeft(120));
    presetRow.removeFromLeft(4);
    saveAsButton_.setBounds(presetRow.removeFromLeft(70));
    presetRow.removeFromLeft(4);
    deleteButton_.setBounds(presetRow.removeFromLeft(56));
    presetRow.removeFromLeft(4);
    resetButton_.setBounds(presetRow);
    area.removeFromTop(kRowGap);

    auto searchRow = area.removeFromTop(kControlHeight);
    if (addGroupButton_.isVisible()) {
        addGroupButton_.setBounds(searchRow.removeFromRight(100));
        searchRow.removeFromRight(4);
    }
    searchEditor_.setBounds(searchRow);
    area.removeFromTop(kRowGap);

    if (const int used = layoutExtraControls(area.withHeight(kControlHeight)); used > 0)
        area.removeFromTop(used + kRowGap / 2);

    missingLabel_.setVisible(model_.missingNames().size() > 0);
    if (missingLabel_.isVisible()) {
        missingLabel_.setBounds(area.removeFromTop(18));
        area.removeFromTop(kRowGap / 2);
    }

    rowsViewport_.setBounds(area);
    layOutRows();
}

void CardLayoutEditorComponent::layOutRows() {
    const int width = rowsViewport_.getWidth() - rowsViewport_.getScrollBarThickness();
    const int height = rows_.size() * CardLayoutEditorRow::kRowHeight;
    rowsContent_.setSize(juce::jmax(width, 1), juce::jmax(height, 1));

    int y = 0;
    for (auto* row : rows_) {
        row->setBounds(0, y, rowsContent_.getWidth(), CardLayoutEditorRow::kRowHeight);
        y += CardLayoutEditorRow::kRowHeight;
    }
    if (rowDrag_.isReordering())
        placeDragRows(); // a settle carrying on across the rebuild a commit causes
}

} // namespace synth::ui
