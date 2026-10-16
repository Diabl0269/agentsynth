// Concern: the track header's Mono | Poly switch end to end on a real MainComponent -- an instrument track shows it,
// pressing a side switches the track's whole voice chain as one undo step, and the header follows the graph both ways.
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"

using synth::ui::TrackPolyState;

class TrackPolySwitchAppTest : public MainComponentTest {
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

TEST_F(TrackPolySwitchAppTest, PressingASideOnAnInstrumentTrackSwitchesItsChainAndUndoPutsItBack) {
    mc->getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    auto* header = mc->getTimelinePanel().getTrackHeaderAt(mc->getTimelinePanel().getTrackHeaderCount() - 1);
    ASSERT_NE(header, nullptr);
    auto& sw = header->getPolySwitch();
    auto press = [&](juce::Button& button) {
        button.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    };

    // The track plays through a poly-capable oscillator, so the switch is there.
    header->refreshFromDoc();
    ASSERT_TRUE(sw.isVisible());
    ASSERT_NE(sw.getState(), TrackPolyState::none);

    press(sw.getMonoButton());
    EXPECT_EQ(sw.getState(), TrackPolyState::mono) << "the header follows what the graph says";
    EXPECT_TRUE(sw.getMonoButton().getToggleState());

    press(sw.getPolyButton());
    EXPECT_EQ(sw.getState(), TrackPolyState::poly);
    EXPECT_TRUE(sw.getPolyButton().getToggleState());

    mc->getUndoManager().undo();
    EXPECT_EQ(sw.getState(), TrackPolyState::mono) << "one undo step puts the whole chain back";
    EXPECT_TRUE(sw.getMonoButton().getToggleState());
}
