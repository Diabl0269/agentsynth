// MixerColumnComponentMeterTests.cpp -- FRO146: the clip-readout reset fan-out (an Option/Alt-click
// on ANY column's readout, or the mixer dock's "Reset Meters" button, resets every strip column's
// AND Master's readout) plus a PNG render-to-file inspection of a clipped meter. Drives a real,
// off-screen MainComponent (MixerPanelComponentTests.cpp's own rig style) so the columns exist
// through the real MixerPanelComponent::rebuild() wiring, not a hand-built stand-in.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cstdlib>
#include <gtest/gtest.h>

namespace {

class MockProviderMCMT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMCMT"; }
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

/** Same recipe MixerColumnComponentTests.cpp's own synthesizeMouseUp uses. */
void synthesizeMouseUp(juce::Component& component, bool altDown) {
    const juce::Point<int> centre(component.getWidth() / 2, component.getHeight() / 2);
    const auto mods =
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | (altDown ? juce::ModifierKeys::altModifier : 0));
    component.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre.toFloat(), mods, 0.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, &component, &component, juce::Time::getCurrentTime(),
                                       centre.toFloat(), juce::Time::getCurrentTime(), 1, false));
}

} // namespace

TEST(MixerColumnComponentMeterTests, AnAltClickOnOneColumnsReadoutResetsEveryColumnAndMaster) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMCMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    mixerPanel.rebuild();
    ASSERT_GE(mixerPanel.getColumnCount(), 4) << "2 strips + Direct + Master";

    auto* columnA = mixerPanel.getStripColumnForTest(0);
    auto* columnB = mixerPanel.getStripColumnForTest(1);
    auto* master = mixerPanel.getMasterColumnForTest();
    ASSERT_NE(columnA, nullptr);
    ASSERT_NE(columnB, nullptr);
    ASSERT_NE(master, nullptr);

    columnA->getMeterReadoutForTest().setSize(30, 12);
    columnA->getMeterReadoutForTest().updatePeak(3.0f);
    columnB->getMeterReadoutForTest().updatePeak(1.0f);
    master->getMeterReadoutForTest().updatePeak(2.0f);
    ASSERT_TRUE(columnA->getMeterReadoutForTest().isClippedForTest());
    ASSERT_TRUE(columnB->getMeterReadoutForTest().isClippedForTest());
    ASSERT_TRUE(master->getMeterReadoutForTest().isClippedForTest());

    // The real mouse path, Alt held -- fires column A's onResetAllRequested, which
    // MixerPanelComponent::rebuild() wired to resetAllMeterReadouts().
    synthesizeMouseUp(columnA->getMeterReadoutForTest(), /*altDown=*/true);

    EXPECT_FALSE(columnA->getMeterReadoutForTest().isClippedForTest());
    EXPECT_FALSE(columnB->getMeterReadoutForTest().isClippedForTest())
        << "the fan-out must reach every OTHER column too, not just the one clicked";
    EXPECT_FALSE(master->getMeterReadoutForTest().isClippedForTest());
}

TEST(MixerColumnComponentMeterTests, TheResetMetersButtonResetsEveryColumn) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMCMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& dock = mc.getBottomDock();
    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    auto& mixerPanel = dock.getMixerPanel();
    mixerPanel.rebuild();

    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    column->getMeterReadoutForTest().updatePeak(5.0f);
    ASSERT_TRUE(column->getMeterReadoutForTest().isClippedForTest());

    dock.getResetMetersButtonForTest().onClick();

    EXPECT_FALSE(column->getMeterReadoutForTest().isClippedForTest());
}

TEST(MixerColumnComponentMeterTests, ClippedMeterRendersToPngForVisualInspection) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc(std::make_unique<MockProviderMCMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& dock = mc.getBottomDock();
    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Mixer);
    dock.setSize(1400, 300);
    auto& mixerPanel = dock.getMixerPanel();
    mixerPanel.rebuild();

    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);

    // Drive the meter/readout directly into a clipped state, rather than rendering real hot
    // audio through the strip -- MixerMeter's ballistics and MixerMeterReadout's clip latch are
    // already unit-tested on their own (MixerMeterBallisticsTests.cpp / MixerMeterReadoutTests.cpp);
    // this test is purely about what it looks like painted. Set peakProvider AFTER (never before)
    // any call to refreshMeter() -- that method installs its OWN lambda reading the real strip
    // every time it runs, which would silently overwrite a provider set beforehand.
    column->getMeterForTest().peakProvider = [](int) { return 2.0f; }; // well above 0 dBFS
    column->getMeterForTest().refresh(1.0f);
    column->getMeterReadoutForTest().updatePeak(4.0f);
    ASSERT_TRUE(column->getMeterReadoutForTest().isClippedForTest());
    ASSERT_GT(column->getMeterForTest().getDisplayedDbForTest(0), 0.0f);

    synth::theme::AppLookAndFeel laf;
    dock.setLookAndFeel(&laf);

    const int width = dock.getWidth();
    const int height = dock.getHeight();
    ASSERT_GT(width, 0);
    ASSERT_GT(height, 0);

    juce::Image img(juce::Image::ARGB, width, height, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(dock.paintEntireComponent(g, true));

    dock.setLookAndFeel(nullptr);

    const char* pngPath = std::getenv("MIXER_METER_CLIP_PNG");
    if (pngPath == nullptr || juce::String(pngPath).isEmpty()) {
        GTEST_SKIP() << "set MIXER_METER_CLIP_PNG=<path> to write the rendered clipped meter for "
                        "visual inspection";
    }

    juce::File outFile(pngPath);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    juce::FileOutputStream stream(outFile);
    ASSERT_TRUE(stream.openedOk()) << "failed to open " << pngPath << " for writing";
    juce::PNGImageFormat png;
    ASSERT_TRUE(png.writeImageToStream(img, stream)) << "failed to encode PNG to " << pngPath;
}
