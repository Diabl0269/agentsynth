// MixerPanelComponentTests.cpp -- FRO11 (P9-5, docs/mixer/panel.md#what-the-mixer-shows): the mixer panel's column
// set, PNG render smoke test (dark + light built-in theme, per the ticket's own test list), and
// the column-click-selects-macro gesture. Drives a real, off-screen MainComponent
// (newPatchForTest() + simulateAddAudioTrackClick(), the ChannelFlow suite's own rig style).
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/Theme.h"
#include <gtest/gtest.h>

namespace {

// Same minimal mock as every other headless MainComponent test in this suite (ChannelFlowTestFixture.h's
// MockProviderCFT) -- a unique name to avoid an ODR clash across test translation units.
class MockProviderMPCT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMPCT"; }
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

TEST(MixerPanelComponentTests, MixerPanelRendersOneColumnPerStripPlusDirectPlusMaster) {
    MainComponent mc(std::make_unique<MockProviderMPCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    mixerPanel.rebuild();

    EXPECT_EQ(mixerPanel.getColumnCount(), 4)
        << "2 strips + Direct + Master (docs/mixer/panel.md#what-the-mixer-shows)";
}

TEST(MixerPanelComponentTests, MixerPanelPngRenderSmokeTestDarkTheme) {
    // Isolates "bottomDockActiveTab" on the shared on-disk settings file -- dock.setActiveTab()
    // below persists it, and left uncleared it would clobber a later test's "Timeline" default
    // assumption (see the guard's own comment / BottomDockComponentTests.cpp's own use of it).
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMPCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& dock = mc.getBottomDock();
    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    dock.setSize(1400, 300);

    synth::theme::AppLookAndFeel laf;
    for (const auto& t : synth::theme::builtInThemes()) {
        if (t.name != "Obsidian")
            continue;
        laf.applyTheme(t);
        dock.setLookAndFeel(&laf);
        const auto img = dock.createComponentSnapshot(dock.getLocalBounds());
        EXPECT_GT(img.getWidth(), 0);
        EXPECT_GT(img.getHeight(), 0);
        dock.setLookAndFeel(nullptr);
    }
}

TEST(MixerPanelComponentTests, MixerPanelPngRenderSmokeTestLightTheme) {
    // Same isolation as the dark-theme test above.
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMPCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& dock = mc.getBottomDock();
    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    dock.setSize(1400, 300);

    synth::theme::AppLookAndFeel laf;
    for (const auto& t : synth::theme::builtInThemes()) {
        if (t.name != "Daylight")
            continue;
        laf.applyTheme(t);
        dock.setLookAndFeel(&laf);
        const auto img = dock.createComponentSnapshot(dock.getLocalBounds());
        EXPECT_GT(img.getWidth(), 0);
        EXPECT_GT(img.getHeight(), 0);
        dock.setLookAndFeel(nullptr);
    }
}

TEST(MixerPanelComponentTests, ClickingAColumnSelectsItsOwningMacroOnTheCanvas) {
    MainComponent mc(std::make_unique<MockProviderMPCT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick(); // T173a boxes {Track Audio, Gate, EQ, Compressor, Strip} into one macro

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    mixerPanel.rebuild();
    mixerPanel.setSize(1400, 300);
    mixerPanel.resized();

    ASSERT_EQ(mc.getGraphEditor().getMacros().size(), 1);
    const auto macroId = mc.getGraphEditor().getMacros().getAll().front().id;
    EXPECT_FALSE(mc.getGraphEditor().getMacroController().isMacroSelected(macroId));

    // getStripColumnForTest is a stable handle onto the real column component -- a real
    // juce::Viewport's own child layout (scrollbars, the viewed-content wrapper) is not something
    // a test should have to reason about (see this accessor's own comment).
    auto* columnComponent = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(columnComponent, nullptr);
    const juce::Point<int> centre(columnComponent->getWidth() / 2, 12); // inside the header band
    columnComponent->mouseDown(juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), centre.toFloat(),
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, columnComponent,
        columnComponent, juce::Time::getCurrentTime(), centre.toFloat(), juce::Time::getCurrentTime(), 1, false));
    columnComponent->mouseUp(juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), centre.toFloat(),
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, columnComponent,
        columnComponent, juce::Time::getCurrentTime(), centre.toFloat(), juce::Time::getCurrentTime(), 1, false));

    EXPECT_TRUE(mc.getGraphEditor().getMacroController().isMacroSelected(macroId));
}
