// FRO19 (founder review round 3, item 2): DragStateResetTests.cpp's synthetic real-gesture sweep
// (PR #324) found no leak because every gesture it drives ends with a real mouseUp on the SAME
// component that armed the drag. The actual bug (found by code inspection, not by that sweep) is a
// component-lifetime race: an async graph rebuild — an AI patch apply's
// GraphEditor::detachAllModuleComponents(), or the dragged node/macro itself being removed (undo, a
// doc mutation) and pruned by GraphEditor::updateComponents()/syncMacroCards() — can destroy the
// ModuleComponent or MacroCardComponent that armed dragPreviewActive/selectionDragActive in its own
// mouseDown BEFORE its mouseUp ever runs. JUCE delivers no mouseUp to a deleted component, so
// without GraphEditor::cancelLiveDragGestures() (called from those three sites) the flags — and the
// drag-preview ghost they gate — stay armed forever: symptom (b) in the ticket ("the drag/marquee
// rectangle sometimes stays drawn after the module has already been dropped").
//
// Every case below arms the drag through the REAL mouseDown/mouseDrag path (never a direct begin*
// call — see DragStateResetTests.cpp's own header comment on why that matters), then drives the
// exact rebuild call sequence the real bug sites drive, and asserts the flags end clear with nothing
// crashing even though no mouseUp for the destroyed component ever ran. On unmodified code (before
// cancelLiveDragGestures() existed / before its call sites were wired in) DragPreviewLeaks... and
// SelectionDragLeaks... below fail with the ghost/selection-drag flag still true.

#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

ModuleComponent* compFor(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

// Same fixed-mouseDownPosition idiom as DragStateResetTests.cpp's realMouseEvent.
juce::MouseEvent realMouseEvent(juce::Component& eventComp, juce::Point<int> localPos,
                                juce::Point<int> mouseDownLocalPos, juce::ModifierKeys mods, bool wasDragged = false,
                                int numClicks = 1) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(), mods, 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &eventComp, &eventComp, juce::Time::getCurrentTime(),
                            mouseDownLocalPos.toFloat(), juce::Time::getCurrentTime(), numClicks, wasDragged);
}

void expectNoStuckDragState(GraphEditor& editor, const char* context) {
    EXPECT_FALSE(editor.getDragDropController().isDragPreviewActive()) << context << ": drag-preview ghost left stuck";
    EXPECT_FALSE(editor.isSelectionDragActive()) << context << ": selection-drag bookkeeping left stuck";
    // FRO40: cancelLiveDragGestures() must clear the macro drag-candidate highlight too, or an
    // async rebuild mid-Cmd/Ctrl-drag leaves a hull highlighted with no gesture left to end it.
    EXPECT_TRUE(editor.getMacroDragCandidateId().isEmpty()) << context << ": macro drag candidate hull left stuck";
    EXPECT_EQ(editor.getMacroDragDraggedNodeId(), juce::AudioProcessorGraph::NodeID{})
        << context << ": macro drag dragged-node id left stuck";
}

} // namespace

// ---------------------------------------------------------------------------------------------
// detachAllModuleComponents() mid-gesture: the exact MainComponent::aiPatchAboutToApply() call, with
// the still-later async MainComponent::aiPatchApplied() reconcile represented by updateComponents().
// ---------------------------------------------------------------------------------------------

TEST(DragStateAsyncRebuild, DetachAllModuleComponentsCancelsLiveBodyDragMidGesture) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto* comp = compFor(editor, oscId);
    ASSERT_NE(comp, nullptr);

    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    const juce::Point<int> pressPos(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(40, 30);

    // Arm the drag through the real gesture — no mouseUp follows.
    comp->mouseDown(realMouseEvent(*comp, pressPos, pressPos, leftClick));
    comp->mouseDrag(realMouseEvent(*comp, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    ASSERT_TRUE(editor.getDragDropController().isDragPreviewActive())
        << "sanity: a real body drag must arm the ghost preview";

    // Simulate an AI patch apply landing mid-drag: aiPatchAboutToApply() detaches every
    // ModuleComponent (deleting the one holding this live drag, whose mouseUp will now never come),
    // then the later async aiPatchApplied() reconcile rebuilds the component for the surviving node.
    editor.detachAllModuleComponents();
    expectNoStuckDragState(editor, "immediately after detachAllModuleComponents mid-drag");

    editor.updateComponents(); // the async reconcile — must not crash, must not re-arm anything
    expectNoStuckDragState(editor, "after the post-detach updateComponents reconcile");
}

// FRO40: the macro drag-candidate highlight specifically — armed by actually crossing a real
// expanded macro's hull mid-drag, then cancelled by the same detachAllModuleComponents() path the
// plain drag-preview case above exercises, with no mouseUp for the dragged component ever coming.
TEST(DragStateAsyncRebuild, DetachAllModuleComponentsCancelsLiveMacroDragCandidateMidGesture) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    auto* comp = compFor(editor, c);
    ASSERT_NE(comp, nullptr);

    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());
    const auto delta = hull.getCentre() - comp->getBounds().getCentre();

    const juce::ModifierKeys cmdClick(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
    const juce::Point<int> pressPos(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const juce::Point<int> dragPos = pressPos + delta;

    comp->mouseDown(realMouseEvent(*comp, pressPos, pressPos, cmdClick));
    comp->mouseDrag(realMouseEvent(*comp, dragPos, pressPos, cmdClick, /*wasDragged=*/true));
    ASSERT_FALSE(editor.getMacroDragCandidateId().isEmpty())
        << "sanity: dragging C's centre into the hull must arm the candidate before any mouseUp";

    // No mouseUp follows — an AI patch apply (or any other async rebuild) lands mid-drag instead.
    editor.detachAllModuleComponents();
    expectNoStuckDragState(editor, "macro drag candidate, immediately after detachAllModuleComponents");

    editor.updateComponents();
    expectNoStuckDragState(editor, "macro drag candidate, after the post-detach updateComponents reconcile");
}

TEST(DragStateAsyncRebuild, DetachAllModuleComponentsCancelsLiveMultiSelectDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b});
    auto* comp = compFor(editor, a);
    ASSERT_NE(comp, nullptr);

    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    const juce::Point<int> pressPos(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(35, 25);

    comp->mouseDown(realMouseEvent(*comp, pressPos, pressPos, leftClick));
    comp->mouseDrag(realMouseEvent(*comp, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    ASSERT_TRUE(editor.isSelectionDragActive()) << "sanity: a real multi-select body drag must arm group-drag";

    editor.detachAllModuleComponents();
    expectNoStuckDragState(editor, "multi-select drag, immediately after detachAllModuleComponents");

    editor.updateComponents();
    expectNoStuckDragState(editor, "multi-select drag, after the post-detach updateComponents reconcile");
}

TEST(DragStateAsyncRebuild, DetachAllModuleComponentsCancelsLiveMacroCardDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    // Collapsed (the default after grouping): a MacroCardComponent stands in for the whole macro.

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    const juce::Point<int> pressPos(card->getWidth() / 2, card->getHeight() - 20);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(30, 15);
    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);

    card->mouseDown(realMouseEvent(*card, pressPos, pressPos, leftClick));
    card->mouseDrag(realMouseEvent(*card, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    ASSERT_TRUE(editor.isSelectionDragActive()) << "sanity: pressing the card body must arm the group drag";
    ASSERT_TRUE(card->isBodyDragActive());

    // The macro's card is not a ModuleComponent, so detachAllModuleComponents() never touches it
    // directly — cancelLiveDragGestures() being called unconditionally there is what still catches
    // this (the card itself is destroyed later, by syncMacroCards(), once updateComponents() below
    // notices its member modules are also gone from the graph the AI apply just rebuilt).
    editor.detachAllModuleComponents();
    expectNoStuckDragState(editor, "macro card drag, immediately after detachAllModuleComponents");

    editor.updateComponents();
    expectNoStuckDragState(editor, "macro card drag, after the post-detach updateComponents reconcile");
}

// ---------------------------------------------------------------------------------------------
// The dragged node/macro itself vanishing (undo, or any other doc mutation) and being pruned by a
// plain updateComponents() call — no detachAllModuleComponents() in the sequence at all.
// ---------------------------------------------------------------------------------------------

TEST(DragStateAsyncRebuild, DraggedNodeRemovedMidGestureThenUpdateComponentsCancelsLiveDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto* comp = compFor(editor, oscId);
    ASSERT_NE(comp, nullptr);

    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    const juce::Point<int> pressPos(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(40, 30);

    comp->mouseDown(realMouseEvent(*comp, pressPos, pressPos, leftClick));
    comp->mouseDrag(realMouseEvent(*comp, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    ASSERT_TRUE(editor.getDragDropController().isDragPreviewActive())
        << "sanity: a real body drag must arm the ghost preview";

    // Simulate an undo (or any other doc mutation) removing the dragged node itself, out from under
    // the live gesture, then the reconcile pass that always follows a graph mutation.
    engine.getGraph().removeNode(oscId);
    editor.updateComponents(); // must not crash even though comp (mid-drag) is destroyed here
    expectNoStuckDragState(editor, "dragged node removed mid-gesture, after updateComponents");
}

TEST(DragStateAsyncRebuild, MacroDeletedMidCardDragThenUpdateComponentsCancelsLiveDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    const juce::Point<int> pressPos(card->getWidth() / 2, card->getHeight() - 20);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(30, 15);
    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);

    card->mouseDown(realMouseEvent(*card, pressPos, pressPos, leftClick));
    card->mouseDrag(realMouseEvent(*card, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    ASSERT_TRUE(editor.isSelectionDragActive()) << "sanity: pressing the card body must arm the group drag";
    ASSERT_TRUE(card->isBodyDragActive());

    // Every member of the macro vanishes (an undo of the group, or any other doc mutation) — this is
    // what makes MacroSet::retainOnly erase the macro entirely, which is what makes
    // GraphEditor::syncMacroCards() destroy THIS card, with no mouseUp for it ever coming.
    engine.getGraph().removeNode(a);
    engine.getGraph().removeNode(b);
    editor.updateComponents(); // must not crash even though `card` (mid-drag) is destroyed in here
    expectNoStuckDragState(editor, "macro deleted mid card-drag, after updateComponents");
}
