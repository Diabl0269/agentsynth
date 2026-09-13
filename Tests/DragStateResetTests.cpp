// FRO19 (founder review round 3, item 2): "a module's selection sometimes stays stuck after
// clicking it" / "the drag/marquee rectangle sometimes stays drawn after the module has already
// been dropped". GraphEditor owns a small family of drag-in-progress flags (dragPreviewActive,
// marqueeActive, selectionDragActive, macroChipDragId) that must all clear once a gesture ends —
// each has exactly one or two reset sites (endDragPreview/endMarquee/cancelSelectionDrag/
// finalizeSelectionDrag, reached from ModuleComponent::mouseUp / MacroCardComponent::mouseUp /
// GraphEditor::mouseUp), so a gesture whose real mouseUp never runs the matching cleanup leaves a
// visible ghost or marquee rectangle behind and can leave selection-affecting state stuck.
//
// This file drives every real gesture that arms one of these flags through the ACTUAL
// mouseDown/mouseDrag/mouseUp/mouseDoubleClick callbacks (never a direct begin*/cancel* call —
// see MacroPortRealMouseDragTests.cpp's own comment on why a direct-call test can't catch a
// broken real-event path) and asserts every flag is clear afterwards. Two already-fixed cases
// (macro chip / macro card double-click-to-rename swallowing the drag's mouseUp behind a modal
// AlertWindow) are covered here as regression tests, alongside every other flag-arming gesture,
// including releases that land far outside the pressed component's own bounds — the class of
// event JUCE's mouse capture is supposed to keep routed to the original target regardless of
// where the cursor ends up.

#include "../Source/Modules/OscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/MacroCardComponent.h"
#include "../Source/UI/ModuleComponent.h"
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

// Same fixed-mouseDownPosition idiom as MacroPortRealMouseDragTests.cpp's realMouseEvent: JUCE
// holds e.getMouseDownPosition() fixed at the original press point for the whole gesture while
// e.getPosition() tracks wherever the cursor claims to be right now.
juce::MouseEvent realMouseEvent(juce::Component& eventComp, juce::Point<int> localPos,
                                juce::Point<int> mouseDownLocalPos, juce::ModifierKeys mods, bool wasDragged = false,
                                int numClicks = 1) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(), mods, 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &eventComp, &eventComp, juce::Time::getCurrentTime(),
                            mouseDownLocalPos.toFloat(), juce::Time::getCurrentTime(), numClicks, wasDragged);
}

/** Asserts every GraphEditor-owned drag/marquee flag is at rest — the five flags FRO19's ticket
 *  says "live in GraphEditor and have few reset sites". */
void expectNoStuckDragState(GraphEditor& editor, const char* context) {
    EXPECT_FALSE(editor.isDragPreviewActive()) << context << ": drag-preview ghost left stuck";
    EXPECT_FALSE(editor.isMarqueeActive()) << context << ": marquee rectangle left stuck";
    EXPECT_FALSE(editor.isSelectionDragActive()) << context << ": selection-drag bookkeeping left stuck";
    EXPECT_FALSE(editor.isMacroChipDragActive()) << context << ": macro chip drag id left stuck";
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Baseline sweep: one real gesture each, asserting the matching mouseUp leaves nothing behind.
// ---------------------------------------------------------------------------------------------

TEST(DragStateReset, PlainModuleBodyDragClearsAllStateOnMouseUp) {
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
    ASSERT_TRUE(editor.isDragPreviewActive()) << "sanity: a plain body drag must arm the ghost preview";
    // isSelectionDragActive() only turns true for a MULTI-module selection (beginSelectionDrag:
    // selectionDragActive = selectionDragStartPositions.size() > 1) — a lone module's drag never
    // sets it, by design (see GraphEditor.cpp:3386's own comment on why a one-member "group" never
    // arms it), so this single-module case only exercises the drag-preview ghost.

    comp->mouseDrag(realMouseEvent(*comp, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    comp->mouseUp(realMouseEvent(*comp, dragPos, pressPos, leftClick, /*wasDragged=*/true));

    expectNoStuckDragState(editor, "plain body drag");
}

TEST(DragStateReset, MultiSelectBodyDragClearsAllStateOnMouseUp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b}); // plain multi-select, no macro involved
    auto* comp = compFor(editor, a);
    ASSERT_NE(comp, nullptr);

    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    const juce::Point<int> pressPos(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(35, 25);

    // A plain click on an already-selected module keeps the whole selection intact for a group
    // drag (ModuleComponent::mouseDown) — this is the real "drag one of several selected modules"
    // gesture, distinct from the single-module case above.
    comp->mouseDown(realMouseEvent(*comp, pressPos, pressPos, leftClick));
    ASSERT_TRUE(editor.isDragPreviewActive());
    ASSERT_TRUE(editor.isSelectionDragActive()) << "sanity: a real multi-selection body drag must arm group-drag";

    comp->mouseDrag(realMouseEvent(*comp, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    comp->mouseUp(realMouseEvent(*comp, dragPos, pressPos, leftClick, /*wasDragged=*/true));

    expectNoStuckDragState(editor, "multi-select body drag");
}

TEST(DragStateReset, CtrlInsertDragClearsAllStateOnMouseUp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto* comp = compFor(editor, oscId);
    ASSERT_NE(comp, nullptr);

    const juce::ModifierKeys ctrlClick(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
    const juce::Point<int> pressPos(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(60, 5);

    comp->mouseDown(realMouseEvent(*comp, pressPos, pressPos, ctrlClick));
    ASSERT_TRUE(editor.isDragPreviewActive()) << "sanity: Ctrl+drag arms the insert-preview ghost too";

    comp->mouseDrag(realMouseEvent(*comp, dragPos, pressPos, ctrlClick, /*wasDragged=*/true));
    comp->mouseUp(realMouseEvent(*comp, dragPos, pressPos, ctrlClick, /*wasDragged=*/true));

    expectNoStuckDragState(editor, "ctrl insert-drag");
}

TEST(DragStateReset, MarqueeDragClearsAllStateOnMouseUp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);

    const juce::ModifierKeys shiftClick(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    const juce::Point<int> pressPos(10, 10); // empty canvas, well away from the one module
    const juce::Point<int> dragPos(400, 400);

    editor.mouseDown(realMouseEvent(editor, pressPos, pressPos, shiftClick));
    ASSERT_TRUE(editor.isMarqueeActive()) << "sanity: shift-drag on empty canvas must arm the marquee";

    editor.mouseDrag(realMouseEvent(editor, dragPos, pressPos, shiftClick, /*wasDragged=*/true));
    editor.mouseUp(realMouseEvent(editor, dragPos, pressPos, shiftClick, /*wasDragged=*/true));

    expectNoStuckDragState(editor, "marquee drag");
    EXPECT_TRUE(editor.getMarqueeRect().isEmpty()) << "marquee rectangle must be cleared, not just deactivated";
}

TEST(DragStateReset, MacroChipDragClearsAllStateOnMouseUp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false); // expand: the hull + name chip become live

    const auto chipBounds = editor.macroChipBounds(macroId);
    ASSERT_FALSE(chipBounds.isEmpty());
    const juce::Point<int> pressPos = chipBounds.getCentre();
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(50, 20);

    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    editor.mouseDown(realMouseEvent(editor, pressPos, pressPos, leftClick));
    ASSERT_TRUE(editor.isMacroChipDragActive()) << "sanity: pressing the chip must arm the chip drag";
    ASSERT_TRUE(editor.isSelectionDragActive());

    editor.mouseDrag(realMouseEvent(editor, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    editor.mouseUp(realMouseEvent(editor, dragPos, pressPos, leftClick, /*wasDragged=*/true));

    expectNoStuckDragState(editor, "macro chip drag");
}

TEST(DragStateReset, MacroCardDragClearsAllStateOnMouseUp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    // Collapsed (the default after grouping): a MacroCardComponent stands in for the whole macro.

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    // Body point away from the title row (rename zone) and the expand chevron (top-right).
    const juce::Point<int> pressPos(card->getWidth() / 2, card->getHeight() - 20);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(30, 15);
    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);

    card->mouseDown(realMouseEvent(*card, pressPos, pressPos, leftClick));
    ASSERT_TRUE(editor.isSelectionDragActive()) << "sanity: pressing the card body must arm the group drag";

    card->mouseDrag(realMouseEvent(*card, dragPos, pressPos, leftClick, /*wasDragged=*/true));
    card->mouseUp(realMouseEvent(*card, dragPos, pressPos, leftClick, /*wasDragged=*/true));

    expectNoStuckDragState(editor, "macro card drag");
}

// ---------------------------------------------------------------------------------------------
// Regression coverage for the two ALREADY-FIXED "modal rename swallows the drag's mouseUp" bugs —
// both fixes drop the armed drag unconditionally inside mouseDoubleClick rather than depending on
// a mouseUp that a modal AlertWindow's enterModalState(true, ...) will steal. These pin that fix.
// ---------------------------------------------------------------------------------------------

TEST(DragStateReset, MacroChipDoubleClickRenameCancelsTheArmedChipDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto chipBounds = editor.macroChipBounds(macroId);
    ASSERT_FALSE(chipBounds.isEmpty());
    const juce::Point<int> pressPos = chipBounds.getCentre();
    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);

    // First click of the pair: arms the chip drag exactly like MacroChipDragClearsAllStateOnMouseUp.
    editor.mouseDown(realMouseEvent(editor, pressPos, pressPos, leftClick));
    ASSERT_TRUE(editor.isMacroChipDragActive());

    // The second press's mouseDoubleClick fires without an intervening mouseUp ever having run
    // (a real modal rename dialog's enterModalState(true, ...) grabs input before one can) — this
    // is the exact gap GraphEditor::mouseDoubleClick's chip branch exists to close.
    editor.mouseDoubleClick(realMouseEvent(editor, pressPos, pressPos, leftClick, false, 2));

    expectNoStuckDragState(editor, "macro chip double-click rename");
}

TEST(DragStateReset, MacroCardDoubleClickRenameCancelsTheArmedCardDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 400);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    const auto titleRow = card->getTitleRowBoundsForTest();
    ASSERT_FALSE(titleRow.isEmpty());
    const juce::Point<int> pressPos = titleRow.getCentre();
    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);

    card->mouseDown(realMouseEvent(*card, pressPos, pressPos, leftClick));
    ASSERT_TRUE(editor.isSelectionDragActive()) << "sanity: the first press of the pair arms a card drag";

    card->mouseDoubleClick(realMouseEvent(*card, pressPos, pressPos, leftClick, false, 2));

    expectNoStuckDragState(editor, "macro card double-click rename");
}

// ---------------------------------------------------------------------------------------------
// "Drags that end outside the component" (FRO19's own wording): a release whose reported position
// is far outside the pressed component's local bounds. JUCE's mouse capture keeps mouseDrag/
// mouseUp routed to the ORIGINAL mouseDown target regardless of where the cursor ends up, so nothing
// here should behave differently from an ordinary release — this pins that nothing in the handlers
// implicitly assumes the release lands back inside the component.
// ---------------------------------------------------------------------------------------------

TEST(DragStateReset, ModuleBodyDragReleasedFarOutsideComponentBoundsStillClearsState) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto* comp = compFor(editor, oscId);
    ASSERT_NE(comp, nullptr);

    const juce::ModifierKeys leftClick(juce::ModifierKeys::leftButtonModifier);
    const juce::Point<int> pressPos(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    // Comfortably outside the module card's own local bounds (and outside the whole canvas too).
    const juce::Point<int> farOutside(5000, -3000);

    comp->mouseDown(realMouseEvent(*comp, pressPos, pressPos, leftClick));
    ASSERT_TRUE(editor.isDragPreviewActive());

    comp->mouseDrag(realMouseEvent(*comp, farOutside, pressPos, leftClick, /*wasDragged=*/true));
    comp->mouseUp(realMouseEvent(*comp, farOutside, pressPos, leftClick, /*wasDragged=*/true));

    expectNoStuckDragState(editor, "body drag released far outside component bounds");
}

TEST(DragStateReset, MarqueeReleasedFarOutsideCanvasBoundsStillClearsState) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);

    const juce::ModifierKeys shiftClick(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    const juce::Point<int> pressPos(10, 10);
    const juce::Point<int> farOutside(-9000, 9000);

    editor.mouseDown(realMouseEvent(editor, pressPos, pressPos, shiftClick));
    ASSERT_TRUE(editor.isMarqueeActive());

    editor.mouseDrag(realMouseEvent(editor, farOutside, pressPos, shiftClick, /*wasDragged=*/true));
    editor.mouseUp(realMouseEvent(editor, farOutside, pressPos, shiftClick, /*wasDragged=*/true));

    expectNoStuckDragState(editor, "marquee released far outside canvas bounds");
}
