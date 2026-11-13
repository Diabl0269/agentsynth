// AutomationLanesDockOpenScrollTests.cpp -- "Automate" on a closed bottom dock: the lane's row is scrolled into view
// against the Timeline's settled size, not the half-open one the dock has while it slides up.

#include "../../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

class DockScrollProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "Quiet"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"Model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "ok";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "Model";
    int requestTimeoutMs = 240000;
};

void resetDockKey() {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    if (auto* s = props.getUserSettings()) {
        s->setValue("bottomDockVisible", "0");
        s->saveIfNeeded();
    }
}

class DockOpenScrollTest : public ::testing::Test {
protected:
    BottomDockActiveTabResetGuardMDT tabGuard_; // the channel chip switches the dock to the Mixer tab
    void SetUp() override { resetDockKey(); }
    void TearDown() override { resetDockKey(); }
};

// Fourteen MIDI tracks; the sixth plays Track In -> Oscillator -> Filter, so its cutoff lane sits down the list with
// more tracks below it (the scroll is not just clamped at the end).
struct ManyTracksScene {
    MainComponent mc{std::make_unique<DockScrollProvider>()};
    juce::AudioProcessorGraph::NodeID filterId;

    ManyTracksScene() {
        mc.setSize(1600, 900);
        if (auto* welcome = mc.getWelcomeScreenForTest())
            welcome->setVisible(false);
        for (int i = 0; i < 14; ++i)
            mc.simulateAddMidiTrackClick();
        if (mc.isBottomDockConfiguredVisible())
            mc.simulateToggleBottomPanelClick(); // adding a track opened the dock: start from a closed one
        auto& graph = mc.getAudioEngine().getGraph();
        const auto trackInUuid = mc.getTimelineDoc().getTracks()[5].bindingUuid;
        juce::AudioProcessorGraph::Node::Ptr trackIn;
        for (auto* node : graph.getNodes())
            if (node->properties["uuid"].toString() == trackInUuid)
                trackIn = node;
        auto osc = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
        auto filter = graph.addNode(synth::AIStateMapper::createModule("Filter"));
        filterId = filter->nodeID;
        EXPECT_TRUE(trackIn != nullptr);
        graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                             {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
        graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
    }

    synth::ui::PanelSlide& slide() { return mc.panelSlideForTest(MainComponent::SlidingPanel::Timeline); }
    double scrollY() { return mc.getTimelinePanel().getViewState().trackScrollY; }
};

synth::ui::TimelineTrackHeaderComponent* findHeader(juce::Component& root, synth::TrackId track) {
    if (auto* header = dynamic_cast<synth::ui::TimelineTrackHeaderComponent*>(&root))
        if (header->getTrackId() == track)
            return header;
    for (auto* child : root.getChildren())
        if (auto* found = findHeader(*child, track))
            return found;
    return nullptr;
}

// Fourteen Oscillator instrument tracks, each with its own mixer channel; the dock starts closed.
struct InstrumentTracksScene {
    MainComponent mc{std::make_unique<DockScrollProvider>()};
    InstrumentTracksScene() {
        mc.setSize(1600, 900);
        if (auto* welcome = mc.getWelcomeScreenForTest())
            welcome->setVisible(false);
        for (int i = 0; i < 14; ++i)
            mc.simulateAddInstrumentTrackClick(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
        if (mc.isBottomDockConfiguredVisible())
            mc.simulateToggleBottomPanelClick();
    }
};

// Clicks the channel chip of the 11th track and returns where the mixer's scrolling zone ended up.
int mixerScrollAfterChannelChipClick(InstrumentTracksScene& s, bool openDockFirst, bool* clicked) {
    if (openDockFirst)
        s.mc.simulateToggleBottomPanelClick();
    auto* header = findHeader(s.mc, s.mc.getTimelineDoc().getTracks()[10].id);
    *clicked = header != nullptr && header->getChannelChip().onClick != nullptr;
    if (*clicked)
        header->getChannelChip().onClick();
    auto& mixer = s.mc.getBottomDock().getMixerPanel();
    return mixer.getViewportForTest().getViewPositionX();
}

} // namespace

TEST_F(DockOpenScrollTest, TheChannelChipRevealsTheSameMixerColumnWhetherTheDockWasClosedOrOpen) {
    bool clicked = false;
    int openScroll = 0;
    {
        InstrumentTracksScene s;
        openScroll = mixerScrollAfterChannelChipClick(s, true, &clicked);
        ASSERT_TRUE(clicked);
    }
    EXPECT_GT(openScroll, 0) << "the strip sits past the first screenful of columns";
    InstrumentTracksScene closed;
    EXPECT_EQ(mixerScrollAfterChannelChipClick(closed, false, &clicked), openScroll);
}

TEST_F(DockOpenScrollTest, AutomateOnAClosedDockScrollsTheLaneToTheSameSpotAsOnAnOpenDock) {
    double openScroll = 0.0;
    {
        ManyTracksScene s;
        s.mc.simulateToggleBottomPanelClick();
        ASSERT_TRUE(s.mc.isBottomDockConfiguredVisible());
        s.mc.automateParameter(s.filterId, "cutoff");
        openScroll = s.scrollY();
    }
    EXPECT_GT(openScroll, 0.0) << "the lane is far enough down that it needs a scroll";

    ManyTracksScene closed;
    ASSERT_FALSE(closed.mc.isBottomDockConfiguredVisible());
    closed.mc.automateParameter(closed.filterId, "cutoff");
    EXPECT_DOUBLE_EQ(closed.scrollY(), openScroll);
}

TEST_F(DockOpenScrollTest, AutomateWhileTheDockIsStillSlidingScrollsOnceItHasSettled) {
    double openScroll = 0.0;
    {
        ManyTracksScene s;
        s.mc.simulateToggleBottomPanelClick();
        s.mc.automateParameter(s.filterId, "cutoff");
        openScroll = s.scrollY();
    }

    ManyTracksScene sliding;
    sliding.mc.simulateToggleBottomPanelClick();
    sliding.slide().snapTo(0.3f);
    sliding.slide().retarget(1.0f, true);
    sliding.mc.resized(); // the dock is laid out at its partly open height
    const int halfOpenHeight = sliding.mc.getTimelinePanel().getHeight();
    ASSERT_TRUE(sliding.slide().isMoving());
    sliding.mc.automateParameter(sliding.filterId, "cutoff");

    sliding.slide().snapTo(1.0f);
    sliding.mc.resized();
    sliding.slide().runSettled();
    EXPECT_GT(sliding.mc.getTimelinePanel().getHeight(), halfOpenHeight) << "the scroll had a smaller view to aim at";
    EXPECT_DOUBLE_EQ(sliding.scrollY(), openScroll);
}
