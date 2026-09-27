// FRO24 (docs/layout/macro-cards.md#direct-port-addremove-from-the-collapsed-card): the collapsed
// card's own '+' (add a port) and hover-'x' (delete a port) affordances.
//
// Driven through the REAL mouse path — synthesised juce::MouseEvents into the actual
// MacroCardComponent::mouseMove/mouseDown, the same methods a real hover-then-click gesture calls —
// per memory/test-the-real-mouse-path-for-ui-gestures, the same convention MacroCardJackTests.cpp
// already documents for this card's other jack hit-testing.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

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

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

bool hasConnection(AudioEngine& engine, NodeID srcId, int srcCh, NodeID dstId, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == srcId && c.source.channelIndex == srcCh && c.destination.nodeID == dstId &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

/** Groups two fresh Oscillator/Filter modules into a new collapsed macro (the min-2 rule). */
juce::String makeTwoMemberMacro(GraphEditor& editor, AudioEngine& engine) {
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    return editor.getMacroController().groupSelectionIntoMacro();
}

juce::MouseEvent makeMouseEvent(juce::Component& comp, juce::Point<float> position, bool mouseWasDragged = false,
                                juce::Point<float> mouseDownPos = {}) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos, juce::Time::getCurrentTime(), 1,
                            mouseWasDragged);
}

/** Hovers then presses the given card-local point, the same two-call sequence a real gesture
 *  produces (mouseMove arms hoveredPortUuid_, mouseDown re-checks the same hit-test before
 *  acting) — a bare mouseDown with no preceding mouseMove is exercised separately below. */
void hoverThenClick(MacroCardComponent& card, juce::Point<float> pos) {
    card.mouseMove(makeMouseEvent(card, pos));
    card.mouseDown(makeMouseEvent(card, pos));
}

/** Runs a PopupMenu action by its visible text, the same juce::PopupMenu::MenuItemIterator idiom
 *  Tests/Macros/MacroPortFlow/MacroPortFlowEditTests.cpp's RightClickDeletePortDropsByDefault
 *  already uses for a menu this codebase never opens as a real popup in a headless test. */
bool runMenuItem(juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        if (it.getItem().text == text) {
            it.getItem().action();
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================
// The '+' affordance: geometry, hiding gracefully when a side is crowded, and the add-port menu.
// ============================================================================

TEST(MacroCardPortButtons, AddButtonSitsInTheFooterBesideTheCountText) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    const auto inBounds = card->getAddPortButtonBoundsForTest(true);
    const auto outBounds = card->getAddPortButtonBoundsForTest(false);
    EXPECT_LT(inBounds.getCentreX(), outBounds.getCentreX()) << "input '+' on the left, output '+' on the right";
    EXPECT_TRUE(card->getLocalBounds().toFloat().contains(inBounds.getCentre()));
    EXPECT_TRUE(card->getLocalBounds().toFloat().contains(outBounds.getCentre()));
    // FRO24 follow-up: pinned to the footer row, not the jack band this card's own jacks lay out
    // in — its top edge must sit at or below the jack band's own bottom edge, structurally, not
    // just "usually clear of it".
    EXPECT_GE(inBounds.getY(), card->getHeight() - 16.0f) << "off the jack band (kMacroCardJackBandBottom)";
    EXPECT_GE(outBounds.getY(), card->getHeight() - 16.0f);
}

// FRO24 follow-up (founder in-app review): the first cut hid the '+' as soon as a side had 2+
// ports (it sat at the TOP of the jack band, which macroCardPortLayout()'s even-spacing pushes
// the topmost jack toward as the count grows) — usable only on an almost-empty side. The footer
// placement has no such failure mode: it must stay visible and off the jack band no matter how
// many ports are on a side.
TEST(MacroCardPortButtons, AddButtonStaysVisibleWithManyPortsAndNeverOverlapsAJack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    for (int i = 0; i < 6; ++i)
        editor.getMacroController().addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                                 MacroPortShape::Mono, 1, "In " + juce::String(i));
    editor.getMacroController().addMacroPort(macroId, /*isInput=*/false, synth::MacroPortKind::AudioCV,
                                             MacroPortShape::Mono, 1, "Out");

    const auto inBounds = card->getAddPortButtonBoundsForTest(true);
    const auto outBounds = card->getAddPortButtonBoundsForTest(false);
    EXPECT_TRUE(card->getLocalBounds().toFloat().contains(inBounds.getCentre()))
        << "the crowded input side's '+' must still be on-card";
    EXPECT_TRUE(card->getLocalBounds().toFloat().contains(outBounds.getCentre()));

    for (const auto& port : editor.getMacroController().macroCardPortLayout(macroId)) {
        const auto dot =
            juce::Rectangle<float>((float)port.jackPos.x - 5.0f, (float)port.jackPos.y - 5.0f, 10.0f, 10.0f);
        EXPECT_FALSE(inBounds.intersects(dot)) << "the '+' must never overlap a jack: " << port.nodeUuid;
        EXPECT_FALSE(outBounds.intersects(dot)) << "the '+' must never overlap a jack: " << port.nodeUuid;
    }
}

TEST(MacroCardPortButtons, AddPortMenuOffersTheSameKindShapeChoicesConfigureIOsAddPanelDoes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    auto menu = card->buildAddPortMenu(true);
    // docs/macros/configure-io.md#adding-a-port: "Mono, Stereo and Poly-N are all reachable
    // here" (Audio/CV) plus MIDI, the same four choices MacroPortConfigDialogLifecycle.cpp's
    // newKindBox_/newShapeBox_ combos offer.
    for (const auto* expected : {"Audio/CV - Mono", "Audio/CV - Stereo", "Audio/CV - Poly-N", "MIDI"}) {
        juce::PopupMenu::MenuItemIterator it(menu);
        bool found = false;
        while (it.next())
            if (it.getItem().text == expected)
                found = true;
        EXPECT_TRUE(found) << expected;
    }
}

TEST(MacroCardPortButtons, ClickingAddThenChoosingMonoAddsAnInputPortOnTheClickedSideAsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const int nodesBefore = engine.getGraph().getNodes().size();

    juce::PopupMenu captured;
    card->setShowContextMenuHookForTest([&captured](juce::PopupMenu& m) { captured = m; });

    // The real mouse path: a click landing exactly on the input side's '+' bounds.
    const auto pos = card->getAddPortButtonBoundsForTest(true).getCentre();
    card->mouseDown(makeMouseEvent(*card, pos));

    ASSERT_TRUE(runMenuItem(captured, "Audio/CV - Mono"));

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_TRUE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty());
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "undo removes the whole one-step transaction";

    card->setShowContextMenuHookForTest(nullptr);
}

TEST(MacroCardPortButtons, ClickingAddOnTheOutputSideAddsAnOutputPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    juce::PopupMenu captured;
    card->setShowContextMenuHookForTest([&captured](juce::PopupMenu& m) { captured = m; });

    const auto pos = card->getAddPortButtonBoundsForTest(false).getCentre();
    card->mouseDown(makeMouseEvent(*card, pos));
    ASSERT_TRUE(runMenuItem(captured, "MIDI"));

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::Midi);

    card->setShowContextMenuHookForTest(nullptr);
}

// A click that lands nowhere near either '+' (or the 'x', with no jack hovered) must keep
// arming the ordinary card body drag — the '+'/'x' additions must not swallow every mouseDown.
TEST(MacroCardPortButtons, ClickingElsewhereOnTheCardStillArmsTheBodyDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    card->mouseDown(makeMouseEvent(*card, card->getLocalBounds().getCentre().toFloat()));
    EXPECT_TRUE(card->isBodyDragActive());
}

// ============================================================================
// The hover-'x' affordance on an existing port jack.
// ============================================================================

TEST(MacroCardPortButtons, HoveringAJackArmsItsHoverStateAndLeavingClearsIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    const auto layout = editor.getMacroController().macroCardPortLayout(macroId);
    ASSERT_EQ(layout.size(), 1u);

    EXPECT_TRUE(card->getHoveredPortUuidForTest().isEmpty());
    card->mouseMove(makeMouseEvent(*card, layout[0].jackPos.toFloat()));
    EXPECT_EQ(card->getHoveredPortUuidForTest(), portUuid);

    card->mouseExit(makeMouseEvent(*card, layout[0].jackPos.toFloat()));
    EXPECT_TRUE(card->getHoveredPortUuidForTest().isEmpty());
}

TEST(MacroCardPortButtons, ClickingAHoveredJacksXDeletesItAndDropsTheCableByDefaultAsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(editor.getSpliceCableOnMacroPortDeleteEnabled()) << "off by default (FRO235)";

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portNodeId = nodeIdForUuid(engine, portUuid);
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    const auto memberNodeId = nodeIdForUuid(engine, macro->members[0]);

    auto extId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    engine.getGraph().addConnection({{extId, 0}, {portNodeId, 0}});
    editor.connectPorts(portNodeId, 0, memberNodeId, 0, /*isMidi=*/false, /*recordUndo=*/false);
    ASSERT_TRUE(hasConnection(engine, extId, 0, portNodeId, 0));

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto layout = editor.getMacroController().macroCardPortLayout(macroId);
    ASSERT_EQ(layout.size(), 1u);
    const auto jackPos = layout[0].jackPos.toFloat();

    hoverThenClick(*card, jackPos);

    EXPECT_TRUE(nodeIdForUuid(engine, portUuid).uid == 0) << "the port node is gone";
    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty());
    EXPECT_FALSE(hasConnection(engine, extId, 0, memberNodeId, 0)) << "default: dropped, not spliced (FRO235)";
    EXPECT_FALSE(card->isBodyDragActive()) << "the 'x' click must not also arm a card body drag";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    ASSERT_FALSE(nodeIdForUuid(engine, portUuid).uid == 0) << "undo restores the deleted port node";
    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
}

// FRO24 follow-up (founder in-app review): deleting a port via its 'x' reflows
// macroCardPortLayout() for the survivors, so the NEXT jack can slide under the still-resting
// cursor and land within its own hit radius — a quick double-click then deleted two ports, one
// per click. A mouseMove reporting the SAME position as the delete click (JUCE can dispatch one
// as part of the click plumbing itself even with no real cursor movement) must not re-arm hover
// on whatever port the reflow just moved there; only a mouseMove at a genuinely different
// position may.
TEST(MacroCardPortButtons, DeletingAPortSuppressesHoverAtThatSpotSoADoubleClickCannotDeleteTheNextPortToo) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto uuidA = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "A");
    const auto uuidB = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "B");
    ASSERT_FALSE(uuidA.isEmpty());
    ASSERT_FALSE(uuidB.isEmpty());

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    auto layoutBefore = editor.getMacroController().macroCardPortLayout(macroId);
    ASSERT_EQ(layoutBefore.size(), 2u);
    ASSERT_EQ(layoutBefore[0].nodeUuid, uuidA); // topmost, added first
    const auto posA = layoutBefore[0].jackPos.toFloat();

    // Delete A at posA, exactly as HoveringAJack.../ClickingAHoveredJacksX... does above.
    hoverThenClick(*card, posA);
    ASSERT_TRUE(nodeIdForUuid(engine, uuidA).uid == 0) << "A is gone";
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "only B remains";

    // B alone now reflows to a DIFFERENT y within kMacroCardJackHitRadius (10px) of posA's old
    // spot — close enough that a hit-test at posA would still land on B, which is exactly what
    // the bug exploited. Confirm the fixture actually reproduces that precondition rather than
    // trivially passing because the reflowed jack moved out of reach.
    const auto layoutAfter = editor.getMacroController().macroCardPortLayout(macroId);
    ASSERT_EQ(layoutAfter.size(), 1u);
    ASSERT_EQ(layoutAfter[0].nodeUuid, uuidB);
    ASSERT_LT(std::abs(layoutAfter[0].jackPos.y - (int)posA.y), 10)
        << "fixture precondition: B's reflowed jack must sit within the old click's hit radius";

    // A mouseMove reporting the SAME position as the delete click — simulating the double-click's
    // own plumbing landing on that exact pixel with no real movement — must not re-arm hover on B.
    card->mouseMove(makeMouseEvent(*card, posA));
    EXPECT_TRUE(card->getHoveredPortUuidForTest().isEmpty()) << "hover stays suppressed at the delete position";

    // The second click of the double-click, still at posA: must NOT delete B.
    card->mouseDown(makeMouseEvent(*card, posA));
    EXPECT_FALSE(nodeIdForUuid(engine, uuidB).uid == 0) << "B must survive the second click of the double-click";
    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);

    // A mouseMove to a genuinely different position clears the suppression, and moving back onto
    // B's real jack now arms hover normally — the fix suppresses ONE stale re-arm, not hovering
    // forever.
    card->mouseMove(makeMouseEvent(*card, juce::Point<float>(posA.x, posA.y + 30.0f)));
    card->mouseMove(makeMouseEvent(*card, layoutAfter[0].jackPos.toFloat()));
    EXPECT_EQ(card->getHoveredPortUuidForTest(), uuidB) << "hover resumes normally once the mouse really moves";
}

// A mouseDown with no preceding mouseMove — a test driving the handler directly, or (in principle)
// any input path that never fires a hover — must fall through to the card's ordinary click
// handling rather than deleting a jack it was never shown hovering.
TEST(MacroCardPortButtons, MouseDownOnAJackWithNoPriorHoverDoesNotDeleteIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto layout = editor.getMacroController().macroCardPortLayout(macroId);
    ASSERT_EQ(layout.size(), 1u);

    card->mouseDown(makeMouseEvent(*card, layout[0].jackPos.toFloat()));

    EXPECT_FALSE(nodeIdForUuid(engine, portUuid).uid == 0) << "the port must survive an un-hovered click";
    EXPECT_TRUE(card->isBodyDragActive()) << "falls through to the ordinary card drag-arm, unchanged";
}

// A real cable drop onto the port's jack (the drop path already covered end-to-end by
// MacroCardJackTests.cpp) must still land correctly once this card also tracks hover state —
// hovering never diverts the drop-target hit-test macroCardPortForPoint() itself.
TEST(MacroCardPortButtons, HoveringDoesNotBreakARealCableDropOntoThePort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Pitch In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portNodeId = nodeIdForUuid(engine, portUuid);

    auto extOscId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    ModuleComponent* extComp = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == extOscId)
            extComp = c;
    ASSERT_NE(extComp, nullptr);

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto layout = editor.getMacroController().macroCardPortLayout(macroId);
    ASSERT_EQ(layout.size(), 1u);

    // Hover the card's own jack first (as a real mouse pass over the canvas would, on its way to
    // the external module) before the drag itself begins elsewhere.
    card->mouseMove(makeMouseEvent(*card, layout[0].jackPos.toFloat()));
    EXPECT_EQ(card->getHoveredPortUuidForTest(), portUuid);

    const auto targetScreen = card->localPointToGlobal(layout[0].jackPos.toFloat());
    const auto pressPos = extComp->getPortCenter(0, /*isInput=*/false).toFloat();
    extComp->mouseDown(makeMouseEvent(*extComp, pressPos, false, pressPos));
    const auto releaseLocal = extComp->getLocalPoint(nullptr, targetScreen);
    extComp->mouseUp(makeMouseEvent(*extComp, releaseLocal, true, pressPos));

    EXPECT_TRUE(hasConnection(engine, extOscId, 0, portNodeId, 0));
    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "the drop must not also delete the port";
}

// ============================================================================
// PNG render smoke test (docs/development/testing.md's createComponentSnapshot pattern):
// juce::SoftwareImageType() forces a software-backed bitmap so getPixelAt() reads what paint()
// actually drew rather than an all-zero GPU-backed image on a headless CI runner (FRO242, the
// same reasoning ModuleComponentPaintTests.cpp's WavetableCardPaintsAndTicksWithoutCrashing gives).
// ============================================================================

TEST(MacroCardPortButtons, HoveredJackPaintsVisiblyDifferentlyFromUnhovered) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    editor.getMacroController().addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                             MacroPortShape::Mono, 1, "In");
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto layout = editor.getMacroController().macroCardPortLayout(macroId);
    ASSERT_EQ(layout.size(), 1u);
    const auto jackPos = layout[0].jackPos;

    synth::theme::AppLookAndFeel lf;
    card->setLookAndFeel(&lf);

    auto renderCard = [&]() {
        juce::Image img(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(img);
        card->paint(g);
        return img;
    };

    auto unhovered = renderCard();
    ASSERT_TRUE(unhovered.isValid());

    card->mouseMove(makeMouseEvent(*card, jackPos.toFloat()));
    ASSERT_FALSE(card->getHoveredPortUuidForTest().isEmpty());
    auto hovered = renderCard();
    ASSERT_TRUE(hovered.isValid());

    EXPECT_NE(unhovered.getPixelAt(jackPos.x, jackPos.y), hovered.getPixelAt(jackPos.x, jackPos.y))
        << "the hovered jack must repaint with its 'x' overlay, not the plain dot";

    card->setLookAndFeel(nullptr);
}

TEST(MacroCardPortButtons, AddButtonRegionPaintsVisibleContent) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    synth::theme::AppLookAndFeel lf;
    card->setLookAndFeel(&lf);

    juce::Image img(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(img);
    card->paint(g);
    ASSERT_TRUE(img.isValid());

    const auto centre = card->getAddPortButtonBoundsForTest(true).getCentre();
    const auto bgPixel = img.getPixelAt(2, 2); // top-left corner, outside every affordance
    // The '+' is drawn as an unfilled ellipse plus a cross of thin lines — its own centre pixel
    // can legitimately land in the gap between the two crossing strokes, so this samples the
    // ellipse's left edge instead, which drawEllipse always paints.
    const auto edgePixel = img.getPixelAt((int)card->getAddPortButtonBoundsForTest(true).getX(), (int)centre.y);
    EXPECT_NE(edgePixel, bgPixel);

    card->setLookAndFeel(nullptr);
}
