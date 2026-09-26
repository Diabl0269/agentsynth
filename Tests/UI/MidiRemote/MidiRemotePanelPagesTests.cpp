// MidiRemotePanelPagesTests.cpp -- FRO142 (docs/control/midi-remote.md#pages,
// docs/control/midi-remote-ui.md#pages): the page strip, wired into the real panel, actually
// switches the engine's active page and the surface's cell labels -- driven through the strip's
// real button click (Source/UI/CLAUDE.md's "test the real mouse path" convention), never a direct
// setActivePage() call on the panel's own behalf. Reuses MidiRemotePanelTestFixture.h.

#include "MidiRemotePanelTestFixture.h"

using synth::midi::AssignStatus;
using synth::midi::PickTarget;

namespace {

class MidiRemotePanelPagesTest : public MidiRemotePanelLiveRefreshTest {
protected:
    void SetUp() override {
        MidiRemotePanelLiveRefreshTest::SetUp();
        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        profile.pageCount = 2;
        profile.controls = {makeControl("knob", synth::MessageType::cc, 21, "Knob 1")};
        ASSERT_TRUE(controller_->addProfile(profile));
        panel_.selectForTest("p1", "knob");

        // Page 1 -> cutoff, page 2 -> resonance, same control.
        ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "cutoff")),
                  AssignStatus::assigned);
        remoteEngine_.setActivePage("p1", 2);
        ASSERT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(node_->nodeID, "resonance")),
                  AssignStatus::assigned);
        remoteEngine_.setActivePage("p1", 1); // back to page 1 before each test drives its own click
        panel_.selectForTest("p1", "knob");
    }

    static synth::Control makeControl(const juce::String& id, synth::MessageType type, int number,
                                      const juce::String& name) {
        synth::Control c;
        c.id = id;
        c.name = name;
        c.message.type = type;
        c.message.channel = 1;
        c.message.number = number;
        return c;
    }

    // juce::Button narrows mouseDown/mouseUp to protected -- call them through the juce::Component
    // base (whose overrides are public), same as ControllerSurfacePageStripTests.cpp's own helper.
    void click(juce::Button& button) {
        auto& comp = static_cast<juce::Component&>(button);
        const auto pos = comp.getLocalBounds().getCentre().toFloat();
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f,
                                     0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                                     juce::Time::getCurrentTime(), 1, false);
        comp.mouseDown(event);
        comp.mouseUp(event);
    }
};

} // namespace

TEST_F(MidiRemotePanelPagesTest, StripShowsButtonsForEveryEffectivePage) {
    auto& strip = panel_.getPageStripForTest();
    EXPECT_NE(strip.getPageButtonForTest(1), nullptr);
    EXPECT_NE(strip.getPageButtonForTest(2), nullptr);
    EXPECT_EQ(strip.getPageButtonForTest(3), nullptr);
    EXPECT_TRUE(strip.getPageButtonForTest(1)->getToggleState());
}

TEST_F(MidiRemotePanelPagesTest, ClickingPageTwoSwitchesTheEngineActivePageAndCellLabels) {
    auto* cellBefore = panel_.findSurfaceCellForTest("knob");
    ASSERT_NE(cellBefore, nullptr);
    EXPECT_TRUE(cellBefore->getAssignmentLabelForTest().containsIgnoreCase("cutoff"));

    auto& strip = panel_.getPageStripForTest();
    ASSERT_NE(strip.getPageButtonForTest(2), nullptr);
    click(*strip.getPageButtonForTest(2));

    EXPECT_EQ(remoteEngine_.getActivePage("p1"), 2);
    EXPECT_TRUE(strip.getPageButtonForTest(2)->getToggleState());
    auto* cellAfter = panel_.findSurfaceCellForTest("knob");
    ASSERT_NE(cellAfter, nullptr);
    EXPECT_TRUE(cellAfter->getAssignmentLabelForTest().containsIgnoreCase("resonance"))
        << "page 2's target must now show on the same cell";
}
