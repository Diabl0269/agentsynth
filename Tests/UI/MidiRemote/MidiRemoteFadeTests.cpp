// MidiRemoteFadeTests.cpp -- the MIDI Remote panel's fades (docs/layout/animation.md, "Fading things in and
// out"): the Detect and port-warning hint rows (height tweened with the fade), the undo cue, the inspector's
// fields, the inspector / orphan view cross-fade, the page strip, the Add Controller button, and controls
// growing in and shrinking away on the surface. Headless: the animated path is forced
// (FadeAnimateGuard) and stepped by hand. Suite names contain "MidiRemote" per the ship-task filter convention.

#include "../Layout/FadeVisibilityTestGuard.h"
#include "ControllerSurfaceTestHelpers.h"
#include "MidiRemotePanelTestFixture.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceToolbar.h"
#include "UI/MidiRemote/ControllersList/ControllersListComponent.h"
#include "UI/MidiRemote/Inspector/ControlInspectorComponent.h"

using synth::ui::ControlInspectorComponent;
using synth::ui::ControllerSurfaceComponent;
using synth::ui::ControllerSurfaceToolbar;
using synth::ui::FadeVisibility;

namespace {

juce::Component* findById(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findById(*child, id))
            return found;
    return nullptr;
}

} // namespace

// ---- Toolbar ------------------------------------------------------------------------------------------------

TEST(MidiRemoteFadeToolbarTest, DetectHintFadesAndItsRowGrowsWithTheFade) {
    ControllerSurfaceToolbar toolbar;
    toolbar.setSize(600, 100);
    toolbar.setProfileSelected(true);
    auto* hint = findById(toolbar, "detectHintLabel");
    ASSERT_NE(hint, nullptr);
    const int idle = toolbar.getPreferredHeight();
    int heightCallbacks = 0;
    toolbar.onPreferredHeightChanged = [&] { ++heightCallbacks; };

    FadeAnimateGuard guard;
    toolbar.setDetectOn(true);
    EXPECT_TRUE(hint->isVisible()) << "frame 0 is already a fade frame";
    EXPECT_FLOAT_EQ(hint->getAlpha(), 0.0f);
    EXPECT_EQ(toolbar.getPreferredHeight(), idle) << "the row has not opened yet";

    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(toolbar.getPreferredHeight(), idle);
    EXPECT_LT(toolbar.getPreferredHeight(), idle + ControllerSurfaceToolbar::kHintHeight);
    EXPECT_GT(heightCallbacks, 0) << "the owner is told to lay out again each frame";

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(toolbar.getPreferredHeight(), idle + ControllerSurfaceToolbar::kHintHeight);
    EXPECT_TRUE(hint->isVisible());

    toolbar.setDetectOn(false);
    EXPECT_FALSE(toolbar.isDetectOn());
    EXPECT_TRUE(hint->isVisible()) << "it stays until the fade-out has ended";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(toolbar.getPreferredHeight(), idle);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(hint->isVisible());
    EXPECT_EQ(toolbar.getPreferredHeight(), idle);
}

TEST(MidiRemoteFadeToolbarTest, PortHintKeepsItsWordsWhileFadingOutAndReportsEmptyAtOnce) {
    ControllerSurfaceToolbar toolbar;
    toolbar.setSize(600, 100);
    auto* label = dynamic_cast<juce::Label*>(findById(toolbar, "portHintLabel"));
    ASSERT_NE(label, nullptr);
    const int idle = toolbar.getPreferredHeight();

    FadeAnimateGuard guard;
    toolbar.setPortHint("Plugged into another port");
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(toolbar.getPortHint(), "Plugged into another port");
    EXPECT_EQ(toolbar.getPreferredHeight(), idle + ControllerSurfaceToolbar::kHintHeight);

    toolbar.setPortHint({});
    EXPECT_TRUE(toolbar.getPortHint().isEmpty()) << "the logical state is immediate";
    EXPECT_TRUE(label->isVisible());
    EXPECT_EQ(label->getText(), "Plugged into another port") << "its words stay painted while the row shrinks";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(toolbar.getPreferredHeight(), idle);
    EXPECT_LT(toolbar.getPreferredHeight(), idle + ControllerSurfaceToolbar::kHintHeight);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(label->isVisible());
    EXPECT_EQ(toolbar.getPreferredHeight(), idle);
}

TEST(MidiRemoteFadeToolbarTest, PortHintHeightIsInstantOffScreen) {
    ControllerSurfaceToolbar toolbar;
    toolbar.setSize(600, 100);
    const int idle = toolbar.getPreferredHeight();
    toolbar.setPortHint("x");
    EXPECT_EQ(toolbar.getPreferredHeight(), idle + ControllerSurfaceToolbar::kHintHeight);
    toolbar.setPortHint({});
    EXPECT_EQ(toolbar.getPreferredHeight(), idle);
    EXPECT_TRUE(toolbar.getPortHint().isEmpty());
}

TEST(MidiRemoteFadeToolbarTest, UndoCueFadesOutKeepingItsWords) {
    ControllerSurfaceToolbar toolbar;
    toolbar.setSize(600, 100);
    auto* cue = dynamic_cast<juce::Label*>(findById(toolbar, "undoHintLabel"));
    ASSERT_NE(cue, nullptr);

    FadeAnimateGuard guard;
    toolbar.setUndoHint("Cmd+Z undoes: Rename");
    EXPECT_TRUE(cue->isVisible());
    EXPECT_FLOAT_EQ(cue->getAlpha(), 0.0f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(toolbar.getUndoHint(), "Cmd+Z undoes: Rename");

    toolbar.setUndoHint({});
    EXPECT_TRUE(toolbar.getUndoHint().isEmpty());
    EXPECT_TRUE(cue->isVisible());
    EXPECT_EQ(cue->getText(), "Cmd+Z undoes: Rename");
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(cue->isVisible());
}

// ---- Controllers list ---------------------------------------------------------------------------------------

TEST(MidiRemoteFadeListTest, AddControllerButtonFadesOutAndBackIn) {
    synth::ui::ControllersListComponent list;
    list.setSize(240, 300);
    auto* button = findById(list, "addControllerButton");
    ASSERT_NE(button, nullptr);

    FadeAnimateGuard guard;
    list.setHosted(true);
    EXPECT_TRUE(button->isVisible()) << "still fading out";
    EXPECT_FALSE(interceptsClicks(*button)) << "a leaving button cannot be clicked";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(button->isVisible());

    list.setHosted(false);
    EXPECT_TRUE(button->isVisible());
    EXPECT_FLOAT_EQ(button->getAlpha(), 0.0f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(button->getAlpha(), 1.0f);
    EXPECT_TRUE(interceptsClicks(*button));
}

// ---- Inspector ----------------------------------------------------------------------------------------------

TEST(MidiRemoteFadeInspectorTest, ControlFieldsFadeInAndOutAsAGroupAndKeepTheirTitles) {
    ControlInspectorComponent inspector;
    inspector.setSize(300, 500);
    inspector.setControl({}); // nothing selected: the fields are gone
    auto* encoding = findById(inspector, "encodingCombo");
    ASSERT_NE(encoding, nullptr);
    EXPECT_FALSE(encoding->isVisible());

    FadeAnimateGuard guard;
    ControlInspectorComponent::ControlModel model;
    model.hasControl = true;
    model.control.id = "k";
    model.control.name = "Knob";
    inspector.setControl(model);
    EXPECT_TRUE(inspector.areFieldsShownForTest());
    EXPECT_TRUE(encoding->isVisible());
    EXPECT_FLOAT_EQ(encoding->getAlpha(), 0.0f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(encoding->getAlpha(), 1.0f);

    inspector.setControl({});
    EXPECT_FALSE(inspector.areFieldsShownForTest());
    EXPECT_TRUE(encoding->isVisible()) << "stays until faded out";
    EXPECT_EQ(encoding->getTitle(), "Encoding") << "a leaving control keeps its accessibility title";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_TRUE(encoding->isVisible());
    EXPECT_LT(encoding->getAlpha(), 1.0f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(encoding->isVisible());
}

// ---- Panel: orphan vs inspector, page strip -----------------------------------------------------------------

using midiremote_test::MidiRemotePanelLiveRefreshTest;

class MidiRemoteFadePanelTest : public MidiRemotePanelLiveRefreshTest {
protected:
    synth::ControllerProfile makeProfile(const juce::String& id) {
        synth::ControllerProfile p;
        p.id = id;
        p.name = id;
        p.input.identifier = "usb-" + id;
        synth::Control c;
        c.id = "k";
        c.name = "Knob 1";
        c.message.type = synth::MessageType::cc;
        c.message.channel = 1;
        c.message.number = 21;
        p.controls = {c};
        return p;
    }

    void makeOrphan() {
        ASSERT_TRUE(controller_->addProfile(makeProfile("orphan-1")));
        ASSERT_EQ(
            controller_->assignControl("orphan-1", "k", synth::midi::PickTarget::parameter(node_->nodeID, "cutoff")),
            synth::midi::AssignStatus::assigned);
        ASSERT_TRUE(controller_->deleteProfile("orphan-1"));
        undo_.clearUndoHistory();
        panel_.setSize(900, 400);
        panel_.rebuildFromProfiles();
    }
};

TEST_F(MidiRemoteFadePanelTest, OrphanViewAndInspectorCrossFade) {
    makeOrphan();
    FadeAnimateGuard guard;

    panel_.selectForTest("orphan-1", "");
    EXPECT_TRUE(panel_.isOrphanViewShownForTest());
    EXPECT_FALSE(panel_.isInspectorShownForTest());
    EXPECT_TRUE(panel_.getInspectorForTest().isVisible()) << "the inspector fades out under the orphan view";
    EXPECT_TRUE(panel_.getOrphanViewForTest().isVisible());
    EXPECT_FLOAT_EQ(panel_.getOrphanViewForTest().getAlpha(), 0.0f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(panel_.getInspectorForTest().isVisible());
    EXPECT_FLOAT_EQ(panel_.getOrphanViewForTest().getAlpha(), 1.0f);

    panel_.selectForTest("", "");
    EXPECT_TRUE(panel_.getOrphanViewForTest().isVisible());
    EXPECT_TRUE(panel_.getInspectorForTest().isVisible());
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(panel_.getOrphanViewForTest().isVisible());
    EXPECT_TRUE(panel_.getInspectorForTest().isVisible());
}

TEST_F(MidiRemoteFadePanelTest, PageStripFadesWithTheController) {
    ASSERT_TRUE(controller_->addProfile(makeProfile("here")));
    panel_.setSize(900, 400);
    panel_.rebuildFromProfiles();
    FadeAnimateGuard guard;

    panel_.selectForTest("here", "");
    EXPECT_TRUE(panel_.isPageStripShownForTest());
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_TRUE(panel_.getPageStripForTest().isVisible());

    panel_.selectForTest("", "");
    EXPECT_FALSE(panel_.isPageStripShownForTest());
    EXPECT_TRUE(panel_.getPageStripForTest().isVisible()) << "fading out, not popped";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(panel_.getPageStripForTest().isVisible());
}

// ---- Surface: controls grow in and shrink away --------------------------------------------------------------

TEST(MidiRemoteFadeSurfaceTest, AddedControlGrowsInAndRemovedControlShrinksAway) {
    using namespace midiremote_surface_test;
    ControllerSurfaceComponent surface;
    surface.setSize(600, 400);
    surface.setControls("p", {makeCellModel("a", "A", synth::ControlKind::knob, 0, 0),
                              makeCellModel("b", "B", synth::ControlKind::knob, 1, 0)});
    EXPECT_EQ(surface.getShrinkGhostCountForTest(), 0) << "the first fill of a controller lands at once";

    FadeAnimateGuard guard;
    surface.setControls("p", {makeCellModel("a", "A", synth::ControlKind::knob, 0, 0),
                              makeCellModel("b", "B", synth::ControlKind::knob, 1, 0),
                              makeCellModel("c", "C", synth::ControlKind::knob, 2, 0)});
    auto* added = surface.findCellForTest("c");
    ASSERT_NE(added, nullptr);
    EXPECT_FLOAT_EQ(added->getAlpha(), 0.0f);
    EXPECT_FALSE(added->getTransform().isIdentity());
    EXPECT_TRUE(surface.findCellForTest("a")->getTransform().isIdentity()) << "existing cells do not move";
    surface.stepMotionForTest(0.5f);
    EXPECT_GT(added->getAlpha(), 0.0f);
    surface.stepMotionForTest(1.0f);
    EXPECT_FLOAT_EQ(added->getAlpha(), 1.0f);
    EXPECT_TRUE(added->getTransform().isIdentity());

    surface.setControls("p", {makeCellModel("a", "A", synth::ControlKind::knob, 0, 0),
                              makeCellModel("c", "C", synth::ControlKind::knob, 2, 0)});
    EXPECT_EQ(surface.findCellForTest("b"), nullptr);
    EXPECT_EQ(surface.getShrinkGhostCountForTest(), 1) << "a picture of the removed control shrinks where it was";
    surface.stepMotionForTest(1.0f);
    EXPECT_EQ(surface.getShrinkGhostCountForTest(), 0);
}

TEST(MidiRemoteFadeSurfaceTest, SwitchingControllerOrAnimationsOffLandsAtOnce) {
    using namespace midiremote_surface_test;
    ControllerSurfaceComponent surface;
    surface.setSize(600, 400);
    surface.setControls("p", {makeCellModel("a", "A", synth::ControlKind::knob, 0, 0)});

    {
        FadeAnimateGuard guard;
        surface.setControls("q", {makeCellModel("x", "X", synth::ControlKind::knob, 0, 0)});
        EXPECT_EQ(surface.getShrinkGhostCountForTest(), 0);
        EXPECT_FLOAT_EQ(surface.findCellForTest("x")->getAlpha(), 1.0f);
    }
    {
        FadeAnimateGuard guard(synth::ui::AnimationMode::off);
        surface.setControls("q", {makeCellModel("y", "Y", synth::ControlKind::knob, 0, 0)});
        EXPECT_EQ(surface.getShrinkGhostCountForTest(), 0);
        EXPECT_FLOAT_EQ(surface.findCellForTest("y")->getAlpha(), 1.0f);
    }
}

TEST(MidiRemoteFadeSurfaceTest, ReducedMotionFadesWithoutScaling) {
    using namespace midiremote_surface_test;
    ControllerSurfaceComponent surface;
    surface.setSize(600, 400);
    surface.setControls("p", {makeCellModel("a", "A", synth::ControlKind::knob, 0, 0)});

    FadeAnimateGuard guard(synth::ui::AnimationMode::reduced);
    surface.setControls("p", {makeCellModel("a", "A", synth::ControlKind::knob, 0, 0),
                              makeCellModel("b", "B", synth::ControlKind::knob, 1, 0)});
    auto* added = surface.findCellForTest("b");
    ASSERT_NE(added, nullptr);
    EXPECT_FLOAT_EQ(added->getAlpha(), 0.0f);
    EXPECT_TRUE(added->getTransform().isIdentity());
}
