#pragma once

// SilentLookAndFeel.h: the default look and feel of the Tests binary. JUCE plays the system beep when a press lands
// outside a modal prompt (Component::inputAttemptWhenModal), and the suite does that on purpose, so every run would
// beep at the developer's desk. This one counts the beeps it swallowed instead.
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::tests {

class SilentLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void playAlertSound() override { ++suppressed; }
    int suppressed = 0;
};

// Installs a SilentLookAndFeel as the desktop default before every test: a test that sets its own default and puts
// back "none" leaves JUCE's own, audible one for the tests after it.
class SilentAlertSoundsListener final : public ::testing::EmptyTestEventListener {
public:
    static SilentLookAndFeel& instance() {
        static SilentLookAndFeel laf;
        return laf;
    }
    void OnTestStart(const ::testing::TestInfo&) override { juce::LookAndFeel::setDefaultLookAndFeel(&instance()); }
    void OnTestProgramEnd(const ::testing::UnitTest&) override { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
};

} // namespace synth::tests
