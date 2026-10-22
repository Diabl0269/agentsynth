// DockBackToolbarTests.cpp (docs/mixer/panel.md#placement-and-detachable-windows): the "Dock back" button on each
// detachable panel's own toolbar -- Mixer, Timeline (transport row) and Controllers (MIDI Remote). Real off-screen
// MainComponent, the real DetachablePanelHost::setDetached() path: the button is on the toolbar's right edge only
// while detached, nothing on that toolbar overlaps it at the window's minimum width, it is a Tab stop, and a real
// mouse click docks the panel again.

#include "../../../TestSettingsHelpers.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/DetachablePanelHost/DetachablePanelHost.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Layout/DetachablePanelHost/DockBackButton.h"
#include "UI/MidiRemote/MidiRemotePanel/MidiRemotePanelComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <gtest/gtest.h>

namespace {

using synth::test::PersistedKeysGuard;

class MockProviderDBT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockDBT"; }
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

constexpr int kMinimumWindowWidth = 320;
// The Controllers panel keeps its 200 px list and 240 px inspector beside the toolbar, which needs about 320 itself.
constexpr int kControllersMinimumWindowWidth = 200 + 240 + 320;

void clickOnce(juce::Component& target) {
    const auto pos = target.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto make = [&](juce::ModifierKeys mods) {
        return juce::MouseEvent(source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, pos, now, 1,
                                false);
    };
    target.mouseEnter(make(juce::ModifierKeys()));
    target.mouseDown(make(juce::ModifierKeys::leftButtonModifier));
    target.mouseUp(make(juce::ModifierKeys()));
}

// Every other visible control that shares the dock-back button's parent must stay clear of it.
void expectNothingOverlaps(juce::Component& button) {
    auto* parent = button.getParentComponent();
    ASSERT_NE(parent, nullptr);
    for (auto* sibling : parent->getChildren()) {
        if (sibling == &button || !sibling->isVisible() || sibling->getWidth() <= 0)
            continue;
        if (dynamic_cast<juce::Button*>(sibling) == nullptr && dynamic_cast<juce::ComboBox*>(sibling) == nullptr)
            continue;
        EXPECT_FALSE(sibling->getBounds().intersects(button.getBounds()))
            << sibling->getName() << " overlaps the dock-back button";
    }
}

void exerciseHost(synth::ui::DetachablePanelHost& host, juce::Component& panel, int windowWidth) {
    auto& button = host.getDockBackButtonForTest();
    EXPECT_EQ(button.getParentComponent(), nullptr) << "docked toolbars carry no dock-back button";

    host.setDetached(true);
    auto* window = host.getDetachedWindowForTest();
    ASSERT_NE(window, nullptr);
    window->setBounds(0, 0, windowWidth, 420);

    EXPECT_EQ(panel.getBounds(), window->getContentForTest()->getLocalBounds()) << "no header strip";
    ASSERT_NE(button.getParentComponent(), nullptr);
    EXPECT_TRUE(button.isVisible());
    EXPECT_GT(button.getWidth(), 0);
    const auto* toolbar = button.getParentComponent();
    EXPECT_LE(button.getRight(), toolbar->getWidth());
    EXPECT_GE(button.getRight(), toolbar->getWidth() - 8) << "at the toolbar's right edge";
    EXPECT_LE(panel.getLocalArea(&button, button.getLocalBounds()).getBottom(), 40) << "on the toolbar";
    EXPECT_TRUE(button.getWantsKeyboardFocus()) << "Tab-reachable";
    expectNothingOverlaps(button);

    clickOnce(button);
    EXPECT_FALSE(host.isDetached()) << "a real mouse click docks the panel back";
    EXPECT_EQ(button.getParentComponent(), nullptr);
}

} // namespace

TEST(DockBackToolbarTest, MixerToolbarCarriesTheButtonOnlyWhileDetached) {
    PersistedKeysGuard keysGuard({"mixerWindowBounds"});
    MainComponent mc(std::make_unique<MockProviderDBT>());
    mc.setSize(1400, 900);
    exerciseHost(mc.getBottomDock().getMixerHost(), mc.getBottomDock().getMixerPanel(), kMinimumWindowWidth);
}

TEST(DockBackToolbarTest, TimelineTransportRowCarriesTheButtonOnlyWhileDetached) {
    PersistedKeysGuard keysGuard({"timelineWindowBounds"});
    MainComponent mc(std::make_unique<MockProviderDBT>());
    mc.setSize(1400, 900);
    exerciseHost(mc.getBottomDock().getTimelineHost(), mc.getTimelinePanel(), kMinimumWindowWidth);
}

TEST(DockBackToolbarTest, ControllersToolbarCarriesTheButtonOnlyWhileDetached) {
    PersistedKeysGuard keysGuard({"midiRemoteWindowBounds"});
    MainComponent mc(std::make_unique<MockProviderDBT>());
    mc.setSize(1400, 900);
    exerciseHost(mc.getBottomDock().getMidiRemoteHost(), mc.getBottomDock().getMidiRemotePanel(),
                 kControllersMinimumWindowWidth);
}
