// The collapsed card's own '+' (add a port) and hover-ring affordances
// (docs/layout/macro-cards.md#direct-port-addremove-from-the-collapsed-card).
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
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
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

/** Hovers then presses the given card-local point, the same two-call sequence a real gesture produces. */
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

TEST(MacroCardPortButtons, AddButtonSitsAtTheStripFoot) {
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
    EXPECT_FLOAT_EQ(inBounds.getY(), card->getHeight() - 12.0f);
    EXPECT_FLOAT_EQ(outBounds.getY(), card->getHeight() - 12.0f);
    EXPECT_FLOAT_EQ(inBounds.getX(), 4.0f);
    EXPECT_FLOAT_EQ(outBounds.getX(), card->getWidth() - 12.0f);
}

// The '+' sits kMacroPortStripFooter below the last row, and the card grows with its port count,
// so it stays on-card and clear of every jack no matter how many ports a side holds.
TEST(MacroCardPortButtons, AddButtonStaysVisibleWithManyPortsAndNeverOverlapsAJack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    for (int i = 0; i < 12; ++i) {
        editor.getMacroController().addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                                 MacroPortShape::Mono, 1, "In " + juce::String(i));
        editor.getMacroController().addMacroPort(macroId, /*isInput=*/false, synth::MacroPortKind::AudioCV,
                                                 MacroPortShape::Mono, 1, "Out " + juce::String(i));
    }
    ASSERT_EQ(card->getHeight(), 244);

    const auto inBounds = card->getAddPortButtonBoundsForTest(true);
    const auto outBounds = card->getAddPortButtonBoundsForTest(false);
    EXPECT_TRUE(card->getLocalBounds().toFloat().contains(inBounds.getCentre()))
        << "the crowded input side's '+' must still be on-card";
    EXPECT_TRUE(card->getLocalBounds().toFloat().contains(outBounds.getCentre()));

    for (const auto& port : editor.getMacroController().macroCardPortLayout(macroId)) {
        const auto jack = port.jackPos.toFloat();
        EXPECT_GE(jack.getDistanceFrom(inBounds.getCentre()), 10.0f) << "the '+' must clear every jack: " << port.name;
        EXPECT_GE(jack.getDistanceFrom(outBounds.getCentre()), 10.0f) << "the '+' must clear every jack: " << port.name;
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
// The hover ring on an existing port jack (a click opens its panel: PortPanelMacroCardTests.cpp).
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

// A press on a port dot, hovered or not, never deletes it (the panel's Delete port does that); it falls
// through to the card's ordinary click handling, which arms the body drag.
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
    card->mouseUp(makeMouseEvent(*card, layout[0].jackPos.toFloat()));

    hoverThenClick(*card, layout[0].jackPos.toFloat());
    EXPECT_FALSE(nodeIdForUuid(engine, portUuid).uid == 0) << "nor does a hovered press delete it any more";
    EXPECT_TRUE(card->isBodyDragActive());
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
// actually drew rather than an all-zero GPU-backed image on a headless CI runner (the
// same reasoning ModuleComponentPaintTests.cpp's WavetableCardPaintsAndTicksWithoutCrashing gives).
// ============================================================================

TEST(MacroCardPortButtons, HoveredJackPaintsARingAndNoCross) {
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

    EXPECT_EQ(unhovered.getPixelAt(jackPos.x, jackPos.y), hovered.getPixelAt(jackPos.x, jackPos.y))
        << "the dot itself is untouched: no x drawn over it (FRO769)";
    EXPECT_NE(unhovered.getPixelAt(jackPos.x + 7, jackPos.y), hovered.getPixelAt(jackPos.x + 7, jackPos.y))
        << "the hovered jack gets a ring round the dot";

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

// ============================================================================
// Sidebar rows: '+' and a jack clicked at their new positions on a taller card, the '-' button, and
// names hidden by zoom.
// ============================================================================

namespace {
void addInputs(GraphEditor& editor, const juce::String& macroId, int n) {
    for (int i = 0; i < n; ++i)
        editor.getMacroController().addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                                 MacroPortShape::Mono, 1, "In " + juce::String(i));
}
} // namespace

TEST(MacroCardPortButtons, ClickingAddOnACardThatAlreadyHasThreeInputsStillReachesTheButton) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    addInputs(editor, macroId, 3);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    ASSERT_EQ(card->getHeight(), 100);

    juce::PopupMenu captured;
    card->setShowContextMenuHookForTest([&captured](juce::PopupMenu& m) { captured = m; });

    hoverThenClick(*card, card->getAddPortButtonBoundsForTest(true).getCentre());
    ASSERT_TRUE(runMenuItem(captured, "Audio/CV - Mono"));

    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 4u);
    undo.undo();
    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 3u) << "one undo step takes the whole add back";
    card->setShowContextMenuHookForTest(nullptr);
}

TEST(MacroCardPortButtons, MinusRemovesTheBottomPortOnThatSideAsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    addInputs(editor, macroId, 3);
    editor.getMacroController().addMacroPort(macroId, /*isInput=*/false, synth::MacroPortKind::AudioCV,
                                             MacroPortShape::Mono, 1, "Out");
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    const auto minus = card->getRemovePortButtonBoundsForTest(true);
    ASSERT_FALSE(minus.isEmpty());
    EXPECT_GT(minus.getX(), card->getAddPortButtonBoundsForTest(true).getRight()) << "beside the '+', not on it";

    card->mouseDown(makeMouseEvent(*card, minus.getCentre()));

    auto* macro = editor.getMacros().find(macroId);
    int inputs = 0, outputs = 0;
    for (const auto& p : macro->ports) {
        (p.isInput ? inputs : outputs)++;
        EXPECT_NE(p.name, "In 2") << "the bottom input is the one removed";
    }
    EXPECT_EQ(inputs, 2);
    EXPECT_EQ(outputs, 1) << "the other side is untouched";

    undo.undo();
    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 4u) << "one undo step restores the removed port";
}

TEST(MacroCardPortButtons, MinusIsAbsentForASideWithNoPortAndWhenNamesAreHidden) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    addInputs(editor, macroId, 1);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    auto* content = card->getParentComponent();
    ASSERT_NE(content, nullptr);

    EXPECT_FALSE(card->getRemovePortButtonBoundsForTest(true).isEmpty());
    EXPECT_TRUE(card->getRemovePortButtonBoundsForTest(false).isEmpty()) << "no output port to remove";

    content->setTransform(juce::AffineTransform::scale(0.4f));
    EXPECT_TRUE(card->getRemovePortButtonBoundsForTest(true).isEmpty()) << "hidden below 50 percent zoom";
    EXPECT_FALSE(card->getAddPortButtonBoundsForTest(true).isEmpty()) << "the '+' never hides";
    content->setTransform({});
}

TEST(MacroCardPortButtons, NamesAreNotPaintedBelowTheThresholdZoomAndTheTooltipCarriesTheName) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    addInputs(editor, macroId, 1);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    auto* content = card->getParentComponent();
    ASSERT_NE(content, nullptr);
    const auto port = editor.getMacroController().macroCardPortLayout(macroId).front();

    synth::theme::AppLookAndFeel lf;
    card->setLookAndFeel(&lf);
    auto renderCard = [&]() {
        juce::Image img(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(img);
        card->paint(g);
        return img;
    };
    // True when every pixel in the label area is the strip's own fill (nothing drawn over it).
    auto labelAreaIsBlank = [&](const juce::Image& img) {
        // Stops 2px short of the label area's inner edge, where the strip's 1px border line is drawn.
        const auto ref = img.getPixelAt(port.labelArea.getX(), port.labelArea.getBottom() - 1);
        for (int y = port.labelArea.getY(); y < port.labelArea.getBottom(); ++y)
            for (int x = port.labelArea.getX(); x < port.labelArea.getRight() - 2; ++x)
                if (img.getPixelAt(x, y) != ref)
                    return false;
        return true;
    };

    EXPECT_FALSE(labelAreaIsBlank(renderCard())) << "the name is painted at 100 percent zoom";

    content->setTransform(juce::AffineTransform::scale(0.4f));
    EXPECT_TRUE(labelAreaIsBlank(renderCard())) << "no name below the threshold";

    card->mouseMove(makeMouseEvent(*card, port.jackPos.toFloat()));
    EXPECT_EQ(card->getTooltip(), "In 0") << "the hovered jack's tooltip is its name while names are hidden";
    card->mouseExit(makeMouseEvent(*card, port.jackPos.toFloat()));
    EXPECT_NE(card->getTooltip(), "In 0") << "unhovered: the member list again";

    content->setTransform({});
    card->setLookAndFeel(nullptr);
}
