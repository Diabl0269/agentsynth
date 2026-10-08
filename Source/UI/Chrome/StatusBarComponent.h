#pragma once

#include "UI/Layout/IconButton.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>

// StatusBarComponent  (docs/layout/chrome.md#status-bar)
// Bottom chrome strip: patch name, CPU %, voice count, transport cluster, master-mute button.
//
// Headless-safe: all paint paths dynamic_cast<AppLookAndFeel*> and fall back to plain
// JUCE colours when the cast returns null (test runner has no themed LnF installed).
//
// update() is gated — it only calls repaint() when the displayed values actually change
// (cpu delta > 0.5 %, voices changed, or patch name changed). Zero writeToLog calls.
//
// showMessage() displays a transient status message that auto-clears after ~2.5 s.
// While active it overrides the normal patch/cpu/voice text in the centre of the bar. The message
// fades in over 160 ms and out over 110 ms (80 ms plain fade under Reduce Motion), cross-fading with
// the normal text; with the bar off screen, or Animations Off, the final state lands at once.
//
// Transport cluster: a play/stop glyph button + a "bar.beat.ticks   BPM" readout, ALWAYS visible
// regardless of the timeline panel's visibility — before this, play/stop/position only existed
// inside TimelineTransportBar, a child of the (often-hidden) timeline panel. Fed by
// updateTransport() from MainComponent::timerCallback's existing unconditional transport poll; see
// that method's comment. This class lives in Core, which cannot depend on AppUI (where
// TimelineTransportBar and its formatBarBeat() helper live), so updateTransport() takes an
// already-formatted position string rather than formatting one itself.
// Tooltips: the patch/CPU/round-trip/transport segments are PAINTED TEXT, not child components, so
// there is nothing for juce::TooltipWindow to hit-test individually. StatusBarComponent is instead
// itself a juce::TooltipClient — MainComponent already owns the app's one shared
// juce::TooltipWindow (docs/layout/theming.md's "TooltipWindow" entry), which finds any TooltipClient
// is the exact component under the mouse (juce::TooltipWindow::getTipFor() does not walk up
// parents — see juce_TooltipWindow.cpp), which is true here for the painted segments since no
// child covers that area. Hovering the masterMuteButton_/transportButton_ children instead reaches
// their own tooltip text (both are juce::Buttons) — getTooltip() is never even called there.
class StatusBarComponent
    : public juce::Component
    , public juce::TooltipClient
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

    // Stays up until clearMessage()/another message -- for MIDI Learn's armed/settling text, which
    // can outlive showMessage()'s 2.5 s auto-clear.
    void showStickyMessage(const juce::String& msg);
    void clearMessage(); // a no-op for a transient showMessage(), which is left to its own timer

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

    // juce::TooltipClient — delegates to the pure, testable helper below using the real mouse
    // position (relative to this component). Never called for the two child buttons; see the class
    // comment.
    juce::String getTooltip() override;

    // The segment -> tooltip-text mapping, factored out of getTooltip() so it is headlessly
    // testable with a synthetic point (JUCE's real mouse position isn't available/movable in a
    // unit test). Mirrors paint()'s own x-ranges exactly (round-trip and transport-cluster text are
    // only "hit" while paint() would actually be drawing them — a hidden segment has no tooltip).
    // Returns "" for anywhere without an explanation (patch name, voice count, blank space, or
    // while a transient message covers the row).
    juce::String getTooltipForPosition(juce::Point<int> localPosition) const;

    // The bar as a screen reader reads it: the items currently drawn, in order, e.g. "Project clips,
    // CPU 13%, RT 12.5 ms, position 001.1.000, 120 BPM, 0 voices". While a transient or sticky message
    // covers the bar, that message alone. Exposed as the bar's accessibility value.
    juce::String getAccessibilityText() const;

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

    // Test-only: how far the message has faded in (0 normal status, 1 message), the text painted as the
    // message (kept while it fades out), and a hand step of a running fade (1 finishes it) for tests that
    // force the animated path with FadeVisibility::setAnimateOffScreenForTest.
    float getMessageAlphaForTest() const noexcept { return messageAlpha_; }
    const juce::String& getDisplayedMessageForTest() const noexcept { return displayedMessage_; }
    void stepMessageFadeForTest(float t);

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

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    // juce::Timer override — fires once after ~2.5 s to clear the transient message.
    void timerCallback() override;

    // Whether the round-trip segment is CURRENTLY drawn — the single source of truth for its own
    // fit-check-then-drop gate, shared by paint() and getTooltipForPosition() so they can never
    // drift apart (a hidden segment must never answer a tooltip query). Defined in the .cpp because
    // the geometry constants it reads live there (anonymous namespace).
    bool isRoundTripSegmentVisible() const noexcept;

    // Transient/sticky message state. Empty transientMessage_ means neither is active.
    juce::String transientMessage_;
    // What paint() draws as the message: transientMessage_, kept while the message fades out.
    juce::String displayedMessage_;
    float messageAlpha_ = 0.0f; // 0 normal status .. 1 message
    float fadeFrom_ = 0.0f;
    float fadeTo_ = 0.0f;
    juce::VBlankAnimatorUpdater vblankUpdater_{this};
    synth::ui::AnimationDriver messageFade_;
    void fadeMessageTo(bool shown);
    void landMessageAt(float to);
    void paintStatusText(juce::Graphics& g, int rightEdge, int textY, int textH) const;
    bool messageIsSticky_ = false; // set via showStickyMessage(); clearMessage() only touches this one

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

    synth::ui::IconButton transportButton_{"statusBarTransportPlayStop", synth::theme::Glyph::Play}; // Stop while on

    // Transport cluster state, written by updateTransport(), read by paint()/resized().
    bool transportPlaying_{false};
    juce::String transportDisplayText_;
    juce::String transportPositionText_;
    double transportBpm_{0.0};
    int transportRepaintCount_{0};
    bool transportClusterFits_{true};

    juce::DrawableButton masterMuteButton_{"MasterMute", juce::DrawableButton::ImageFitted};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBarComponent)
};
