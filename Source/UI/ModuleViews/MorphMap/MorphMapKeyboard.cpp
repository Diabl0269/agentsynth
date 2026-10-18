// MorphMapKeyboard.cpp -- the morph map's keyboard stops and keys, its screen-reader names and tooltips, the dot
// menu and the inline rename.
#include "MorphMap.h"
#include "MorphMapInternal.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kRenameWidth = 96;
constexpr int kRenameHeight = 22;

juce::String coordinateText(float x, float y) { return "X " + juce::String(x, 2) + ", Y " + juce::String(y, 2); }
} // namespace

juce::Component* MorphMap::getDotStop(int index) const {
    return index >= 0 && index < (int)dotStops_.size() ? dotStops_[(size_t)index].get() : nullptr;
}

juce::Component* MorphMap::getCursorStop() const { return cursorStop_.get(); }

juce::Component* MorphMap::getStripStop(Axis axis) const {
    return axis == Axis::X ? stripStops_[0].get() : axis == Axis::Y ? stripStops_[1].get() : nullptr;
}

/** The names carry the values ("Morph cursor, X 0.40, Y 0.62"): the stop is a plain focusable element, so its
    title is what a screen reader speaks when focus lands on it and after each key step. */
void MorphMap::refreshStopNames() {
    for (int i = 0; i < (int)dotStops_.size(); ++i) {
        const auto& dot = dots_[(size_t)i];
        const juce::String name = dot.name.isEmpty() ? "Shape " + juce::String(i + 1) : dot.name;
        dotStops_[(size_t)i]->setTitle(name + ", " + coordinateText(dot.x, dot.y));
        dotStops_[(size_t)i]->setDescription("Shape on the morph map");
        dotStops_[(size_t)i]->setTooltip(name + " - drag to move, double-click to edit");
    }
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
    return "Morph map: click empty space to add a shape that copies the current blend";
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

bool MorphMap::handleStopKey(Stop& stop, const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ||
        code == juce::KeyPress::downKey)
        return pressArrow(stop, code, mods.isShiftDown());
    if (code == juce::KeyPress::escapeKey) {
        grabKeyboardFocus();
        return true;
    }
    if (stop.isCursor() || stop.isStrip())
        return false;

    const int index = stop.getIndex();
    if (code == juce::KeyPress::returnKey && !mods.isShiftDown()) {
        if (onEditDot)
            onEditDot(index);
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
        if (onDeleteDot && (!canDeleteDot || canDeleteDot())) {
            juce::Component::SafePointer<MorphMap> safe(this);
            onDeleteDot(index);
            // The owner refreshed the dots at once, so dot `index` is the one that followed (or the new last).
            if (safe != nullptr && !safe->dotStops_.empty())
                safe->takeFocus(*safe->dotStops_[(size_t)juce::jmin(index, (int)safe->dotStops_.size() - 1)]);
        }
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
    menu.addItem("Delete", canDelete, false, [safe, index] {
        if (safe != nullptr && safe->onDeleteDot)
            safe->onDeleteDot(index);
    });
    if (showMenu)
        showMenu(menu);
    else
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            localAreaToGlobal(juce::Rectangle<int>(16, 16).withCentre(getDotCentre(index).roundToInt()))));
}

bool MorphMap::showContextMenuForKeyboardFocus() {
    for (int i = 0; i < (int)dotStops_.size(); ++i)
        if (dotStops_[(size_t)i]->hasKeyboardFocus(false)) {
            showDotMenu(i);
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

} // namespace synth::ui
