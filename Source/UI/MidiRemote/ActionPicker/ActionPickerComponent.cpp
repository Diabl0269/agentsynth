// Concern: the action picker's row model (grouping, filtering) and its list rendering.

#include "UI/MidiRemote/ActionPicker/ActionPickerComponent.h"

#include "MidiRemote/ContinuousTarget.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <array>

namespace synth::ui {

std::vector<ActionPickerRow> buildActionPickerRows(const juce::String& filter, int effectivePageCount) {
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
            ActionPickerRow row;
            row.actionId = id;
            row.label = label;
            group.push_back(row);
        }
        if (group.empty())
            continue;
        ActionPickerRow header;
        header.isHeader = true;
        header.label = ShortcutManager::getCategoryName(category);
        rows.push_back(header);
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
        ActionPickerRow row;
        row.isContinuous = true;
        row.label = label;
        row.continuousKind = kind;
        continuousGroup.push_back(row);
    }
    if (!continuousGroup.empty()) {
        ActionPickerRow header;
        header.isHeader = true;
        header.label = "Continuous";
        rows.push_back(header);
        rows.insert(rows.end(), continuousGroup.begin(), continuousGroup.end());
    }

    // FRO142 (docs/control/midi-remote.md#pages): Next page / Previous page always offered, then one
    // "Page N" row per page the selected control's controller currently has -- filtered the same way
    // as every other row above.
    std::vector<ActionPickerRow> pageGroup;
    auto addPageRow = [&](const juce::String& label, synth::PageCommand command, int pageNumber) {
        if (needle.isNotEmpty() && !label.containsIgnoreCase(needle))
            return;
        ActionPickerRow row;
        row.isPage = true;
        row.label = label;
        row.pageCommand = command;
        row.pageNumber = pageNumber;
        pageGroup.push_back(row);
    };
    addPageRow("Next page", synth::PageCommand::next, 1);
    addPageRow("Previous page", synth::PageCommand::previous, 1);
    for (int page = 1; page <= effectivePageCount; ++page)
        addPageRow("Page " + juce::String(page), synth::PageCommand::go, page);
    if (!pageGroup.empty()) {
        ActionPickerRow header;
        header.isHeader = true;
        header.label = "Pages";
        rows.push_back(header);
        rows.insert(rows.end(), pageGroup.begin(), pageGroup.end());
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

    rows_ = buildActionPickerRows({}, effectivePageCount_);
    list_.updateContent();
}

ActionPickerComponent::~ActionPickerComponent() = default;

void ActionPickerComponent::setFilter(const juce::String& filter) {
    rows_ = buildActionPickerRows(filter, effectivePageCount_);
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
    if (chosen.isPage) {
        if (onPageChosen)
            onPageChosen(chosen.pageCommand, chosen.pageNumber);
        return;
    }
    if (onChosen)
        onChosen(chosen.actionId);
}

void ActionPickerComponent::setEffectivePageCount(int count) {
    count = juce::jmax(1, count);
    if (count == effectivePageCount_)
        return;
    effectivePageCount_ = count;
    setFilter(searchEditor_.getText()); // rebuilds rows_ under the current filter
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
