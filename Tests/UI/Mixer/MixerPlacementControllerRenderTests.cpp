// MixerPlacementControllerRenderTests.cpp -- FRO230: render-level coverage for the Mixer
// "Own panel" placement strip MixerPlacementController.{h,cpp} owns (FRO231's own second bottom
// strip, distinct from MixerPlacementControllerTests.cpp's placement/focus-region behaviour
// suite). Same "persist first, then construct" MainComponent harness as that file.
//
// Groups:
//   1. Own panel: strip shown, correct bounds, resize handle on its top edge.
//   2. Tab / Window: the strip and its handle stay hidden.
//   3. Render-to-image: the strip's handle and hosted content both paint non-empty pixels.

#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "MixerDockActiveTabResetGuard.h"
#include "UI/Layout/PanelResizeHandle.h"
#include "UI/Mixer/MixerPlacementController.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMPCRT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMPCRT"; }
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

void writeMixerPlacement(const juce::String& value) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    auto* s = props.getUserSettings();
    ASSERT_NE(s, nullptr);
    s->setValue("mixerPlacement", value);
    s->saveIfNeeded();
}

} // namespace

// ============================================================================
// 1. Own panel: strip shown, correct bounds, resize handle on its top edge
// ============================================================================

TEST(MixerPlacementControllerRenderTests, OwnPanelShowsStripWithHandleOnTopEdgeAndCorrectBounds) {
    MixerDockActiveTabResetGuardMDT guard;
    writeMixerPlacement("ownPanel");

    MainComponent mc(std::make_unique<MockProviderMPCRT>());
    mc.setSize(1400, 900);

    auto& controller = mc.getMixerPlacementControllerForTest();
    ASSERT_TRUE(controller.isOwnPanelShowing());
    EXPECT_TRUE(controller.isVisible());

    auto& handle = controller.getResizeHandle();
    EXPECT_TRUE(handle.isVisible()) << "the drag handle must be visible whenever Own panel is showing";
    EXPECT_EQ(handle.getBounds(),
              juce::Rectangle<int>(0, 0, controller.getWidth(), synth::ui::PanelResizeHandle::kHeight))
        << "the handle must sit flush on the strip's top edge, spanning its full width";

    auto& host = mc.getMixerDock().getMixerHost();
    EXPECT_EQ(host.getParentComponent(), &controller) << "Own panel reparents the mixer host into this strip";
    EXPECT_EQ(host.getBounds(), controller.getLocalBounds().withTrimmedTop(synth::ui::PanelResizeHandle::kHeight))
        << "the hosted mixer must fill everything below the handle";

    const juce::Rectangle<int> handleArea(0, 0, controller.getWidth(), synth::ui::PanelResizeHandle::kHeight);
    EXPECT_FALSE(handleArea.intersects(host.getBounds())) << "the handle and the hosted mixer must never overlap";

    EXPECT_GE(controller.getHeight(), synth::ui::MixerPlacementController::kOwnPanelMinHeight)
        << "the strip's own minimum height must be respected";
}

// ============================================================================
// 2. Tab / Window: the strip and its handle stay hidden
// ============================================================================

TEST(MixerPlacementControllerRenderTests, TabAndWindowPlacementsHideTheStripAndItsHandle) {
    for (const juce::String placement : {"tab", "window"}) {
        MixerDockActiveTabResetGuardMDT guard;
        writeMixerPlacement(placement);

        MainComponent mc(std::make_unique<MockProviderMPCRT>());
        mc.setSize(1400, 900);

        auto& controller = mc.getMixerPlacementControllerForTest();
        EXPECT_FALSE(controller.isOwnPanelShowing()) << "placement '" << placement << "' is not Own panel";
        EXPECT_FALSE(controller.isVisible()) << "the strip itself must stay hidden for '" << placement << "'";
        EXPECT_FALSE(controller.getResizeHandle().isVisible())
            << "the handle must stay hidden for '" << placement << "' too";
    }
}

// ============================================================================
// 3. Render-to-image
// ============================================================================

TEST(MixerPlacementControllerRenderTests, OwnPanelStripRendersNonEmptyHandleAndContentPixels) {
    MixerDockActiveTabResetGuardMDT guard;
    writeMixerPlacement("ownPanel");

    MainComponent mc(std::make_unique<MockProviderMPCRT>());
    mc.setSize(1400, 900);

    auto& controller = mc.getMixerPlacementControllerForTest();
    ASSERT_TRUE(controller.isOwnPanelShowing());

    const auto img = controller.createComponentSnapshot(controller.getLocalBounds());
    ASSERT_GT(img.getWidth(), 0);
    ASSERT_GT(img.getHeight(), 0);

    // The handle paints an opaque 1px hairline along row 0 even at rest (PanelResizeHandle::paint).
    bool handlePainted = false;
    for (int x = 0; x < img.getWidth() && !handlePainted; ++x)
        if (img.getPixelAt(x, 0).getAlpha() > 0)
            handlePainted = true;
    EXPECT_TRUE(handlePainted) << "the resize handle's hairline must render on the strip's top edge";

    // The hosted mixer content below the handle must paint something too.
    bool contentPainted = false;
    for (int x = 0; x < img.getWidth() && !contentPainted; ++x)
        for (int y = synth::ui::PanelResizeHandle::kHeight; y < img.getHeight() && !contentPainted; ++y)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                contentPainted = true;
    EXPECT_TRUE(contentPainted) << "the hosted mixer panel must render below the handle";
}
