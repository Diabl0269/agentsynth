// MacroContainerInteractionTests.cpp
// Chrome/interaction tests for Macros — the collapse button affordance, colour-picker recolour
// (preview + one-undo-step commit), title-row double-click-to-rename vs. expand, the member
// right-click context menu (including submenu-driven ungroup), and the card right-click
// add/remove-membership menu. Shared helpers live in MacroContainerTestHelpers.h.
//
// Lifecycle (wrap/unwrap, persistence, snippets, delete, membership) lives in
// MacroContainerTests.cpp; geometry/canvas behaviour (collapse, hull/chip, chip drag, undo,
// cables, trust) lives in MacroContainerGeometryTests.cpp.

#include "MacroContainerTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Macros/MacroCardComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// Collapse button (founder-review fix G5): "add a button to collapse it - currently there's only
// a button to expand" — a visible affordance on the EXPANDED hull, mirroring
// MacroCardComponent::getExpandButtonBounds on the collapsed card, at the opposite end of the same
// top-of-hull row the name chip occupies.
// ============================================================================

TEST(MacroCollapseButton, BoundsIsEmptyWhileCollapsedAndSitsInsideTheHullClearOfTheChipWhileExpanded) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // collapses by default
    ASSERT_FALSE(macroId.isEmpty());

    EXPECT_TRUE(editor.macroCollapseButtonBounds(macroId).isEmpty())
        << "a collapsed macro already has its own expand chevron on the card - no button on the hull";
    EXPECT_TRUE(editor.macroCollapseButtonBounds("no-such-macro-id").isEmpty());

    editor.setMacroCollapsed(macroId, false);
    const auto hull = editor.macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());

    const auto button = editor.macroCollapseButtonBounds(macroId);
    ASSERT_FALSE(button.isEmpty());
    EXPECT_TRUE(hull.contains(button)) << "the button must sit entirely inside the hull";

    const auto chip = editor.macroChipBounds(macroId);
    ASSERT_FALSE(chip.isEmpty());
    EXPECT_FALSE(chip.intersects(button))
        << "chip " << chip.toString() << " overlaps the collapse button " << button.toString();
}

TEST(MacroCollapseButton, DoesNotOverlapTheChipOrAMemberEvenForATwoModuleMacroPackedTight) {
    // The degenerate case the founder-review checklist calls out: a small macro made of exactly
    // two adjacent modules, so the hull is as narrow as a real macro's ever gets. The chip and
    // button still must not overlap each other, and the button must not land on a member.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 700); // stacked, same X

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto button = editor.macroCollapseButtonBounds(macroId);
    ASSERT_FALSE(button.isEmpty());
    const auto chip = editor.macroChipBounds(macroId);
    ASSERT_FALSE(chip.isEmpty());
    EXPECT_FALSE(chip.intersects(button));

    for (auto id : {a, b}) {
        auto* comp = findComponent(editor, id);
        ASSERT_NE(comp, nullptr);
        EXPECT_FALSE(button.intersects(comp->getBounds()))
            << "button " << button.toString() << " overlaps member " << comp->getBounds().toString();
    }
}

TEST(MacroCollapseButton, AtHitsInsideAndMissesJustOutsideAndWhileCollapsed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    EXPECT_TRUE(editor.macroCollapseButtonAt({150, 150}).isEmpty());

    editor.setMacroCollapsed(macroId, false);
    const auto button = editor.macroCollapseButtonBounds(macroId);
    ASSERT_FALSE(button.isEmpty());

    EXPECT_EQ(editor.macroCollapseButtonAt(button.getCentre()), macroId);
    EXPECT_TRUE(
        editor.macroCollapseButtonAt(juce::Point<int>(button.getRight() + 5, button.getBottom() + 5)).isEmpty());
}

TEST(MacroCollapseButton, ClickingItCollapsesTheMacroThroughTheRealMousePath) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);
    ASSERT_FALSE(editor.getMacros().find(macroId)->collapsed);

    editor.clearSelection();
    const auto buttonCentre = editor.macroCollapseButtonBounds(macroId).getCentre();

    editor.mouseDown(makeCanvasMouseEvent(editor, buttonCentre));

    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(editor.getMacros().find(macroId)->collapsed) << "clicking the button must collapse the macro";
    EXPECT_FALSE(editor.isSelectionDragActive()) << "the button click must not also arm a chip/selection drag";
    EXPECT_TRUE(editor.getSelectedNodes().empty()) << "the button click must not change the selection as a side effect";

    // The gesture must resolve cleanly on mouseUp too (nothing left armed from the mouseDown that
    // returned early) — send it and confirm nothing further changes.
    editor.mouseUp(makeCanvasMouseEvent(editor, buttonCentre));
    EXPECT_TRUE(editor.getMacros().find(macroId)->collapsed);
}

TEST(MacroCollapseButton, ClickJustOutsideItDoesNotCollapse) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto button = editor.macroCollapseButtonBounds(macroId);
    ASSERT_FALSE(button.isEmpty());
    // Just left of the button, same row (button's own vertical centre) - stays inside the hull
    // (the button sits comfortably clear of the hull's edges), so a miss here proves the hit-test
    // is scoped to the button itself rather than the whole hull.
    const juce::Point<int> justOutside(button.getX() - 5, button.getCentreY());
    ASSERT_TRUE(editor.macroHullBounds(macroId).contains(justOutside))
        << "the probe point must still land inside the hull, so a miss proves the hit-test is "
           "scoped to the button rather than the whole hull";

    editor.mouseDown(makeCanvasMouseEvent(editor, justOutside));
    editor.mouseUp(makeCanvasMouseEvent(editor, justOutside));

    EXPECT_FALSE(editor.getMacros().find(macroId)->collapsed) << "a click just outside the button must not collapse";
}

TEST(MacroCollapseButton, CollapseViaTheButtonIsOneUndoStepAndUndoReExpands) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);

    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto buttonCentre = editor.macroCollapseButtonBounds(macroId).getCentre();
    const int serialBeforeCollapse = undo.getEditSerial();

    editor.mouseDown(makeCanvasMouseEvent(editor, buttonCentre));

    ASSERT_TRUE(editor.getMacros().find(macroId)->collapsed);
    EXPECT_EQ(undo.getEditSerial(), serialBeforeCollapse + 1)
        << "collapsing via the button must be exactly ONE undo step";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_FALSE(editor.getMacros().find(macroId)->collapsed) << "the single undo step must re-expand the macro";
}

// ============================================================================
// Recolour (P8-14): "Change Colour..." opens the shared synth::ui::ColourPickerPopup, with a
// live preview (no undo) and exactly one undo step spanning original -> final colour on commit.
// ============================================================================
//
// promptRecolourMacro launches a real juce::CallOutBox, which never runs in the headless test
// process. createMacroColourPickerForTest() is the seam that hands back the SAME popup, wired
// with the SAME onPreview/onCommit callbacks buildMacroColourPicker builds for the real path
// (mirrors TimelineRulerComponent::createMarkerColourPickerForTest()) -- the tests below drive
// its preview/commit through ColourPickerPopup's own test seams (setCurrentColourForTest /
// commitForTest) rather than duplicating the recolour logic here.

TEST(MacroRecolour, PreviewThenCommitToADifferentColourIsOneUndoStepThatRestoresTheOriginal) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto originalColour = editor.getMacros().find(macroId)->colour;
    const juce::Colour intermediateColour(0xffaa11bbu);
    const juce::Colour finalColour(0xff33cc44u);
    ASSERT_NE(intermediateColour, originalColour);
    ASSERT_NE(finalColour, originalColour);

    auto popup = editor.createMacroColourPickerForTest(macroId);
    ASSERT_NE(popup, nullptr);

    const int serialBeforeRecolour = undo.getEditSerial();

    popup->setCurrentColourForTest(intermediateColour); // simulates the user mid-drag
    EXPECT_EQ(editor.getMacros().find(macroId)->colour, intermediateColour)
        << "a preview must write straight to the macro";
    EXPECT_EQ(undo.getEditSerial(), serialBeforeRecolour) << "a preview must not push an undo step";

    popup->setCurrentColourForTest(finalColour); // the drag settles here
    popup->commitForTest();

    EXPECT_EQ(editor.getMacros().find(macroId)->colour, finalColour);
    EXPECT_EQ(undo.getEditSerial(), serialBeforeRecolour + 1)
        << "commit to a different colour must be exactly ONE undo step";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_EQ(editor.getMacros().find(macroId)->colour, originalColour)
        << "the single undo step must restore the ORIGINAL colour, not the intermediate preview value";
}

TEST(MacroRecolour, PreviewThenCommitBackToTheOriginalColourPushesNoUndoEntry) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto originalColour = editor.getMacros().find(macroId)->colour;
    const juce::Colour driftedColour(0xffaa11bbu);
    ASSERT_NE(driftedColour, originalColour);

    auto popup = editor.createMacroColourPickerForTest(macroId);
    ASSERT_NE(popup, nullptr);

    const int serialBeforeRecolour = undo.getEditSerial();

    popup->setCurrentColourForTest(driftedColour);  // a preview nudges the colour away
    popup->setCurrentColourForTest(originalColour); // ... and the drag settles back on the original
    popup->commitForTest();

    EXPECT_EQ(editor.getMacros().find(macroId)->colour, originalColour);
    EXPECT_EQ(undo.getEditSerial(), serialBeforeRecolour) << "a no-net-change commit must push NO undo entry";
}

// ============================================================================
// Collapsed-card double-click (P8-14): title row renames in place (like ModuleComponent's own
// title), anywhere else on the card still expands.
// ============================================================================

namespace {
// Matches MacroCardComponent::getTitleRowBounds() exactly (280x90 card: getLocalBounds().
// reduced(10, 6), top 20 px, minus the right-hand 28 px chevron reservation) -- a point safely
// inside x:[10,242) y:[6,26). Kept here rather than exposing the private helper to tests: paint()
// and mouseDoubleClick() sharing the ONE method is what this whole change is guarding.
constexpr juce::Point<int> kTitleRowPoint(40, 16);
constexpr juce::Point<int> kBodyPoint(140, 60); // below the title row, still inside the card
} // namespace

TEST(MacroCardDoubleClick, TitleRowStartsInlineRenameAndDoesNotExpand) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro(); // collapses by default
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    ASSERT_FALSE(card->isRenamingTitle());

    card->mouseDoubleClick(makeCanvasMouseEvent(*card, kTitleRowPoint, 2));

    EXPECT_TRUE(card->isRenamingTitle());
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(editor.getMacros().find(macroId)->collapsed) << "a title-row double-click must not expand the macro";

    card->finishRename(false); // tidy up the open editor before the test ends
}

TEST(MacroCardDoubleClick, OutsideTitleRowExpandsAndDoesNotRename) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    card->mouseDoubleClick(makeCanvasMouseEvent(*card, kBodyPoint, 2));

    EXPECT_FALSE(card->isRenamingTitle());
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_FALSE(editor.getMacros().find(macroId)->collapsed)
        << "a double-click outside the title row must still expand, as before";
}

TEST(MacroCardDoubleClick, TitleRowRenameCancelsAnyArmedCardDragSoMouseUpIsANoOp) {
    // Regression guard: mouseDown already arms a card drag (dragStartPosition/bodyDragActive/
    // dragger.startDraggingComponent/owner.beginMacroCardDrag) before a double-click's second
    // press resolves. If opening the rename editor didn't cancel that drag, this mouseUp would
    // resolve it as a real (zero-delta) drag -- or worse, leave it armed for whatever gesture
    // comes next.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    auto* compA = findComponent(editor, a);
    auto* compB = findComponent(editor, b);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);

    const auto cardStart = card->getPosition();
    const auto startA = compA->getPosition();
    const auto startB = compB->getPosition();
    const int serialBeforeGesture = undo.getEditSerial();

    card->mouseDown(makeCanvasMouseEvent(*card, kTitleRowPoint)); // arms a card drag
    card->mouseDoubleClick(makeCanvasMouseEvent(*card, kTitleRowPoint, 2));
    ASSERT_TRUE(card->isRenamingTitle());
    card->mouseUp(makeCanvasMouseEvent(*card, kTitleRowPoint));

    EXPECT_EQ(card->getPosition(), cardStart) << "no drag should have resolved after the rename opened";
    EXPECT_EQ(compA->getPosition(), startA);
    EXPECT_EQ(compB->getPosition(), startB);
    EXPECT_EQ(undo.getEditSerial(), serialBeforeGesture)
        << "a stuck drag resolving on mouseUp would push a finalize-drag undo step";

    card->finishRename(false); // tidy up the open editor before the test ends
}

// ============================================================================
// Right-click on a macro MEMBER module also offers the macro (founder-review item 4)
// ============================================================================
//
// GraphEditor::mouseDown already offers the macro's own menu when a right-click lands on EMPTY
// canvas inside an expanded macro's hull (macroHullAt() -> buildMacroMenu(), with
// selectMacro(hullMacroId, false) called first because buildMacroMenu()'s "Ungroup"/"Save as
// Snippet..." act on the CURRENT SELECTION, not the macro id passed in). This section covers the
// companion path: right-clicking a MEMBER MODULE itself
// (ModuleComponent::buildModuleContextMenu()), which appends the same buildMacroMenu() items as a
// "Macro: <name>" submenu when -- and only when -- the clicked module resolves to a macro via
// MacroSet::findByMember(). Because a member module's own right-click menu must NOT disturb the
// module selection its OWN items (Copy/Duplicate/Delete Module...) act on, buildMacroMenu() itself
// was changed to select its macro immediately before running "Ungroup"/"Save as Snippet...",
// rather than relying on a pre-select from the call site the way the hull path does.

namespace {

juce::MouseEvent makeModuleRightClick(ModuleComponent& comp, juce::Point<int> position) {
    const auto pos = position.toFloat();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

/** A point on the card body that is neither a jack nor the header's double-click-to-rename band --
 *  the ordinary "right-click the body" target every test below uses. */
juce::Point<int> bodyClickPoint(const ModuleComponent& comp) { return {comp.getWidth() / 2, comp.getHeight() - 10}; }

/** Finds an item anywhere in `menu`, including inside submenus, by its exact text -- the item's
 *  `.action` is what a real click on it would invoke. Returns nullptr if not found. */
const juce::PopupMenu::Item* findMenuItemByText(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next()) {
        if (it.getItem().text == text)
            return &it.getItem();
    }
    return nullptr;
}

bool anyMenuItemStartsWith(const juce::PopupMenu& menu, const juce::String& prefix) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text.startsWith(prefix))
            return true;
    return false;
}

} // namespace

TEST(MacroMemberContextMenu, ModuleInNoMacroMenuIsUnchanged) {
    // A real right-click mouseDown() would otherwise call PopupMenu::showMenuAsync(), which opens
    // a real popup and segfaults on a headless (no-display) Linux CI runner --
    // setShowContextMenuHookForTest() intercepts the menu mouseDown() actually built instead, so
    // this drives the real gesture without ever opening one.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto* comp = findComponent(editor, a);
    ASSERT_NE(comp, nullptr);
    ASSERT_EQ(editor.macroForNode(a), nullptr) << "precondition: this module is in no macro";

    juce::PopupMenu capturedMenu;
    comp->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });

    comp->mouseDown(makeModuleRightClick(*comp, bodyClickPoint(*comp)));
    EXPECT_TRUE(editor.isNodeSelected(a)) << "the ordinary right-click retarget must still run";

    EXPECT_FALSE(anyMenuItemStartsWith(capturedMenu, "Macro: "))
        << "a module in no macro must see no macro submenu at all -- no change from before this fix";
    EXPECT_NE(findMenuItemByText(capturedMenu, "Delete Module"), nullptr) << "the module's own items are still there";
}

TEST(MacroMemberContextMenu, RightClickingAMacroMemberOffersTheMacroSubmenu) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false); // expand: members are what get right-clicked

    // Select something OTHER than `a` first, so the retarget below is actually observable.
    editor.setSelectedNodes({b});
    ASSERT_FALSE(editor.isNodeSelected(a));

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);

    juce::PopupMenu capturedMenu;
    compA->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });

    compA->mouseDown(makeModuleRightClick(*compA, bodyClickPoint(*compA)));

    // Went through the real gesture entry point: mouseDown's own hit-test/retarget logic moved
    // the selection onto the clicked module, collapsing it off `b`.
    EXPECT_TRUE(editor.isNodeSelected(a));
    EXPECT_FALSE(editor.isNodeSelected(b));

    const auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);

    const auto* macroSubmenu = findMenuItemByText(capturedMenu, "Macro: " + macro->name);
    ASSERT_NE(macroSubmenu, nullptr) << "a member module's own menu must offer its macro's options";
    ASSERT_NE(macroSubmenu->subMenu, nullptr);

    // Spot-check a few of buildMacroMenu()'s own items made it into the submenu, so this can't
    // silently pass against an empty or unrelated submenu.
    EXPECT_NE(findMenuItemByText(capturedMenu, "Ungroup"), nullptr);
    EXPECT_NE(findMenuItemByText(capturedMenu, "Configure I/O..."), nullptr);
    EXPECT_NE(findMenuItemByText(capturedMenu, "Delete Macro && Modules"), nullptr);
}

TEST(MacroMemberContextMenu, UngroupFromTheSubmenuDissolvesTheRightMacroDespiteAMixedSelection) {
    // The load-bearing trap: buildMacroMenu()'s "Ungroup" acts on the CURRENT SELECTION
    // (ungroupSelection()), not on the macro id it was built for. A member module's right-click
    // does NOT retarget selection when the clicked module is already part of a multi-selection
    // (mouseDown's "if (!owner.isNodeSelected(nodeId))" guard skips), so a selection spanning two
    // macros can genuinely reach the submenu's "Ungroup" unchanged. This must still dissolve only
    // the ONE macro whose submenu was opened.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 300, 100);
    editor.setSelectedNodes({a1, a2});
    auto macroA = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroA.isEmpty());
    editor.setMacroCollapsed(macroA, false);

    auto b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 700, 100);
    auto b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    editor.setSelectedNodes({b1, b2});
    auto macroB = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroB.isEmpty());
    editor.setMacroCollapsed(macroB, false);

    // A mixed selection spanning BOTH macros, with b1 (about to be right-clicked) already part of
    // it -- mouseDown's retarget guard therefore does NOT fire, and the selection at click time
    // genuinely stays {a1, b1} right through to whichever submenu item gets invoked.
    editor.setSelectedNodes({a1, b1});
    ASSERT_TRUE(editor.isNodeSelected(a1));
    ASSERT_TRUE(editor.isNodeSelected(b1));

    auto* compB1 = findComponent(editor, b1);
    ASSERT_NE(compB1, nullptr);

    juce::PopupMenu capturedMenu;
    compB1->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });

    compB1->mouseDown(makeModuleRightClick(*compB1, bodyClickPoint(*compB1)));

    // Confirms the retarget guard really did skip: selection is exactly as set up above, a
    // "different module was selected beforehand" (a1, in the OTHER macro).
    EXPECT_TRUE(editor.isNodeSelected(a1));
    EXPECT_TRUE(editor.isNodeSelected(b1));

    const auto* ungroupItem = findMenuItemByText(capturedMenu, "Ungroup");
    ASSERT_NE(ungroupItem, nullptr);
    ASSERT_TRUE(static_cast<bool>(ungroupItem->action));

    ungroupItem->action();

    EXPECT_EQ(editor.getMacros().find(macroB), nullptr)
        << "the RIGHT macro (B -- the one whose submenu was actually opened) must dissolve";
    ASSERT_NE(editor.getMacros().find(macroA), nullptr)
        << "macro A must survive -- a naive graft (no select-before-act) would have ungrouped it "
           "too, since a1 was still part of the selection at click time";
    EXPECT_EQ(editor.getMacros().find(macroA)->members.size(), 2u);

    // Ungrouping never deletes member nodes -- all four modules must still be real graph nodes.
    for (auto id : {a1, a2, b1, b2})
        EXPECT_NE(engine.getGraph().getNodeForId(id), nullptr);
}

TEST(MacroMemberContextMenu, RightClickFiresTheContextMenuHookExactlyOnce) {
    // Regression guard for the seam itself (docs/macros_ports.md §5.8): a real right-click body mouseDown()
    // must hand its menu to showContextMenuHook_ exactly once, rather than calling
    // PopupMenu::showMenuAsync() directly -- the latter opens a real popup and segfaults on a
    // headless (no-display) Linux CI runner. This pins the wiring so a future revert back to a
    // direct showMenuAsync() call fails here first, instead of only as an unexplained CI segfault.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto* comp = findComponent(editor, a);
    ASSERT_NE(comp, nullptr);

    int hookCallCount = 0;
    comp->setShowContextMenuHookForTest([&hookCallCount](juce::PopupMenu&) { ++hookCallCount; });

    comp->mouseDown(makeModuleRightClick(*comp, bodyClickPoint(*comp)));

    EXPECT_EQ(hookCallCount, 1) << "a right-click body mouseDown() must build and hand off exactly "
                                   "one context menu, never zero (a dropped gesture) or more than "
                                   "one (a real popup opened alongside the hook)";
}

TEST(MacroMemberContextMenu, TopLevelRemoveFromMacroItemActsOnThisModuleAloneRegardlessOfSelection) {
    // T138 second live-testing round (2026-09-10): a user's first instinct was "right-click the
    // module and remove it from the macro", not "open its macro's own nested submenu" -- this
    // pins the top-level escape hatch buildModuleContextMenu() now grafts on directly, one level up
    // from "Macro: <name>" -> "Remove from Macro".
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto uuidA = uuidOf(engine, a);

    editor.setSelectedNodes({a, b, c});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    // The whole macro is selected -- if this item read live selection the way the nested submenu's
    // own "Remove Selection from Macro" does, it would remove all three. It must not: it targets
    // ONLY the module whose card was actually right-clicked.
    ASSERT_TRUE(editor.isNodeSelected(b));
    ASSERT_TRUE(editor.isNodeSelected(c));

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);

    juce::PopupMenu capturedMenu;
    compA->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });
    compA->mouseDown(makeModuleRightClick(*compA, bodyClickPoint(*compA)));

    const auto* item = findMenuItemByText(capturedMenu, "Remove from Macro");
    ASSERT_NE(item, nullptr) << "a top-level item must exist, not just the nested submenu's own copy";
    ASSERT_TRUE(static_cast<bool>(item->action));

    item->action();

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_FALSE(macro->hasMember(uuidA)) << "only the right-clicked module leaves the macro";
    EXPECT_EQ(macro->members.size(), 2u) << "b and c must both still be members";
}

// ============================================================================
// Membership menu gating (T138): buildMacroMenu() captures the selection at BUILD time (before
// any item's own handler can move it), then shows "Add Selection to Macro" only when that
// captured selection has something addable, and "Remove from Macro" only when it has something
// removable -- see the capture comment at the top of buildMacroMenu() itself.
// ============================================================================

TEST(MacroMembershipMenu, AddItemAppearsOnlyWhenSelectionHasSomethingToAddAndActsOnItWhenInvoked) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    // Nothing else selected -- the collapsed card's own right-click (entry point 1) does NOT touch
    // selection, so whatever was selected before the click is still what buildMacroMenu() sees.
    editor.setSelectedNodes({a, b}); // exactly the macro's own members -- nothing addable
    EXPECT_EQ(findMenuItemByText(editor.buildMacroMenu(macroId), "Add Selection to Macro"), nullptr);

    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto uuidC = uuidOf(engine, c);
    editor.setSelectedNodes({c});
    const auto menu = editor.buildMacroMenu(macroId); // named, so pointers into it outlive this call
    const auto* addItem = findMenuItemByText(menu, "Add Selection to Macro");
    ASSERT_NE(addItem, nullptr) << "a loose module in the current selection makes the item appear";
    ASSERT_TRUE(static_cast<bool>(addItem->action));

    addItem->action();

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(uuidC));
}

TEST(MacroMembershipMenu, RemoveItemAppearsOnlyWhenSelectionHasAMemberAndActsOnItWhenInvoked) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto loose = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    editor.setSelectedNodes({loose}); // touches no member of this macro -- nothing removable
    EXPECT_EQ(findMenuItemByText(editor.buildMacroMenu(macroId), "Remove from Macro"), nullptr);

    auto uuidA = uuidOf(engine, a);
    editor.setSelectedNodes({a});
    const auto menu = editor.buildMacroMenu(macroId); // named, so pointers into it outlive this call
    const auto* removeItem = findMenuItemByText(menu, "Remove from Macro");
    ASSERT_NE(removeItem, nullptr);
    ASSERT_TRUE(static_cast<bool>(removeItem->action));

    removeItem->action();

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_FALSE(macro->hasMember(uuidA));
    EXPECT_EQ(macro->members.size(), 1u);
}

TEST(MacroMembershipMenu, RemoveItemLabelPluralizesForAMultiMemberSelection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    editor.setSelectedNodes({a, b, c});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.setSelectedNodes({a, b});
    const auto menu = editor.buildMacroMenu(macroId);
    EXPECT_EQ(findMenuItemByText(menu, "Remove from Macro"), nullptr);
    EXPECT_NE(findMenuItemByText(menu, "Remove Selection from Macro"), nullptr);
}

namespace {
juce::MouseEvent makeCardRightClick(MacroCardComponent& comp, juce::Point<int> position) {
    const auto pos = position.toFloat();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}
} // namespace

// Regression guard for a real bug found via live GUI testing (2026-09-10): MacroCardComponent's
// own real right-click handler calls owner.selectMacro(macroId, false) BEFORE building the menu
// (mouseDown's "if (!owner.isMacroSelected(macroId))" guard), which silently clobbers any OTHER
// selection the user made before right-clicking. Calling GraphEditor::buildMacroMenu() directly
// with setSelectedNodes() already set (as every other MacroMembershipMenu test above does) can
// never catch this -- it bypasses the real mouseDown()/reselect entirely, which is exactly why the
// earlier version of this feature shipped with "Add Selection to Macro" unreachable from the
// card's own right-click despite passing every one of those direct-call tests. This test drives
// the REAL gesture instead, the same "test the real mouse path" rule MacroMemberContextMenu's own
// suite already follows for ModuleComponent's right-click.
TEST(MacroMembershipMenu, AddItemAndSelectionBorderBothSurviveTheRealCardRightClick) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    auto loose = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto uuidLoose = uuidOf(engine, loose);
    editor.setSelectedNodes({loose}); // the external batch the card's own reselect must not lose

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    juce::PopupMenu capturedMenu;
    card->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });

    card->mouseDown(makeCardRightClick(*card, {10, 10}));

    // T138 live-testing follow-up (2026-09-10): the reselect is now SKIPPED whenever there was ANY
    // prior selection, specifically so the user still sees `loose`'s own selection border while the
    // menu is open -- forcing it here would silently swap the border onto the macro's own members
    // with no visual cue for what "Add Selection to Macro" is about to insert, which is the exact
    // confusion a live user hit even though the menu item itself worked correctly. See
    // CardRightClickStillReselectsMacroWhenNothingWasSelected below for the case where the reselect
    // must still fire, and RemoveSelectionFromMacroSurvivesTheRealCardRightClickWithAPartialSubset
    // for the second round this generalized fix was needed for (a subset of the macro's OWN
    // members, picked for a targeted removal, used to get force-reselected to ALL members too).
    EXPECT_TRUE(editor.isNodeSelected(loose));

    const auto* addItem = findMenuItemByText(capturedMenu, "Add Selection to Macro");
    ASSERT_NE(addItem, nullptr) << "the pre-reselect selection must still reach buildMacroMenu";
    ASSERT_TRUE(static_cast<bool>(addItem->action));

    // With the reselect skipped, live selection is still just `loose` -- so "Remove Selection
    // from Macro" (which reads live selection, not the captured candidate) must NOT appear; a
    // pure "add" gesture has nothing of the macro's own to remove.
    EXPECT_EQ(findMenuItemByText(capturedMenu, "Remove Selection from Macro"), nullptr);
    EXPECT_EQ(findMenuItemByText(capturedMenu, "Remove from Macro"), nullptr);

    addItem->action();

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(uuidLoose));
}

TEST(MacroMembershipMenu, CardRightClickStillReselectsMacroWhenNothingWasSelected) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.setSelectedNodes({}); // nothing selected -- the plain "click a fresh card" case

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    juce::PopupMenu capturedMenu;
    card->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });

    card->mouseDown(makeCardRightClick(*card, {10, 10}));

    // Nothing at all was selected beforehand, so the old "highlight what you're about to act
    // on" reselect must still fire -- this is what lets "Remove Selection from Macro" and
    // "Ungroup" find the macro's own members via live selection when nothing else was selected.
    EXPECT_TRUE(editor.isMacroSelected(macroId));
    EXPECT_NE(findMenuItemByText(capturedMenu, "Remove Selection from Macro"), nullptr);
}

TEST(MacroMembershipMenu, RemoveSelectionFromMacroSurvivesTheRealCardRightClickWithAPartialSubset) {
    // T138 second live-testing round (2026-09-10): reported live as "if I select one module and
    // then right-click, it just auto-selects all of the modules in the macro. So if I click remove
    // selected from macro, it just removes all of the modules". A partial subset of a macro's OWN
    // members is exactly as "non-empty" as an outside module -- the generalized fix (skip the
    // reselect whenever there was ANY prior selection, not just an addable one) has to cover this
    // case too, or a targeted single-member removal is unreachable from the card's own right-click.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);
    auto uuidA = uuidOf(engine, a);

    editor.setSelectedNodes({a, b, c});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.setSelectedNodes({a}); // deliberately just ONE of the macro's own three members

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    juce::PopupMenu capturedMenu;
    card->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });

    card->mouseDown(makeCardRightClick(*card, {10, 10}));

    // The forced reselect must NOT have clobbered the subset back to all three members.
    EXPECT_TRUE(editor.isNodeSelected(a));
    EXPECT_FALSE(editor.isNodeSelected(b));
    EXPECT_FALSE(editor.isNodeSelected(c));

    const auto* item = findMenuItemByText(capturedMenu, "Remove from Macro");
    ASSERT_NE(item, nullptr) << "singular label -- exactly one module was captured, not all three";
    ASSERT_TRUE(static_cast<bool>(item->action));
    item->action();

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_FALSE(macro->hasMember(uuidA)) << "only the captured subset leaves the macro";
    EXPECT_EQ(macro->members.size(), 2u) << "b and c must both still be members -- the exact bug "
                                            "reported was this removing ALL THREE instead";
}
