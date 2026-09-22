// GraphEditorMacroApi.cpp
//
// FRO254: this file used to hold GraphEditor's one-line macro forwarders onto macroController_
// (FRO77 PR2 / FRO91 lever E). Every call site now reaches MacroGroupController directly, either
// through GraphEditor::getMacroController() (external callers) or the macroController_ member
// itself (GraphEditor's own other .cpp files) — except changeMacroPortColour below, kept here as
// the one deliberate exception: its body is not a pure pass-through (it also forces a repaint of
// both port-colour paint surfaces and disarms any live preview), so callers must keep going
// through GraphEditor rather than MacroGroupController::changeMacroPortColour directly. See
// GraphEditor.h's doc comment on the declaration for the full rationale.

#include "GraphEditor.h"

// T152: sets (or, with nullopt, clears back to the kind-tint default) the user colour for the
// port fronted by `nodeUuid`.
void GraphEditor::changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                                        std::optional<juce::Colour> newColour) {
    macroController_.changeMacroPortColour(macroId, nodeUuid, newColour);

    // A commit is exactly when any armed live preview must be disarmed, so BOTH: repaint the just-stored
    // colour on the two surfaces through the shared finder (forced, so a NON-previewed commit - an AI or
    // programmatic colour - still shows it immediately: the repaint guarantee a preview alone never had),
    // then clear the armed preview (a real no-op when nothing was previewed, so a previewed-then-committed
    // port paints only that one new colour). Doing it here, the public commit boundary, means EVERY path
    // that commits a colour does both - not just the Configure I/O modal - so the jack shows the just-
    // stored value (which equals the armed preview) and never glitches on commit.
    repaintMacroPortColourTargets(macroId, nodeUuid);
    clearMacroPortColourPreview(macroId, nodeUuid);
}
