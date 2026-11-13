#pragma once

// TestModeRig.h
//
// Shared by the TestMode suites (Tests/TestMode/*.cpp). Header-only; not compiled on its own. A small component tree on
// a real native window so a synthetic event can travel the ComponentPeer path the test mode uses, and the helpers that
// run a test-mode command to completion on the test's own (message) thread.

#include "../UI/Layout/NativeWindowTestGuard.h"
#include "TestMode/TestModeCommands.h"
#include "TestMode/TestModeInput.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace testmode_rig {

// Counts the events it receives.
struct Probe : juce::Component {
    int downs = 0, ups = 0, drags = 0, doubleClicks = 0, keys = 0, wheels = 0, enters = 0, moves = 0;
    float lastWheelY = 0.0f;
    juce::ModifierKeys downMods;
    void mouseDown(const juce::MouseEvent& e) override {
        ++downs;
        downMods = e.mods;
    }
    void mouseUp(const juce::MouseEvent&) override { ++ups; }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        ++wheels;
        lastWheelY = w.deltaY;
    }
    void mouseEnter(const juce::MouseEvent&) override { ++enters; }
    void mouseMove(const juce::MouseEvent&) override { ++moves; }
    void mouseDrag(const juce::MouseEvent&) override { ++drags; }
    void mouseDoubleClick(const juce::MouseEvent&) override { ++doubleClicks; }
    bool keyPressed(const juce::KeyPress&) override {
        ++keys;
        return true;
    }
};

// A 400x300 native window holding two probes side by side, a button, a slider, a label and a combo box.
struct Rig : juce::Component {
    Probe left, right;
    juce::TextButton button{"Go"};
    juce::Slider slider;
    juce::Label label{"labelName", "hello"};
    juce::ComboBox combo;
    int keys = 0; // keys the window itself received (nothing below it had focus)

    bool keyPressed(const juce::KeyPress&) override {
        ++keys;
        return true;
    }

    // The test mode runs with the window-order check off; the rig does the same for the length of a test.
    const bool previousIgnoreOrder = juce::ComponentPeer::ignoreWindowOrderForHitTests;
    ~Rig() override { juce::ComponentPeer::ignoreWindowOrderForHitTests = previousIgnoreOrder; }

    Rig() {
        juce::ComponentPeer::ignoreWindowOrderForHitTests = true;
        setName("rigWindow");
        left.setComponentID("left");
        right.setComponentID("right");
        right.setName("rightByName");
        button.setComponentID("goButton");
        slider.setComponentID("volume");
        slider.setRange(0.0, 10.0);
        slider.setValue(4.0, juce::dontSendNotification);
        label.setComponentID("greeting");
        combo.setComponentID("choice");
        combo.addItem("one", 1);
        combo.addItem("two", 2);
        combo.setSelectedId(2, juce::dontSendNotification);
        for (juce::Component* c : {static_cast<juce::Component*>(&left), static_cast<juce::Component*>(&right),
                                   static_cast<juce::Component*>(&button), static_cast<juce::Component*>(&slider),
                                   static_cast<juce::Component*>(&label), static_cast<juce::Component*>(&combo)})
            addAndMakeVisible(c);
        left.setBounds(10, 10, 100, 100);
        right.setBounds(150, 10, 100, 100);
        button.setBounds(10, 130, 80, 30);
        slider.setBounds(10, 170, 200, 24);
        label.setBounds(10, 200, 100, 24);
        combo.setBounds(10, 230, 100, 24);
        setSize(400, 300);
        setWantsKeyboardFocus(true);
    }

    // False when this environment cannot make a native window (then the test skips).
    bool show() {
        if (nativeWindowsAbortHere())
            return false;
        addToDesktop(juce::ComponentPeer::windowIsTemporary);
        setVisible(true);
        setTopLeftPosition(100, 100);
        return isOnDesktop();
    }
};

inline void pump(int ms) {
    const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    while (juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
}

inline juce::var args(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        o->setProperty(key, value);
    return juce::var(o);
}

// Runs a standard command to completion, pumping the message loop between steps (bounded).
inline synth::testmode::StepResult run(const juce::String& name, const juce::var& a) {
    static const auto table = synth::testmode::CommandTable::standard();
    const auto* spec = table.find(name);
    synth::testmode::Scratch scratch;
    for (int attempt = 0; attempt < 2000; ++attempt) {
        auto result = spec->step(a, scratch, attempt);
        if (result.state != synth::testmode::StepResult::State::pending)
            return result;
        pump(2);
    }
    return synth::testmode::StepResult::failed("test gave up waiting");
}

} // namespace testmode_rig
