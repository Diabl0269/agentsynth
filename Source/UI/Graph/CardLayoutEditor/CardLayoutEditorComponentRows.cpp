// CardLayoutEditorComponentRows.cpp -- the row list: building it from the model (filtered by the
// search), the edits a row reports, + Add group, and applyCurrentLayout(), the one write every edit
// ends in. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutEditorComponent.h"
#include "CardLayoutEditorRow.h"

namespace synth::ui {

void CardLayoutEditorComponent::rebuildRows() {
    if (rowDrag_.animator().isPressed() || rowDrag_.animator().isDragging())
        rowDrag_.discard(); // a lifted row cannot outlive its component; a settle in flight carries on
    rows_.clear();
    for (const auto& row : model_.rows(searchEditor_.getText()))
        addRow(row);
    refreshShortcutTooltips();
    layOutRows();
}

// A control row can be renamed and dragged while it is in the layout; with hidden rows that leave the
// layout (a hosted plugin) an unticked row is neither. A header is renamed, never dragged.
void CardLayoutEditorComponent::addRow(const CardLayoutEditorModel::Row& row) {
    using Kind = CardLayoutEditorModel::Row::Kind;
    const bool header = row.kind == Kind::Header;
    const bool draggable = !header && row.placed;
    const bool renameable = header || (row.kind == Kind::Param && row.placed);
    auto* added = rows_.add(new CardLayoutEditorRow(row, draggable, renameable));
    rowsContent_.addAndMakeVisible(added);
    wireRow(*added, row);
}

void CardLayoutEditorComponent::wireRow(CardLayoutEditorRow& row, const CardLayoutEditorModel::Row& model) {
    const auto key = model.key;
    row.onToggled = [this, key](bool checked) { setChecked(key, checked); };
    row.onRenamed = [this, model](const juce::String& text) { renameRow(model, text); };
    row.onWidgetChosen = [this, key](CardWidget widget) {
        model_.setWidget(key, widget);
        applyCurrentLayout();
    };
    row.onKey = [this, &row](const juce::KeyPress& key) { return handleRowKey(row, key); };
    row.onDragStarted = [this, key](const juce::MouseEvent& e) { beginRowDrag(key, e); };
    row.onDragUpdated = [this](const juce::MouseEvent& e) { updateRowDrag(e); };
    row.onDragEnded = [this] { endRowDrag(); };
}

// Ticking a row that leaves the layout when unticked moves it between the two lists, so the list is
// rebuilt; a row that stays in place only changes its own tick.
void CardLayoutEditorComponent::setChecked(const juce::String& paramId, bool checked) {
    model_.setShown(paramId, checked);
    applyCurrentLayout();
    if (source_->hiddenRows() == CardLayoutEditorSource::HiddenRows::LeaveTheLayout) {
        rebuildRows();
        focusRow(paramId); // the keyboard stays on the row it just moved
    }
}

void CardLayoutEditorComponent::showParameter(const juce::String& paramId) { setChecked(paramId, true); }

// A rename alone does not rebuild: the row already shows what it committed, and a rebuild mid-edit
// would flicker for nothing.
void CardLayoutEditorComponent::renameRow(const CardLayoutEditorModel::Row& row, const juce::String& text) {
    if (row.kind == CardLayoutEditorModel::Row::Kind::Header)
        model_.setSectionTitle(row.section, text);
    else
        model_.setLabel(row.key, text);
    applyCurrentLayout();
}

void CardLayoutEditorComponent::addGroup() {
    const int section = model_.addGroup();
    commitAndRebuild(CardLayoutEditorModel::headerKey(section));
}

void CardLayoutEditorComponent::commitAndRebuild(const juce::String& focusKey) {
    applyCurrentLayout();
    rebuildRows();
    if (focusKey.isNotEmpty())
        focusRow(focusKey);
}

void CardLayoutEditorComponent::updateMissingLabel() {
    const auto names = model_.missingNames();
    if (names.isEmpty()) {
        missingLabel_.setText({}, juce::dontSendNotification);
        return;
    }
    missingLabel_.setText(juce::String(names.size()) +
                              " parameters missing in this version: " + names.joinIntoString(", "),
                          juce::dontSendNotification);
}

// Writes whichever scope "Apply to" names; the source decides what that means and how it undoes. The
// first write drops the missing items for good (they are not in the model's layout), so their line
// goes away with it.
void CardLayoutEditorComponent::applyCurrentLayout() {
    if (!source_->isAlive())
        return;
    source_->apply(model_.toLayout(), applyToAll_);
    if (model_.missingNames().size() > 0) {
        model_.clearMissing();
        updateMissingLabel();
        resized();
    }
}

} // namespace synth::ui
