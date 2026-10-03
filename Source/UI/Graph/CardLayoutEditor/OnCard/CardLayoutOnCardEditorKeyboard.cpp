// CardLayoutOnCardEditorKeyboard.cpp -- the on-card editor's keys: the rebindable arrow keys nudge the
// focused control (1 px, Shift 8 px) and write once the keys settle, Esc cancels a drag or the session,
// each move is announced. docs/layout/module-card-layout.md#accessibility.
#include "CardLayoutOnCardEditor.h"
#include "ShortcutManager/ShortcutManager.h"
#include <utility>

namespace synth::ui {

namespace {

constexpr int kNudgeSettleMs = 250;

struct NudgeKey {
    const char* actionId;
    int keyCode;
    int dx;
    int dy;
    bool big;
};

// The default of each action is its arrow, with Shift for the big step (ShortcutManagerDefaults.cpp).
const NudgeKey kNudgeKeys[] = {
    {"layoutEditorNudgeLeft", juce::KeyPress::leftKey, -1, 0, false},
    {"layoutEditorNudgeRight", juce::KeyPress::rightKey, 1, 0, false},
    {"layoutEditorNudgeUp", juce::KeyPress::upKey, 0, -1, false},
    {"layoutEditorNudgeDown", juce::KeyPress::downKey, 0, 1, false},
    {"layoutEditorNudgeLeftBig", juce::KeyPress::leftKey, -1, 0, true},
    {"layoutEditorNudgeRightBig", juce::KeyPress::rightKey, 1, 0, true},
    {"layoutEditorNudgeUpBig", juce::KeyPress::upKey, 0, -1, true},
    {"layoutEditorNudgeDownBig", juce::KeyPress::downKey, 0, 1, true},
};

} // namespace

bool CardLayoutOnCardEditor::matchesAction(const juce::KeyPress& press, const char* actionId,
                                           const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return press == fallback;
    return ShortcutManager::keyPressMatches(shortcuts_->getBinding(actionId), press);
}

// `key` is the control whose outline holds focus. A key the editor does not use is left to bubble.
bool CardLayoutOnCardEditor::handleKey(const juce::String& key, const juce::KeyPress& press) {
    if (closed_)
        return false;
    if (keyPressed(press))
        return true;
    if (drag_.pressed)
        return false;
    for (const auto& nudgeKey : kNudgeKeys) {
        const auto mods = nudgeKey.big ? juce::ModifierKeys::shiftModifier : juce::ModifierKeys::noModifiers;
        if (!matchesAction(press, nudgeKey.actionId, juce::KeyPress(nudgeKey.keyCode, mods, 0)))
            continue;
        const int step = nudgeKey.big ? oncard::kNudgeBigStep : oncard::kNudgeStep;
        nudge(key, nudgeKey.dx * step, nudgeKey.dy * step);
        return true;
    }
    return false;
}

// Esc: an open control panel closes first, then a drag in progress goes back, else the session is cancelled. It reaches
// here from the outlines (through handleKey) and from the bar's buttons, which leave the key to bubble.
bool CardLayoutOnCardEditor::keyPressed(const juce::KeyPress& key) {
    if (closed_ || key != juce::KeyPress::escapeKey)
        return false;
    if (panel_ != nullptr)
        closePanel();
    else if (drag_.pressed)
        cancelDrag();
    else
        cancel();
    return true;
}

// The control moves at once, under the key; the write waits until the keys stop for a moment, so a
// held arrow is one write and one glide for whatever it pushed.
void CardLayoutOnCardEditor::nudge(const juce::String& key, int dx, int dy) {
    int cell = indexOfCell(key);
    if (cell < 0)
        return;
    if (nudgeKey_ != key) {
        flushNudge();
        cell = indexOfCell(key);
        if (cell < 0)
            return;
        nudgeKey_ = key;
        nudgeStart_ = cells_[(size_t)cell].rect;
    }
    const auto before = cells_[(size_t)cell].rect;
    const auto after = clampToSection(cell, before.translated(dx, dy));
    if (after == before)
        return;
    moveCellTo(cell, after);
    announce(
        oncard::describeMove(cells_[(size_t)cell].caption, after.getX() - before.getX(), after.getY() - before.getY()));
    startTimer(kNudgeSettleMs);
}

void CardLayoutOnCardEditor::timerCallback() { flushNudge(); }

void CardLayoutOnCardEditor::flushNudge() {
    stopTimer();
    if (nudgeKey_.isEmpty())
        return;
    const auto key = std::exchange(nudgeKey_, juce::String());
    if (const int cell = indexOfCell(key); cell >= 0)
        commitMove(cell, cells_[(size_t)cell].rect, nudgeStart_, false);
}

void CardLayoutOnCardEditor::announce(const juce::String& text) {
    lastAnnouncement_ = text;
    juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}

} // namespace synth::ui
