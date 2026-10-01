#include "UI/Layout/NonModalLabel.h"

namespace synth::ui {

// juce::Label::showEditor() calls enterModalState() right after this hook returns, so the exit has to be
// deferred one message-loop turn. While a Label is modal, macOS reports the window as blocked
// (isAccessibilityModal) and the accessibility tree collapses to the title bar. Nothing the modal state
// gave us is lost: the editor is a child that holds keyboard focus, and losing that focus commits (or
// discards, per setEditable's lossOfFocusDiscardsChanges) through Label::textEditorFocusLost, exactly
// as a click outside did via inputAttemptWhenModal.
void NonModalLabel::editorShown(juce::TextEditor*) {
    juce::Component::SafePointer<NonModalLabel> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis != nullptr && safeThis->isCurrentlyModal(false))
            safeThis->exitModalState(0);
    });
}

// juce::Label::textEditorEscapeKeyPressed restores the label's text into the editor and calls hideEditor(true),
// which deletes the editor. JUCE dispatches Escape from TextEditor::handleCommandMessage while holding that
// editor's listener-list lock, so deleting it there makes the unlock write into freed memory. Restoring the
// text stays synchronous, which also makes a focus-loss commit arriving before the deferred hide a no-op
// (the text is already the original); only the deletion is deferred, until the dispatch has returned.
void NonModalLabel::textEditorEscapeKeyPressed(juce::TextEditor& editor) {
    if (getCurrentTextEditor() != &editor)
        return;
    editor.setText(getTextValue().toString(), false);
    juce::Component::SafePointer<NonModalLabel> safeThis(this);
    juce::Component::SafePointer<juce::TextEditor> safeEditor(&editor);
    juce::MessageManager::callAsync([safeThis, safeEditor] {
        if (safeThis != nullptr && safeEditor != nullptr &&
            safeThis->getCurrentTextEditor() == safeEditor.getComponent())
            safeThis->hideEditor(true);
    });
}

} // namespace synth::ui
