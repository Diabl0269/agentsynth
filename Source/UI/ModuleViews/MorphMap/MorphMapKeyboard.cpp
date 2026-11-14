// MorphMapKeyboard.cpp -- the morph map's keyboard stops and keys, its screen-reader names and tooltips, the dot
// menu and the inline rename.
#include "MorphMap.h"
#include "MorphMapInternal.h"
#include "MorphMapPalette.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/PointEditing/PointEditorKeys.h"
#include "UI/Layout/PopupHost.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kRenameWidth = 96;
constexpr int kRenameHeight = 22;

juce::String coordinateText(float x, float y) { return "X " + juce::String(x, 2) + ", Y " + juce::String(y, 2); }

int percent(float v) { return juce::roundToInt(v * 100.0f); }

/** "Dot 2 of 4, shape Saw, position X 30%, Y 25%": where the dot is in the list and on the map, in words. */
juce::String dotTitle(int index, int count, const juce::String& shape, float x, float y) {
    return "Dot " + juce::String(index + 1) + " of " + juce::String(count) + ", shape " + shape + ", position X " +
           juce::String(percent(x)) + "%, Y " + juce::String(percent(y)) + "%";
}
} // namespace

juce::Component* MorphMap::getDotStop(int index) const {
    return index >= 0 && index < (int)dotStops_.size() ? dotStops_[(size_t)index].get() : nullptr;
}

juce::Component* MorphMap::getCursorStop() const { return cursorStop_.get(); }

juce::Component* MorphMap::getStripStop(Axis axis) const {
    return axis == Axis::X ? stripStops_[0].get() : axis == Axis::Y ? stripStops_[1].get() : nullptr;
}

/** The names carry the values ("Dot 2 of 4, shape Saw, position X 30%, Y 25%", "Morph cursor, X 0.40, Y 0.62"): the
    stop is a plain focusable element, so its title is what a screen reader speaks when focus lands on it and after
    each key step. This runs on every move, insert, removal and rename, so the index and count never go stale. */
void MorphMap::refreshStopNames() {
    for (int i = 0; i < (int)dotStops_.size(); ++i) {
        const auto& dot = dots_[(size_t)i];
        const juce::String name = dot.name.isEmpty() ? "Shape " + juce::String(i + 1) : dot.name;
        dotStops_[(size_t)i]->setComponentID("morphDot" + juce::String(i)); // test mode finds a dot by index
        dotStops_[(size_t)i]->setTitle(dotTitle(i, (int)dots_.size(), name, dot.x, dot.y));
        dotStops_[(size_t)i]->setDescription("Shape on the morph map");
        dotStops_[(size_t)i]->setTooltip(name + " - drag to move, double-click to delete, Return for its menu");
    }
    cursorStop_->setComponentID("morphCursor"); // test mode finds the cursor and the strips by id
    stripStops_[0]->setComponentID("morphStripX");
    stripStops_[1]->setComponentID("morphStripY");
    cursorStop_->setTitle("Morph cursor, " + coordinateText(cursor_.x, cursor_.y));
    cursorStop_->setDescription("Where the shapes are blended");
    cursorStop_->setTooltip("Morph cursor - drag to blend shapes");
    stripStops_[0]->setTitle("Morph X, " + juce::String(cursor_.x, 2));
    stripStops_[0]->setDescription("Slider for the cursor's horizontal position");
    stripStops_[0]->setTooltip("Morph X - drag to move the cursor sideways, or drop a cable to modulate it");
    stripStops_[1]->setTitle("Morph Y, " + juce::String(cursor_.y, 2));
    stripStops_[1]->setDescription("Slider for the cursor's vertical position");
    stripStops_[1]->setTooltip("Morph Y - drag to move the cursor up and down, or drop a cable to modulate it");
}

juce::String MorphMap::tooltipAt(juce::Point<int> point) const {
    const auto p = point.toFloat();
    if (extraTooltip)
        if (const auto text = extraTooltip(p); text.isNotEmpty())
            return text;
    if (cursorAt(p))
        return cursorStop_->getTooltip();
    if (const Axis strip = stripAt(p); strip != Axis::None && cursorVisible_)
        return stripStops_[strip == Axis::X ? 0 : 1]->getTooltip();
    if (const int dot = dotAt(p); dot >= 0)
        return dotStops_[(size_t)dot]->getTooltip();
    return "Morph map: double-click empty space to add a shape that copies the current blend";
}

juce::String MorphMap::getTooltip() { return tooltipAt(getMouseXYRelative()); }

void MorphMap::focusGained(FocusChangeType) { repaint(); }
void MorphMap::focusLost(FocusChangeType) { repaint(); }

/** On the map itself Down or Right steps into the first dot, so a user with arrows can reach the dots without
    counting Tab stops; everything else is left to the stops. */
bool MorphMap::keyPressed(const juce::KeyPress& key) {
    if (hasKeyboardFocus(false) && (key == juce::KeyPress::downKey || key == juce::KeyPress::rightKey) &&
        !dotStops_.empty()) {
        dotStops_.front()->grabKeyboardFocus();
        return true;
    }
    return false;
}

/** One key step is one gesture bracket (one undo step), reported through the same callbacks a drag uses. */
bool MorphMap::stepStop(Stop& stop, float dx, float dy) {
    const bool cursor = stop.isCursor() || stop.isStrip();
    const juce::Point<float> from =
        cursor ? cursor_ : juce::Point<float>{dots_[(size_t)stop.getIndex()].x, dots_[(size_t)stop.getIndex()].y};
    const juce::Point<float> to{juce::jlimit(0.0f, 1.0f, from.x + dx), juce::jlimit(0.0f, 1.0f, from.y + dy)};
    if (to == from)
        return true;
    const Gesture kind = cursor ? Gesture::Cursor : Gesture::Dot;
    if (onGestureStart)
        onGestureStart(kind);
    if (cursor)
        moveCursor(to);
    else
        moveDot(stop.getIndex(), to);
    if (onGestureEnd)
        onGestureEnd(kind);
    return true;
}

/** The owner refreshed the dots at once, so dot `index` is the one that followed (or the new last). */
void MorphMap::deleteDotAndRefocus(int index) {
    if (!onDeleteDot)
        return;
    juce::Component::SafePointer<MorphMap> safe(this);
    onDeleteDot(index);
    if (safe != nullptr && !safe->dotStops_.empty())
        safe->takeFocus(*safe->dotStops_[(size_t)juce::jmin(index, (int)safe->dotStops_.size() - 1)]);
}

/** Ctrl+T: the new dot is the owner's to make (halfway to the next one, or somewhere free after the last), and takes
    focus. A full map has no room and says so through `onNotice`. */
bool MorphMap::insertAfterDot(int index) {
    juce::Component::SafePointer<MorphMap> safe(this);
    const int added = onInsertAfterDot(index);
    if (safe == nullptr)
        return true;
    if (added < 0) {
        if (onNotice)
            onNotice("No room for another shape");
    } else if (added < (int)dotStops_.size()) {
        takeFocus(*dotStops_[(size_t)added]);
    }
    return true;
}

/** Cmd+D: the owner adds the copy and selects it; the focus follows the selection. A full map says so. */
bool MorphMap::duplicateDotAndFocus(int index) {
    juce::Component::SafePointer<MorphMap> safe(this);
    const auto before = dots_.size();
    onDuplicateDot(index);
    if (safe == nullptr)
        return true;
    if (dots_.size() == before) {
        if (onNotice)
            onNotice("No room for another shape");
    } else if (selected_ >= 0 && selected_ < (int)dotStops_.size()) {
        takeFocus(*dotStops_[(size_t)selected_]);
    }
    return true;
}

/** The stops Tab walks: dots by index, the cursor, then the X and Y sliders. One that shows nothing (the cursor and
    sliders while the cursor is hidden) is skipped, as a focus grab would refuse it. */
std::vector<juce::Component*> MorphMap::getWalkStops() const {
    std::vector<juce::Component*> stops;
    const auto add = [&stops](juce::Component* stop) {
        if (stop != nullptr && stop->isVisible() && !stop->getBounds().isEmpty())
            stops.push_back(stop);
    };
    for (const auto& stop : dotStops_)
        add(stop.get());
    add(cursorStop_.get());
    add(stripStops_[0].get());
    add(stripStops_[1].get());
    return stops;
}

/** Tab (`forward`) or Shift+Tab from `stop` to its neighbour. False past either end, so the key goes on to the region
    cycle and focus leaves the map as it always did. */
bool MorphMap::walkFrom(Stop& stop, bool forward) {
    const auto stops = getWalkStops();
    const auto at = std::find(stops.begin(), stops.end(), &stop);
    if (at == stops.end())
        return false;
    const auto next = at - stops.begin() + (forward ? 1 : -1);
    if (next < 0 || next >= (std::ptrdiff_t)stops.size())
        return false;
    lastWalkTarget_ = stops[(size_t)next];
    takeFocus(*lastWalkTarget_);
    return true;
}

bool MorphMap::handleStopKey(Stop& stop, const juce::KeyPress& key) {
    if (const auto forward = focusCycleDirection(key, shortcuts_ ? shortcuts_() : nullptr))
        return walkFrom(stop, *forward);
    if (isInsertPointKey(key) && onInsertAfterDot && !stop.isCursor() && !stop.isStrip())
        return insertAfterDot(stop.getIndex());
    if (pointKeyAction(key).kind == PointKeyAction::Kind::duplicate && onDuplicateDot && !stop.isCursor() &&
        !stop.isStrip())
        return duplicateDotAndFocus(stop.getIndex());
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ||
        code == juce::KeyPress::downKey)
        return pressArrow(stop, code, mods.isShiftDown());
    if (code == juce::KeyPress::escapeKey)
        return false; // bubbles up to the window, which closes
    if (stop.isCursor() || stop.isStrip())
        return false;

    const int index = stop.getIndex();
    if (code == juce::KeyPress::returnKey && !mods.isShiftDown()) {
        showDotMenu(index); // the right-click menu, Edit first: Return, Return still edits
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
        if (!canDeleteDot || canDeleteDot())
            deleteDotAndRefocus(index);
        return true;
    }
    if (code == juce::KeyPress::F2Key) {
        beginRename(index);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Menu and rename
// ---------------------------------------------------------------------------------------------------------------

void MorphMap::showDotMenu(int index) {
    if (index < 0 || index >= (int)dots_.size())
        return;
    juce::PopupMenu menu;
    juce::Component::SafePointer<MorphMap> safe(this);
    const bool canDelete = !canDeleteDot || canDeleteDot();
    if (editItemShown_)
        menu.addItem("Edit", [safe, index] {
            if (safe != nullptr && safe->onEditDot)
                safe->onEditDot(index);
        });
    menu.addItem("Duplicate", [safe, index] {
        if (safe != nullptr && safe->onDuplicateDot)
            safe->onDuplicateDot(index);
    });
    menu.addItem("Rename", [safe, index] {
        if (safe != nullptr)
            safe->beginRename(index);
    });
    if (onCommitDotColour)
        menu.addItem("Colour...", [safe, index] {
            if (safe != nullptr)
                safe->showDotColour(index);
        });
    menu.addItem("Delete", canDelete, false, [safe, index] {
        if (safe != nullptr && safe->onDeleteDot)
            safe->onDeleteDot(index);
    });
    const auto area = dotAnchorArea(index);
    if (showMenu)
        showMenu(menu, area);
    else
        // A menu closed with Escape or a click away leaves the keyboard on the dot it was opened from.
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(area), [safe, index](int result) {
            if (result == 0 && safe != nullptr && index < (int)safe->dotStops_.size() && safe->renameEditor_ == nullptr)
                safe->dotStops_[(size_t)index]->grabKeyboardFocus();
        });
}

/** A screen area centred on a point of this map and reaching `reach` px each way, which a menu or popover opens beside:
 *  the arrow tip lands on its edge, so `reach` must clear what is drawn there. */
juce::Rectangle<int> MorphMap::menuAreaAt(juce::Point<float> localCentre, float reach) const {
    const int side = juce::roundToInt(reach * 2.0f);
    return localAreaToGlobal(juce::Rectangle<int>(side, side).withCentre(localCentre.roundToInt()));
}

/** The dot, its selected ring and 5 px of air. */
juce::Rectangle<int> MorphMap::dotAnchorArea(int index) const {
    return menuAreaAt(getDotCentre(index), kDotRadius + kDotRingGap + 5.0f);
}

/** The area of a slider's menu: its landing dot, so the menu opens beside the slider. */
void MorphMap::showStripMenu(Axis axis) {
    if (onStripMenu)
        onStripMenu(axis, menuAreaAt(stripAnchor(axis), landingDot_ * 0.5f + 4.0f));
}

bool MorphMap::showContextMenuForKeyboardFocus() {
    for (int i = 0; i < (int)dotStops_.size(); ++i)
        if (dotStops_[(size_t)i]->hasKeyboardFocus(false)) {
            showDotMenu(i);
            return true;
        }
    for (const Axis axis : {Axis::X, Axis::Y})
        if (cursorVisible_ && stripStops_[axis == Axis::X ? 0 : 1]->hasKeyboardFocus(false)) {
            showStripMenu(axis);
            return true;
        }
    return false;
}

/** The editor is a child over the dot; Return or leaving it commits, Escape cancels. It is released one turn
    later because it is being called from its own key handler. */
void MorphMap::beginRename(int index) {
    if (index < 0 || index >= (int)dots_.size())
        return;
    if (renameEditor_ != nullptr)
        endRename(true);
    renameIndex_ = index;
    renameEditor_ = std::make_unique<juce::TextEditor>();
    auto& editor = *renameEditor_;
    editor.setTitle("Rename shape");
    editor.setTooltip("Type a new name for the shape, then press Return");
    editor.setText(dots_[(size_t)index].name, false);
    editor.setSelectAllWhenFocused(true);
    editor.setInputRestrictions(24);
    const auto centre = getDotCentre(index).roundToInt();
    auto bounds = juce::Rectangle<int>(kRenameWidth, kRenameHeight).withCentre(centre.translated(0, -18));
    editor.setBounds(bounds.constrainedWithin(getLocalBounds()));
    editor.onReturnKey = [this] { endRename(true); };
    editor.onEscapeKey = [this] { endRename(false); };
    editor.onFocusLost = [this] { endRename(true); };
    addAndMakeVisible(editor);
    editor.grabKeyboardFocus();
}

void MorphMap::endRename(bool commit) {
    if (renameEditor_ == nullptr)
        return;
    auto editor = std::shared_ptr<juce::TextEditor>(std::move(renameEditor_));
    const int index = renameIndex_;
    const juce::String text = editor->getText().trim();
    editor->onFocusLost = nullptr;
    editor->setVisible(false);
    juce::MessageManager::callAsync([editor] {}); // released a turn later, outside its own callback
    if (commit && !text.isEmpty() && index >= 0 && index < (int)dots_.size() && text != dots_[(size_t)index].name &&
        onRenameDot)
        onRenameDot(index, text);
    if (index >= 0 && index < (int)dotStops_.size())
        dotStops_[(size_t)index]->grabKeyboardFocus();
}

// ---------------------------------------------------------------------------------------------------------------
// A dot's own colour: the "Colour..." menu item and the Alt+C key open the shared colour picker beside the dot. It
// previews live (no undo step); closing it asks the owner for ONE step, or none when the pick ended where it began.
// ---------------------------------------------------------------------------------------------------------------

bool MorphMap::showDotColour(int index) {
    if (index < 0 || index >= (int)dots_.size() || !onCommitDotColour)
        return false;
    const auto& colors = synth::theme::themeOf(*this).colors;
    const std::optional<juce::uint32> original = dots_[(size_t)index].colour;
    const juce::Colour shown =
        morphShapeColour(colors, morphColourSlot(dots_[(size_t)index].colourSlot, index), original);
    juce::Component::SafePointer<MorphMap> safe(this);
    auto popup = std::make_unique<ColourPickerPopup>(
        shown, colourFavourites,
        [safe, index](juce::Colour c) {
            if (safe == nullptr || index >= (int)safe->dots_.size())
                return;
            safe->dots_[(size_t)index].colour = c.getARGB();
            safe->repaint();
            if (safe->onPreviewDotColour)
                safe->onPreviewDotColour(index, c);
        },
        [safe, index, original, shown](juce::Colour finalColour) {
            if (safe == nullptr)
                return;
            const bool noNetChange = finalColour.getARGB() == shown.getARGB();
            if (index < (int)safe->dots_.size())
                safe->dots_[(size_t)index].colour = original;
            if (safe->onCommitDotColour)
                safe->onCommitDotColour(
                    index, original, noNetChange ? std::nullopt : std::optional<juce::uint32>(finalColour.getARGB()));
            if (index < (int)safe->dotStops_.size())
                safe->dotStops_[(size_t)index]->grabKeyboardFocus();
        });
    launchPopover(std::move(popup), dotAnchorArea(index), this);
    return true;
}

bool MorphMap::openColourForKeyboardFocus() {
    for (int i = 0; i < (int)dotStops_.size(); ++i)
        if (dotStops_[(size_t)i]->hasKeyboardFocus(false))
            return showDotColour(i);
    return false;
}

} // namespace synth::ui
