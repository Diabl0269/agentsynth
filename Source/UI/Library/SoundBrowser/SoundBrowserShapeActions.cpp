// SoundBrowserShapeActions.cpp -- what you can do to a shape row besides dragging it: Return stamps it onto the
// selected card, a saved shape has a right-click menu (Rename, Show in Finder, Delete) and the Delete key, and F
// favourites any shape.
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserPackLayout.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include "UI/ModuleViews/ShapeEditor/ShapeSavePrompts.h"

namespace synth::ui {

namespace {
bool isSavedShape(const SoundBrowserComponent::Row& row) {
    return row.kind == SoundBrowserComponent::RowKind::Shape && row.shape.has_value() && !row.shape->factory;
}

juce::String idOf(const synth::shapes::LibraryShape& shape) {
    return synth::shapes::shapeId(shape.kind, shape.factory, shape.name);
}

void announce(const juce::String& text) {
    juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}
} // namespace

// What F toggles: a sound's file path, a shape's id; empty for a row that cannot be a favourite.
juce::String SoundBrowserComponent::favouriteKeyOf(const Row& row) const {
    if (row.kind == RowKind::Sound)
        return row.entry->path;
    if (row.kind == RowKind::Shape)
        return idOf(*row.shape);
    return {};
}

// After the selected row left the list, the nearest row that can be selected takes the selection.
void SoundBrowserComponent::selectNearest(int before) {
    if (selected_ >= 0 || rows_.empty())
        return;
    for (int i = juce::jmin(before, (int)rows_.size() - 1); i >= 0; --i)
        if (rows_[(size_t)i].navigable()) {
            selected_ = i;
            break;
        }
    scrollSelectionIntoView();
    refreshAccessibilityValue();
    repaint();
}

bool SoundBrowserComponent::activateSelectedShape() {
    if (selected_ < 0 || selected_ >= (int)rows_.size() || rows_[(size_t)selected_].kind != RowKind::Shape ||
        !onShapeActivated)
        return false;
    const auto shape = *rows_[(size_t)selected_].shape;
    if (!onShapeActivated(shape))
        return false;
    if (shapeLibrary_ != nullptr)
        shapeLibrary_->noteUsed(shape);
    return true;
}

// ---- The menu ----

juce::PopupMenu SoundBrowserComponent::buildShapeMenu(int rowIndex) const {
    juce::PopupMenu menu;
    if (rowIndex < 0 || rowIndex >= (int)rows_.size() || !isSavedShape(rows_[(size_t)rowIndex]))
        return menu;
    menu.addItem(1, "Rename");
    menu.addItem(2, "Show in " + fileManagerName());
    menu.addSeparator();
    menu.addItem(3, "Delete");
    return menu;
}

void SoundBrowserComponent::showShapeMenu(int rowIndex, std::optional<juce::Rectangle<int>> anchor) {
    auto menu = buildShapeMenu(rowIndex);
    if (menu.getNumItems() == 0)
        return;
    const auto id = idOf(*rows_[(size_t)rowIndex].shape);
    juce::Component::SafePointer<SoundBrowserComponent> safe(this);
    menu.showMenuAsync(contextMenuOptions(anchor), [safe, id](int result) {
        if (safe == nullptr || result < 1 || result > 3)
            return;
        for (int i = 0; i < (int)safe->rows_.size(); ++i)
            if (isSavedShape(safe->rows_[(size_t)i]) && idOf(*safe->rows_[(size_t)i].shape) == id) {
                safe->applyShapeAction(i, (ShapeAction)(result - 1));
                return;
            }
    });
}

bool SoundBrowserComponent::applyShapeAction(int rowIndex, ShapeAction action) {
    if (shapeLibrary_ == nullptr || rowIndex < 0 || rowIndex >= (int)rows_.size() ||
        !isSavedShape(rows_[(size_t)rowIndex]))
        return false;
    const auto shape = *rows_[(size_t)rowIndex].shape;
    switch (action) {
    case ShapeAction::Rename:
        return beginShapeRename(rowIndex);
    case ShapeAction::ShowInFinder: {
        const auto file = shapeLibrary_->fileOf(shape.kind, shape.name);
        if (!file.existsAsFile())
            return false;
        if (reveal_)
            reveal_(file);
        else
            file.revealToUser();
        return true;
    }
    case ShapeAction::Delete: {
        juce::Component::SafePointer<SoundBrowserComponent> safe(this);
        askShapeDelete(shape.name, [safe, shape](bool yes) {
            if (yes && safe != nullptr)
                safe->deleteShape(shape);
        });
        return true;
    }
    }
    return false;
}

// The file goes through ShapeLibrary::remove, whose change notice fades the list to its new rows; the shape also leaves
// Favourites and Used recently, and the selection lands on the neighbour.
void SoundBrowserComponent::deleteShape(const synth::shapes::LibraryShape& shape) {
    if (shapeLibrary_ == nullptr)
        return;
    int before = selected_;
    for (int i = 0; i < (int)rows_.size(); ++i)
        if (rows_[(size_t)i].kind == RowKind::Shape && idOf(*rows_[(size_t)i].shape) == idOf(shape))
            before = i;
    if (!shapeLibrary_->remove(shape.kind, shape.name))
        return;
    lists_.forget(idOf(shape));
    rebuildRows();
    selectNearest(before);
    announce("Deleted shape " + shape.name);
}

bool SoundBrowserComponent::deleteSelectedShape() {
    if (selected_ < 0 || selected_ >= (int)rows_.size() || !isSavedShape(rows_[(size_t)selected_]))
        return false;
    return applyShapeAction(selected_, ShapeAction::Delete);
}

// ---- Renaming ----

bool SoundBrowserComponent::beginShapeRename(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)rows_.size() || !isSavedShape(rows_[(size_t)rowIndex]))
        return false;
    const auto& row = rows_[(size_t)rowIndex];
    renamingShape_ = std::make_pair(row.shape->kind, row.shape->name);
    renaming_ = juce::File();
    renameLabel_.setTitle("Shape name");
    renameLabel_.setTooltip("Type a new name for this shape. Return saves it, Escape keeps the old name.");
    scrollSelectionIntoView();
    const auto bounds = getRowBounds(rowIndex);
    const int left = kSoundRowSideMargin + 44 + 10; // past the thumbnail, over the name
    renameLabel_.setBounds(bounds.getX() + left - 4, bounds.getY() + 3,
                           juce::jmax(40, bounds.getWidth() - left - kSoundRowSideMargin), bounds.getHeight() - 6);
    renameLabel_.setText(row.text, juce::dontSendNotification);
    renameLabel_.setVisible(true);
    renameLabel_.showEditor();
    return true;
}

// A name that is empty, unchanged or already taken leaves the shape as it was; the last says so.
void SoundBrowserComponent::commitShapeRename() {
    if (shapeLibrary_ == nullptr || !renamingShape_.has_value())
        return;
    const auto [kind, from] = *renamingShape_;
    renamingShape_.reset();
    const auto typed = synth::shapes::ShapeLibrary::sanitiseName(renameLabel_.getText());
    if (typed.isEmpty() || typed == from)
        return;
    pendingSelection_ = "h:" + juce::String(synth::shapes::kindWord(kind)) + ":u:" + typed;
    using Result = synth::shapes::ShapeLibrary::RenameResult;
    const auto result = shapeLibrary_->rename(kind, from, typed);
    if (result == Result::Renamed) {
        lists_.replace(synth::shapes::shapeId(kind, false, from), synth::shapes::shapeId(kind, false, typed));
        rebuildRows();
        announce("Renamed shape to " + typed);
        return;
    }
    pendingSelection_ = {};
    announce(result == Result::Exists ? "A shape called " + typed + " is already saved" : "Could not rename the shape");
}

} // namespace synth::ui
