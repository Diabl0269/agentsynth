// PickTargetOverlayTests.cpp -- FRO135 (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn):
// the "Pick a module control" overlay. Real mouse path: a left MouseEvent is delivered to the overlay's own
// mouseDown() at the centre of an outlined control's rectangle -- the overlay swallows it and resolves it
// against the candidates the surfaces reported, exactly as a click does (a synthesized OS click cannot
// reach a native JUCE window, so this is the click's real handler, driven with a real MouseEvent). The
// candidates are the live registries of a real ModuleComponent (via GraphEditor) and the real transport
// bar. Suite name contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "../Mixer/MixerDockActiveTabResetGuard.h"
#include "MainComponent/MainComponent.h"
#include "MidiRemoteMockProvider.h"
#include "MidiRemotePanelTestFixture.h"

#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/PickTargetOverlay/GraphPickCandidates.h"
#include "UI/Graph/PickTargetOverlay/PickTargetOverlay.h"
#include "UI/Timeline/TimelineTransportBar.h"

using synth::midi::PickTarget;
using synth::ui::PickTargetOverlay;

namespace {

juce::MouseEvent overlayMouseEvent(juce::Component& overlay, juce::Point<int> pos, juce::ModifierKeys mods) {
    const auto p = pos.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &overlay, &overlay, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1,
                            false);
}

juce::ModifierKeys leftClick() { return juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier); }
juce::ModifierKeys rightClick() { return juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier); }

class MidiRemotePickTargetTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        host_.setSize(1200, 1300);
        host_.setVisible(true);
        host_.addAndMakeVisible(*graphEditor_);
        graphEditor_->setBounds(0, 0, 1200, 1100); // tall enough for the Filter card's default spot
        graphEditor_->updateComponents();
        host_.addAndMakeVisible(bar_);
        bar_.setBounds(0, 1150, 1200, 100);
        bar_.resized();

        controller_->setPickOverlayHost(&host_);
        controller_->setTransportBar(&bar_);

        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        synth::Control knob;
        knob.id = "knob";
        knob.name = "Knob 1";
        knob.message.type = synth::MessageType::cc;
        knob.message.channel = 1;
        knob.message.number = 21;
        profile.controls = {knob};
        ASSERT_TRUE(controller_->addProfile(profile));
        undo_.clearUndoHistory();
    }

    void TearDown() override {
        controller_->setPickOverlayHost(nullptr);
        MidiRemotePanelLiveRefreshTest::TearDown();
    }

    PickTargetOverlay& overlay() { return *controller_->getPickOverlayForTest(); }

    // The overlay-space centre of the outline whose candidate satisfies `wanted`.
    template <typename Pred>
    juce::Point<int> centreOf(Pred wanted) {
        for (int i = 0; i < overlay().getOutlineCountForTest(); ++i) {
            const auto centre = overlay().getOutlineBoundsForTest(i).getCentre();
            if (const auto* candidate = overlay().findCandidateAt(centre);
                candidate != nullptr && wanted(candidate->target))
                return centre;
        }
        ADD_FAILURE() << "no outlined candidate matched";
        return {0, 0};
    }

    juce::Component host_;
    synth::ui::TimelineTransportBar bar_;
};

} // namespace

TEST_F(MidiRemotePickTargetTest, OutlinesAppearOnRegisteredControlsOnlyAcrossCanvasAndTransportBar) {
    ASSERT_TRUE(controller_->beginPickTarget("p1", "knob"));
    ASSERT_TRUE(controller_->isPickingTarget());

    std::vector<synth::ui::PickCandidate> expected;
    synth::ui::collectGraphPickCandidates(*graphEditor_, expected);
    bar_.collectPickCandidates(expected);
    ASSERT_GT(expected.size(), 4u) << "the Filter card's controls plus the four transport buttons";
    EXPECT_EQ(overlay().getOutlineCountForTest(), static_cast<int>(expected.size()));

    bool sawCutoff = false;
    int actions = 0;
    for (int i = 0; i < overlay().getOutlineCountForTest(); ++i) {
        const auto* c = overlay().findCandidateAt(overlay().getOutlineBoundsForTest(i).getCentre());
        ASSERT_NE(c, nullptr);
        sawCutoff = sawCutoff || (c->target.kind == PickTarget::Kind::parameter && c->target.paramId == "cutoff" &&
                                  c->target.nodeId == node_->nodeID);
        actions += c->target.kind == PickTarget::Kind::action ? 1 : 0;
    }
    EXPECT_TRUE(sawCutoff);
    EXPECT_EQ(actions, 4);

    // Empty canvas, and the card's own body outside any control, are not candidates.
    EXPECT_EQ(overlay().findCandidateAt({1195, 1095}), nullptr);
    auto* card = graphEditor_->getModuleComponents().getFirst();
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(overlay().findCandidateAt(host_.getLocalPoint(card, juce::Point<int>(1, 1))), nullptr);
}

TEST_F(MidiRemotePickTargetTest, ClickOnAParameterControlAssignsAtProjectScopeAndEndsTheOverlay) {
    ASSERT_TRUE(controller_->beginPickTarget("p1", "knob"));
    const auto where =
        centreOf([](const PickTarget& t) { return t.kind == PickTarget::Kind::parameter && t.paramId == "cutoff"; });

    overlay().mouseDown(overlayMouseEvent(overlay(), where, leftClick()));

    EXPECT_FALSE(controller_->isPickingTarget());
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "cutoff");
    EXPECT_EQ(doc_.assignments[0].control.controlId, "knob");
    EXPECT_TRUE(controller_->getProfiles().front().actions.empty());
    ASSERT_TRUE(undo_.canUndo()) << "recorded through recordMidiRemoteChange";
    undo_.undo();
    EXPECT_TRUE(doc_.assignments.empty());
}

TEST_F(MidiRemotePickTargetTest, ClickOnATransportButtonAssignsAGlobalAction) {
    ASSERT_TRUE(controller_->beginPickTarget("p1", "knob"));
    const auto where = centreOf([](const PickTarget& t) {
        return t.kind == PickTarget::Kind::action && t.actionId == "transportTogglePlayStop";
    });

    overlay().mouseDown(overlayMouseEvent(overlay(), where, leftClick()));

    EXPECT_TRUE(doc_.assignments.empty()) << "an action is global, not a project assignment";
    ASSERT_EQ(controller_->getProfiles().front().actions.size(), 1u);
    EXPECT_EQ(controller_->getProfiles().front().actions[0].target.action.actionId, "transportTogglePlayStop");
}

TEST_F(MidiRemotePickTargetTest, EscapeAnyOtherClickAndARightClickCancelWithoutAssigning) {
    const auto begin = [&] { ASSERT_TRUE(controller_->beginPickTarget("p1", "knob")); };

    begin();
    EXPECT_TRUE(overlay().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(controller_->isPickingTarget());

    begin();
    overlay().mouseDown(overlayMouseEvent(overlay(), {1195, 1095}, leftClick()));
    EXPECT_FALSE(controller_->isPickingTarget()) << "a click on nothing cancels";

    begin();
    const auto where = centreOf([](const PickTarget& t) { return t.kind == PickTarget::Kind::parameter; });
    overlay().mouseDown(overlayMouseEvent(overlay(), where, rightClick()));
    EXPECT_FALSE(controller_->isPickingTarget()) << "only a left click on a control picks it";

    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_TRUE(controller_->getProfiles().front().actions.empty());
    EXPECT_EQ(statusBar_.getTransientMessageForTest(), "Assign cancelled");
}

TEST_F(MidiRemotePickTargetTest, ARebuildOfTheCanvasCancelsThePickSession) {
    // The wiring MainComponent gives GraphEditor::onBeforeDetachAllModuleComponents.
    graphEditor_->onBeforeDetachAllModuleComponents = [this] { controller_->cancelPickTarget(); };
    ASSERT_TRUE(controller_->beginPickTarget("p1", "knob"));

    graphEditor_->detachAllModuleComponents();

    EXPECT_FALSE(controller_->isPickingTarget());
    EXPECT_TRUE(doc_.assignments.empty());
    graphEditor_->onBeforeDetachAllModuleComponents = nullptr;
}

TEST_F(MidiRemotePickTargetTest, BeginNeedsAnOverlayHostAndAKnownControl) {
    EXPECT_FALSE(controller_->beginPickTarget("p1", "no-such-control"));
    EXPECT_FALSE(controller_->beginPickTarget("no-such-profile", "knob"));
    controller_->setPickOverlayHost(nullptr);
    EXPECT_FALSE(controller_->beginPickTarget("p1", "knob"));
    EXPECT_FALSE(controller_->isPickingTarget());
}

TEST_F(MidiRemotePickTargetTest, BeginSaysWhatToClickInTheStatusBarAndAnArmedLearnIsCancelled) {
    controller_->arm(node_->nodeID, "cutoff");
    ASSERT_TRUE(controller_->isArmed());

    ASSERT_TRUE(controller_->beginPickTarget("p1", "knob"));

    EXPECT_FALSE(controller_->isArmed());
    EXPECT_EQ(statusBar_.getTransientMessageForTest(),
              "Click the knob, slider or button that 'Knob 1' should drive - Esc to cancel");
}

TEST_F(MidiRemotePickTargetTest, AControlClippedOutByTheCanvasEdgeIsNotOutlinedOrPickable) {
    // The card's default spot straddles y=680: everything past it is off the canvas, under the dock.
    graphEditor_->setBounds(0, 0, 1200, 680);
    ASSERT_TRUE(controller_->beginPickTarget("p1", "knob"));

    bool sawCutoff = false;
    bool sawHeaderButton = false;
    for (int i = 0; i < overlay().getOutlineCountForTest(); ++i) {
        const auto bounds = overlay().getOutlineBoundsForTest(i);
        const auto* c = overlay().findCandidateAt(bounds.getCentre());
        ASSERT_NE(c, nullptr);
        if (c->target.kind != PickTarget::Kind::parameter)
            continue; // the transport bar sits below the canvas and is unaffected
        EXPECT_LE(bounds.getBottom(), 680) << "canvas outlines stop at the canvas edge";
        sawCutoff = sawCutoff || c->target.paramId == "cutoff";
        sawHeaderButton = sawHeaderButton || c->target.paramId == "bypassed";
    }
    EXPECT_FALSE(sawCutoff) << "the Cutoff knob is fully below the canvas edge";
    EXPECT_TRUE(sawHeaderButton) << "the header buttons are still on the canvas";
    controller_->cancelPickTarget();
}

TEST_F(MidiRemotePickTargetTest, APassThroughComponentReceivesTheClickAndTheSessionSurvivesIt) {
    juce::TextButton tab("Mixer");
    host_.addAndMakeVisible(tab);
    tab.setBounds(1000, 1120, 80, 22); // over the canvas/bar gap, where no candidate sits
    controller_->setPickPassThrough({&tab});
    ASSERT_TRUE(controller_->beginPickTarget("p1", "knob"));

    const auto centre = tab.getBounds().getCentre();
    EXPECT_EQ(host_.getComponentAt(centre), &tab) << "the overlay steps aside over a pass-through component";
    EXPECT_EQ(host_.getComponentAt(juce::Point<int>(centre.x, centre.y - 60)), &overlay())
        << "everywhere else it still swallows the click";
    EXPECT_TRUE(controller_->isPickingTarget());

    tab.setVisible(false);
    EXPECT_EQ(host_.getComponentAt(centre), &overlay()) << "a hidden pass-through no longer opens a hole";
    controller_->cancelPickTarget();
}

TEST_F(MidiRemotePickTargetTest, RefreshPickTargetRecollectsCandidatesAfterATabSwitchRevealedAnotherSurface) {
    bar_.setVisible(false); // the Timeline tab is not showing yet
    ASSERT_TRUE(controller_->beginPickTarget("p1", "knob"));
    int actions = 0;
    for (int i = 0; i < overlay().getOutlineCountForTest(); ++i)
        if (const auto* c = overlay().findCandidateAt(overlay().getOutlineBoundsForTest(i).getCentre());
            c != nullptr && c->target.kind == PickTarget::Kind::action)
            ++actions;
    EXPECT_EQ(actions, 0);

    bar_.setVisible(true); // the user clicked the Timeline tab
    controller_->refreshPickTarget();
    EXPECT_GT(overlay().getOutlineCountForTest(), 0);
    bool sawPlay = false;
    for (int i = 0; i < overlay().getOutlineCountForTest(); ++i)
        if (const auto* c = overlay().findCandidateAt(overlay().getOutlineBoundsForTest(i).getCentre());
            c != nullptr && c->target.kind == PickTarget::Kind::action &&
            c->target.actionId == "transportTogglePlayStop")
            sawPlay = true;
    EXPECT_TRUE(sawPlay) << "the transport buttons are pickable once their tab is showing";
    controller_->cancelPickTarget();
}

// ---- The wiring MainComponent gives the session (not the test doubles above) ----------------------------

TEST(MidiRemotePickTargetMainComponentTest, TheOverlayCoversTheWholeWindowAndEscapeAndACanvasRebuildEndTheSession) {
    MixerDockActiveTabResetGuardMDT resetGuard;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("agentsynth-pickoverlay-mc-" + juce::Uuid().toString());
    {
        MainComponent mc(std::make_unique<MidiRemoteMockProvider>(), synth::AIProviderRegistry::createDefault(),
                         synth::ControllerProfileStore(root));
        mc.setSize(1400, 900);
        mc.setVisible(true);
        mc.newPatchForTest();

        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        synth::Control knob;
        knob.id = "knob";
        knob.name = "Knob 1";
        profile.controls = {knob};
        auto& controller = mc.getMidiLearnControllerForTest();
        ASSERT_TRUE(controller.addProfile(profile));

        ASSERT_TRUE(controller.beginPickTarget("p1", "knob"));
        auto* overlay = controller.getPickOverlayForTest();
        ASSERT_NE(overlay, nullptr);
        EXPECT_EQ(overlay->getParentComponent(), &mc) << "covers canvas, dock and transport bar alike";
        EXPECT_EQ(overlay->getBounds(), mc.getLocalBounds());

        EXPECT_TRUE(mc.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        EXPECT_FALSE(controller.isPickingTarget()) << "MainComponent's Esc route ends the session";

        ASSERT_TRUE(controller.beginPickTarget("p1", "knob"));
        mc.getGraphEditor().detachAllModuleComponents();
        EXPECT_FALSE(controller.isPickingTarget()) << "a graph rebuild ends the session";
        EXPECT_TRUE(controller.getProfiles().front().actions.empty());

        // The dock's tab buttons let clicks through, and a tab switch keeps the session and re-collects.
        ASSERT_TRUE(controller.beginPickTarget("p1", "knob"));
        EXPECT_EQ(mc.getMixerDock().getTabButtons().size(), 3u);
        ASSERT_TRUE(static_cast<bool>(mc.getMixerDock().onActiveTabChanged));
        // Calling the real tab-change hook must leave the session up (it re-collects, never ends).
        mc.getMixerDock().onActiveTabChanged();
        EXPECT_TRUE(controller.isPickingTarget()) << "switching dock tabs must not end the pick";
        controller.cancelPickTarget();
    }
    root.deleteRecursively();
}
