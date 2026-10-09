// PortPanelClickTests.cpp: which presses on a jack open the port connections panel, and which do not. The real mouse
// path (ModuleComponent::mouseDown / mouseUp), with the panel captured headlessly by the controller's launcher.

#include "PortPanelTestFixture.h"

namespace {

using synth::ui::PortConnectionsPanel;
}

TEST_F(GraphEditorTest, ClickingAJackWithThreeCablesOpensAPanelListingThem) {
    PortFixture f(3);
    ASSERT_EQ(f.totalOscCables(), 3);

    f.clickOscOutput();

    ASSERT_EQ(f.launches, 1);
    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->rowCount(), 3);
    const auto own = f.titleOf(f.oscId) + f.dot() + f.module(f.oscId)->getOutputPortLabel(0);
    EXPECT_EQ(panel->titleText(), own + " (3 connections)");
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(panel->rowAt(i)->connection().label, f.vcaRowText(i)) << "row " << i;
    EXPECT_TRUE(panel->isDisconnectAllShown());
    EXPECT_EQ(panel->getWidth(), synth::ui::ModDotPage::kWidth);
}

TEST_F(GraphEditorTest, ThePanelPointsAtTheJackThatWasClicked) {
    PortFixture f(2);
    f.clickOscOutput();
    ASSERT_NE(f.panel(), nullptr);
    const auto jack = f.oscCard()->localPointToGlobal(f.oscOut());
    EXPECT_TRUE(f.launchedAt.contains(jack)) << "the anchor is the jack's screen rectangle";
    EXPECT_LT(f.launchedAt.getWidth(), 60) << "a jack, not the whole card";
}

TEST_F(GraphEditorTest, ClickingAnInputJackListsTheSourcesFeedingIt) {
    PortFixture f(0, 1);
    auto osc2 = f.engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    f.engine.updateModuleNames();
    f.refresh();
    sizeModuleComponents(*f.editor);
    f.editor->connectPorts(f.oscId, 0, f.vcaIds[0], 0, false, false);
    f.editor->connectPorts(osc2->nodeID, 0, f.vcaIds[0], 0, false, false);
    f.refresh();

    auto* vca = f.card(f.vcaIds[0]);
    f.click(*vca, vca->getPortCenter(0, true));

    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->rowCount(), 2);
    const auto outLabel = f.module(f.oscId)->getOutputPortLabel(0);
    EXPECT_EQ(panel->rowAt(0)->connection().label, f.titleOf(f.oscId) + f.dot() + outLabel);
    EXPECT_EQ(panel->rowAt(1)->connection().label, f.titleOf(osc2->nodeID) + f.dot() + outLabel);
}

TEST_F(GraphEditorTest, ADragFromAJackStillMakesACableAndOpensNothing) {
    PortFixture f(0, 1);
    auto* osc = f.oscCard();
    auto* vca = f.card(f.vcaIds[0]);
    const auto from = f.oscOut();
    const auto target = osc->getLocalPoint(vca, vca->getPortCenter(0, true));

    osc->mouseDown(portEvent(*osc, from, from));
    osc->mouseDrag(portEvent(*osc, target, from));
    osc->mouseUp(portEvent(*osc, target, from));

    EXPECT_EQ(f.cablesBetweenOscAnd(0), 1) << "the drag connected the two jacks";
    EXPECT_EQ(f.launches, 0);
    EXPECT_EQ(f.panel(), nullptr);
    EXPECT_FALSE(f.controller().hasPendingOpenForTest());
}

TEST_F(GraphEditorTest, AReleaseTwoPixelsFromThePressIsStillAClick) {
    PortFixture f(2);
    auto* osc = f.oscCard();
    const auto from = f.oscOut();
    osc->mouseDown(portEvent(*osc, from, from));
    osc->mouseUp(portEvent(*osc, from + juce::Point<int>(2, 0), from));
    EXPECT_NE(f.panel(), nullptr);
}

TEST_F(GraphEditorTest, ARightClickOrAModifiedClickOnAJackOpensNothing) {
    PortFixture f(2);
    auto* osc = f.oscCard();
    const auto p = f.oscOut();
    osc->mouseUp(portEvent(*osc, p, p, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    osc->mouseUp(portEvent(*osc, p, p, juce::ModifierKeys(juce::ModifierKeys::commandModifier)));
    osc->mouseUp(portEvent(*osc, p, p, juce::ModifierKeys(juce::ModifierKeys::shiftModifier)));
    EXPECT_EQ(f.launches, 0);
}

TEST_F(GraphEditorTest, ClickingAnUnconnectedJackOpensAPanelThatSaysSo) {
    PortFixture f(0, 1);
    f.clickOscOutput();

    auto* panel = f.panel();
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->rowCount(), 0);
    EXPECT_FALSE(panel->isDisconnectAllShown());
    EXPECT_TRUE(panel->titleText().endsWith("(No connections yet)")) << panel->titleText();
}

TEST_F(GraphEditorTest, AOneCablePanelStatesTheCountInTheSingular) {
    PortFixture f(1);
    f.editor->setDoubleClickPortDisconnectEnabled(false); // opens at once
    f.clickOscOutput();
    ASSERT_NE(f.panel(), nullptr);
    EXPECT_TRUE(f.panel()->titleText().endsWith("(1 connection)")) << f.panel()->titleText();
    EXPECT_FALSE(f.panel()->isDisconnectAllShown()) << "nothing to cut but the one row";
}

TEST_F(GraphEditorTest, ASecondClickOnTheSameJackClosesThePanel) {
    PortFixture f(3);
    f.clickOscOutput();
    ASSERT_NE(f.panel(), nullptr);

    f.clickOscOutput();

    EXPECT_EQ(f.panel(), nullptr);
    EXPECT_EQ(f.launches, 1) << "closing does not reopen";
    f.clickOscOutput();
    EXPECT_NE(f.panel(), nullptr) << "and a third click opens it again";
    EXPECT_EQ(f.launches, 2);
}

TEST_F(GraphEditorTest, ClickingAnotherJackReplacesThePanel) {
    PortFixture f(2);
    f.clickOscOutput();
    ASSERT_NE(f.panel(), nullptr);
    auto* vca = f.card(f.vcaIds[0]);

    f.click(*vca, vca->getPortCenter(0, true));
    f.controller().firePendingOpenForTest(); // one cable: the open waits out the double-click

    ASSERT_NE(f.panel(), nullptr);
    EXPECT_EQ(f.panel()->port().node, f.vcaIds[0]);
    EXPECT_EQ(f.panel()->rowCount(), 1);
    EXPECT_EQ(f.launches, 2);
}

TEST_F(GraphEditorTest, DoubleClickingAJackWithOneCableDisconnectsItWithoutFlashingThePanel) {
    PortFixture f(1);
    ASSERT_TRUE(f.editor->getDoubleClickPortDisconnectEnabled());
    auto* osc = f.oscCard();
    const auto p = f.oscOut();

    osc->mouseDown(portEvent(*osc, p, p, {}, 1));
    osc->mouseUp(portEvent(*osc, p, p, {}, 1));
    EXPECT_EQ(f.launches, 0) << "the first click waits out the double-click";
    EXPECT_TRUE(f.controller().hasPendingOpenForTest());
    osc->mouseDown(portEvent(*osc, p, p, {}, 2));
    osc->mouseUp(portEvent(*osc, p, p, {}, 2));

    EXPECT_EQ(f.cablesBetweenOscAnd(0), 0);
    EXPECT_FALSE(f.controller().hasPendingOpenForTest());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(juce::MouseEvent::getDoubleClickTimeout() + 80);
    EXPECT_EQ(f.launches, 0);
    EXPECT_EQ(f.panel(), nullptr);
}

TEST_F(GraphEditorTest, ASingleClickOnAOneCableJackOpensItOnceTheDoubleClickWindowPasses) {
    PortFixture f(1);
    f.clickOscOutput();
    ASSERT_EQ(f.launches, 0);
    ASSERT_TRUE(f.controller().hasPendingOpenForTest());

    f.controller().firePendingOpenForTest();

    ASSERT_NE(f.panel(), nullptr);
    EXPECT_EQ(f.panel()->rowCount(), 1);
    EXPECT_EQ(f.cablesBetweenOscAnd(0), 1) << "nothing was disconnected";
}

TEST_F(GraphEditorTest, WithTheDoubleClickPreferenceOffAOneCableJackOpensAtOnce) {
    PortFixture f(1);
    f.editor->setDoubleClickPortDisconnectEnabled(false);
    f.clickOscOutput();
    EXPECT_NE(f.panel(), nullptr);
    EXPECT_FALSE(f.controller().hasPendingOpenForTest());
}

TEST_F(GraphEditorTest, DoubleClickingAJackWithSeveralCablesKeepsThePanelAndDisconnectsNothing) {
    PortFixture f(3);
    auto* osc = f.oscCard();
    const auto p = f.oscOut();

    osc->mouseDown(portEvent(*osc, p, p, {}, 1));
    osc->mouseUp(portEvent(*osc, p, p, {}, 1));
    ASSERT_NE(f.panel(), nullptr) << "several cables: no waiting";
    osc->mouseDown(portEvent(*osc, p, p, {}, 2));
    osc->mouseUp(portEvent(*osc, p, p, {}, 2));

    EXPECT_EQ(f.totalOscCables(), 3);
    ASSERT_NE(f.panel(), nullptr);
    EXPECT_EQ(f.panel()->rowCount(), 3);
    EXPECT_EQ(f.launches, 1) << "the same panel, not a second one";
}

TEST_F(GraphEditorTest, ADoubleClickOnAnUnconnectedJackIsStillANoOp) {
    PortFixture f(0, 1);
    auto* osc = f.oscCard();
    const auto p = f.oscOut();
    osc->mouseDown(portEvent(*osc, p, p, {}, 1));
    osc->mouseUp(portEvent(*osc, p, p, {}, 1));
    osc->mouseDown(portEvent(*osc, p, p, {}, 2));
    osc->mouseUp(portEvent(*osc, p, p, {}, 2));
    EXPECT_EQ(f.engine.getGraph().getConnections().size(), 0u);
    EXPECT_EQ(f.launches, 1) << "the first click opened it, the second does nothing";
}
