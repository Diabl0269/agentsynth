#pragma once

// MixerNativeHost.h -- shared by the mixer tests that click and type through the real ComponentPeer (header-only, not
// registered in Tests/CMakeLists.txt): the panel of an off-screen MainComponent moved onto a native window.
#include "../Layout/NativeWindowTestGuard.h"
#include "TestMode/TestModeInput.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::test::mixer {

// The real panel moved onto a native window for the length of a test, next to a stand-in for the canvas (a control
// that takes the keyboard). Puts the panel back where it was afterwards.
struct NativeHost : juce::Component {
    synth::ui::MixerPanelComponent& panel;
    juce::Component* const home;
    juce::Component canvas;
    const bool previousIgnoreOrder = juce::ComponentPeer::ignoreWindowOrderForHitTests;

    explicit NativeHost(synth::ui::MixerPanelComponent& p)
        : panel(p)
        , home(p.getParentComponent()) {
        juce::ComponentPeer::ignoreWindowOrderForHitTests = true;
        setSize(1400, 380);
        addAndMakeVisible(panel);
        panel.setBounds(0, 0, 1400, 300);
        canvas.setWantsKeyboardFocus(true);
        addAndMakeVisible(canvas);
        canvas.setBounds(0, 310, 1400, 60);
        addToDesktop(juce::ComponentPeer::windowIsTemporary);
        setVisible(true);
    }
    ~NativeHost() override {
        removeChildComponent(&panel);
        if (home != nullptr)
            home->addChildComponent(panel);
        removeFromDesktop();
        juce::ComponentPeer::ignoreWindowOrderForHitTests = previousIgnoreOrder;
    }

    /** What a key the panel left unhandled reaches next (the window's own commands, such as Space to play). */
    std::function<bool(const juce::KeyPress&)> onUnhandledKey;
    bool keyPressed(const juce::KeyPress& key) override { return onUnhandledKey && onUnhandledKey(key); }
};

inline bool click(juce::Component& target, juce::Point<int> local, synth::testmode::Button button = {}) {
    juce::String error;
    const bool sent = synth::testmode::click(target, local, button, 1, {}, error);
    EXPECT_TRUE(sent) << error;
    return sent;
}

inline bool nativeWindowsUnavailable() {
    return nativeWindowsAbortHere() || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr;
}

/** Sends `key` the way the OS does: to the window's focused component, bubbling up its parents. */
inline bool pressThroughPeer(juce::Component& window, const juce::KeyPress& key) {
    auto* peer = window.getPeer();
    EXPECT_NE(peer, nullptr);
    return peer != nullptr && peer->handleKeyPress(key);
}

} // namespace synth::test::mixer
