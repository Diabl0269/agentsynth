// SoundBrowserInput.cpp -- mouse (hover, click, double-click, path parts) and keyboard (selection, folders, favourites,
// jumping to the search field) for the Sounds view, including the keys the search field hands over.
#include "UI/Layout/DragCursor.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"

namespace synth::ui {

namespace {
constexpr const char* kDefaultTooltip =
    "Sounds. Up and Down move, Right opens a folder, Left goes back, F adds to Favourites.";
}

juce::Rectangle<int> SoundBrowserComponent::getRowBounds(int index) const {
    return {0, kListTop + index * kRowHeight - scrollOffset_, contentWidth(), kRowHeight};
}

juce::Rectangle<int> SoundBrowserComponent::getPathPartBounds(int partIndex) const {
    if (partIndex < 0 || partIndex >= (int)pathParts_.size())
        return {};
    return pathParts_[(size_t)partIndex].bounds;
}

int SoundBrowserComponent::getRowIndexAt(juce::Point<int> p) const {
    if (!listArea().contains(p) || p.x >= contentWidth())
        return -1;
    const int index = (p.y - kListTop + scrollOffset_) / kRowHeight;
    return index >= 0 && index < (int)rows_.size() ? index : -1;
}

juce::String SoundBrowserComponent::tooltipForRow(int index) const {
    const auto& row = rows_[(size_t)index];
    if (row.kind == RowKind::Sound)
        return row.entry->path;
    if (row.kind == RowKind::Hint)
        return kDefaultTooltip;
    return "Open " + row.text;
}

void SoundBrowserComponent::mouseMove(const juce::MouseEvent& e) {
    int part = -1;
    for (int i = 0; i + 1 < (int)pathParts_.size(); ++i)
        if (pathParts_[(size_t)i].bounds.contains(e.getPosition()))
            part = i;
    int row = getRowIndexAt(e.getPosition());
    if (row >= 0 && !rows_[(size_t)row].navigable())
        row = -1;
    if (row != hovered_ || part != hoveredPart_) {
        hovered_ = row;
        hoveredPart_ = part;
        if (part >= 0)
            setTooltip("Back to " + pathParts_[(size_t)part].label);
        else
            setTooltip(row >= 0 ? tooltipForRow(row) : juce::String(kDefaultTooltip));
        repaint();
    }
    const bool leadsSomewhere = part >= 0 || (row >= 0 && rows_[(size_t)row].kind != RowKind::Sound);
    setMouseCursor(leadsSomewhere ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void SoundBrowserComponent::mouseExit(const juce::MouseEvent&) {
    if (hovered_ != -1 || hoveredPart_ != -1) {
        hovered_ = hoveredPart_ = -1;
        setTooltip(kDefaultTooltip);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SoundBrowserComponent::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i + 1 < (int)pathParts_.size(); ++i) {
        const auto& part = pathParts_[(size_t)i];
        if (part.bounds.contains(e.getPosition())) {
            const auto from = i + 1 < (int)pathParts_.size() ? pathParts_[(size_t)i + 1] : PathPart{};
            navigateTo(part.placeId, part.folder, -1,
                       from.folder.isNotEmpty() ? "f:" + from.folder : "p:" + from.placeId);
            grabKeyboardFocus();
            return;
        }
    }
    const int row = getRowIndexAt(e.getPosition());
    if (row < 0 || !rows_[(size_t)row].navigable())
        return;
    grabKeyboardFocus();
    selectRow(row);
    if (rows_[(size_t)row].kind != RowKind::Sound)
        openSelected();
}

void SoundBrowserComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    const int row = getRowIndexAt(e.getPosition());
    if (row >= 0 && rows_[(size_t)row].kind == RowKind::Sound && onSoundActivated)
        onSoundActivated(*rows_[(size_t)row].entry);
}

void SoundBrowserComponent::notifySelected() {
    if (selected_ >= 0 && selected_ < (int)rows_.size() && rows_[(size_t)selected_].kind == RowKind::Sound &&
        onSoundSelected)
        onSoundSelected(*rows_[(size_t)selected_].entry);
}

void SoundBrowserComponent::selectRow(int index) {
    if (index < 0 || index >= (int)rows_.size() || !rows_[(size_t)index].navigable())
        return;
    selected_ = index;
    scrollSelectionIntoView();
    refreshAccessibilityValue();
    repaint();
    notifySelected();
}

bool SoundBrowserComponent::moveSelection(int delta) {
    const int n = (int)rows_.size();
    int next = selected_;
    for (int i = selected_ + delta; i >= 0 && i < n; i += delta)
        if (rows_[(size_t)i].navigable()) {
            next = i;
            break;
        }
    if (selected_ < 0) {
        next = -1;
        for (int i = delta > 0 ? 0 : n - 1; i >= 0 && i < n; i += delta)
            if (rows_[(size_t)i].navigable()) {
                next = i;
                break;
            }
    }
    if (next < 0)
        return false;
    selectRow(next);
    return true;
}

void SoundBrowserComponent::toggleSelectedFavourite() {
    if (selected_ < 0 || rows_[(size_t)selected_].kind != RowKind::Sound)
        return;
    const int before = selected_;
    lists_.toggleFavourite(rows_[(size_t)selected_].entry->path);
    rebuildRows();
    if (selected_ < 0 && !rows_.empty()) // the row left the Favourites list: land on its neighbour
        for (int i = juce::jmin(before, (int)rows_.size() - 1); i >= 0; --i)
            if (rows_[(size_t)i].navigable()) {
                selected_ = i;
                break;
            }
    refreshAccessibilityValue();
    repaint();
}

void SoundBrowserComponent::jumpToSearchWith(juce::juce_wchar character) {
    search_.grabKeyboardFocus();
    search_.setText(search_.getText() + juce::String::charToString(character), juce::sendNotification);
    search_.moveCaretToEnd();
}

bool SoundBrowserComponent::handleSearchKey(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (key.isKeyCode(juce::KeyPress::downKey) || key.isKeyCode(juce::KeyPress::returnKey)) {
        focusList();
        if (selected_ < 0)
            moveSelection(+1);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        if (search_.getText().isNotEmpty())
            setQuery({});
        else
            focusList();
        return true;
    }
    return false;
}

bool SoundBrowserComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0))
        return goUp();
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    if (key.isKeyCode(juce::KeyPress::upKey))
        return moveSelection(-1);
    if (key.isKeyCode(juce::KeyPress::downKey))
        return moveSelection(+1);
    if (key.isKeyCode(juce::KeyPress::pageUpKey) || key.isKeyCode(juce::KeyPress::pageDownKey)) {
        const int step = juce::jmax(1, listArea().getHeight() / kRowHeight - 1);
        const int dir = key.isKeyCode(juce::KeyPress::pageUpKey) ? -1 : +1;
        bool moved = false;
        for (int i = 0; i < step; ++i)
            moved = moveSelection(dir) || moved;
        return moved;
    }
    if (key.isKeyCode(juce::KeyPress::leftKey))
        return goUp();
    if (key.isKeyCode(juce::KeyPress::rightKey))
        return openSelected();
    if (key.isKeyCode(juce::KeyPress::returnKey)) {
        if (selected_ >= 0 && rows_[(size_t)selected_].kind == RowKind::Sound) {
            if (onSoundActivated)
                onSoundActivated(*rows_[(size_t)selected_].entry);
            return true;
        }
        return openSelected();
    }
    if (key.isKeyCode(juce::KeyPress::escapeKey) && isSearchActive()) {
        setQuery({});
        return true;
    }
    const auto ch = key.getTextCharacter();
    if ((ch == 'f' || ch == 'F') && selected_ >= 0 && rows_[(size_t)selected_].kind == RowKind::Sound) {
        toggleSelectedFavourite();
        return true;
    }
    if (ch > 32 && juce::CharacterFunctions::isPrintable(ch)) {
        jumpToSearchWith(ch);
        return true;
    }
    return false; // Space stays free for the preview player
}

} // namespace synth::ui
