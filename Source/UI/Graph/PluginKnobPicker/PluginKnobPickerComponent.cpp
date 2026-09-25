// PluginKnobPickerComponent.cpp -- construction, chrome, paint/resized. Row list management and
// apply-to-scope live in the sibling units below (this class is split by concern, root CLAUDE.md's
// "Code structure" rule): PluginKnobPickerComponentRows.cpp (search/tick/reorder/label/apply),
// PluginKnobPickerComponentScope.cpp (Apply to / presets / touch-to-add wiring), and
// PluginKnobPickerComponentTestSeams.cpp. See docs/control/plugin-card-layout.md#choosing-knobs.
#include "PluginKnobPickerComponent.h"
#include "Plugin/Hosting/HostedPluginCardLayout.h"
#include "PluginKnobPickerRow.h"
#include "PluginKnobPickerTouchCapture.h"

namespace synth::ui {

namespace {
constexpr int kMargin = 10;
constexpr int kRowGap = 6;
constexpr int kControlHeight = 24;
} // namespace

PluginKnobPickerComponent::PluginKnobPickerComponent(HostedPluginModule& module, PluginCardLayoutStore* store,
                                                     juce::AudioProcessorGraph& graph,
                                                     juce::AudioProcessorGraph::NodeID nodeId,
                                                     AppUndoManager* undoManager)
    : module_(&module)
    , store_(store)
    , graph_(graph)
    , nodeId_(nodeId)
    , undoManager_(undoManager)
    , identity_(module.getIdentity())
    , titleLabel_("title", "Knobs for \"" + module.getPluginName() + "\"") {
    for (const auto& info : module.getInstanceParameters())
        allParams_.push_back({info.paramId, info.index, info.displayName});

    const auto resolved = resolveHostedCardLayout(module, store_);
    partitionSlots(resolved.layout.slots);

    touchCapture_ = std::make_unique<PluginKnobPickerTouchCapture>(module);
    touchCapture_->onParameterTouched = [this](int index) { handleParameterTouched(index); };
    touchCapture_->onRequestOpenEditor = [this] {
        if (onOpenPluginEditorRequested)
            onOpenPluginEditorRequested();
    };

    buildChrome();
    refreshPresetCombo();
    rebuildRows();

    setSize(kWidth, kHeight);
}

// touchCapture_'s destructor already disarms and unhooks from every instance parameter; nothing else
// here owns a live listener.
PluginKnobPickerComponent::~PluginKnobPickerComponent() = default;

void PluginKnobPickerComponent::buildChrome() {
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    addAndMakeVisible(applyToLabel_);
    applyToCombo_.setComponentID("knobPickerApplyTo");
    applyToCombo_.addItem("This instance", 1);
    applyToCombo_.addItem("All " + module_.get()->getPluginName() + " instances", 2);
    applyToCombo_.setSelectedId(1, juce::dontSendNotification);
    applyToCombo_.onChange = [this] {
        applyToAllInstances_ = applyToCombo_.getSelectedId() == 2;
        applyCurrentLayout();
    };
    addAndMakeVisible(applyToCombo_);

    addAndMakeVisible(presetLabel_);
    presetCombo_.setComponentID("knobPickerPreset");
    presetCombo_.setTextWhenNothingSelected("Presets...");
    presetCombo_.onChange = [this] {
        const auto name = presetCombo_.getText();
        if (name.isNotEmpty())
            loadPreset(name);
    };
    addAndMakeVisible(presetCombo_);

    saveAsButton_.setComponentID("knobPickerSaveAs");
    saveAsButton_.onClick = [this] { promptSaveAsPreset(); };
    addAndMakeVisible(saveAsButton_);

    deleteButton_.setComponentID("knobPickerDeletePreset");
    deleteButton_.onClick = [this] { promptDeletePreset(); };
    addAndMakeVisible(deleteButton_);

    resetButton_.setComponentID("knobPickerResetToAutomatic");
    resetButton_.onClick = [this] { resetToAutomatic(); };
    addAndMakeVisible(resetButton_);

    searchEditor_.setComponentID("knobPickerSearch");
    searchEditor_.setTextToShowWhenEmpty("Search parameters...", juce::Colours::grey);
    searchEditor_.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible(searchEditor_);

    touchToAddToggle_.setComponentID("knobPickerTouchToAdd");
    touchToAddToggle_.onClick = [this] { touchCapture_->setArmed(touchToAddToggle_.getToggleState()); };
    addAndMakeVisible(touchToAddToggle_);

    missingLabel_.setJustificationType(juce::Justification::centredLeft);
    missingLabel_.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(missingLabel_);
    updateMissingLabel();

    rowsContent_.setInterceptsMouseClicks(true, true);
    rowsViewport_.setViewedComponent(&rowsContent_, false);
    rowsViewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(rowsViewport_);
}

void PluginKnobPickerComponent::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void PluginKnobPickerComponent::resized() {
    auto area = getLocalBounds().reduced(kMargin);

    titleLabel_.setBounds(area.removeFromTop(kControlHeight + 4));
    area.removeFromTop(kRowGap);

    auto scopeRow = area.removeFromTop(kControlHeight);
    applyToLabel_.setBounds(scopeRow.removeFromLeft(60));
    applyToCombo_.setBounds(scopeRow.removeFromLeft(220));
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
    searchEditor_.setBounds(searchRow);
    area.removeFromTop(kRowGap);

    touchToAddToggle_.setBounds(area.removeFromTop(kControlHeight));
    area.removeFromTop(kRowGap / 2);

    if (!missingSlots_.empty()) {
        missingLabel_.setBounds(area.removeFromTop(18));
        area.removeFromTop(kRowGap / 2);
    }

    rowsViewport_.setBounds(area);
    layOutRows();
}

void PluginKnobPickerComponent::layOutRows() {
    const int width = rowsViewport_.getWidth() - rowsViewport_.getScrollBarThickness();
    const int height = rows_.size() * PluginKnobPickerRow::kRowHeight;
    rowsContent_.setSize(juce::jmax(width, 1), juce::jmax(height, 1));

    int y = 0;
    for (auto* row : rows_) {
        row->setBounds(0, y, rowsContent_.getWidth(), PluginKnobPickerRow::kRowHeight);
        y += PluginKnobPickerRow::kRowHeight;
    }
}

} // namespace synth::ui
