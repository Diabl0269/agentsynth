// MidiRemotePanelMoveTests.cpp (docs/control/midi-remote-ui.md#surface-centre): a real
// multi-step drag on the panel's own surface persists to the PROFILE's col/row (not just the
// surface's in-memory bounds) once the gesture ends, and one undo on the controller history
// restores the pre-drag position -- driven through the real cell mouseDown/mouseDrag/mouseUp path,
// same convention as ControllerSurfaceTests.cpp/ControllerSurfaceGroupDragTests.cpp. Reuses
// MidiRemotePanelTestFixture.h exactly like MidiRemotePanelGroupDeleteTests.cpp.

#include "ControllerSurfaceTestHelpers.h"
#include "MidiRemotePanelTestFixture.h"

using midiremote_surface_test::DragDriver;
using midiremote_test::MidiRemotePanelLiveRefreshTest;
using synth::ui::ControllerSurfaceCell;

namespace {

synth::Control makeControl(const juce::String& id, int cc, const juce::String& name, int col, int row) {
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

class MidiRemotePanelMoveTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.controls = {makeControl("k1", 100, "Knob 1", 0, 0), makeControl("k2", 101, "Knob 2", 4, 4)};
        ASSERT_TRUE(controller_->addProfile(profile));
        panel_.selectForTest("p1", {});
    }

    const synth::Control* findControl(const juce::String& id) const {
        for (const auto& p : controller_->getProfiles())
            if (p.id == "p1")
                for (const auto& c : p.controls)
                    if (c.id == id)
                        return &c;
        return nullptr;
    }
};

} // namespace

// The ticket's own repro, at the panel level: 3 cols right, 1 row down, over several mouseDrag
// steps -- and the drop must actually reach the PROFILE (MidiLearnController::updateProfile via
// handleControlsMoved), which only runs off ControllerSurfaceComponent::onDragEnded -- not just
// move the cell's on-screen bounds.
TEST_F(MidiRemotePanelMoveTest, MultiStepDragPersistsOriginPlusDeltaToTheProfileOnMouseUp) {
    auto* cell = const_cast<ControllerSurfaceCell*>(panel_.findSurfaceCellForTest("k1"));
    ASSERT_NE(cell, nullptr);

    DragDriver drag(*cell);
    drag.stepToOffset(1, 0);
    drag.stepToOffset(2, 0);
    drag.stepToOffset(3, 0);
    drag.stepToOffset(3, 1);
    drag.end();

    pump(); // handleControlsMoved's own deferred refreshSurfaceForSelectedProfile()

    const auto* moved = findControl("k1");
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->layout.col, 3);
    EXPECT_EQ(moved->layout.row, 1);
}

TEST_F(MidiRemotePanelMoveTest, OneUndoAfterADragRestoresTheOriginalPosition) {
    auto* cell = const_cast<ControllerSurfaceCell*>(panel_.findSurfaceCellForTest("k1"));
    ASSERT_NE(cell, nullptr);

    DragDriver drag(*cell);
    drag.stepToOffset(1, 0);
    drag.stepToOffset(2, 0);
    drag.stepToOffset(3, 1);
    drag.end();
    pump();

    ASSERT_NE(findControl("k1"), nullptr);
    EXPECT_EQ(findControl("k1")->layout.col, 3);
    EXPECT_EQ(findControl("k1")->layout.row, 1);

    ASSERT_TRUE(controller_->undoProfileEdit());

    const auto* restored = findControl("k1");
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->layout.col, 0) << "undo restores the pre-drag position";
    EXPECT_EQ(restored->layout.row, 0);
}
