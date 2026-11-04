// Concern: the track header's voice toggle end to end on a real MainComponent -- an instrument track shows it,
// pressing it switches the track's whole voice chain as one undo step, and the header follows the graph both ways.
#include "../../TestWait.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"

using synth::ui::TrackPolyState;

class TrackVoiceToggleAppTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
    }
    void TearDown() override {
        mc.reset();
        MainComponentTest::TearDown();
    }
    std::unique_ptr<MainComponent> mc;
};

TEST_F(TrackVoiceToggleAppTest, PressingItOnAnInstrumentTrackSwitchesItsChainAndUndoPutsItBack) {
    mc->getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    auto* header = mc->getTimelinePanel().getTrackHeaderAt(mc->getTimelinePanel().getTrackHeaderCount() - 1);
    ASSERT_NE(header, nullptr);
    auto& toggle = header->getVoiceToggle();
    auto press = [&] {
        toggle.triggerClick();
        synth::test::flushMessageQueue();
    };

    // The track plays through a poly-capable oscillator, so the toggle is there.
    header->refreshFromDoc();
    ASSERT_TRUE(toggle.isVisible());
    ASSERT_NE(toggle.getState(), TrackPolyState::none);

    if (toggle.getState() == TrackPolyState::poly)
        press();
    EXPECT_EQ(toggle.getState(), TrackPolyState::mono) << "the header follows what the graph says";
    EXPECT_FALSE(toggle.getToggleState());

    press();
    EXPECT_EQ(toggle.getState(), TrackPolyState::poly);
    EXPECT_TRUE(toggle.getToggleState());

    mc->getUndoManager().undo();
    EXPECT_EQ(toggle.getState(), TrackPolyState::mono) << "one undo step puts the whole chain back";
    EXPECT_FALSE(toggle.getToggleState());
}
