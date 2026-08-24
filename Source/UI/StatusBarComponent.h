#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// StatusBarComponent  §4.1
// Bottom chrome strip: patch name, CPU %, voice count, transport cluster, master-mute button.
//
// Headless-safe: all paint paths dynamic_cast<AppLookAndFeel*> and fall back to plain
// JUCE colours when the cast returns null (test runner has no themed LnF installed).
//
// update() is gated — it only calls repaint() when the displayed values actually change
// (cpu delta > 0.5 %, voices changed, or patch name changed). Zero writeToLog calls.
//
// showMessage() displays a transient status message that auto-clears after ~2.5 s.
// While active it overrides the normal patch/cpu/voice text in the centre of the bar.
//
// Transport cluster: a play/stop glyph button + a "bar.beat.ticks   BPM" readout, ALWAYS visible
// regardless of the timeline panel's visibility — before this, play/stop/position only existed
// inside TimelineTransportBar, a child of the (often-hidden) timeline panel. Fed by
// updateTransport() from MainComponent::timerCallback's existing unconditional transport poll; see
// that method's comment. This class lives in Core, which cannot depend on AppUI (where
// TimelineTransportBar and its formatBarBeat() helper live), so updateTransport() takes an
// already-formatted position string rather than formatting one itself.
class StatusBarComponent
    : public juce::Component
    , private juce::Timer {
public:
    StatusBarComponent();

    // Called at ~5 Hz from MainComponent::timerCallback (via every-other tick guard).
    // Gated: only repaints if any value changed by a visible amount.
    void update(float cpuPct, int voices, const juce::String& patch);

    // The round-trip latency readout ("RT 4.0 ms") — input device + graph + output device at
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

    // Push play-state + a pre-formatted position readout + BPM into the transport cluster. See the
    // class comment for why `positionText` arrives pre-formatted (typically the caller's own
    // synth::ui::TimelineTransportBar::formatBarBeat(ppq, tsNumerator, tsDenominator)).
    //
    // Gated independently of update()/updateRoundTripLatency(): a diff on (playing, positionText,
    // bpm) is what triggers a repaint, so an unchanged tick costs nothing extra, same shape as the
    // round-trip segment's own string-diff gate.
    void updateTransport(bool playing, const juce::String& positionText, double bpm);

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::DrawableButton& getMasterMuteButton() noexcept { return masterMuteButton_; }

    // The play/stop button. "The transport is the truth": its toggle state is set ONLY by
    // updateTransport() above, never by the click itself (setClickingTogglesState(false) in the
    // ctor) — same idiom as TimelineTransportBar::getPlayStopButton(). The owner (MainComponent)
    // wires onClick to the same TransportService play()/stop() calls the timeline transport bar
    // uses.
    juce::Button& getTransportButton() noexcept { return transportButton_; }

    // Test-only: the currently-displayed transient message ("" when none is active). Production
    // code never reads this back — showMessage() is fire-and-forget.
    const juce::String& getTransientMessageForTest() const noexcept { return transientMessage_; }

    // Test-only: the round-trip segment's rendered string, and how many times it has asked
    // for a repaint. The counter is the same seam TimelineClipLaneArea's live strip uses to prove
    // its own gating — two updates with the same value must cost exactly one repaint.
    const juce::String& getRoundTripTextForTest() const noexcept { return roundTripText_; }
    int getRoundTripRepaintCountForTest() const noexcept { return roundTripRepaintCount_; }

    // Test-only: the transport cluster's rendered readout ("001.1.000   120.0 BPM") and how many
    // times it actually changed (and so requested a repaint) — same counting idiom as
    // getRoundTripRepaintCountForTest().
    const juce::String& getTransportDisplayTextForTest() const noexcept { return transportDisplayText_; }
    int getTransportRepaintCountForTest() const noexcept { return transportRepaintCount_; }

    // Test-only: whether the transport cluster currently fits before the voice-count slot (the
    // cramped-width drop, computed in resized() — see its comment). Unlike the round-trip TEXT, the
    // play/stop button is a live child component, so resized() has to actually hide it, not just
    // skip drawing over it.
    bool isTransportClusterVisibleForTest() const noexcept { return transportClusterFits_; }

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

    // The round-trip segment. The STRING is the gate — the diff is on what would actually be
    // drawn, so a latency that moves by less than the printed resolution costs no repaint at all.
    // Empty until the first updateRoundTripLatency(), and drawn as nothing while it is.
    juce::String roundTripText_;
    int roundTripRepaintCount_{0};

    // A small juce::Button subclass that draws the play/stop glyph as a plain path — the same
    // triangle/square shapes TimelineTransportBar::GlyphButton::paintButton draws for its own
    // PlayStop glyph, reproduced here rather than shared (that class is private to AppUI; this bar
    // lives in Core — see the class comment).
    class TransportButton : public juce::Button {
    public:
        TransportButton()
            : juce::Button("statusBarTransportPlayStop") {}
        void paintButton(juce::Graphics& g, bool shouldDrawHighlighted, bool shouldDrawDown) override;
    };

    TransportButton transportButton_;

    // Transport cluster state, written by updateTransport(), read by paint()/resized().
    bool transportPlaying_{false};
    juce::String transportDisplayText_;
    int transportRepaintCount_{0};
    bool transportClusterFits_{true};

    juce::DrawableButton masterMuteButton_{"MasterMute", juce::DrawableButton::ImageFitted};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBarComponent)
};
