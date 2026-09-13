// GraphEditor.cpp
//
// GraphEditor's constructor/destructor and GraphContentComponent's constructor -- the class's
// core lifecycle. GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in
// this directory hold the rest of the class (cables, connections, smart connections, canvas,
// selection, macros, channels, commands, drag/drop, stereo wiring, persistence).

#include "GraphEditor.h"
// ~GraphEditor()/~GraphContentComponent() are defined here, so the OwnedArrays of these types
// (declared in GraphEditor.h with only a forward declaration) need their full definitions
// wherever the implicit member destructors are instantiated.
#include "../MacroCardComponent.h"
#include "../ModuleComponent.h"

GraphEditor::GraphEditor(AudioEngine& engine, AppUndoManager* undoMgr)
    : audioEngine(engine)
    , content(*this)
    , modMatrix(engine, undoMgr)
    , undoManager(undoMgr) {
    addAndMakeVisible(content);
    addAndMakeVisible(modMatrix);
    content.setInterceptsMouseClicks(false, true); // Fallback clicks to parent

    // Minimap (issue #159): visibility is driven by setMinimapVisible(), called by the owner once
    // it has restored the persisted preference — NOT addAndMakeVisible, which would show it before
    // that preference is known.
    addChildComponent(minimap);
    minimap.setVisible(minimapVisible);
    minimap.onNavigate = [this](juce::Point<float> p) { centreViewOn(p); };
    minimap.onZoom = [this](float d) { zoomAroundCentre(d); };

    // Tooltips on GraphEditor-owned affordances.
    // The canvas itself hints at pan/zoom. Double-click on an attenuverter knob removes it.
    setTooltip(synth::ui::formatShortcutHint("Patch canvas - drag modules here to build your patch",
                                             "Scroll to zoom | Drag to pan | Shift+drag to select | Double-click mod "
                                             "knob to remove"));

    // Needed for the canvas-scoped Delete/Escape keys (see keyPressed).
    setWantsKeyboardFocus(true);

    startTimerHz(30);
}

GraphEditor::~GraphEditor() { stopTimer(); }

GraphEditor::GraphContentComponent::GraphContentComponent(GraphEditor& ed)
    : editor(ed) {
    // The canvas fills its whole bounds opaquely (bg1 + grid), so tell JUCE not to repaint
    // whatever is behind it on every frame — a real win for zoom/pan and the 30Hz wire animation.
    setOpaque(true);
}
