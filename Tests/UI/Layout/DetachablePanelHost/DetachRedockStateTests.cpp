// DetachRedockStateTests.cpp -- FRO12 (P9-6, docs/mixer.md §5.9): proves detach/redock preserves
// REAL, production panel state end to end through MixerDockComponent/MainComponent, not just the
// generic identity/mutation invariant DetachablePanelHostTests.cpp pins against a stub panel.
// Drives a real, off-screen MainComponent (newPatchForTest() + simulateAddAudioTrackClick(), the
// ChannelFlow suite's own rig style -- see MixerPanelComponentTests.cpp).
//
// Both cases below rely on the SAME mechanism: DetachablePanelHost::setDetached() only ever
// REPARENTS panel_ (never rebuilds/recreates it), so anything the panel itself owns survives
// untouched -- unlike MixerPanelComponent::rebuild() (destroys and replaces every column), which
// this test deliberately never calls after the state is set.

#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>

namespace {

class MockProviderDRST : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockDRST"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
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
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace

TEST(DetachRedockStateTests, TimelineZoomAndScrollSurviveDetachAndRedock) {
    MainComponent mc(std::make_unique<MockProviderDRST>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    auto& viewState = mc.getTimelinePanel().getViewState();
    viewState.pixelsPerBeat = 48.0;
    viewState.firstVisibleBeat = 12.5;

    auto& host = mc.getMixerDock().getTimelineHost();
    host.setDetached(true);
    ASSERT_TRUE(host.isDetached());
    EXPECT_EQ(&mc.getTimelinePanel(), &host.getPanelForTest()) << "the SAME instance, never rebuilt";
    EXPECT_DOUBLE_EQ(viewState.pixelsPerBeat, 48.0) << "zoom must survive being reparented into the window";
    EXPECT_DOUBLE_EQ(viewState.firstVisibleBeat, 12.5) << "scroll position must survive too";

    host.setDetached(false);
    EXPECT_FALSE(host.isDetached());
    EXPECT_DOUBLE_EQ(viewState.pixelsPerBeat, 48.0) << "and survive redocking back";
    EXPECT_DOUBLE_EQ(viewState.firstVisibleBeat, 12.5);
}

TEST(DetachRedockStateTests, MixerColumnSelectionSurvivesDetachAndRedock) {
    MainComponent mc(std::make_unique<MockProviderDRST>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    mixerPanel.rebuild();
    ASSERT_GT(mixerPanel.getColumnCount(), 0);
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);

    column->setSelected(true);
    ASSERT_TRUE(column->isSelectedForTest());

    auto& host = mc.getMixerDock().getMixerHost();
    host.setDetached(true);
    ASSERT_TRUE(host.isDetached());
    EXPECT_EQ(&mixerPanel, &host.getPanelForTest()) << "the SAME MixerPanelComponent, never rebuilt";
    // No rebuild() happened -- the same MixerColumnComponent (and its selection flag) is still
    // the one showing, now inside the detached window.
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), column);
    EXPECT_TRUE(column->isSelectedForTest()) << "selection must survive being reparented into the window";

    host.setDetached(false);
    EXPECT_FALSE(host.isDetached());
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), column);
    EXPECT_TRUE(column->isSelectedForTest()) << "and survive redocking back";
}
