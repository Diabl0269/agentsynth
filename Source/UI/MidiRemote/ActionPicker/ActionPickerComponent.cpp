// Concern: the action picker's row model (grouping, filtering) and its list rendering.

#include "UI/MidiRemote/ActionPicker/ActionPickerComponent.h"

#include "MidiRemote/ContinuousTarget.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <array>

namespace synth::ui {

std::vector<ActionPickerRow> buildActionPickerRows(const juce::String& filter) {
    std::vector<ActionPickerRow> rows;
    const auto needle = filter.trim();
    for (const auto category : ShortcutManager::getCategoryOrder()) {
        std::vector<ActionPickerRow> group;
        for (const auto& id : ShortcutManager::getActionIdsInCategory(category)) {
            if (AppCommands::getCommandForAction(id) == AppCommands::kNoCommand)
                continue;
            const auto label = ShortcutManager::getActionDescription(id);
            if (needle.isNotEmpty() && !label.containsIgnoreCase(needle))
                continue;
            group.push_back({false, false, id, label, {}});
        }
        if (group.empty())
            continue;
        rows.push_back({true, false, {}, ShortcutManager::getCategoryName(category), {}});
        rows.insert(rows.end(), group.begin(), group.end());
    }

    // FRO236 (docs/control/midi-remote.md#continuous-targets): one more group, appended last, same
    // filter rule as every action category above.
    static constexpr std::array<synth::ContinuousTargetKind, 3> kContinuousKinds{
        synth::ContinuousTargetKind::bpm, synth::ContinuousTargetKind::playhead,
        synth::ContinuousTargetKind::masterVolume};
    std::vector<ActionPickerRow> continuousGroup;
    for (const auto kind : kContinuousKinds) {
        const auto label = synth::continuousTargetDisplayName(kind);
        if (needle.isNotEmpty() && !label.containsIgnoreCase(needle))
            continue;
        continuousGroup.push_back({false, true, {}, label, kind});
    }
    if (!continuousGroup.empty()) {
        rows.push_back({true, false, {}, "Continuous", {}});
        rows.insert(rows.end(), continuousGroup.begin(), continuousGroup.end());
    }
    return rows;
}

ActionPickerComponent::ActionPickerComponent() {
    setSize(kWidth, kHeight);

    searchEditor_.setComponentID("actionSearchEditor");
    searchEditor_.setTextToShowWhenEmpty("Search actions", juce::Colours::grey);
    searchEditor_.onTextChange = [this] { setFilter(searchEditor_.getText()); };
    addAndMakeVisible(searchEditor_);

    list_.setComponentID("actionList");
    list_.setRowHeight(24);
    addAndMakeVisible(list_);

    rows_ = buildActionPickerRows({});
    list_.updateContent();
}

ActionPickerComponent::~ActionPickerComponent() = default;

void ActionPickerComponent::setFilter(const juce::String& filter) {
    rows_ = buildActionPickerRows(filter);
    list_.updateContent();
    list_.repaint();
}

void ActionPickerComponent::chooseRow(int row) {
    if (row < 0 || row >= static_cast<int>(rows_.size()) || rows_[static_cast<size_t>(row)].isHeader)
        return;
    const auto& chosen = rows_[static_cast<size_t>(row)];
    if (chosen.isContinuous) {
        if (onContinuousChosen)
            onContinuousChosen(chosen.continuousKind);
        return;
    }
    if (onChosen)
        onChosen(chosen.actionId);
}

void ActionPickerComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour mutedColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::grey;
    const juce::Colour textColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    const auto& r = rows_[static_cast<size_t>(row)];

    if (r.isHeader) {
        g.setColour(mutedColour);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(r.label.toUpperCase(), 8, 0, width - 16, height, juce::Justification::centredLeft);
        return;
    }
    if (selected) {
        g.setColour(accentColour.withAlpha(0.2f));
        g.fillRect(0, 0, width, height);
    }
    g.setColour(textColour);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(r.label, 16, 0, width - 24, height, juce::Justification::centredLeft);
}

void ActionPickerComponent::resized() {
    auto bounds = getLocalBounds().reduced(8);
    searchEditor_.setBounds(bounds.removeFromTop(26));
    bounds.removeFromTop(6);
    list_.setBounds(bounds);
}

} // namespace synth::ui
