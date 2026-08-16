#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// StatusBarComponent  §4.1
// Bottom chrome strip: patch name, CPU %, voice count, master-mute button.
//
// Headless-safe: all paint paths dynamic_cast<AppLookAndFeel*> and fall back to plain
// JUCE colours when the cast returns null (test runner has no themed LnF installed).
//
// update() is gated — it only calls repaint() when the displayed values actually change
// (cpu delta > 0.5 %, voices changed, or patch name changed). Zero writeToLog calls.
//
// showMessage() displays a transient status message that auto-clears after ~2.5 s.
// While active it overrides the normal patch/cpu/voice text in the centre of the bar.
class StatusBarComponent
    : public juce::Component
    , private juce::Timer {
public:
    StatusBarComponent();

    // Called at ~5 Hz from MainComponent::timerCallback (via every-other tick guard).
    // Gated: only repaints if any value changed by a visible amount.
    void update(float cpuPct, int voices, const juce::String& patch);

    // TL6-8: the round-trip latency readout ("RT 4.0 ms") — input device + graph + output device at
    // the current sample rate, i.e. AudioEngine::getRecordingLatencySamples(), which is the amount a
    // recorded take is shifted back by. Fed from the same 5 Hz poll as update() above, and gated the
    // same way but INDEPENDENTLY: its own string diff, so a moving CPU figure never repaints on
    // account of an unchanged latency and vice versa.
    //
    // `available == false` (Hosted mode: the host owns the device, so there is no round trip of ours
    // to report) draws the placeholder instead of a number.
    void updateRoundTripLatency(double milliseconds, bool available);

    // Display a transient message (e.g. "Saving...", "Loaded: Bright Pad") for ~2.5 s,
    // then auto-clear and restore normal status. Safe to call from any message-thread code.
    void showMessage(const juce::String& msg);

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::DrawableButton& getMasterMuteButton() noexcept { return masterMuteButton_; }

    // Test-only: the currently-displayed transient message ("" when none is active). Production
    // code never reads this back — showMessage() is fire-and-forget.
    const juce::String& getTransientMessageForTest() const noexcept { return transientMessage_; }

    // Test-only (TL6-8): the round-trip segment's rendered string, and how many times it has asked
    // for a repaint. The counter is the same seam TimelineClipLaneArea's live strip uses to prove
    // its own gating — two updates with the same value must cost exactly one repaint.
    const juce::String& getRoundTripTextForTest() const noexcept { return roundTripText_; }
    int getRoundTripRepaintCountForTest() const noexcept { return roundTripRepaintCount_; }

    // --- Static format helpers (headless-testable, no JUCE GUI deps) ---
    // formatCpu: 0.756f -> "75.6%"
    static juce::String formatCpu(float fraction);
    // formatVoices: 0 -> "0 voices", 1 -> "1 voice", 8 -> "8 voices"
    static juce::String formatVoices(int n);
    // formatPatch: "" or whitespace-only -> "Untitled"
    static juce::String formatPatch(const juce::String& s);
    // formatRoundTrip: (12.34, true) -> "RT 12.3 ms";  (anything, false) -> "RT —".
    // Negative input is clamped to 0 rather than printed.
    static juce::String formatRoundTrip(double milliseconds, bool available);

private:
    // juce::Timer override — fires once after ~2.5 s to clear the transient message.
    void timerCallback() override;

    // Transient message state. Empty string means no transient message is active.
    juce::String transientMessage_;

    // Last-rendered values, used for gated-repaint comparison.
    float lastCpu_{-1.f};
    int lastVoices_{-1};
    juce::String lastPatch_;

    // Current display values, written by update(), read by paint().
    float cpuPct_{0.f};
    int voices_{0};
    juce::String patchName_{"Default"};

    // TL6-8: the round-trip segment. The STRING is the gate — the diff is on what would actually be
    // drawn, so a latency that moves by less than the printed resolution costs no repaint at all.
    // Empty until the first updateRoundTripLatency(), and drawn as nothing while it is.
    juce::String roundTripText_;
    int roundTripRepaintCount_{0};

    juce::DrawableButton masterMuteButton_{"MasterMute", juce::DrawableButton::ImageFitted};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBarComponent)
};
