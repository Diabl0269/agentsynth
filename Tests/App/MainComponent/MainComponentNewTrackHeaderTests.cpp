// Concern: a track added from the + Track menu arrives with its whole header, not with the controls that need the
// track's graph (voice toggle, channel chip, drum machine button) a fade later. The header is built on the doc change
// that adds the track, before the graph behind it exists; the reconcile that follows the build shows those controls,
// and they must land with the row, not fade in on their own. Nothing below pumps the message loop or steps a fade:
// what the user sees on the first frame is what is asserted.
#include "../../UI/Layout/FadeVisibilityTestGuard.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"

using synth::ui::TimelinePanelComponent;
using synth::ui::TimelineTrackHeaderComponent;

class NewTrackHeaderAppTest : public MainComponentTest {
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

    // Adds a track the way the menu does and returns its header, with no loop turn in between.
    TimelineTrackHeaderComponent* addTrack(int menuId) {
        mc->getTimelinePanel().applyAddTrackMenuChoice(menuId);
        auto* header = mc->getTimelinePanel().getTrackHeaderAt(mc->getTimelinePanel().getTrackHeaderCount() - 1);
        // The panel hands a new row its width as its glide starts; give it one here so a control's share of the row
        // (which follows its fade) can be read.
        if (header != nullptr && header->getWidth() == 0)
            header->setSize(240, 56);
        return header;
    }

    // Shown whole on the first frame: visible, opaque and given its share of the row.
    static void expectArrivedWhole(const juce::Component& control, const char* what) {
        EXPECT_TRUE(control.isVisible()) << what << " is not shown on the first frame";
        EXPECT_EQ(control.getAlpha(), 1.0f) << what << " is fading in after its row arrived";
        EXPECT_GT(control.getWidth(), 0) << what << " has no room yet";
    }

    FadeAnimateGuard animate; // run the animated path: no VBlank reaches a test, so a fade would stay at its start
    std::unique_ptr<MainComponent> mc;
};

TEST_F(NewTrackHeaderAppTest, AnOscillatorTracksVoiceToggleAndChannelChipArriveWithTheRow) {
    auto* header = addTrack(TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    ASSERT_NE(header, nullptr);
    expectArrivedWhole(header->getVoiceToggle(), "the voice toggle");
    expectArrivedWhole(header->getChannelChip(), "the channel chip");
    expectArrivedWhole(header->getShowModuleButton(), "the show-module button");
}

TEST_F(NewTrackHeaderAppTest, AWavetableTracksVoiceToggleAndChannelChipArriveWithTheRow) {
    auto* header = addTrack(TimelinePanelComponent::kAddInstrumentWavetableMenuId);
    ASSERT_NE(header, nullptr);
    expectArrivedWhole(header->getVoiceToggle(), "the voice toggle");
    expectArrivedWhole(header->getChannelChip(), "the channel chip");
}

TEST_F(NewTrackHeaderAppTest, ADrumKitTracksDrumMachineButtonAndChannelChipArriveWithTheRow) {
    auto* header = addTrack(TimelinePanelComponent::kAddInstrumentDrumKitMenuId);
    ASSERT_NE(header, nullptr);
    expectArrivedWhole(header->getDrumMachineButton(), "the drum machine button");
    expectArrivedWhole(header->getChannelChip(), "the channel chip");
}

TEST_F(NewTrackHeaderAppTest, AnAudioTracksChannelChipArrivesWithTheRow) {
    auto* header = addTrack(TimelinePanelComponent::kAddAudioTrackMenuId);
    ASSERT_NE(header, nullptr);
    expectArrivedWhole(header->getChannelChip(), "the channel chip");
}
