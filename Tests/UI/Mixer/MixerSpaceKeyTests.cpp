// MixerSpaceKeyTests.cpp -- every focusable button in the mixer panel claims a bare Space: the key presses the
// button once and is consumed, so it never bubbles to the window's play/stop command. Walks the real panel
// (a strip with an insert chain and a send, a bus, the toolbar and the zones pane) rather than a list of
// classes, so a new focusable button joins the check by existing.
#include "../../TestSettingsHelpers.h"
#include "../../TestWait.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerSendList.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMSKT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMSKT"; }
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

bool visibleUpTo(const juce::Component& c, const juce::Component& root) {
    for (auto* p = &c; p != nullptr; p = p->getParentComponent()) {
        if (!p->isVisible())
            return false;
        if (p == &root)
            return true;
    }
    return false;
}

void collectButtons(juce::Component& parent, juce::Component& root,
                    std::vector<juce::Component::SafePointer<juce::Button>>& out) {
    for (auto* child : parent.getChildren()) {
        if (auto* button = dynamic_cast<juce::Button*>(child);
            button != nullptr && button->getWantsKeyboardFocus() && button->isEnabled() && visibleUpTo(*button, root))
            out.emplace_back(button);
        collectButtons(*child, root, out);
    }
}

} // namespace

// Regression test for FRO974: Space on a focused mixer button also toggled playback because the button
// registered Space as a shortcut and left the key unconsumed.
TEST(MixerSpaceKeyTest, EveryFocusableMixerButtonConsumesSpaceAndClicksOnce) {
    synth::test::PersistedKeysGuard guard{{"bottomDockVisible", "bottomDockActiveTab", "bottomDockTabOrder",
                                           "mixerSectionInsertsHidden", "mixerSectionSendsHidden",
                                           "mixerSectionEqHidden"}};
    MainComponent mc{std::make_unique<MockProviderMSKT>()};
    mc.setSize(1400, 900);
    mc.getBottomDock().getMixerPanel().setVisible(true);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();
    auto& panel = mc.getBottomDock().getMixerPanel();
    const auto bus = panel.createBus();
    panel.rebuild();
    panel.getStripColumnForTest(0)->getSendList().addSendTo(bus);
    panel.rebuild();
    for (const auto section :
         {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
        panel.getSectionLayout().setHidden(section, false);
    panel.setSize(1400, 700);
    panel.resized();

    // Let every pending rebuild and fade land, or a column is replaced under a button mid-press.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
    std::vector<juce::Component::SafePointer<juce::Button>> buttons;
    collectButtons(panel, panel, buttons);
    // Toolbar toggles + add bus + reset meters + chips + dots + icon buttons + rows' bypass: well over a dozen.
    ASSERT_GT(buttons.size(), 12u);

    const juce::KeyPress space(juce::KeyPress::spaceKey);
    for (auto& pointer : buttons) {
        ASSERT_NE(pointer.getComponent(), nullptr);
        // Count the click instead of running the button's action (a section toggle would rebuild the panel).
        int clicks = 0;
        pointer->onClick = [&clicks] { ++clicks; };
        const auto label = pointer->getTitle() + " / " + pointer->getName();
        const int expectedClicks = pointer->isEnabled() ? 1 : 0; // a greyed control consumes Space and does nothing
        const bool consumed = static_cast<juce::Component&>(*pointer).keyPressed(space);
        synth::test::waitUntil([&] { return (pointer.getComponent()) != (nullptr); });
        ASSERT_NE(pointer.getComponent(), nullptr) << label << " was destroyed by its own press";
        EXPECT_TRUE(consumed) << label << " lets Space bubble to the window";
        EXPECT_EQ(clicks, expectedClicks) << label;
        pointer->onClick = nullptr;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    }
}
