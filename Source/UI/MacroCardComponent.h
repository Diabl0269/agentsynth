#pragma once

#include "../AppUndoManager.h"
#include "../MacroSet.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class GraphEditor; // Forward declaration

/** The collapsed on-canvas representation of a synth::Macro (P8-12) — "a macro reads as one
 *  card on the canvas". One instance per collapsed macro, owned by GraphEditor's
 *  GraphContentComponent exactly like a ModuleComponent, and only ever visible while its macro
 *  is collapsed (GraphEditor::syncMacroCards hides it otherwise).
 *
 *  Deliberately NOT a ModuleComponent: a macro has no processor, no ports, no jacks — it is
 *  pure presentation over a GraphEditor::MacroSet entry. Body-drag mirrors ModuleComponent's own
 *  (ComponentDragger + GraphEditor::beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag) so
 *  dragging the card moves every one of its (hidden) members via the exact same group-drag path
 *  a plain multi-select drag uses — see GraphEditor::beginMacroCardDrag. */
class MacroCardComponent : public juce::Component {
public:
    MacroCardComponent(GraphEditor& owner, juce::String macroId);
    ~MacroCardComponent() override;

    const juce::String& getMacroId() const noexcept { return macroId; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    /** Opens the inline rename editor over the card. Public so a test can drive it without
     *  synthesising a right-click + menu selection. */
    void beginRename();
    void finishRename(bool commit);
    bool isRenamingTitle() const noexcept { return nameEditor != nullptr; }

private:
    void showContextMenu();
    juce::PopupMenu buildColourSubMenu();

    GraphEditor& owner;
    juce::String macroId;

    juce::ComponentDragger dragger;
    juce::Point<int> dragStartPosition;
    bool bodyDragActive = false;

    std::unique_ptr<juce::TextEditor> nameEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroCardComponent)
};
