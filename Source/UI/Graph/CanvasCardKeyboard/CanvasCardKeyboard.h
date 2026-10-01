#pragma once

#include "UI/Graph/CardNavigation.h"
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;
class GraphEditor;
class ShortcutManager;

// The canvas's card keys: the arrows select the nearest card in that direction, Alt+arrows move
// the selected card(s) one grid step, Return steps into the selected card. Every key is a
// rebindable Graph action. GraphEditor owns one and hands it every key press first.
class CanvasCardKeyboard {
public:
    CanvasCardKeyboard(GraphEditor& editor, AppUndoManager* undoManager);

    /** Non-owning; null falls back to the default keys. Message thread. */
    void setShortcutManager(const ShortcutManager* manager) noexcept { shortcuts_ = manager; }

    /** True when `key` was one of the card actions and was consumed. Keys arriving from a control
     *  inside a card (or the Mod Matrix) are left alone. */
    bool keyPressed(const juce::KeyPress& key);

    /** Selects the nearest visible card in `direction` and pans it into view; with nothing
     *  selected, the first card in step order. False when the canvas has no card. */
    bool selectInDirection(synth::ui::CardDirection direction);

    /** Moves the selected card(s) by `gridSteps` grid cells as one undo step, through the same
     *  finalize a drag uses. False when nothing is selected. */
    bool moveSelectionBy(juce::Point<int> gridSteps);

    /** Moves keyboard focus to the first control of the selected card. False with no selection
     *  or a card that has no control. */
    bool enterSelectedCard();

private:
    bool matches(const juce::KeyPress& key, const char* actionId, const juce::KeyPress& fallback) const;
    bool focusIsInsideACanvasChild() const;

    GraphEditor& editor_;
    AppUndoManager* undo_ = nullptr;
    const ShortcutManager* shortcuts_ = nullptr;
};
