// PortPanelMacroCardTests.cpp: a plain click on a port of a COLLAPSED macro card opens the port connections panel for
// that port's node, listing the cables drawn to the card. The real mouse path on MacroCardComponent (mouseMove /
// mouseDown / mouseUp), the panel captured headlessly by the controller's launcher.
// docs/layout/cables.md#port-connections-panel.

#include "PortPanelTestFixture.h"

#include "Modules/FilterModule.h"
#include "UI/Graph/PortPanel/PortTargetList.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::ui::PortTarget;

// A collapsed macro (oscillator + filter) with one inlet "Pitch In" wired from an external oscillator, plus an outlet
// "Out" wired to an external filter, all on a real GraphEditor with an undo manager.
struct MacroCardPortFixture {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::String macroId, inletUuid, outletUuid;
    NodeID inlet, outlet, extOsc, extOsc2, extSink;
    std::unique_ptr<juce::Component> held;
    juce::Rectangle<int> launchedAt;
    int launches = 0;

    NodeID addAt(std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", x);
        node->properties.set("y", y);
        node->properties.set("uuid", juce::Uuid().toDashedString());
        editor->updateComponents();
        return node->nodeID;
    }
    NodeID nodeFor(const juce::String& uuid) {
        for (auto* node : engine.getGraph().getNodes())
            if (node->properties["uuid"].toString() == uuid)
                return node->nodeID;
        return {};
    }

    explicit MacroCardPortFixture(int inletCables = 1) {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        editor->setSize(1600, 1200);
        const auto a = addAt(std::make_unique<OscillatorModule>(), 100, 100);
        const auto b = addAt(std::make_unique<FilterModule>(), 500, 100);
        editor->setSelectedNodes({a, b});
        macroId = editor->getMacroController().groupSelectionIntoMacro();
        auto& macros = editor->getMacroController();
        inletUuid =
            macros.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Pitch In");
        outletUuid = macros.addMacroPort(macroId, false, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Out");
        inlet = nodeFor(inletUuid);
        outlet = nodeFor(outletUuid);
        extOsc = addAt(std::make_unique<OscillatorModule>(), 900, 900);
        extOsc2 = addAt(std::make_unique<OscillatorModule>(), 900, 1050);
        extSink = addAt(std::make_unique<FilterModule>(), 900, 600);
        editor->connectPorts(extOsc, 0, inlet, 0, false, false);
        if (inletCables > 1)
            editor->connectPorts(extOsc2, 0, inlet, 0, false, false);
        editor->connectPorts(outlet, 0, extSink, 0, false, false);
        editor->getPortPanel().panelLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&,
                                                      juce::Rectangle<int> screenAnchor) {
            held = std::move(content);
            launchedAt = screenAnchor;
            ++launches;
        };
        editor->timerCallback();
    }
    ~MacroCardPortFixture() {
        held.reset();
        editor.reset();
        engine.shutdown();
    }

    MacroCardComponent* card() { return editor->getMacroController().getMacroCardForTest(macroId); }
    synth::ui::PortPanelController& controller() { return editor->getPortPanel(); }
    PortConnectionsPanel* panel() { return controller().getPanel(); }
    GraphEditor::MacroCardPort dot(bool isInput) {
        for (const auto& p : editor->getMacroController().macroCardPortLayout(macroId))
            if (p.isInput == isInput)
                return p;
        return {};
    }
    // A press and release on the same spot, with no mouseMove before it (so no hover is armed).
    void click(juce::Point<int> p, int clicks = 1) {
        card()->mouseDown(portEvent(*card(), p, p, {}, clicks));
        card()->mouseUp(portEvent(*card(), p, p, {}, clicks));
    }
    // A click that opens the panel: with one cable and the double-click-disconnect preference the open waits out the
    // double-click interval, so the test runs that wait (asserting it was queued, not shown).
    void clickAndOpen(juce::Point<int> p) {
        click(p);
        if (panel() == nullptr && controller().hasPendingOpenForTest()) {
            EXPECT_EQ(launches, 0) << "nothing shows before the double-click interval is out";
            controller().firePendingOpenForTest();
        }
    }
    bool connected(NodeID src, NodeID dst) { return countAudioConnectionsBetween(engine.getGraph(), src, dst) > 0; }
    juce::String titleOf(NodeID id) {
        auto* node = engine.getGraph().getNodeForId(id);
        return editor->getModuleTitle(id, node->getProcessor());
    }
    juce::String outLabel(NodeID id) {
        return dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(id)->getProcessor())->getOutputPortLabel(0);
    }
};

} // namespace

TEST(PortPanelMacroCard, ClickingACollapsedCardsInletOpensAPanelListingTheOutsideCable) {
    MacroCardPortFixture f;
    ASSERT_TRUE(f.editor->getMacros().find(f.macroId)->collapsed);
    ASSERT_NE(f.card(), nullptr);
    const auto jack = f.dot(true).jackPos;

    f.clickAndOpen(jack);

    ASSERT_EQ(f.launches, 1);
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->titleText(), "Pitch In (1 connection)") << "the panel names the macro port";
    ASSERT_EQ(panel->rowCount(), 1);
    EXPECT_EQ(panel->rowAt(0)->connection().label,
              f.titleOf(f.extOsc) + synth::ui::portSeparator() + f.outLabel(f.extOsc));
    EXPECT_TRUE(f.launchedAt.contains(f.card()->localPointToGlobal(jack))) << "anchored on the card's port dot";
    EXPECT_LT(f.launchedAt.getWidth(), 30) << "the dot, not the card";
}

TEST(PortPanelMacroCard, AnOutletListsTheCablesLeavingTheCardAndASecondClickCloses) {
    MacroCardPortFixture f;
    f.clickAndOpen(f.dot(false).jackPos);
    ASSERT_NE(f.panel(), nullptr);
    EXPECT_EQ(f.panel()->titleText(), "Out (1 connection)");
    ASSERT_EQ(f.panel()->rowCount(), 1);
    EXPECT_TRUE(f.panel()->rowAt(0)->connection().label.startsWith(f.titleOf(f.extSink)));

    f.click(f.dot(false).jackPos);
    EXPECT_EQ(f.panel(), nullptr) << "a second click on the same dot closes it";
}

TEST(PortPanelMacroCard, DraggingFromTheBodyOrFromAPortStillMovesTheCardAndOpensNothing) {
    MacroCardPortFixture f;
    auto* card = f.card();
    for (const auto from : {juce::Point<int>(card->getWidth() / 2, card->getHeight() / 2), f.dot(true).jackPos}) {
        const auto before = card->getPosition();
        const auto to = from + juce::Point<int>(40, 25);
        card->mouseDown(portEvent(*card, from, from));
        card->mouseDrag(portEvent(*card, to, from));
        card->mouseUp(portEvent(*card, to, from));
        EXPECT_NE(card->getPosition(), before) << "the card was dragged, as before";
        EXPECT_GT(card->getPosition().getX(), before.getX() + 30);
        EXPECT_EQ(f.launches, 0);
        EXPECT_EQ(f.panel(), nullptr);
    }
}

TEST(PortPanelMacroCard, HoverThenClickOnADotOpensThePanelAndDeletesNothing) {
    MacroCardPortFixture f;
    const auto jack = f.dot(true).jackPos;
    f.card()->mouseMove(portEvent(*f.card(), jack, jack));
    ASSERT_EQ(f.card()->getHoveredPortUuidForTest(), f.inletUuid);

    f.clickAndOpen(jack);

    ASSERT_NE(f.panel(), nullptr) << "a real click on a hovered dot reaches the panel";
    EXPECT_EQ(f.panel()->port().node, f.inlet);
    EXPECT_FALSE(f.nodeFor(f.inletUuid).uid == 0) << "the press no longer deletes the port";
    EXPECT_EQ(f.editor->getMacros().find(f.macroId)->ports.size(), 2u);
}

TEST(PortPanelMacroCard, DeletePortInThePanelDeletesThePortInOneUndoStepAndClosesThePanel) {
    MacroCardPortFixture f;
    f.clickAndOpen(f.dot(true).jackPos);
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(panel->isDeletePortShown());
    ASSERT_TRUE(f.connected(f.extOsc, f.inlet));
    bool dismissed = false;
    panel->onDismiss = [&dismissed] { dismissed = true; };

    clickNow(panel->deletePortButton());

    EXPECT_TRUE(f.nodeFor(f.inletUuid).uid == 0) << "the port node is gone";
    for (const auto& port : f.editor->getMacros().find(f.macroId)->ports)
        EXPECT_NE(port.nodeUuid, f.inletUuid);
    EXPECT_EQ(f.editor->getMacros().find(f.macroId)->ports.size(), 1u) << "the outlet is untouched";
    EXPECT_TRUE(dismissed) << "the panel closes";

    f.editor->finishCableRetractForTest();
    ASSERT_TRUE(f.undo.undo()) << "one Cmd+Z";
    EXPECT_FALSE(f.nodeFor(f.inletUuid).uid == 0) << "restores the port node";
    EXPECT_EQ(f.editor->getMacros().find(f.macroId)->ports.size(), 2u);
}

TEST(PortPanelMacroCard, DeletePortHasATitleATooltipAndSitsLastInTheTabOrder) {
    MacroCardPortFixture f(2);
    f.click(f.dot(true).jackPos);
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    auto& button = panel->deletePortButton();
    EXPECT_EQ(button.getTitle(), "Delete port");
    EXPECT_EQ(button.getTooltip(), "Delete this macro port");
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    ASSERT_TRUE(panel->isDisconnectAllShown());
    auto order = panel->createFocusTraverser()->getAllComponents(panel);
    const auto at = [&order](juce::Component* c) { return std::find(order.begin(), order.end(), c) - order.begin(); };
    ASSERT_LT(at(&button), (std::ptrdiff_t)order.size()) << "reachable with Tab";
    EXPECT_GT(at(&button), at(&panel->disconnectAllButton())) << "after Disconnect all";
    EXPECT_GT(at(&button), at(&panel->addConnectionButton()));
}

TEST(PortPanelMacroCard, AnOrdinaryJacksPanelHasNoDeletePort) {
    MacroCardPortFixture f;
    ModuleComponent* ext = nullptr;
    for (auto* c : f.editor->getModuleComponents())
        if (c != nullptr && c->getNodeId() == f.extOsc)
            ext = c;
    ASSERT_NE(ext, nullptr);
    f.controller().open(*ext, {}, synth::ui::PortRef{f.extOsc, 0, false, false});
    ASSERT_NE(f.panel(), nullptr);
    EXPECT_FALSE(f.panel()->isDeletePortShown());
}

TEST(PortPanelMacroCard, TheHoveredDotPaintsNoCrossOverIt) {
    MacroCardPortFixture f;
    synth::theme::AppLookAndFeel lf;
    f.card()->setLookAndFeel(&lf);
    const auto jack = f.dot(true).jackPos;
    auto render = [&] {
        juce::Image img(juce::Image::ARGB, f.card()->getWidth(), f.card()->getHeight(), true,
                        juce::SoftwareImageType());
        juce::Graphics g(img);
        f.card()->paint(g);
        return img;
    };
    const auto before = render();
    f.card()->mouseMove(portEvent(*f.card(), jack, jack));
    const auto after = render();
    EXPECT_EQ(before.getPixelAt(jack.x, jack.y), after.getPixelAt(jack.x, jack.y)) << "no x over the dot";
    EXPECT_NE(before.getPixelAt(jack.x + 7, jack.y), after.getPixelAt(jack.x + 7, jack.y)) << "a ring instead";
    f.card()->setLookAndFeel(nullptr);
}

TEST(PortPanelMacroCard, ADoubleClickOnTheOnlyCablesPortNeverFlashesThePanelAndStillExpands) {
    MacroCardPortFixture f;
    const auto jack = f.dot(true).jackPos;
    f.click(jack); // the first click of the double-click
    EXPECT_TRUE(f.controller().hasPendingOpenForTest());
    EXPECT_EQ(f.launches, 0);
    f.card()->mouseDoubleClick(portEvent(*f.card(), jack, jack, {}, 2));
    EXPECT_FALSE(f.controller().hasPendingOpenForTest()) << "the deferred open was dropped";
    EXPECT_FALSE(f.editor->getMacros().find(f.macroId)->collapsed);
    EXPECT_EQ(f.launches, 0);
}

TEST(PortPanelMacroCard, ModifiedOrDoubleClicksDoNotOpenThePanel) {
    MacroCardPortFixture f;
    const auto jack = f.dot(true).jackPos;
    f.card()->mouseDown(portEvent(*f.card(), jack, jack, juce::ModifierKeys::shiftModifier));
    f.card()->mouseUp(portEvent(*f.card(), jack, jack, juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(f.launches, 0) << "shift selects the macro, as before";

    f.clickAndOpen(jack);
    ASSERT_NE(f.panel(), nullptr);
    f.card()->mouseDoubleClick(portEvent(*f.card(), jack, jack, {}, 2));
    EXPECT_FALSE(f.editor->getMacros().find(f.macroId)->collapsed) << "a double-click still expands the macro";
    EXPECT_EQ(f.panel(), nullptr) << "and takes the card's own panel with it";
}

TEST(PortPanelMacroCard, RemovingARowDisconnectsThatCableInOneUndoStep) {
    MacroCardPortFixture f(2);
    f.click(f.dot(true).jackPos);
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->rowCount(), 2);
    ASSERT_TRUE(f.connected(f.extOsc, f.inlet));

    clickNow(panel->rowAt(0)->removeButton());

    EXPECT_FALSE(f.connected(f.extOsc, f.inlet));
    EXPECT_TRUE(f.connected(f.extOsc2, f.inlet)) << "only that cable";
    EXPECT_EQ(panel->rowCount(), 1);
    f.editor->finishCableRetractForTest();
    ASSERT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.connected(f.extOsc, f.inlet)) << "one Cmd+Z brings it back";
    EXPECT_TRUE(f.connected(f.extOsc2, f.inlet));
}

TEST(PortPanelMacroCard, DisconnectAllClearsTheMacroPortsOutsideCablesInOneUndoStep) {
    MacroCardPortFixture f(2);
    f.click(f.dot(true).jackPos);
    ASSERT_NE(f.panel(), nullptr);
    ASSERT_TRUE(f.panel()->isDisconnectAllShown());

    clickNow(f.panel()->disconnectAllButton());

    EXPECT_FALSE(f.connected(f.extOsc, f.inlet));
    EXPECT_FALSE(f.connected(f.extOsc2, f.inlet));
    EXPECT_TRUE(f.connected(f.outlet, f.extSink)) << "the other port is untouched";
    f.editor->finishCableRetractForTest();
    ASSERT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.connected(f.extOsc, f.inlet));
    EXPECT_TRUE(f.connected(f.extOsc2, f.inlet));
}

TEST(PortPanelMacroCard, HoveringARowHighlightsTheCableDrawnToTheCard) {
    MacroCardPortFixture f(2);
    f.click(f.dot(true).jackPos);
    ASSERT_NE(f.panel(), nullptr);
    auto* row = f.panel()->rowAt(1);
    row->mouseEnter(portEvent(*row, {20, 10}, {20, 10}));

    ASSERT_TRUE(f.controller().highlightedCable().has_value());
    const auto highlighted = *f.controller().highlightedCable();
    bool drawn = false;
    for (const auto& cable : f.editor->buildVisibleCables())
        if (cable.id == highlighted) {
            drawn = true;
            const auto anchor = f.card()->getPosition().toFloat() + f.dot(true).jackPos.toFloat();
            EXPECT_EQ(cable.p2, anchor) << "the highlighted cable is the one drawn to the card's dot";
        }
    EXPECT_TRUE(drawn) << "the row's cable id matches a cable the canvas draws";
}

TEST(PortPanelMacroCard, AddConnectionPreviewStartsAtTheCardsDot) {
    MacroCardPortFixture f(0);
    f.clickAndOpen(f.dot(false).jackPos); // the outlet: its preview runs from the dot to a target input
    ASSERT_NE(f.panel(), nullptr);
    PortTarget target;
    target.kind = PortTarget::Kind::Jack;
    target.node = f.extSink;
    target.jack = synth::ui::PortRef{f.extSink, 0, true, false};
    f.controller().setPreview(f.panel()->port(), &target);
    ASSERT_TRUE(f.controller().hasPreview());
    EXPECT_EQ(f.controller().preview()->p1, f.card()->getPosition().toFloat() + f.dot(false).jackPos.toFloat());
}
