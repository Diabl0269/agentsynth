// MixerAccessibilityTests.cpp -- FRO18: JUCE AccessibilityHandler names/values on the mixer's
// faders, pan knobs, meters and M/S buttons (plan (c)). Calls createAccessibilityHandler()
// directly on the component under test rather than going through getAccessibilityHandler() --
// the latter needs a native peer this suite never creates (the same headless-focus gap
// TimelineTrackFocusTests.cpp documents), where the former is a plain virtual callable either way
// and returns a fresh handler wrapping live state.
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerFader.h"
#include "UI/Mixer/MixerMeter.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMACT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMACT"; }
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

TEST(MixerAccessibilityTest, FaderTextFromValueFunctionFormatsAsMinusThreeDb) {
    synth::ui::MixerFader fader;
    ASSERT_TRUE((bool)fader.getSlider().textFromValueFunction);
    EXPECT_EQ(fader.getSlider().textFromValueFunction(-3.0), "-3.0 dB")
        << "JUCE speaks the minus sign as \"minus\" -- VoiceOver reads this as \"minus 3 dB\"";
    EXPECT_EQ(fader.getSlider().textFromValueFunction(0.0), "0.0 dB");
}

TEST(MixerAccessibilityTest, PanTextFromValueFunctionFormatsAsPercentLeftRight) {
    synth::ui::MixerColumnComponent column;
    auto& fn = column.getPanSliderForTest().textFromValueFunction;
    ASSERT_TRUE((bool)fn);
    EXPECT_EQ(fn(0.0), "Center");
    EXPECT_EQ(fn(-0.5), "50% left");
    EXPECT_EQ(fn(0.5), "50% right");
    EXPECT_EQ(fn(-1.0), "100% left");
    EXPECT_EQ(fn(1.0), "100% right");
}

TEST(MixerAccessibilityTest, ColumnTitleIsTheChannelName) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    mixerPanel.rebuild();

    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    EXPECT_FALSE(column->getTitle().isEmpty());

    auto handler = column->createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::group);
    EXPECT_EQ(handler->getTitle(), column->getTitle())
        << "the default getTitle() implementation reads Component::getTitle() -- setColumn() must "
           "have called setTitle(column.name)";
}

TEST(MixerAccessibilityTest, MuteSoloButtonsMirrorToggleState) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    mixerPanel.rebuild();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);

    EXPECT_FALSE(column->getMuteButtonForTest().getToggleState());
    column->toggleMuted();
    EXPECT_TRUE(column->getMuteButtonForTest().getToggleState())
        << "the button's own PAINTED pressed state must track reality, not stay always-unpressed "
           "(the latent bug this mirroring fixes)";
    EXPECT_TRUE(column->getMuteButtonForTest().getTitle().contains("on"));

    EXPECT_FALSE(column->getSoloButtonForTest().getToggleState());
    column->toggleSoloed();
    EXPECT_TRUE(column->getSoloButtonForTest().getToggleState());
    EXPECT_TRUE(column->getSoloButtonForTest().getTitle().contains("on"));
}

TEST(MixerAccessibilityTest, MeterAccessibilityValueIsReadOnly) {
    synth::ui::MixerMeter meter;
    meter.peakProvider = [](int) { return 0.5f; };
    meter.refresh(1.0f); // attack is instant regardless of elapsed time -- see MixerMeterBallistics.h

    auto handler = meter.createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::staticText);
    auto* value = handler->getValueInterface();
    ASSERT_NE(value, nullptr);
    EXPECT_TRUE(value->isReadOnly());
    // FRO146: dBFS text, not a percentage -- a linear 0.5 peak is ~-6.0 dBFS.
    EXPECT_EQ(value->getCurrentValueAsString(), "-6.0 dBFS");
}
