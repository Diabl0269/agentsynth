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

} // namespace synth::ui
