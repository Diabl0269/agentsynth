// ControllerSurfaceMoveGlideTests.cpp (docs/layout/undo-glide.md "Controller surface cells"): undo and redo of a
// controller-cell move (one cell or a group) glide the cell from where it is drawn to its restored grid place instead
// of popping; a live drag and its own drop never glide. Driven through the real panel: DragDriver's mouse path, then
// MidiLearnController's profile history, with the panel's deferred refresh flushed.

#include "../../TestWait.h"
#include "../Layout/FadeVisibilityTestGuard.h"
#include "ControllerSurfaceTestHelpers.h"
#include "MidiRemotePanelTestFixture.h"

#include <cmath>
#include <optional>

using midiremote_surface_test::DragDriver;
using midiremote_test::MidiRemotePanelLiveRefreshTest;
using synth::ui::ControllerSurfaceComponent;

namespace {

synth::Control makeGlideKnob(const juce::String& id, int cc, const juce::String& name, int col, int row) {
    synth::Control c;
    c.id = id;
    c.name = name;
    c.message.type = synth::MessageType::cc;
    c.message.channel = 1;
    c.message.number = cc;
    c.layout.col = col;
    c.layout.row = row;
    return c;
}

class ControllerSurfaceMoveGlideTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.controls = {makeGlideKnob("k1", 100, "Knob 1", 0, 0), makeGlideKnob("k2", 101, "Knob 2", 4, 4),
                            makeGlideKnob("k3", 102, "Knob 3", 1, 0)};
        ASSERT_TRUE(controller_->addProfile(profile));
        panel_.selectForTest("p1", {});
        synth::test::flushMessageQueue();
        guard_.emplace();
        synth::ui::setReducedMotionForTest(false);
        surface().forceMoveGlideAnimateForTest(true);
    }
    void TearDown() override {
        synth::ui::setReducedMotionForTest(std::nullopt);
        guard_.reset();
        MidiRemotePanelLiveRefreshTest::TearDown();
    }

    ControllerSurfaceComponent& surface() { return panel_.getSurfaceForTest(); }
    float drawnX(const char* id) { return surface().drawnCellBoundsForTest(id).getX(); }
    float drawnY(const char* id) { return surface().drawnCellBoundsForTest(id).getY(); }
    float restX(const char* id) { return (float)surface().findCellForTest(id)->getX(); }
    float restY(const char* id) { return (float)surface().findCellForTest(id)->getY(); }

    // One drag of `id` by whole cells through the real mouse path, then the panel's deferred refresh.
    void drag(const char* id, int dCols, int dRows) {
        auto* cell = surface().findCellForTest(id);
        ASSERT_NE(cell, nullptr);
        DragDriver driver(*cell);
        driver.stepToOffset(dCols, 0);
        driver.stepToOffset(dCols, dRows);
        driver.end();
        synth::test::flushMessageQueue();
    }
    bool undo() {
        const bool did = controller_->undoProfileEdit();
        synth::test::flushMessageQueue();
        return did;
    }
    bool redo() {
        const bool did = controller_->redoProfileEdit();
        synth::test::flushMessageQueue();
        return did;
    }
    const synth::Control* findControl(const juce::String& id) const {
        for (const auto& p : controller_->getProfiles())
            if (p.id == "p1")
                for (const auto& c : p.controls)
                    if (c.id == id)
                        return &c;
        return nullptr;
    }

    std::optional<FadeAnimateGuard> guard_;
};

// Fraction of the way from `from` to `to` that `now` has covered.
float progress(float now, float from, float to) { return (now - from) / (to - from); }

} // namespace

TEST_F(ControllerSurfaceMoveGlideTest, UndoOfADragGlidesTheCellBackToItsPlaceAndRedoGlidesItForward) {
    const float home = restX("k1");
    drag("k1", 3, 1);
    surface().stepMoveGlideForTest(1000.0);
    const float dropped = restX("k1");
    ASSERT_GT(dropped, home + 100.0f);

    ASSERT_TRUE(undo());
    EXPECT_FLOAT_EQ(restX("k1"), home) << "the cell's own bounds are the restored place at once";
    EXPECT_TRUE(surface().isMoveGlidingForTest());
    EXPECT_NEAR(drawnX("k1"), dropped, 0.5f) << "the first frame is drawn where it was dropped";
    float last = progress(drawnX("k1"), dropped, home);
    for (const double ms : {20.0, 60.0, 100.0, 140.0}) {
        surface().stepMoveGlideForTest(ms);
        const float p = progress(drawnX("k1"), dropped, home);
        EXPECT_GT(p, last) << "monotonic toward the restored place at " << ms << " ms";
        EXPECT_GT(p, 0.0f);
        EXPECT_LT(p, 1.0f) << "never past it";
        last = p;
    }
    surface().stepMoveGlideForTest(1000.0);
    EXPECT_FALSE(surface().isMoveGlidingForTest());
    EXPECT_EQ(surface().drawnCellBoundsForTest("k1"), surface().findCellForTest("k1")->getBounds().toFloat());
    EXPECT_TRUE(surface().findCellForTest("k1")->getTransform().isIdentity());

    ASSERT_TRUE(redo());
    EXPECT_TRUE(surface().isMoveGlidingForTest());
    EXPECT_NEAR(drawnX("k1"), home, 0.5f);
    surface().stepMoveGlideForTest(60.0);
    const float p = progress(drawnX("k1"), home, dropped);
    EXPECT_GT(p, 0.05f);
    EXPECT_LT(p, 0.95f);
    surface().stepMoveGlideForTest(1000.0);
    EXPECT_FALSE(surface().isMoveGlidingForTest());
    EXPECT_FLOAT_EQ(drawnX("k1"), dropped);
}

TEST_F(ControllerSurfaceMoveGlideTest, ADraggedGroupGlidesBackTogether) {
    panel_.selectControlsForTest({"k1", "k3"});
    const float home1 = restY("k1");
    const float home3 = restY("k3");
    drag("k1", 0, 2);
    surface().stepMoveGlideForTest(1000.0);
    const float dropped1 = restY("k1");
    const float dropped3 = restY("k3");
    ASSERT_GT(dropped1, home1 + 50.0f);
    ASSERT_GT(dropped3, home3 + 50.0f);

    ASSERT_TRUE(undo());
    for (const double ms : {1.0, 40.0, 90.0}) {
        surface().stepMoveGlideForTest(ms);
        const float p1 = progress(drawnY("k1"), dropped1, home1);
        const float p3 = progress(drawnY("k3"), dropped3, home3);
        EXPECT_NEAR(p1, p3, 0.01f) << "both members share one progress at " << ms << " ms";
        EXPECT_GT(p1, 0.0f);
        EXPECT_LT(p1, 1.0f);
    }
    EXPECT_GT(drawnY("k1"), home1 + 1.0f) << "still on its way";
    surface().stepMoveGlideForTest(1000.0);
    EXPECT_FALSE(surface().isMoveGlidingForTest());
    EXPECT_FLOAT_EQ(drawnY("k1"), home1);
    EXPECT_FLOAT_EQ(drawnY("k3"), home3);
}

TEST_F(ControllerSurfaceMoveGlideTest, ASecondStepMidGlideStartsFromTheDrawnPlace) {
    const float home = restX("k1");
    drag("k1", 3, 0);
    surface().stepMoveGlideForTest(1000.0);
    const float dropped = restX("k1");

    ASSERT_TRUE(undo());
    surface().stepMoveGlideForTest(50.0);
    const float mid = drawnX("k1");
    ASSERT_GT(mid, home + 1.0f);
    ASSERT_LT(mid, dropped - 1.0f);

    ASSERT_TRUE(redo());
    EXPECT_NEAR(drawnX("k1"), mid, 0.5f) << "the redo continues from the drawn place, no jump";
    surface().stepMoveGlideForTest(40.0);
    EXPECT_GT(drawnX("k1"), mid) << "and heads for the dropped place";
    surface().stepMoveGlideForTest(1000.0);
    EXPECT_FLOAT_EQ(drawnX("k1"), dropped);
}

TEST_F(ControllerSurfaceMoveGlideTest, TheDragItselfAndItsDropNeverGlide) {
    auto* cell = surface().findCellForTest("k1");
    DragDriver driver(*cell);
    driver.stepToOffset(2, 0);
    EXPECT_FALSE(surface().isMoveGlidingForTest()) << "a live drag moves the cell directly";
    EXPECT_EQ(surface().drawnCellBoundsForTest("k1"), cell->getBounds().toFloat());
    driver.stepToOffset(3, 1);
    driver.end();
    synth::test::flushMessageQueue();
    EXPECT_FALSE(surface().isMoveGlidingForTest()) << "the drop rebuilds the cells where they already are";
    EXPECT_EQ(surface().drawnCellBoundsForTest("k1"), surface().findCellForTest("k1")->getBounds().toFloat());
    EXPECT_EQ(findControl("k1")->layout.col, 3);
}

TEST_F(ControllerSurfaceMoveGlideTest, OneUndoRestoresADrag) {
    drag("k1", 3, 1);
    ASSERT_TRUE(undo());
    EXPECT_EQ(findControl("k1")->layout.col, 0);
    EXPECT_EQ(findControl("k1")->layout.row, 0);
}

TEST_F(ControllerSurfaceMoveGlideTest, ADragStartedOnAGlidingCellTakesTheCellUnderThePointer) {
    drag("k1", 3, 0);
    surface().stepMoveGlideForTest(1000.0);
    ASSERT_TRUE(undo());
    surface().stepMoveGlideForTest(40.0);
    ASSERT_TRUE(surface().isMoveGlidingForTest());

    auto* cell = surface().findCellForTest("k1");
    DragDriver driver(*cell);
    driver.stepToOffset(1, 0);
    EXPECT_FALSE(surface().isMoveGlidingForTest()) << "the glide ends when the drag takes the cell";
    EXPECT_TRUE(cell->getTransform().isIdentity());
    driver.end();
}

TEST_F(ControllerSurfaceMoveGlideTest, UnderReduceMotionTheRestoredCellLandsAtOnceAndFadesIn) {
    const float home = restX("k1");
    drag("k1", 3, 0);
    surface().stepMoveGlideForTest(1000.0);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
    synth::ui::setReducedMotionForTest(true);

    ASSERT_TRUE(undo());
    EXPECT_FLOAT_EQ(drawnX("k1"), home) << "no slide: it is at the restored place on the first frame";
    EXPECT_LT(surface().drawnCellAlphaForTest("k1"), 0.9f);
    EXPECT_LT(surface().findCellForTest("k1")->getAlpha(), 0.9f);
    EXPECT_TRUE(surface().isMoveGlidingForTest()) << "the fade runs";
    surface().stepMoveGlideForTest(1000.0);
    EXPECT_FALSE(surface().isMoveGlidingForTest());
    EXPECT_FLOAT_EQ(surface().drawnCellAlphaForTest("k1"), 1.0f);
    EXPECT_FLOAT_EQ(surface().findCellForTest("k1")->getAlpha(), 1.0f);
}

TEST_F(ControllerSurfaceMoveGlideTest, WithAnimationsOffTheRestoredCellLandsAtOnce) {
    const float home = restX("k1");
    drag("k1", 3, 0);
    surface().stepMoveGlideForTest(1000.0);
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);

    ASSERT_TRUE(undo());
    EXPECT_FALSE(surface().isMoveGlidingForTest());
    EXPECT_FLOAT_EQ(drawnX("k1"), home);
    EXPECT_FLOAT_EQ(surface().findCellForTest("k1")->getAlpha(), 1.0f);
}
