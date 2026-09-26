// Concern: FRO273 (docs/control/midi-remote.md#undo) -- which history Cmd+Z acts on. While keyboard
// focus is inside this panel, MainComponent's Undo/Redo commands act on the controller edit history
// (MidiLearnController) rather than the project's AppUndoManager; this unit owns the focus side of
// that: taking focus on any press inside the panel, answering "is focus here?", and the toolbar cue
// that says what Cmd+Z would undo right now.
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemotePanelComponent.h"

namespace synth::ui {

namespace {

#if JUCE_MAC
constexpr const char* kUndoKeyName = "Cmd+Z";
#else
constexpr const char* kUndoKeyName = "Ctrl+Z";
#endif

} // namespace

// isShowing() as well as focus: a panel hidden by a dock tab switch or a closed detached window
// must never keep Cmd+Z, whatever a stale focus pointer says (the same rule resolveEditSurface()
// applies to the timeline and mixer). The test override exists because a real focus grab needs a
// native peer, which the headless test suite never creates.
bool MidiRemotePanelComponent::holdsUndoFocus() const {
    if (holdsUndoFocusForTest_.has_value())
        return *holdsUndoFocusForTest_;
    return isShowing() && hasKeyboardFocus(true);
}

void MidiRemotePanelComponent::setHoldsUndoFocusForTest(std::optional<bool> focused) {
    holdsUndoFocusForTest_ = focused;
    refreshUndoHint();
}

void MidiRemotePanelComponent::refreshUndoHint() {
    juce::String text;
    if (learnController_ != nullptr && holdsUndoFocus() && learnController_->canUndoProfileEdit())
        text << kUndoKeyName << " undoes: " << learnController_->getUndoProfileEditLabel();
    toolbar_.setUndoHint(text);
}

void MidiRemotePanelComponent::focusGained(FocusChangeType) { refreshUndoHint(); }

void MidiRemotePanelComponent::focusLost(FocusChangeType) { refreshUndoHint(); }

void MidiRemotePanelComponent::focusOfChildComponentChanged(FocusChangeType) { refreshUndoHint(); }

// Registered on the panel for every nested child (addMouseListener(..., true)), and JUCE calls a
// component's own mouseDown BEFORE its parents' listeners -- so by the time this runs, a child
// that wants focus for itself (the surface, which grabs it on cell selection so Delete/arrows
// reach its keyPressed; a text editor in the list or inspector) already has it. Grabbing only when
// focus is NOT already somewhere inside the panel keeps those children's focus intact. JUCE's own
// click-to-focus climb usually lands focus here anyway (the panel wants keyboard focus); this makes
// it hold for a child that opts out of click focus too.
void MidiRemotePanelComponent::FocusOnClick::mouseDown(const juce::MouseEvent&) {
    if (!owner_.hasKeyboardFocus(true))
        owner_.grabKeyboardFocus();
    owner_.refreshUndoHint();
}

} // namespace synth::ui
