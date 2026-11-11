// SoundBrowserInput.cpp -- mouse (hover, click, double-click, path parts) and keyboard (selection, folders, favourites,
// jumping to the search field) for the Sounds view, including the keys the search field hands over.
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"

namespace synth::ui {

namespace {
constexpr const char* kDefaultTooltip =
    "Sounds. Up and Down move, Right opens a folder, Left goes back, F adds to Favourites. Return loads it into the "
    "selected Sampler or audio track.";
}

juce::Rectangle<int> SoundBrowserComponent::getRowBounds(int index) const {
    return {0, getListTop() + index * kRowHeight - scrollOffset_, contentWidth(), kRowHeight};
}

juce::Rectangle<int> SoundBrowserComponent::getPathPartBounds(int partIndex) const {
    if (partIndex < 0 || partIndex >= (int)pathParts_.size())
        return {};
    return pathParts_[(size_t)partIndex].bounds;
}

int SoundBrowserComponent::getRowIndexAt(juce::Point<int> p) const {
    if (!listArea().contains(p) || p.x >= contentWidth())
        return -1;
    const int index = (p.y - getListTop() + scrollOffset_) / kRowHeight;
    return index >= 0 && index < (int)rows_.size() ? index : -1;
}

juce::String SoundBrowserComponent::tooltipForRow(int index) const {
    const auto& row = rows_[(size_t)index];
    if (row.kind == RowKind::Sound)
        return row.entry->path +
               "\nDrag it onto a Sampler or an audio track, or press Return to load it into the "
               "selected one. " +
               previewKeyText() + " plays or stops it.";
    if (row.kind == RowKind::Hint)
        return kDefaultTooltip;
    if (row.kind == RowKind::GetPack)
        return packTooltip(row);
    if (row.kind == RowKind::PackHeading)
        return juce::String(isGetMoreShowing() ? "Hide" : "Show") + " the " + row.detail +
               " you can download. Click, or press Return, Left or Right.";
    if (row.kind == RowKind::Shape)
        return shapeTooltip(row);
    if (row.kind == RowKind::AddFolder)
        return "Add a folder of sounds. You can also drag a folder here from " + fileManagerName() + ".";
    if (row.missing)
        return shownName(row) + " was not found. It comes back when its drive is plugged in.";
    if (row.userFolder)
        return "Open " + shownName(row) + ". Right-click, or press Shift+F10, for Rename, Show in " +
               fileManagerName() + ", Scan again and Remove.";
    return "Open " + shownName(row);
}

void SoundBrowserComponent::mouseMove(const juce::MouseEvent& e) {
    int part = -1;
    for (int i = 0; i + 1 < (int)pathParts_.size(); ++i)
        if (pathParts_[(size_t)i].bounds.contains(e.getPosition()))
            part = i;
    int row = getRowIndexAt(e.getPosition());
    if (row >= 0 && !rows_[(size_t)row].navigable())
        row = -1;
    const int hotButton = row >= 0 && packButtonHit(row, e.getPosition()) ? row : -1;
    if (row != hovered_ || part != hoveredPart_ || hotButton != packButtonHot_) {
        hovered_ = row;
        hoveredPart_ = part;
        packButtonHot_ = hotButton;
        if (part >= 0)
            setTooltip("Back to " + pathParts_[(size_t)part].label);
        else if (hotButton >= 0 && rows_[(size_t)row].busy)
            setTooltip(packTooltip(rows_[(size_t)row]));
        else
            setTooltip(row >= 0 ? tooltipForRow(row) : juce::String(kDefaultTooltip));
        repaint();
    }
    const bool leadsSomewhere =
        part >= 0 ||
        (row >= 0 && (rows_[(size_t)row].kind == RowKind::GetPack
                          ? hotButton >= 0
                          : rows_[(size_t)row].kind != RowKind::Sound && rows_[(size_t)row].kind != RowKind::Shape));
    setMouseCursor(leadsSomewhere ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void SoundBrowserComponent::mouseExit(const juce::MouseEvent&) {
    if (hovered_ != -1 || hoveredPart_ != -1) {
        hovered_ = hoveredPart_ = packButtonHot_ = -1;
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
    const auto kind = rows_[(size_t)row].kind;
    if (e.mods.isPopupMenu()) { // a right-click selects; it never opens, downloads or starts a drag
        if (rows_[(size_t)row].userFolder)
            showFolderMenu(row, std::nullopt);
        else if (kind == RowKind::Shape)
            showShapeMenu(row, std::nullopt);
    } else if (kind == RowKind::Sound || kind == RowKind::Shape) {
        pressedSound_ = row;
    } else if (kind == RowKind::GetPack) {
        if (packButtonHit(row, e.getPosition()))
            toggleDownload(row);
    } else if (kind == RowKind::PackHeading) {
        toggleGetMore();
    } else if (kind == RowKind::AddFolder) {
        chooseFolderToAdd();
    } else {
        openSelected();
    }
}

void SoundBrowserComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    const int row = getRowIndexAt(e.getPosition());
    if (row >= 0 && rows_[(size_t)row].kind == RowKind::Sound && onSoundActivated) {
        lists_.noteUsed(rows_[(size_t)row].entry->path);
        onSoundActivated(*rows_[(size_t)row].entry);
    }
}

void SoundBrowserComponent::notifySelected() {
    if (selected_ >= 0 && selected_ < (int)rows_.size() && rows_[(size_t)selected_].kind == RowKind::Shape)
        stopPreview(); // the footer shows no Stop for a shape, so a sound still playing ends here
    if (selected_ < 0 || selected_ >= (int)rows_.size() || rows_[(size_t)selected_].kind != RowKind::Sound)
        return;
    if (onSoundSelected)
        onSoundSelected(*rows_[(size_t)selected_].entry);
    if (footer_.isAutoPlay())
        playSelected();
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
    if (selected_ < 0)
        return;
    const auto key = favouriteKeyOf(rows_[(size_t)selected_]);
    if (key.isEmpty())
        return;
    const int before = selected_;
    lists_.toggleFavourite(key);
    rebuildRows();
    selectNearest(before); // the row left the Favourites list: land on its neighbour
    refreshAccessibilityValue();
    repaint();
}

void SoundBrowserComponent::jumpToSearchWith(juce::juce_wchar character) {
    search_.grabKeyboardFocus();
    search_.setText(search_.getText() + juce::String::charToString(character), juce::sendNotification);
    search_.moveCaretToEnd();
}

bool SoundBrowserComponent::handlePreviewKey(const juce::KeyPress& key) {
    const auto binding =
        shortcuts_ != nullptr ? shortcuts_->getBinding("soundPreviewToggle") : SoundPreviewFooter::kDefaultPreviewKey;
    if (!ShortcutManager::keyPressMatches(binding, key))
        return false;
    togglePreview(); // with nothing to play the key is still ours: it is not the song's
    return true;
}

// Esc steps back out of the panel wherever the keyboard is: a panel control, the list and the search box all end here.
bool SoundBrowserComponent::closeFiltersWithEscape(const juce::KeyPress& key) {
    if (!key.isKeyCode(juce::KeyPress::escapeKey) || key.getModifiers().isAnyModifierKeyDown() || !filtersOpen_)
        return false;
    setFiltersOpen(false);
    return true;
}

bool SoundBrowserComponent::handleSearchKey(const juce::KeyPress& key) {
    if (handlePreviewKey(key))
        return true;
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
            setQuery({}); // the first Esc clears; the next closes the panel and returns to the list
        else {
            closeFiltersWithEscape(key);
            focusListStop(false);
        }
        return true;
    }
    return false;
}

bool SoundBrowserComponent::keyPressed(const juce::KeyPress& key) {
    // A key that bubbled up from a child (the Filters button, the chips, the panel): only Esc closes the panel.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused != nullptr && focused != this && isParentOf(focused)) {
        return handlePreviewKey(key) || closeFiltersWithEscape(key); // the panel's own Esc lands the focus on Filters
    }
    if (handlePreviewKey(key) || handleFooterTab(key, false))
        return true;
    if (key == juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0))
        return goUp();
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    if (key.isKeyCode(juce::KeyPress::upKey)) {
        if (!mods.isAnyModifierKeyDown() && atListEnd(-1))
            return climbToHeader();
        return moveSelection(-1);
    }
    if (key.isKeyCode(juce::KeyPress::downKey)) {
        if (!mods.isAnyModifierKeyDown() && atListEnd(+1) && enterFooter())
            return true;
        return moveSelection(+1);
    }
    if (key.isKeyCode(juce::KeyPress::pageUpKey) || key.isKeyCode(juce::KeyPress::pageDownKey)) {
        const int step = juce::jmax(1, listArea().getHeight() / kRowHeight - 1);
        const int dir = key.isKeyCode(juce::KeyPress::pageUpKey) ? -1 : +1;
        bool moved = false;
        for (int i = 0; i < step; ++i)
            moved = moveSelection(dir) || moved;
        return moved;
    }
    const bool onHeading = selected_ >= 0 && rows_[(size_t)selected_].kind == RowKind::PackHeading;
    if (key.isKeyCode(juce::KeyPress::leftKey)) {
        if (onHeading) {
            setGetMoreOpen(false);
            return true;
        }
        return goUp();
    }
    if (key.isKeyCode(juce::KeyPress::rightKey)) {
        if (onHeading) {
            setGetMoreOpen(true);
            return true;
        }
        return openSelected();
    }
    if (key.isKeyCode(juce::KeyPress::returnKey)) {
        if (selected_ >= 0 && rows_[(size_t)selected_].kind == RowKind::Sound) {
            if (onSoundActivated) {
                lists_.noteUsed(rows_[(size_t)selected_].entry->path);
                onSoundActivated(*rows_[(size_t)selected_].entry);
            }
            return true;
        }
        if (selected_ >= 0 && rows_[(size_t)selected_].kind == RowKind::Shape) {
            activateSelectedShape(); // with no card that fits, nothing happens; the row's tooltip says what it needs
            return true;
        }
        return activateSelectedAction() || openSelected();
    }
    if (key.isKeyCode(juce::KeyPress::escapeKey) && isSearchActive()) {
        setQuery({});
        return true;
    }
    if (closeFiltersWithEscape(key)) {
        focusListStop(false);
        return true;
    }
    const auto ch = key.getTextCharacter();
    if ((ch == 'f' || ch == 'F') && selected_ >= 0 && !favouriteKeyOf(rows_[(size_t)selected_]).isEmpty()) {
        toggleSelectedFavourite();
        return true;
    }
    if (ch > 32 && juce::CharacterFunctions::isPrintable(ch)) {
        jumpToSearchWith(ch);
        return true;
    }
    // Plain Space on a sound is the song's play key (the app takes it); the preview is Shift+Space (handlePreviewKey).
    if (key.isKeyCode(juce::KeyPress::spaceKey) && selected_ >= 0 && rows_[(size_t)selected_].kind != RowKind::Sound)
        return activateSelectedAction();
    if ((key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey)) && selected_ >= 0) {
        if (rows_[(size_t)selected_].kind == RowKind::GetPack && rows_[(size_t)selected_].busy)
            return toggleDownload(selected_);
        return deleteSelectedShape();
    }
    return false;
}

} // namespace synth::ui
