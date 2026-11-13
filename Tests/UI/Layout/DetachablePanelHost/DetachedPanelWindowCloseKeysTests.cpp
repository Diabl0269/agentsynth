// DetachedPanelWindowCloseKeysTests.cpp
//
// Esc and Cmd+W in a detached panel window return the section to the bottom panel, the same way the window's
// close button does (docs/mixer/panel.md#placement-and-detachable-windows). The keys go through the window's own
// keyPressed, the entry a real key reaches once nothing inside the panel took it.
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachablePanelHost.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include <gtest/gtest.h>

using synth::ui::DetachablePanelHost;

namespace {

constexpr auto kCmd = juce::ModifierKeys::commandModifier;

struct StubPanel : public juce::Component {};

class DetachedPanelWindowCloseKeysTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "DetachedPanelWindowCloseKeysTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
        host = std::make_unique<DetachablePanelHost>(panel, "Test Panel", "closeKeysWindowBounds", &appProperties,
                                                     nullptr, &shortcutManager);
        host->setDetached(true);
        window = host->getDetachedWindowForTest();
        ASSERT_NE(window, nullptr);
    }
    void TearDown() override {
        host.reset();
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }

    StubPanel panel;
    juce::ApplicationProperties appProperties;
    ShortcutManager shortcutManager;
    std::unique_ptr<DetachablePanelHost> host;
    synth::ui::DetachedPanelWindow* window = nullptr;
};

} // namespace

TEST_F(DetachedPanelWindowCloseKeysTest, EscapeClosesTheWindowAndTheSectionIsDockedAgain) {
    EXPECT_TRUE(window->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));

    EXPECT_FALSE(host->isDetached());
    EXPECT_EQ(host->getDetachedWindowForTest(), nullptr);
    EXPECT_EQ(panel.getParentComponent(), host.get()) << "the same panel is back in its dock slot";
}

TEST_F(DetachedPanelWindowCloseKeysTest, CommandWClosesTheWindowAndTheSectionIsDockedAgain) {
    EXPECT_TRUE(window->keyPressed(juce::KeyPress('w', kCmd, 0)));

    EXPECT_FALSE(host->isDetached());
    EXPECT_EQ(panel.getParentComponent(), host.get());
}

TEST_F(DetachedPanelWindowCloseKeysTest, TheKeysUseTheCloseButtonsOwnPath) {
    int requests = 0;
    window->onCloseRequested = [&requests] { ++requests; };

    EXPECT_TRUE(window->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(window->keyPressed(juce::KeyPress('w', kCmd, 0)));
    window->closeButtonPressed();

    EXPECT_EQ(requests, 3) << "Esc, Cmd+W and the close button all ask the host to redock";
}

TEST_F(DetachedPanelWindowCloseKeysTest, OtherKeysLeaveTheWindowOpen) {
    for (const auto& key : {juce::KeyPress('w'), juce::KeyPress('w', juce::ModifierKeys::shiftModifier, 0),
                            juce::KeyPress('w', kCmd | juce::ModifierKeys::shiftModifier, 0),
                            juce::KeyPress('q', kCmd, 0), juce::KeyPress(juce::KeyPress::returnKey),
                            juce::KeyPress(juce::KeyPress::escapeKey, juce::ModifierKeys::shiftModifier, 0)}) {
        EXPECT_FALSE(window->keyPressed(key)) << key.getTextDescription();
        EXPECT_TRUE(host->isDetached()) << key.getTextDescription();
    }
}

TEST_F(DetachedPanelWindowCloseKeysTest, AnAppShortcutThatTakesTheKeyKeepsTheWindowOpen) {
    window->onAppShortcut = [](const juce::KeyPress& key) { return key == juce::KeyPress::escapeKey; };

    EXPECT_TRUE(window->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(host->isDetached()) << "the app's own Esc (cancelling a MIDI pick) wins";

    EXPECT_TRUE(window->keyPressed(juce::KeyPress('w', kCmd, 0)));
    EXPECT_FALSE(host->isDetached()) << "a key the app does not claim still closes";
}

// A control inside the panel that already uses Esc keeps it: JUCE hands a key to the focused control first and
// walks up to the window only while nobody returns true, so a text field that returns true never lets the key reach
// the window. Real focus needs a native peer, so the focused control's own answer is what is pinned here.
TEST_F(DetachedPanelWindowCloseKeysTest, ATextFieldInThePanelConsumesEscapeItself) {
    juce::TextEditor editor;
    panel.addAndMakeVisible(editor);
    editor.setText("draft");

    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(host->isDetached()) << "the text field took it; the window never saw it";
}
