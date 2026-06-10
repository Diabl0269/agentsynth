#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// StatusBarComponent  §4.1
// Bottom chrome strip: patch name, CPU %, voice count, master-mute button.
//
// Headless-safe: all paint paths dynamic_cast<GravisynthLookAndFeel*> and fall back to plain
// JUCE colours when the cast returns null (test runner has no themed LnF installed).
//
// update() is gated — it only calls repaint() when the displayed values actually change
// (cpu delta > 0.5 %, voices changed, or patch name changed). Zero writeToLog calls.
class StatusBarComponent : public juce::Component {
public:
    StatusBarComponent();

    // Called at ~5 Hz from MainComponent::timerCallback (via every-other tick guard).
    // Gated: only repaints if any value changed by a visible amount.
    void update(float cpuPct, int voices, const juce::String& patch);

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::DrawableButton& getMasterMuteButton() noexcept { return masterMuteButton_; }

    // --- Static format helpers (headless-testable, no JUCE GUI deps) ---
    // formatCpu: 0.756f -> "75.6%"
    static juce::String formatCpu(float fraction);
    // formatVoices: 0 -> "0 voices", 1 -> "1 voice", 8 -> "8 voices"
    static juce::String formatVoices(int n);
    // formatPatch: "" or whitespace-only -> "Untitled"
    static juce::String formatPatch(const juce::String& s);

private:
    // Last-rendered values, used for gated-repaint comparison.
    float lastCpu_{-1.f};
    int lastVoices_{-1};
    juce::String lastPatch_;

    // Current display values, written by update(), read by paint().
    float cpuPct_{0.f};
    int voices_{0};
    juce::String patchName_{"Default"};

    juce::DrawableButton masterMuteButton_{"MasterMute", juce::DrawableButton::ImageFitted};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBarComponent)
};
