// FRO40: Cmd+drag (Ctrl+drag on Windows/Linux, where JUCE's commandModifier IS ctrlModifier) a
// module across an EXPANDED macro's hull border to add it to / remove it from that macro, with a
// live candidate-hull highlight, exactly ONE membership mutation at mouseUp, and the whole gesture
// (position + membership + any macro-port splicing) landing in ONE undo step.
//
// Every test below drives the REAL ModuleComponent::mouseDown/mouseDrag/mouseUp callbacks, never a
// direct addSelectionToMacro/removeSelectionFromMacro call —
// docs/macros/menu-and-membership.md#adding-to-and-removing-from-a-macro's T138 note documents a real prior case of a
// direct-call test hiding an unreachable feature, and this file's whole point is to prove the GESTURE (modifier
// arbitration, deferred click-vs-drag classification, the live highlight, the single undo step) actually works end to
// end.

#include "AppUndoManager.h"
#include "MacroContainerTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include <algorithm>
#include <functional>
#include <gtest/gtest.h>

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

// Simulates the Windows/Linux reality that commandModifier IS ctrlModifier there (both bits are
// always set together) — on macOS this sets two genuinely distinct bits, which is exactly what
// lets ONE test construction pin the arbitration on every platform: ModuleComponent::mouseDown's
// isCtrlDown() branch wins the press either way (real Ctrl-only on macOS, or the combined bit on
// Windows/Linux), and mouseUp's hull-crossing check is what decides reparent vs. plain finalize.
const juce::ModifierKeys kCtrlOrWindowsLinuxCmdClick(juce::ModifierKeys::leftButtonModifier |
                                                     juce::ModifierKeys::ctrlModifier |
                                                     juce::ModifierKeys::commandModifier);

/** Drives a full real body-drag gesture on `comp`, from its current position to `delta` away,
 *  under `mods`. `afterDragBeforeUp`, when given, runs after mouseDrag but before mouseUp — the
 *  ONE place a test can observe `GraphEditor::getMacroDragCandidateId()` for real, so a test whose
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
 *  live, and returns the macro id. */
juce::String makeExpandedTwoMemberMacro(GraphEditor& editor, AudioEngine& engine, NodeID& outA, NodeID& outB) {
    outA = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    outB = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({outA, outB});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    if (!macroId.isEmpty())
        editor.getMacroController().setMacroCollapsed(macroId, false);
    return macroId;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// 1. Cmd+drag an outside module into an expanded hull -> it becomes a member.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, CmdDragOutsideModuleIntoExpandedHullJoinsTheMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);
    ASSERT_EQ(editor.getMacroController().macroForNode(c), nullptr) << "sanity: C starts outside every macro";

    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());
    const auto delta = hull.getCentre() - compC->getBounds().getCentre();

    dragBodyBy(*compC, delta, kCmdClick, [&] {
        EXPECT_FALSE(editor.getMacroDragCandidateId().isEmpty())
            << "sanity: dragging C's centre into the hull must arm the JOIN candidate mid-drag -- "
               "without this the assertions below could silently pass on the plain finalize path";
    });

    const auto* macro = editor.getMacroController().macroForNode(c);
    ASSERT_NE(macro, nullptr) << "Cmd-dragging C's centre into the hull must join it to the macro";
    EXPECT_EQ(macro->id, macroId);

    // Regression pin: finalizeMacroMembershipDrag initially cleared the candidate/repainted but
    // never called endDragPreview(), so a SUCCESSFUL reparent left the landing ghost + grid overlay
    // on screen (Tests/UI/Graph/DragStateResetTests.cpp's CmdReparentDrag* cases cover this more
    // generally; pinned here too since this is the file that owns the feature).
    EXPECT_FALSE(editor.isDragPreviewActive()) << "a completed reparent must tear down the drag preview too";
}

// ---------------------------------------------------------------------------------------------
// 2. Cmd+drag a member out past the hull -> it leaves.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, CmdDragMemberOutPastHullLeavesTheMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(editor.getMacroController().macroForNode(a), nullptr) << "sanity: A starts as a member";

    // Comfortably outside the (B-only) hull-excluding-self that this drag is tested against — see
    // MacroGroupController::macroDragJoinOrLeaveTarget's own comment on why the LEAVE test can
    // never pass without excluding the dragged member's own contribution first.
    dragBodyBy(*compA, {2400, 0}, kCmdClick, [&] {
        EXPECT_FALSE(editor.getMacroDragCandidateId().isEmpty())
            << "sanity: dragging A well outside the hull must arm the LEAVE candidate mid-drag";
    });

    EXPECT_EQ(editor.getMacroController().macroForNode(a), nullptr)
        << "Cmd-dragging A well outside the hull must remove it";

    const auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr) << "the macro itself must survive — B is still a member";
    EXPECT_TRUE(macro->hasMember(uuidOf(engine, b)));
    EXPECT_FALSE(macro->hasMember(uuidOf(engine, a)));
}

// ---------------------------------------------------------------------------------------------
// 2b. Bug fix (in-app report): the PAINTED hull for the macro a member is being dragged OUT OF
//    must shrink away from it immediately, using macroHullBoundsExcluding — not the live union
//    macroHullBounds(), which keeps inflating around the dragged member and visually chases it,
//    making "remove from macro" look impossible. Only the macro being LEFT is affected; a macro
//    the drag might JOIN instead keeps its ordinary live hull (nothing to exclude — the dragged
//    module isn't yet a member of it).
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, PaintedHullOfOwnMacroExcludesDraggedMemberFromTheFirstTick) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);

    // B is the macro's only OTHER member, so excluding A leaves exactly B's own padded hull — a
    // fixed rectangle that does not move as A is dragged, which is what makes this assertable
    // without duplicating the union math here.
    const auto expectedExcludingA = editor.getMacroController().macroHullBoundsExcluding(macroId, uuidOf(engine, a));
    ASSERT_FALSE(expectedExcludingA.isEmpty());

    // A small drag, nowhere near leaving the excluding hull — proves the shrink happens on the
    // FIRST tick of the gesture, not only once a LEAVE candidate actually arms (the bug the user
    // hit: the live union kept including A for the whole first stretch of the pull-out).
    dragBodyBy(*compA, {20, 15}, kCmdClick, [&] {
        EXPECT_TRUE(editor.getMacroDragCandidateId().isEmpty())
            << "sanity: this small a drag must not arm any LEAVE/JOIN candidate yet";
        EXPECT_EQ(editor.paintedMacroHullBounds(macroId), expectedExcludingA)
            << "the macro A is being dragged OUT of must already paint as the excluding hull, "
               "before any candidate is armed";
    });
}

TEST(MacroDragMembership, PaintedHullOfAJoinTargetStaysTheOrdinaryLiveHull) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);

    const auto liveHull = editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(liveHull.isEmpty());
    const auto delta = liveHull.getCentre() - compC->getBounds().getCentre();

    dragBodyBy(*compC, delta, kCmdClick, [&] {
        EXPECT_FALSE(editor.getMacroDragCandidateId().isEmpty()) << "sanity: this must arm the JOIN candidate";
        EXPECT_EQ(editor.paintedMacroHullBounds(macroId), liveHull)
            << "a macro C might JOIN (C isn't a member of anything yet) must keep painting its "
               "ordinary live hull — nothing to exclude C from";
    });
}

// ---------------------------------------------------------------------------------------------
// 2c. Bug 2 (in-app report): "LEAVE doesn't work" raised the question of whether the LEAVE
//    predicate itself (canvasCentre outside macroHullBoundsExcluding(self)) is wrong once a macro
//    has MORE than two members, since the union of the REMAINING members can stay large. It is
//    not -- excluding the dragged member's own contribution still only unions the OTHER members'
//    bounds, so the escape distance is direction-dependent, not unbounded, and never unreachable:
//    three members in a horizontal row is the worst case for exactly this concern (the two outer
//    members keep the excluding hull's bounding box wide), and it still proves reachable in the
//    direction perpendicular to the row, at a short, predictable distance.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, ThreeMemberMacroLeaveIsReachablePerpendicularToTheRow) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    editor.setSelectedNodes({a, b, c});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* compB = findComponent(editor, b);
    ASSERT_NE(compB, nullptr);

    // The excluding hull is a real, computed rectangle from the two OUTER members (A and C) -- not
    // hand-derived here, so this test can never silently drift from what the predicate actually
    // computes.
    const auto hullExcludingB = editor.getMacroController().macroHullBoundsExcluding(macroId, uuidOf(engine, b));
    ASSERT_FALSE(hullExcludingB.isEmpty());
    ASSERT_TRUE(hullExcludingB.contains(compB->getBounds().getCentre()))
        << "sanity: B starts inside the hull excluding its own contribution";

    // ALONG the row: B's centre stays inside the wide A/C bounding box for a long horizontal move
    // -- the concern this test exists to check is real (this direction genuinely needs a much
    // bigger move than a 2-member macro would), but it is not unreachable, just directional; the
    // next drag below proves the short direction.
    dragBodyBy(*compB, {150, 0}, kCmdClick, [&] {
        EXPECT_TRUE(editor.getMacroDragCandidateId().isEmpty())
            << "moving the middle member ALONG the row must stay inside the wide A/C bounding box";
    });
    ASSERT_NE(editor.getMacroController().macroForNode(b), nullptr)
        << "sanity: B is still a member after the along-row move";

    // PERPENDICULAR to the row: exits just past the hull's own bottom edge -- bounded by half the
    // row's height plus the hull margin, regardless of how far apart A and C are horizontally.
    const int perpendicularDrop = hullExcludingB.getBottom() - compB->getBounds().getCentreY() + 5;
    dragBodyBy(*compB, {0, perpendicularDrop}, kCmdClick, [&] {
        EXPECT_FALSE(editor.getMacroDragCandidateId().isEmpty())
            << "moving the middle member PAST the hull's bottom edge must arm the LEAVE candidate";
    });

    EXPECT_EQ(editor.getMacroController().macroForNode(b), nullptr) << "dragging B past the row's hull must remove it";
    const auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr) << "the macro survives with A and C still members";
    EXPECT_TRUE(macro->hasMember(uuidOf(engine, a)));
    EXPECT_TRUE(macro->hasMember(uuidOf(engine, c)));
    EXPECT_FALSE(macro->hasMember(uuidOf(engine, b)));
}

// ---------------------------------------------------------------------------------------------
// 3. A crossing drag auto-creates macro ports for newly-crossing cables, and splices out ports
//    that became interior — in the SAME gesture.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, CmdDragJoinSplicesOutInteriorPortAndCreatesNewCrossingPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    auto f = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto g = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1300, 100);

    // A->F crosses what's about to become the macro's boundary (F is outside it); F->G stays
    // entirely outside for now. Both pre-exist the group, exactly like
    // MacroPortRealMouseDragTests.cpp's own crossing setups.
    editor.connectPorts(a, 0, f, 0, /*isMidi=*/false);
    editor.connectPorts(f, 0, g, 0, /*isMidi=*/false);

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    auto* macroBeforeDrag = editor.getMacros().find(macroId);
    ASSERT_NE(macroBeforeDrag, nullptr);
    ASSERT_EQ(macroBeforeDrag->ports.size(), 1u) << "sanity: grouping auto-created ONE port for A->F";
    EXPECT_FALSE(macroBeforeDrag->ports[0].isInput);
    const juce::String originalPortUuid = macroBeforeDrag->ports[0].nodeUuid;

    const int nodesBeforeDrag = engine.getGraph().getNodes().size();

    auto* compF = findComponent(editor, f);
    ASSERT_NE(compF, nullptr);
    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());
    dragBodyBy(*compF, hull.getCentre() - compF->getBounds().getCentre(), kCmdClick, [&] {
        EXPECT_FALSE(editor.getMacroDragCandidateId().isEmpty())
            << "sanity: dragging F's centre into the hull must arm the JOIN candidate mid-drag";
    });

    ASSERT_NE(editor.getMacroController().macroForNode(f), nullptr) << "F must have joined the macro";

    // Net node count is unchanged: the OLD port (A->F, now fully interior) is spliced out, and a
    // NEW port (F->G, now the crossing cable) is created to replace it.
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBeforeDrag)
        << "one port removed, one created -- the node count must net to zero";

    EXPECT_EQ(nodeIdForUuid(engine, originalPortUuid).uid, 0u) << "the old A->F port node must be gone";

    auto* macroAfterDrag = editor.getMacros().find(macroId);
    ASSERT_NE(macroAfterDrag, nullptr);
    ASSERT_EQ(macroAfterDrag->ports.size(), 1u) << "exactly one NEW port, for the F->G crossing";
    EXPECT_FALSE(macroAfterDrag->ports[0].isInput);
    EXPECT_NE(macroAfterDrag->ports[0].nodeUuid, originalPortUuid);

    // A->F is direct again (the spliced-out port's cable is restored, not dropped).
    bool foundDirectAtoF = false;
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == a && c.destination.nodeID == f)
            foundDirectAtoF = true;
    EXPECT_TRUE(foundDirectAtoF) << "splicing out the interior port must restore the direct A->F cable";
}

// ---------------------------------------------------------------------------------------------
// 4. ONE undo restores BOTH the position AND the membership.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, OneUndoStepRestoresBothPositionAndMembership) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    const juce::String uuidC = uuidOf(engine, c);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);
    const int originalX = engine.getGraph().getNodeForId(c)->properties["x"];
    const int originalY = engine.getGraph().getNodeForId(c)->properties["y"];

    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());
    const auto delta = hull.getCentre() - compC->getBounds().getCentre();

    const int serialBeforeDrag = undo.getEditSerial();
    dragBodyBy(*compC, delta, kCmdClick, [&] {
        EXPECT_FALSE(editor.getMacroDragCandidateId().isEmpty())
            << "sanity: dragging C's centre into the hull must arm the JOIN candidate mid-drag";
    });

    ASSERT_NE(editor.getMacroController().macroForNode(c), nullptr) << "sanity: the drag must have joined the macro";
    EXPECT_EQ(undo.getEditSerial(), serialBeforeDrag + 1)
        << "position + membership + port splicing must land in exactly ONE undo step, not two";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    // The restore is a full graph/macro snapshot roundtrip, so re-resolve by uuid rather than
    // trusting `c`/`compC` to still name anything real — MacroContainerTestHelpers.h's
    // nodeIdForUuid is the established idiom for this (see e.g. MacroContainerTests.cpp).
    const auto cAfterUndo = nodeIdForUuid(engine, uuidC);
    ASSERT_NE(cAfterUndo.uid, 0u);
    auto* nodeAfterUndo = engine.getGraph().getNodeForId(cAfterUndo);
    ASSERT_NE(nodeAfterUndo, nullptr);

    EXPECT_EQ((int)nodeAfterUndo->properties["x"], originalX) << "the single undo must restore the pre-drag position";
    EXPECT_EQ((int)nodeAfterUndo->properties["y"], originalY);
    EXPECT_EQ(editor.getMacros().findByMember(uuidC), nullptr) << "the SAME undo must also restore membership";

    // The macro's own grouping is a SEPARATE, earlier undo step, still intact -- proving the drag
    // really was ONE combined step rather than two (had it been two, undoing "the drag" once would
    // have left either the position OR the membership change still in place above).
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
}

// ---------------------------------------------------------------------------------------------
// 5. Cmd+CLICK with no movement still toggles selection (the deferred classification), leaving
//    membership unchanged.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, CmdClickWithNoMovementStillTogglesSelectionMembershipUnchanged) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    editor.setSelectedNodes({b}); // A is not currently selected
    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);

    const int serialBeforeClick = undo.getEditSerial();
    const juce::Point<int> pressPos(compA->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);

    compA->mouseDown(realMouseEvent(*compA, pressPos, pressPos, kCmdClick));
    compA->mouseUp(realMouseEvent(*compA, pressPos, pressPos, kCmdClick, /*wasDragged=*/false));

    EXPECT_EQ(undo.getEditSerial(), serialBeforeClick) << "a click-only toggle must push no undo step at all";

    const auto selected = editor.getSelectedNodes();
    EXPECT_EQ(selected.size(), 2u) << "Cmd+click must toggle A into the pre-press selection ({B}), additively";
    EXPECT_TRUE(std::find(selected.begin(), selected.end(), a) != selected.end());
    EXPECT_TRUE(std::find(selected.begin(), selected.end(), b) != selected.end());

    ASSERT_NE(editor.getMacroController().macroForNode(a), nullptr)
        << "membership must be untouched by a click with no movement";
    EXPECT_EQ(editor.getMacroController().macroForNode(a)->id, macroId);
    ASSERT_NE(editor.getMacroController().macroForNode(b), nullptr);
}

// ---------------------------------------------------------------------------------------------
// 6. Cmd+drag that stays entirely outside every macro -> a plain move, membership unchanged.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, CmdDragStayingOutsideEveryHullIsAPlainMoveMembershipUnchanged) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    NodeID a, b;
    ASSERT_FALSE(makeExpandedTwoMemberMacro(editor, engine, a, b).isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);
    const auto positionBeforeDrag = compC->getPosition();

    const int serialBeforeDrag = undo.getEditSerial();
    dragBodyBy(*compC, {30, 30}, kCmdClick, [&] {
        EXPECT_TRUE(editor.getMacroDragCandidateId().isEmpty())
            << "sanity: nowhere near any hull must NOT arm a candidate mid-drag";
    }); // small move, nowhere near the macro's hull

    EXPECT_EQ(undo.getEditSerial(), serialBeforeDrag + 1) << "the plain finalize path still pushes its usual one step";
    EXPECT_NE(compC->getPosition(), positionBeforeDrag) << "sanity: it actually moved";
    EXPECT_EQ(editor.getMacroController().macroForNode(c), nullptr)
        << "a drag that never crossed a hull must not join anything";
}

// ---------------------------------------------------------------------------------------------
// 6b. Gap 3 (in-app report): reparentArmed was sampled ONLY at mouseDown, so the user had to
//    already be holding Cmd before the press -- grabbing a module plain and pressing Cmd partway
//    through the drag never armed reparent, with no way to discover the gesture from the
//    highlight following the cursor. mouseDrag now re-derives it live for a single-module drag.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, CmdPressedAfterDragBeganStillReparentsInOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    const juce::String uuidA = uuidOf(engine, a);
    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);
    const int originalX = engine.getGraph().getNodeForId(a)->properties["x"];
    const int originalY = engine.getGraph().getNodeForId(a)->properties["y"];

    const juce::ModifierKeys plain(juce::ModifierKeys::leftButtonModifier);
    const juce::Point<int> pressPos(compA->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    // Same delta as CmdDragMemberOutPastHullLeavesTheMacro -- comfortably outside the (B-only)
    // hull-excluding-self.
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(2400, 0);

    // makeExpandedTwoMemberMacro leaves {A, B} multi-selected (how it grouped them); a plain click
    // on an ALREADY-selected module keeps the whole selection intact for dragging (see mouseDown's
    // own comment), which would make this a group drag and correctly exempt it from Gap 3's live
    // re-arming. Collapse onto A alone first, same as a real single-module grab would.
    editor.setSelectedNodes({a});

    // Press with NO modifier at all -- an ordinary grab, exactly what the user does before deciding
    // mid-drag that they actually want to reparent.
    compA->mouseDown(realMouseEvent(*compA, pressPos, pressPos, plain));

    // First tick, still plain: nothing may arm yet.
    compA->mouseDrag(realMouseEvent(*compA, dragPos, pressPos, plain, /*wasDragged=*/true));
    EXPECT_TRUE(editor.getMacroDragCandidateId().isEmpty())
        << "sanity: no modifier held yet -- this tick must be an ordinary plain-drag tick";

    // Cmd goes down MID-drag, cursor otherwise held at the SAME screen point -- this must arm the
    // LEAVE candidate live. ComponentDragger::dragComponent (juce_ComponentDragger.cpp) ADDS
    // (e.getPosition() - mouseDownWithinTarget) onto the component's CURRENT bounds each call, so
    // a truly stationary cursor after the first tick is expressed as `pressPos` again here (a
    // zero further delta), NOT the same `dragPos` as tick one -- reusing `dragPos` would double
    // the move instead of holding it still.
    compA->mouseDrag(realMouseEvent(*compA, pressPos, pressPos, kCmdClick, /*wasDragged=*/true));
    EXPECT_EQ(editor.getMacroDragCandidateId(), macroId)
        << "pressing Cmd mid-drag, after the press already happened, must still arm the candidate";

    const int serialBeforeUp = undo.getEditSerial();
    compA->mouseUp(realMouseEvent(*compA, pressPos, pressPos, kCmdClick, /*wasDragged=*/true));

    EXPECT_EQ(editor.getMacroController().macroForNode(nodeIdForUuid(engine, uuidA)), nullptr)
        << "Cmd armed only mid-drag must still finalize as a real reparent";
    EXPECT_EQ(undo.getEditSerial(), serialBeforeUp + 1) << "and land as exactly ONE undo step, same as any other";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    // Full graph/macro snapshot roundtrip -- re-resolve by uuid, same idiom as
    // OneUndoStepRestoresBothPositionAndMembership above.
    const auto aAfterUndo = nodeIdForUuid(engine, uuidA);
    ASSERT_NE(aAfterUndo.uid, 0u);
    auto* nodeAfterUndo = engine.getGraph().getNodeForId(aAfterUndo);
    ASSERT_NE(nodeAfterUndo, nullptr);
    EXPECT_EQ((int)nodeAfterUndo->properties["x"], originalX) << "the single undo must restore the pre-drag position";
    EXPECT_EQ((int)nodeAfterUndo->properties["y"], originalY);
    EXPECT_NE(editor.getMacros().findByMember(uuidA), nullptr)
        << "the SAME undo must also restore membership -- one gesture armed mid-drag, one undo step";
}

TEST(MacroDragMembership, CmdReleasedMidDragRevertsToAPlainMove) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);
    const auto positionBeforeDrag = compA->getPosition();

    const juce::ModifierKeys plain(juce::ModifierKeys::leftButtonModifier);
    const juce::Point<int> pressPos(compA->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const juce::Point<int> dragPos = pressPos + juce::Point<int>(2400, 0);

    compA->mouseDown(realMouseEvent(*compA, pressPos, pressPos, kCmdClick));

    compA->mouseDrag(realMouseEvent(*compA, dragPos, pressPos, kCmdClick, /*wasDragged=*/true));
    EXPECT_EQ(editor.getMacroDragCandidateId(), macroId) << "sanity: LEAVE must arm first, same as test 2";

    // Cmd goes UP mid-drag, cursor otherwise held at the SAME screen point (see the sibling test
    // above for why that means `pressPos` again here, not `dragPos`) -- this must disarm the
    // candidate immediately, not wait for mouseUp to notice.
    compA->mouseDrag(realMouseEvent(*compA, pressPos, pressPos, plain, /*wasDragged=*/true));
    EXPECT_TRUE(editor.getMacroDragCandidateId().isEmpty())
        << "releasing Cmd mid-drag must disarm the candidate immediately";

    compA->mouseUp(realMouseEvent(*compA, pressPos, pressPos, plain, /*wasDragged=*/true));

    EXPECT_NE(editor.getMacroController().macroForNode(a), nullptr)
        << "Cmd released before mouseUp must finalize as an ordinary plain move -- A stays a member";
    EXPECT_NE(compA->getPosition(), positionBeforeDrag) << "sanity: it actually moved";
}

// ---------------------------------------------------------------------------------------------
// 7. The platform matrix (docs/macros/menu-and-membership.md#cmd-drag-across-a-hull-border): reparenting is gated on
// `reparentArmed`
//    (== e.mods.isCommandDown() at press), NOT on which of ctrlTogglePending/cmdReparentPending
//    armed the press. On macOS Ctrl and Cmd are distinct keys, so a PLAIN Ctrl-drag must NEVER
//    reparent even when it crosses a hull — that is the shipped insert-between gesture and this
//    feature must not silently compound it. On Windows/Linux commandModifier IS ctrlModifier, so
//    a Ctrl-drag there IS reparent-armed and is arbitrated at mouseUp by whether it crosses a hull
//    (kCtrlOrWindowsLinuxCmdClick below reproduces that shape on any platform, including macOS,
//    by setting BOTH bits explicitly).
// ---------------------------------------------------------------------------------------------

TEST(MacroDragMembership, WindowsLinuxCtrlDragCrossingHullReparents) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);

    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());
    dragBodyBy(*compC, hull.getCentre() - compC->getBounds().getCentre(), kCtrlOrWindowsLinuxCmdClick, [&] {
        EXPECT_FALSE(editor.getMacroDragCandidateId().isEmpty())
            << "sanity: dragging C's centre into the hull must arm the JOIN candidate mid-drag, "
               "with BOTH ctrlModifier and commandModifier set (the Windows/Linux shape)";
    });

    const auto* macro = editor.getMacroController().macroForNode(c);
    ASSERT_NE(macro, nullptr) << "a Windows/Linux-shaped Ctrl-drag that crosses the hull must reparent";
    EXPECT_EQ(macro->id, macroId);
}

TEST(MacroDragMembership, WindowsLinuxCtrlDragNotCrossingAHullKeepsThePlainInsertPath) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);
    const auto positionBeforeDrag = compC->getPosition();

    dragBodyBy(*compC, {60, 5}, kCtrlOrWindowsLinuxCmdClick, [&] {
        EXPECT_TRUE(editor.getMacroDragCandidateId().isEmpty())
            << "sanity: nowhere near any hull must NOT arm a candidate mid-drag";
    }); // nowhere near the macro's hull

    EXPECT_NE(compC->getPosition(), positionBeforeDrag) << "sanity: the existing Ctrl-drag machinery still moves it";
    EXPECT_EQ(editor.getMacroController().macroForNode(c), nullptr)
        << "no hull crossed -> no reparent, same as before FRO40";
}

// THE REGRESSION this coordinator review caught: a PLAIN macOS Ctrl-drag (ctrlModifier alone,
// commandModifier genuinely NOT set — the two keys are distinct there) must stay the shipped
// insert-between gesture ONLY, even when it crosses a real expanded macro's hull. The first cut of
// the arbitration gated reparent on `ctrlTogglePending || cmdReparentPending`, which is true for
// this exact gesture too, and would have silently joined the macro as well. reparentArmed
// (== isCommandDown() alone) is the fix; this pins it.
//
// Only expressible where Ctrl and Cmd are genuinely distinct bits (macOS) — JUCE defines
// commandModifier == ctrlModifier on Windows/Linux (see kCtrlOrWindowsLinuxCmdClick above), so a
// "Ctrl down, Cmd up" ModifierKeys value cannot exist there: constructing it with ctrlModifier
// alone ALSO sets isCommandDown() on those platforms, reparentArmed is then correctly true, the
// module correctly reparents, and this test's "membership unchanged" assertion would fail — not
// because the production code is wrong (WindowsLinuxCtrlDragCrossingHullReparents below covers
// exactly that platform's real behaviour), but because the test's own premise doesn't exist there.
// Confirmed on CI (PR #415, run 35293581829): both Ubuntu jobs ("Build, Test, and Coverage" and
// "Build and Test (ASAN)") failed on this one test, while macOS and Windows passed. Skips on the
// modifier semantics themselves (checked as a compile-time constant), not on a platform macro, so
// this stays correct if a platform ever changes which bits alias.
TEST(MacroDragMembership, MacOsPlainCtrlDragCrossingHullDoesNotReparent) {
    if constexpr (juce::ModifierKeys::commandModifier == juce::ModifierKeys::ctrlModifier) {
        GTEST_SKIP() << "Ctrl and Cmd are the same modifier on this platform, so a "
                        "Ctrl-without-Cmd press cannot be expressed; the reparent-on-crossing "
                        "behaviour here is covered by WindowsLinuxCtrlDragCrossingHullReparents.";
    }

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);

    const juce::ModifierKeys macOsCtrlOnly(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
    ASSERT_FALSE(macOsCtrlOnly.isCommandDown()) << "sanity: this construction must NOT set the command bit";

    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());
    const auto delta = hull.getCentre() - compC->getBounds().getCentre();

    dragBodyBy(*compC, delta, macOsCtrlOnly, [&] {
        EXPECT_TRUE(editor.getMacroDragCandidateId().isEmpty())
            << "a plain Ctrl-drag (no Cmd) must never arm the reparent candidate, even crossing a hull";
    });

    EXPECT_EQ(editor.getMacroController().macroForNode(c), nullptr)
        << "a plain macOS Ctrl-drag across a hull must stay insert-between only and must NOT join the macro";
}
