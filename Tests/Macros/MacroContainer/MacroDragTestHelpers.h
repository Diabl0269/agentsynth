#pragma once

// Real-gesture helpers shared by the macro drag tests (MacroDragMembershipTests.cpp,
// MacroDragTransferTests.cpp): synthesised ModuleComponent mouseDown/mouseDrag/mouseUp events and
// the expanded-macro fixtures they drag across. Header-only; not compiled on its own.

#include "MacroContainerTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include <functional>

namespace {

// Same fixed-mouseDownPosition idiom as MacroPortRealMouseDragTests.cpp / DragStateResetTests.cpp:
// JUCE holds e.getMouseDownPosition() fixed at the original press point for the whole gesture
// while e.getPosition() tracks wherever the cursor claims to be right now.
juce::MouseEvent realMouseEvent(juce::Component& eventComp, juce::Point<int> localPos,
                                juce::Point<int> mouseDownLocalPos, juce::ModifierKeys mods, bool wasDragged = false) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(), mods, 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &eventComp, &eventComp, juce::Time::getCurrentTime(),
                            mouseDownLocalPos.toFloat(), juce::Time::getCurrentTime(), 1, wasDragged);
}

const juce::ModifierKeys kCmdClick(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);

const juce::ModifierKeys kPlainClick(juce::ModifierKeys::leftButtonModifier);

/** Drives a full real body-drag gesture on `comp`, from its current position to `delta` away,
 *  under `mods`. `afterDragBeforeUp`, when given, runs after mouseDrag but before mouseUp — the
 *  ONE place a test can observe `GraphEditor::hasMacroDragCandidate()` for real, so a test whose
 *  whole point is "this crosses a hull" can assert the candidate was actually armed instead of
 *  trusting the post-mouseUp membership check alone to have exercised the right branch. */
void dragBodyBy(ModuleComponent& comp, juce::Point<int> delta, juce::ModifierKeys mods,
                std::function<void()> afterDragBeforeUp = nullptr) {
    const juce::Point<int> pressPos(comp.getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const juce::Point<int> dragPos = pressPos + delta;
    comp.mouseDown(realMouseEvent(comp, pressPos, pressPos, mods));
    comp.mouseDrag(realMouseEvent(comp, dragPos, pressPos, mods, /*wasDragged=*/true));
    if (afterDragBeforeUp)
        afterDragBeforeUp();
    comp.mouseUp(realMouseEvent(comp, dragPos, pressPos, mods, /*wasDragged=*/true));
}

/** Sets up a 2-member macro (Oscillator + Filter, both unconnected), expanded so its hull is
 *  live, and returns the macro id. B's y is chosen so its own padded hull comfortably reaches
 *  A's card centre (the relationship PaintedHullOfOwnMacroExcludesDraggedMemberFromTheFirstTick
 *  depends on) -- originally y=400 against a 533px-tall A; FRO312 shrank Oscillator's real height
 *  to 433 (its Level/Pan/etc. CV jacks are knob-bound now), moving A's centre up by the same 100px,
 *  so B moves up by that same 100px to preserve the original relative geometry exactly. */
juce::String makeExpandedTwoMemberMacro(GraphEditor& editor, AudioEngine& engine, NodeID& outA, NodeID& outB) {
    outA = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    outB = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 300);
    editor.setSelectedNodes({outA, outB});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    if (!macroId.isEmpty())
        editor.getMacroController().setMacroCollapsed(macroId, false);
    return macroId;
}

} // namespace
