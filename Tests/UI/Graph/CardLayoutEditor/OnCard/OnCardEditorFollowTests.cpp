// OnCardEditorFollowTests.cpp -- Edit Layout when something other than the editor moves the card being edited:
// the session goes on and the canvas pans, eased, only as far as needed to keep the card on screen; and
// starting an edit on a second module ends the first one's, its changes kept. Real undo path, real menu click.
// And a control, a group or a row of the Add control panel dragged to the edge of the canvas view scrolls the canvas,
// so the part of a cut card (and the "Drop to hide" area under it) that was out of sight comes under the still pointer;
// Esc and the release stop it, and the drop is still one undo step. The 30 Hz edge-scroll timer is ticked by hand.
// Bug kinds: undo-gap, motion-over-time, drag-leaves-view.

#include "../../../Layout/ViewportDragFollow.h"
#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardEditorOwner.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

void drop(CardLayoutOnCardEditor& editor, const juce::String& key, juce::Point<int> by) {
    Pointer pointer(editor, key);
    pointer.moveBy(by, kCommand);
    pointer.release();
}

// The module moved on the canvas, as one undo step (an auto-arrange or a drag records the same).
void recordModuleMove(OnCardRig& rig, NodeID id, int x, int y) {
    auto& graph = rig.canvas.engine.getGraph();
    rig.canvas.undo.recordStructuralChange(graph, [&] {
        auto* node = graph.getNodeForId(id);
        node->properties.set("x", x);
        node->properties.set("y", y);
    });
    rig.canvas.editor.updateComponents();
}

// What the app does on Cmd+Z with the editor open: the editor sees the key and leaves it, the app undoes.
bool pressUndo(OnCardRig& rig, CardLayoutOnCardEditor& editor, const juce::String& focused) {
    EXPECT_FALSE(editor.getOutlineForTest(focused)->keyPressed(juce::KeyPress('z', juce::ModifierKeys(kCommand), 0)));
    return rig.canvas.undo.undo();
}

bool shows(OnCardRig& rig, NodeID id) {
    return rig.canvas.editor.getVisibleCanvasRect().contains(rig.card(id)->getBounds().toFloat());
}

float panX(OnCardRig& rig) { return rig.canvas.editor.getViewDoc().panX; }

/** The card at (100, 100) is moved to (3000, 100), off to the right of a 2400 px wide view; the view is centred on it
 *  there and the editor opened; an undo then puts the card back at (100, 100), off to the left of the view. */
struct FarAway : OnCardRig {
    FarAway() {
        id = add(std::make_unique<FilterModule>(), 100, 100);
        recordModuleMove(*this, id, 3000, 100);
        canvas.editor.centreViewOn(card(id)->getBounds().toFloat().getCentre());
        editor = openOnCard(id);
    }

    NodeID id;
    CardLayoutOnCardEditor* editor = nullptr;
};

} // namespace

TEST(OnCardEditorFollow, AnUndoThatMovesTheEditedCardKeepsTheEditorOpenAndPansTheViewToTheCard) {
    FarAway far;
    ASSERT_NE(far.editor, nullptr);
    ASSERT_TRUE(shows(far, far.id));
    const int outlines = far.editor->getOutlineCountForTest();
    const auto zoom = far.canvas.editor.getViewDoc().zoom;

    ASSERT_TRUE(pressUndo(far, *far.editor, "cutoff"));
    far.editor->runQueuedSyncForTest();
    EXPECT_EQ(far.card(far.id)->getX(), 100) << "the undo did move the card";
    EXPECT_FALSE(far.editor->isClosed()) << "editing goes on";
    EXPECT_EQ(far.editor->getOutlineCountForTest(), outlines);
    EXPECT_TRUE(shows(far, far.id)) << "and the view follows the card";
    EXPECT_FLOAT_EQ(far.canvas.editor.getViewDoc().zoom, zoom) << "only a pan, never a zoom";
    EXPECT_EQ(far.editor->getBounds().getPosition(), far.card(far.id)->getBounds().getPosition())
        << "the overlay is still over the card";
}

TEST(OnCardEditorFollow, TheViewPansOnlyWhenTheCardWouldLeaveTheVisibleArea) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 100, 100);
    recordModuleMove(rig, id, 600, 300);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto before = rig.canvas.editor.getViewDoc();

    ASSERT_TRUE(pressUndo(rig, *editor, "cutoff"));
    editor->runQueuedSyncForTest();
    EXPECT_EQ(rig.card(id)->getX(), 100);
    EXPECT_FLOAT_EQ(panX(rig), before.panX) << "the card stayed on screen: the view does not move";
    EXPECT_FLOAT_EQ(rig.canvas.editor.getViewDoc().panY, before.panY);
    EXPECT_FALSE(editor->isFollowingForTest());
}

TEST(OnCardEditorFollow, ACardHalfOffTheEdgeIsPannedInByTheLeastThatShowsItWhole) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>(), 100, 100);
    recordModuleMove(rig, id, 2300, 100); // runs past the right edge of the 2400 px view
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    ASSERT_FALSE(shows(rig, id));
    // Move the card back in with an undo, then forward again with a redo: the redo puts it past the edge.
    ASSERT_TRUE(pressUndo(rig, *editor, "cutoff"));
    editor->runQueuedSyncForTest();
    ASSERT_TRUE(shows(rig, id));
    const float panBefore = panX(rig);

    ASSERT_TRUE(rig.canvas.undo.redo());
    editor->runQueuedSyncForTest();
    EXPECT_EQ(rig.card(id)->getX(), 2300);
    EXPECT_TRUE(shows(rig, id));
    const auto visible = rig.canvas.editor.getVisibleCanvasRect();
    const auto card = rig.card(id)->getBounds().toFloat();
    EXPECT_LT(panX(rig), panBefore) << "the canvas moved left";
    EXPECT_NEAR(visible.getRight() - card.getRight(), 24.0f, 1.0f) << "a small margin, no more than needed";
}

TEST(OnCardEditorFollow, ThePanEasesWhenOnScreenAndLandsAtOnceUnderReduceMotion) {
    {
        FarAway far;
        ASSERT_NE(far.editor, nullptr);
        synth::ui::setReducedMotionForTest(false);
        far.editor->setForceAnimateForTest(true);
        const float start = panX(far);
        ASSERT_TRUE(pressUndo(far, *far.editor, "cutoff"));
        far.editor->runQueuedSyncForTest();
        ASSERT_TRUE(far.editor->isFollowingForTest());
        EXPECT_FLOAT_EQ(panX(far), start) << "the first frame has not moved yet";
        far.editor->stepFollowForTest(0.0f);
        float previous = panX(far);
        for (float ms = 16.0f; ms <= 400.0f; ms += 16.0f) {
            far.editor->stepFollowForTest(ms);
            EXPECT_GE(panX(far), previous) << "pans one way only (the card is to the left), at " << ms << " ms";
            previous = panX(far);
        }
        far.editor->stepFollowForTest(1.0e6f);
        EXPECT_TRUE(shows(far, far.id));
        EXPECT_GT(panX(far), start);
    }
    FarAway reduced;
    ASSERT_NE(reduced.editor, nullptr);
    synth::ui::setReducedMotionForTest(true);
    reduced.editor->setForceAnimateForTest(true);
    ASSERT_TRUE(pressUndo(reduced, *reduced.editor, "cutoff"));
    reduced.editor->runQueuedSyncForTest();
    EXPECT_TRUE(shows(reduced, reduced.id)) << "no tween under Reduce Motion";
    EXPECT_FALSE(reduced.editor->isFollowingForTest());
}

TEST(OnCardEditorFollow, StartingAnEditOnASecondModuleEndsTheFirstAndKeepsItsChanges) {
    OnCardRig rig;
    ModuleComponent::setCardLayoutEditorLauncherForTest(nullptr); // the real owner, as the app has (the rig's
                                                                  // destructor clears the seam either way)
    const auto first = rig.add(std::make_unique<FilterModule>(), 100, 100);
    const auto second = rig.add(std::make_unique<FilterModule>(), 700, 100);
    const auto editorOver = [&rig](NodeID id) -> CardLayoutOnCardEditor* {
        for (auto* comp : rig.canvas.editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == id)
                if (auto* parent = comp->getParentComponent())
                    for (auto* sibling : parent->getChildren())
                        if (auto* e = dynamic_cast<CardLayoutOnCardEditor*>(sibling))
                            if (!e->isClosed() && e->getBounds().getPosition() == comp->getPosition())
                                return e;
        return nullptr;
    };

    rig.card(first)->showCardLayoutEditor();
    // Starting the second edit deletes the first editor, so it is held by a SafePointer: a closed editor either says
    // so or is already gone.
    juce::Component::SafePointer<CardLayoutOnCardEditor> a(editorOver(first));
    ASSERT_NE(a, nullptr);
    drop(*a, "cutoff", {40, 0});
    ASSERT_TRUE(rig.storedLayout(first).has_value());
    const auto kept = rig.storedLayout(first);

    rig.card(second)->showCardLayoutEditor();
    EXPECT_TRUE(a == nullptr || a->isClosed()) << "the first module's edit ended, no dialog";
    EXPECT_EQ(rig.storedLayout(first), kept) << "its change stays";
    auto* b = editorOver(second);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(editorOver(first), nullptr);

    ASSERT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.storedLayout(first).has_value()) << "one Cmd+Z takes the first module's move back";
    synth::ui::closeOnCardLayoutEditor(rig.canvas.editor); // ends the session while the canvas is alive
}

namespace {

// ---- The editor's own drags held at the canvas view's edge ----

constexpr int kLeft = juce::ModifierKeys::leftButtonModifier;

// A card being edited, with the view's bottom edge cut through the middle of it: its lower half, the Add strip and
// the hide area are out of sight, and the pointer the edge scroll reads sits on that edge.
struct CutByTheView {
    OnCardRig rig;
    NodeID id;
    CardLayoutOnCardEditor* editor = nullptr;
    juce::Point<int> edge;     // GraphEditor-local: where the pointer is now, as the edge scroll reads it
    juce::Point<int> edgeRest; // the point on the view's bottom edge the drags are held at

    // `cut` is how far down the card the view's bottom edge falls (the middle by default).
    explicit CutByTheView(std::unique_ptr<juce::AudioProcessor> module, float cut = 0.5f,
                          std::optional<synth::CardLayout> layout = std::nullopt) {
        if (!layout.has_value() && dynamic_cast<FilterModule*>(module.get()) != nullptr)
            layout = EditorCanvas::rowFilterLayout(); // the Filter's built-in is a lane grid; the editor edits rows
        id = rig.canvas.add(std::move(module), 200, 200, layout);
        rig.canvas.editor.updateComponents();
        editor = rig.openOnCard(id);
        auto& graph = rig.canvas.editor;
        const auto area = graph.getLocalArea(rig.card(id), rig.card(id)->getLocalBounds());
        graph.setSize(graph.getWidth(), area.getY() + (int)((float)area.getHeight() * cut) + 2);
        edge = {graph.getLocalArea(rig.card(id), rig.card(id)->getLocalBounds()).getCentreX(), graph.getHeight() - 2};
        edgeRest = edge;
        graph.getEdgeScroll().setPointerForTest([this] { return edge.toFloat(); });
    }
    // The pointer well inside the view (outside every edge zone): where a real drag is when it arms.
    void pointerInside() { edge = {edgeRest.x, edgeRest.y - 100}; }
    void pointerAtEdge() { edge = edgeRest; }
    GraphEditor& graph() { return rig.canvas.editor; }
    float panY() { return graph().getViewDoc().panY; }
    void tick(int times) {
        for (int i = 0; i < times; ++i)
            graph().getEdgeScroll().tickForTest();
    }
    bool scrolling() { return graph().getEdgeScroll().isActive(); }
    // The pointer `px` below the view's bottom edge (the real pointer left the window).
    void pointerPast(float px) { edge = {edgeRest.x, graph().getHeight() + (int)px}; }
    // What the editor draws for the drag (an overlay rectangle) still starts inside the view.
    bool startsInView(juce::Rectangle<int> overlayRect) {
        return graph().getLocalArea(editor, overlayRect).getY() < graph().getHeight();
    }
    // A real mouse event on `comp` at the edge point.
    juce::MouseEvent atEdge(juce::Component& comp, int mods) {
        return cardbody_test::mouseAt(comp, comp.getLocalPoint(&graph(), edge).toFloat(), juce::ModifierKeys(mods));
    }
};

} // namespace

TEST(OnCardEditorEdgeScroll, AControlHeldAtTheViewEdgeScrollsTheCanvasDownToTheHideAreaAndTheDropIsOneUndo) {
    CutByTheView cut(std::make_unique<FilterModule>());
    ASSERT_NE(cut.editor, nullptr);
    auto* outline = cut.editor->getOutlineForTest("cutoff");
    ASSERT_NE(outline, nullptr);
    const auto press = outline->getLocalBounds().getCentre().toFloat();
    outline->mouseDown(cardbody_test::mouseAt(*outline, press, juce::ModifierKeys(kLeft)));
    cut.pointerInside(); // the drag arms with the pointer inside the view, then goes to the edge
    outline->mouseDrag(cut.atEdge(*outline, kLeft | kCommand));
    cut.pointerAtEdge();
    outline->mouseDrag(cut.atEdge(*outline, kLeft | kCommand));
    ASSERT_TRUE(cut.editor->isDraggingForTest());
    ASSERT_FALSE(cut.editor->isHidingDragForTest()) << "sanity: the pointer starts on the card";
    ASSERT_TRUE(cut.scrolling()) << "the control drag armed the canvas edge scroll";

    const float before = cut.panY();
    cut.tick(40);
    EXPECT_LT(cut.panY(), before - 100.0f) << "the canvas scrolled down";
    EXPECT_TRUE(cut.editor->isHidingDragForTest()) << "the hide area that was out of sight came under the pointer";
    EXPECT_FALSE(cut.rig.storedLayout(cut.id).has_value()) << "nothing is written until the release";

    cut.editor->getOutlineForTest("cutoff")->mouseUp(cut.atEdge(*outline, 0));
    EXPECT_FALSE(cut.scrolling()) << "the release stops it";
    ASSERT_TRUE(cut.rig.storedLayout(cut.id).has_value());
    EXPECT_TRUE(cut.rig.storedLayout(cut.id)->hidden.contains("cutoff"));
    ASSERT_TRUE(cut.rig.canvas.undo.undo());
    cut.editor->runQueuedSyncForTest();
    EXPECT_FALSE(cut.rig.storedLayout(cut.id).has_value()) << "one Cmd+Z undoes the whole drag";

    // A control pressed with the pointer already inside the view's bottom edge zone stays put until the pointer moves
    // toward that edge.
    CutByTheView still(std::make_unique<FilterModule>());
    auto* stillOutline = still.editor->getOutlineForTest("cutoff");
    ASSERT_NE(stillOutline, nullptr);
    const auto stillPress = stillOutline->getLocalBounds().getCentre().toFloat();
    const auto edgeY = still.edge.y;
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             still.edge.y = edgeY - 10;
             stillOutline->mouseDown(cardbody_test::mouseAt(*stillOutline, stillPress, juce::ModifierKeys(kLeft)));
             stillOutline->mouseDrag(still.atEdge(*stillOutline, kLeft | kCommand));
         },
         [&] { still.tick(1); },
         [&] {
             still.edge.y = edgeY - 4;
             stillOutline->mouseDrag(still.atEdge(*stillOutline, kLeft | kCommand));
         },
         [&] { return (double)still.panY(); }, [&] { stillOutline->mouseUp(still.atEdge(*stillOutline, 0)); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the canvas scrolling, and the control
    // stays in view.
    std::unique_ptr<CutByTheView> past;
    CardLayoutOutline* pastOutline = nullptr;
    viewport_drag_follow::expectPastWindowFollows(
        {[&] {
             // Cut near the card's bottom: its controls are in view, the Add strip and the hide area are not (far
             // enough down that the canvas still has to scroll).
             past = std::make_unique<CutByTheView>(std::make_unique<FilterModule>(), 0.85f);
             pastOutline = past->editor->getOutlineForTest("cutoff");
             pastOutline->mouseDown(cardbody_test::mouseAt(
                 *pastOutline, pastOutline->getLocalBounds().getCentre().toFloat(), juce::ModifierKeys(kLeft)));
             past->pointerInside();
             pastOutline->mouseDrag(past->atEdge(*pastOutline, kLeft | kCommand));
         },
         [&](float px) {
             past->pointerPast(px);
             pastOutline->mouseDrag(past->atEdge(*pastOutline, kLeft | kCommand));
         },
         [&] { past->tick(1); }, [&] { return (double)past->panY(); },
         [&] { return past->startsInView(past->editor->getCellRectForTest("cutoff")); },
         [&] { pastOutline->mouseUp(past->atEdge(*pastOutline, 0)); }});
}

// Regression test for FRO1688: held at the edge, the canvas scrolled on past the card and its hide strip until the
// content ran out, leaving the control's ghost far from the pointer.
TEST(OnCardEditorEdgeScroll, TheScrollStopsOnceTheCardAndItsHideStripAreInView) {
    CutByTheView cut(std::make_unique<FilterModule>());
    ASSERT_NE(cut.editor, nullptr);
    auto* outline = cut.editor->getOutlineForTest("cutoff");
    ASSERT_NE(outline, nullptr);
    outline->mouseDown(
        cardbody_test::mouseAt(*outline, outline->getLocalBounds().getCentre().toFloat(), juce::ModifierKeys(kLeft)));
    cut.pointerInside();
    outline->mouseDrag(cut.atEdge(*outline, kLeft | kCommand));
    cut.pointerAtEdge();
    outline->mouseDrag(cut.atEdge(*outline, kLeft | kCommand));
    ASSERT_TRUE(cut.scrolling());
    cut.tick(200);
    const float settled = cut.panY();
    cut.tick(40);
    EXPECT_FLOAT_EQ(cut.panY(), settled) << "the scroll stopped";
    const auto overlay = cut.graph().getLocalArea(cut.editor, cut.editor->getLocalBounds());
    EXPECT_NEAR(overlay.getBottom(), cut.graph().getHeight(), 1)
        << "it stopped where the card's hide strip meets the view's bottom, not further";
    EXPECT_TRUE(cut.editor->isHidingDragForTest()) << "the pointer held at the edge rests on the hide strip";
    cut.editor->getOutlineForTest("cutoff")->mouseUp(cut.atEdge(*outline, 0));
}

TEST(OnCardEditorEdgeScroll, EscapeMidDragStopsTheScrollWithTheDrag) {
    CutByTheView cut(std::make_unique<FilterModule>());
    ASSERT_NE(cut.editor, nullptr);
    auto* outline = cut.editor->getOutlineForTest("cutoff");
    const auto press = outline->getLocalBounds().getCentre().toFloat();
    outline->mouseDown(cardbody_test::mouseAt(*outline, press, juce::ModifierKeys(kLeft)));
    outline->mouseDrag(cut.atEdge(*outline, kLeft | kCommand));
    cut.tick(2);
    ASSERT_TRUE(cut.scrolling());
    EXPECT_TRUE(cut.editor->sendEscapeToDragForTest());
    EXPECT_FALSE(cut.scrolling());
    const float stopped = cut.panY();
    cut.tick(5);
    EXPECT_FLOAT_EQ(cut.panY(), stopped);
}

TEST(OnCardEditorEdgeScroll, AGroupHeldAtTheViewEdgeScrollsTheCanvasAndEscapeStopsIt) {
    CutByTheView cut(std::make_unique<OscillatorModule>(), 0.5f, rowOscillatorLayout());
    ASSERT_NE(cut.editor, nullptr);
    auto* handle = cut.editor->getGroupHandleForTest("unison");
    ASSERT_NE(handle, nullptr);
    const auto press = handle->getTitleRowArea().getCentre().toFloat();
    handle->mouseDown(cardbody_test::mouseAt(*handle, press, juce::ModifierKeys(kLeft)));
    cut.pointerInside(); // the drag arms with the pointer inside the view, then goes to the edge
    handle->mouseDrag(
        cardbody_test::mouseAt(*handle, press + juce::Point<float>(0.0f, 12.0f), juce::ModifierKeys(kLeft)));
    cut.pointerAtEdge();
    handle->mouseDrag(cut.atEdge(*handle, kLeft));
    ASSERT_TRUE(cut.editor->isDraggingGroupForTest());
    ASSERT_TRUE(cut.scrolling()) << "the group drag armed the canvas edge scroll";

    const float before = cut.panY();
    cut.tick(10);
    EXPECT_LT(cut.panY(), before - 50.0f) << "the canvas scrolled down";
    EXPECT_TRUE(cut.editor->isDraggingGroupForTest()) << "the group is still lifted under the pointer";

    EXPECT_TRUE(cut.editor->sendEscapeToGroupDragForTest());
    EXPECT_FALSE(cut.scrolling()) << "Esc stops the scroll with the drag";
    const float stopped = cut.panY();
    cut.tick(5);
    EXPECT_FLOAT_EQ(cut.panY(), stopped);
    handle->mouseUp(cut.atEdge(*handle, 0));

    // A group pressed with the pointer already inside the view's bottom edge zone stays put until the pointer moves
    // toward that edge.
    CutByTheView still(std::make_unique<OscillatorModule>(), 0.5f, rowOscillatorLayout());
    auto* stillHandle = still.editor->getGroupHandleForTest("unison");
    ASSERT_NE(stillHandle, nullptr);
    const auto stillPress = stillHandle->getTitleRowArea().getCentre().toFloat();
    const auto edgeY = still.edge.y;
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             still.edge.y = edgeY - 10;
             stillHandle->mouseDown(cardbody_test::mouseAt(*stillHandle, stillPress, juce::ModifierKeys(kLeft)));
             stillHandle->mouseDrag(cardbody_test::mouseAt(*stillHandle, stillPress + juce::Point<float>(0.0f, 12.0f),
                                                           juce::ModifierKeys(kLeft)));
             stillHandle->mouseDrag(still.atEdge(*stillHandle, kLeft));
         },
         [&] { still.tick(1); },
         [&] {
             still.edge.y = edgeY - 4;
             stillHandle->mouseDrag(still.atEdge(*stillHandle, kLeft));
         },
         [&] { return (double)still.panY(); }, [&] { stillHandle->mouseUp(still.atEdge(*stillHandle, 0)); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the canvas scrolling, and the group
    // stays in view.
    std::unique_ptr<CutByTheView> past;
    synth::ui::CardLayoutGroupHandle* pastHandle = nullptr;
    viewport_drag_follow::expectPastWindowFollows(
        {[&] {
             past = std::make_unique<CutByTheView>(std::make_unique<OscillatorModule>(), 0.5f, rowOscillatorLayout());
             pastHandle = past->editor->getGroupHandleForTest("unison");
             const auto at = pastHandle->getTitleRowArea().getCentre().toFloat();
             pastHandle->mouseDown(cardbody_test::mouseAt(*pastHandle, at, juce::ModifierKeys(kLeft)));
             past->pointerInside();
             pastHandle->mouseDrag(
                 cardbody_test::mouseAt(*pastHandle, at + juce::Point<float>(0.0f, 12.0f), juce::ModifierKeys(kLeft)));
         },
         [&](float px) {
             past->pointerPast(px);
             pastHandle->mouseDrag(past->atEdge(*pastHandle, kLeft));
         },
         [&] { past->tick(1); }, [&] { return (double)past->panY(); },
         [&] { return past->startsInView(past->editor->getGroupBlockForTest("unison")); },
         [&] { pastHandle->mouseUp(past->atEdge(*pastHandle, 0)); }});
}

TEST(OnCardEditorEdgeScroll, ARowFromTheAddPanelHeldAtTheViewEdgeScrollsAndItsGhostFollowsDownTheCard) {
    CutByTheView cut(std::make_unique<FilterModule>());
    ASSERT_NE(cut.editor, nullptr);
    hideThroughPanel(cut.rig, *cut.editor, "drive");
    auto* panel = openAddPanel(cut.rig, *cut.editor);
    ASSERT_NE(panel, nullptr);
    auto* row = panel->getRowForTest("drive");
    ASSERT_NE(row, nullptr);

    const auto from = row->localPointToGlobal(row->getLocalBounds().getCentre());
    const auto target = cut.graph().localPointToGlobal(cut.edge);
    row->mouseDown(eventAtScreen(*row, from, kLeft));
    cut.pointerInside(); // the drag arms with the pointer inside the view, then goes to the edge
    row->mouseDrag(eventAtScreen(*row, (from + target) / 2, kLeft));
    cut.pointerAtEdge();
    row->mouseDrag(eventAtScreen(*row, target, kLeft));
    const auto ghostBefore = cut.editor->getAddGhostForTest();
    ASSERT_FALSE(ghostBefore.isEmpty()) << "sanity: the pointer is over the card";
    ASSERT_TRUE(cut.scrolling()) << "the row drag armed the canvas edge scroll";

    const float before = cut.panY();
    cut.tick(3);
    EXPECT_LT(cut.panY(), before - 20.0f) << "the canvas scrolled down";
    EXPECT_GT(cut.editor->getAddGhostForTest().getY(), ghostBefore.getY())
        << "the ghost stayed under the still pointer, lower on the card";

    row->mouseUp(eventAtScreen(*row, target, 0));
    EXPECT_FALSE(cut.scrolling()) << "the release stops it";
    ASSERT_TRUE(cut.rig.storedLayout(cut.id).has_value());
    EXPECT_FALSE(cut.rig.storedLayout(cut.id)->hidden.contains("drive")) << "the drop added the control";

    // A row pressed with the pointer already inside the view's bottom edge zone stays put until the pointer moves
    // toward that edge.
    CutByTheView still(std::make_unique<FilterModule>());
    hideThroughPanel(still.rig, *still.editor, "drive");
    auto* stillPanel = openAddPanel(still.rig, *still.editor);
    ASSERT_NE(stillPanel, nullptr);
    auto* stillRow = stillPanel->getRowForTest("drive");
    ASSERT_NE(stillRow, nullptr);
    const auto rowFrom = stillRow->localPointToGlobal(stillRow->getLocalBounds().getCentre());
    const auto edgeY = still.edge.y;
    const auto at = [&](int y) {
        still.edge.y = y;
        return still.graph().localPointToGlobal(still.edge);
    };
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             stillRow->mouseDown(eventAtScreen(*stillRow, rowFrom, kLeft));
             const auto target = at(edgeY - 10);
             stillRow->mouseDrag(eventAtScreen(*stillRow, (rowFrom + target) / 2, kLeft));
             stillRow->mouseDrag(eventAtScreen(*stillRow, target, kLeft));
         },
         [&] { still.tick(1); }, [&] { stillRow->mouseDrag(eventAtScreen(*stillRow, at(edgeY - 4), kLeft)); },
         [&] { return (double)still.panY(); },
         [&] { stillRow->mouseUp(eventAtScreen(*stillRow, still.graph().localPointToGlobal(still.edge), 0)); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the canvas scrolling, and the ghost
    // stays in view.
    std::unique_ptr<CutByTheView> past;
    CardLayoutAddRow* pastRow = nullptr;
    viewport_drag_follow::expectPastWindowFollows(
        {[&] {
             past.reset(); // one popover at a time: the last run's Add panel must be gone before this one opens
             past = std::make_unique<CutByTheView>(std::make_unique<FilterModule>());
             hideThroughPanel(past->rig, *past->editor, "drive");
             pastRow = openAddPanel(past->rig, *past->editor)->getRowForTest("drive");
             const auto start = pastRow->localPointToGlobal(pastRow->getLocalBounds().getCentre());
             pastRow->mouseDown(eventAtScreen(*pastRow, start, kLeft));
             past->pointerInside();
             const auto inside = past->graph().localPointToGlobal(past->edge);
             pastRow->mouseDrag(eventAtScreen(*pastRow, inside, kLeft));
         },
         [&](float px) {
             past->pointerPast(px);
             pastRow->mouseDrag(eventAtScreen(*pastRow, past->graph().localPointToGlobal(past->edge), kLeft));
         },
         [&] { past->tick(1); }, [&] { return (double)past->panY(); },
         [&] {
             // The ghost sits in the section's own limits, which can put its top a little below the pointer kept at
             // the edge (the section's inset), never far past the view.
             const auto g = past->graph().getLocalArea(past->editor, past->editor->getAddGhostForTest());
             return g.getY() < past->graph().getHeight() + 16;
         },
         [&] { pastRow->mouseUp(eventAtScreen(*pastRow, past->graph().localPointToGlobal(past->edge), 0)); }});
}
