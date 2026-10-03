// Concern: Ctrl+R / Ctrl+M reach Record and Toggle Metronome through the real key path of a real
// MainComponent, and the Ctrl hint hold labels both transport buttons -- under the Mac table and the
// Windows/Linux table (ShortcutManager::setDefaultsPlatform), so both platforms are checked from any host.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintOverlay.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintText.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Timeline/TimelineTransportBar.h"

namespace {

using Platform = ShortcutManager::DefaultsPlatform;
using synth::ui::ShortcutHintOverlay;

constexpr int kCtrl = juce::ModifierKeys::ctrlModifier;

// What the OS peer delivers for a physical Ctrl+<letter>: upper-case key code, the Ctrl flag, no text.
juce::KeyPress peerKey(char letter) { return juce::KeyPress(juce::CharacterFunctions::toUpperCase(letter), kCtrl, 0); }

// ComponentPeer::handleKeyPress in miniature: the focused component gets the key first, then each parent.
bool dispatchFrom(juce::Component& focused, const juce::KeyPress& key) {
    for (auto* c = &focused; c != nullptr; c = c->getParentComponent())
        if (c->keyPressed(key))
            return true;
    return false;
}

ShortcutHintOverlay* findOverlay(MainComponent& mc) {
    for (int i = 0; i < mc.getNumChildComponents(); ++i)
        if (auto* overlay = dynamic_cast<ShortcutHintOverlay*>(mc.getChildComponent(i)))
            return overlay;
    return nullptr;
}

bool bubbleOver(ShortcutHintOverlay& overlay, juce::Component& button, const juce::String& text) {
    const auto bounds = overlay.getLocalArea(&button, button.getLocalBounds());
    for (const auto& e : overlay.getEntries())
        if (!e.isPill && e.keyText == text &&
            std::abs(e.bounds.getCentreX() - bounds.getCentreX()) <= bounds.getWidth())
            return true;
    return false;
}

class TransportCtrlChordsTest : public ::testing::TestWithParam<Platform> {
protected:
    // The metronome state persists to the shared settings file; restore it afterwards.
    synth::test::PersistedKeysGuard settingsGuard_{juce::StringArray{"timelineMetronomeEnabled"}};
    BottomDockActiveTabResetGuardMDT tabGuard_;

    void SetUp() override {
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1600, 900);
        mc_->newPatchForTest();
        mc_->getShortcutManager().setDefaultsPlatform(GetParam());
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    }

    void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

    std::unique_ptr<MainComponent> mc_;
};

} // namespace

TEST_P(TransportCtrlChordsTest, CtrlMTogglesTheMetronomeFromAFocusedTimelineControl) {
    auto& bar = mc_->getTimelinePanel().getTransportBar();
    const bool before = bar.getMetronomeButton().getToggleState();

    EXPECT_TRUE(dispatchFrom(bar.getRecordButton(), peerKey('m')));
    pump();
    EXPECT_EQ(bar.getMetronomeButton().getToggleState(), !before);
    EXPECT_EQ(mc_->getAudioEngine().getMetronome().isEnabled(), !before);

    EXPECT_TRUE(dispatchFrom(bar.getRecordButton(), peerKey('m')));
    pump();
    EXPECT_EQ(bar.getMetronomeButton().getToggleState(), before);
}

TEST_P(TransportCtrlChordsTest, CtrlRTogglesRecordFromAFocusedTimelineControl) {
    auto& bar = mc_->getTimelinePanel().getTransportBar();
    ASSERT_FALSE(bar.isRecordingForTest());

    EXPECT_TRUE(dispatchFrom(bar.getMetronomeButton(), peerKey('r')));
    pump();
    EXPECT_TRUE(bar.isRecordingForTest());

    EXPECT_TRUE(dispatchFrom(bar.getMetronomeButton(), peerKey('r')));
    pump();
    EXPECT_FALSE(bar.isRecordingForTest());
}

TEST_P(TransportCtrlChordsTest, TheDisplacedActionsStayReachableOnTheirOwnChords) {
    auto& shortcuts = mc_->getShortcutManager();
    // Neither of the Ctrl chords may also fire Repeat or the mod matrix.
    EXPECT_EQ(shortcuts.getActionsForKeyPress(peerKey('m')).size(), 1);
    EXPECT_EQ(shortcuts.getActionsForKeyPress(peerKey('r')).size(), 1);
    for (const char* id : {"toggleModMatrix", "repeatSelection"}) {
        const auto binding = shortcuts.getBinding(id);
        ASSERT_TRUE(binding.isValid()) << id;
        EXPECT_FALSE(shortcuts.getActionsForKeyPress(binding).isEmpty()) << id;
    }
}

TEST_P(TransportCtrlChordsTest, HoldingCtrlLabelsRecordAndMetronome) {
    auto* overlay = findOverlay(*mc_);
    ASSERT_NE(overlay, nullptr);
    double now = 0.0;
    overlay->setClockForTest([&] { return now; });
    const juce::ModifierKeys ctrl(kCtrl);
    overlay->modifierKeysChanged(ctrl);
    now += ShortcutHintOverlay::kShowDelayMs;
    overlay->modifierKeysChanged(ctrl);
    ASSERT_TRUE(overlay->areHintsShowing());

    auto& bar = mc_->getTimelinePanel().getTransportBar();
    const auto text = [&](const char* id) {
        return synth::ui::hint::formatKeyCapTextForPlatform(mc_->getShortcutManager().getBinding(id));
    };
    EXPECT_FALSE(text("transportRecord").isEmpty());
    EXPECT_TRUE(bubbleOver(*overlay, bar.getRecordButton(), text("transportRecord")));
    EXPECT_TRUE(bubbleOver(*overlay, bar.getMetronomeButton(), text("transportToggleMetronome")));
    overlay->modifierKeysChanged(juce::ModifierKeys());
}

INSTANTIATE_TEST_SUITE_P(Platforms, TransportCtrlChordsTest, ::testing::Values(Platform::Mac, Platform::Other),
                         [](const ::testing::TestParamInfo<Platform>& info) {
                             return info.param == Platform::Mac ? "Mac" : "WindowsAndLinux";
                         });
