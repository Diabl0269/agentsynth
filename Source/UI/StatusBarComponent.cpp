#include "StatusBarComponent.h"
#include "Theme/AppLookAndFeel.h"

namespace {
// Mute-button slot (matches resized()'s `getWidth() - kMuteSlotWidth` and the `padH` every text
// segment in paint() insets from the edges).
constexpr int kMuteSlotWidth = 28;
constexpr int kPadH = 6;
// Matches the voice-count draw's own width in paint() (`rightEdge - kVoiceSlotWidth`).
constexpr int kVoiceSlotWidth = 80;
// The CPU % segment, right after the patch name. Named (unlike when it was inline in paint()) so
// getTooltipForPosition() can hit-test the exact same range paint() draws into.
constexpr int kCpuX = 170;
constexpr int kCpuWidth = 60;

// Transport cluster geometry: play/stop glyph button + "bar.beat.ticks   BPM" readout, placed
// right after the round-trip segment (x=236, width 90 — see paint()). Named constants here (unlike
// the older segments' inline literals) because, unlike the round-trip TEXT, the play/stop button is
// a live child component: resized() has to run the exact same fit check paint() uses so it can
// actually hide the button, not merely skip drawing over it.
constexpr int kRoundTripX = 236;
constexpr int kRoundTripWidth = 90;
constexpr int kTransportGap = 6;
constexpr int kTransportX = kRoundTripX + kRoundTripWidth + kTransportGap;
constexpr int kTransportButtonSize = 16;
constexpr int kTransportTextGap = 4;
constexpr int kTransportTextWidth = 118;
constexpr int kTransportClusterWidth = kTransportButtonSize + kTransportTextGap + kTransportTextWidth;
} // namespace

// ---------------------------------------------------------------------------
StatusBarComponent::StatusBarComponent() {
    addAndMakeVisible(masterMuteButton_);

    addAndMakeVisible(transportButton_);
    transportButton_.setComponentID("statusBarTransportPlayStop");
    transportButton_.setClickingTogglesState(false); // the transport is the truth — see updateTransport()
    transportButton_.setTooltip("Play / Stop");
}

// ---------------------------------------------------------------------------
void StatusBarComponent::update(float cpuPct, int voices, const juce::String& patch) {
    const bool changed = (std::abs(cpuPct - lastCpu_) > 0.5f) || (voices != lastVoices_) || (patch != lastPatch_);
    if (!changed)
        return;

    cpuPct_ = cpuPct;
    voices_ = voices;
    patchName_ = patch;
    lastCpu_ = cpuPct;
    lastVoices_ = voices;
    lastPatch_ = patch;

    repaint();
}

// ---------------------------------------------------------------------------
void StatusBarComponent::updateRoundTripLatency(double milliseconds, bool available) {
    const juce::String text = formatRoundTrip(milliseconds, available);
    if (text == roundTripText_)
        return; // nothing a repaint would change

    roundTripText_ = text;
    ++roundTripRepaintCount_;
    repaint();
}

// ---------------------------------------------------------------------------
void StatusBarComponent::updateTransport(bool playing, const juce::String& positionText, double bpm) {
    const juce::String text = positionText + "   " + juce::String(bpm, 1) + " BPM";
    const bool changed = (playing != transportPlaying_) || (text != transportDisplayText_);
    if (!changed)
        return;

    transportPlaying_ = playing;
    transportDisplayText_ = text;
    transportButton_.setToggleState(playing, juce::dontSendNotification);
    ++transportRepaintCount_;
    repaint();
}

// ---------------------------------------------------------------------------
// TransportButton::paintButton — the play/stop glyph. Same triangle/square shapes as
// TimelineTransportBar::GlyphButton's PlayStop case (Source/UI/TimelineTransportBar.cpp),
// reproduced rather than shared — see the class comment in the header. getToggleState() is playing
// (true draws the stop square, false draws the play triangle); accent while playing is the
// "obviously running" cue the always-visible cluster exists for.
void StatusBarComponent::TransportButton::paintButton(juce::Graphics& g, bool shouldDrawHighlighted, bool) {
    using namespace synth::theme;

    juce::Colour accent = juce::Colours::cyan;
    juce::Colour textPrimary = juce::Colours::white;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        accent = lf->getTheme().colors.accent;
        textPrimary = lf->getTheme().colors.textPrimary;
    }

    if (shouldDrawHighlighted) {
        g.setColour(textPrimary.withAlpha(0.08f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
    }

    const auto bounds = getLocalBounds().toFloat();
    const float side = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto glyphArea = juce::Rectangle<float>(side, side).withCentre(bounds.getCentre()).reduced(side * 0.2f);

    g.setColour(getToggleState() ? accent : textPrimary.withAlpha(0.75f));
    if (getToggleState()) {
        g.fillRoundedRectangle(glyphArea.reduced(glyphArea.getWidth() * 0.06f), 1.5f); // stop = square
    } else {
        // Optical centring, same nudge TimelineTransportBar's PlayStop triangle uses.
        const auto tri = glyphArea.withTrimmedLeft(glyphArea.getWidth() * 0.12f);
        juce::Path triangle;
        triangle.addTriangle(tri.getX(), tri.getY(), tri.getX(), tri.getBottom(), tri.getRight(), tri.getCentreY());
        g.fillPath(triangle);
    }
}

// ---------------------------------------------------------------------------
bool StatusBarComponent::isRoundTripSegmentVisible() const noexcept {
    const int rightEdge = getWidth() - kMuteSlotWidth - kPadH;
    return roundTripText_.isNotEmpty() && kRoundTripX + kRoundTripWidth <= rightEdge - kVoiceSlotWidth - kPadH;
}

// ---------------------------------------------------------------------------
// getTooltip()/getTooltipForPosition() — see the header's class comment and the two methods' own
// comments for why this exists (painted text has no component of its own for juce::TooltipWindow
// to hit-test) and why the mapping is split into a pure, testable half.
juce::String StatusBarComponent::getTooltip() { return getTooltipForPosition(getMouseXYRelative()); }

juce::String StatusBarComponent::getTooltipForPosition(juce::Point<int> localPosition) const {
    if (transientMessage_.isNotEmpty())
        return {}; // the transient message covers this row; nothing painted underneath to explain

    if (localPosition.y < 0 || localPosition.y >= getHeight())
        return {};

    const int x = localPosition.x;

    // CPU % — AudioEngine::isHosted() ? 0.0f : deviceManager.getCpuUsage() * 100.0, read at
    // MainComponent::timerCallback's 5 Hz status-bar poll (see docs/layout.md §5). getCpuUsage() is
    // JUCE's own "proportion of the audio callback's time budget spent in the callback" figure.
    if (x >= kCpuX && x < kCpuX + kCpuWidth)
        return "Audio-engine DSP load: percentage of the audio callback's time budget spent "
               "rendering this block.";

    // Round trip — AudioEngine::getRecordingLatencySamples() (input device + graph + output
    // device), converted to ms at the transport's sample rate; see updateRoundTripLatencyReadout()
    // and this class's own updateRoundTripLatency() doc comment. Only "hit" while actually drawn.
    if (isRoundTripSegmentVisible() && x >= kRoundTripX && x < kRoundTripX + kRoundTripWidth)
        return "Round-trip latency: input device + audio graph + output device delay - the amount "
               "a recorded take is shifted back to line it up.";

    // Transport position + tempo readout — only "hit" while the cluster fits and has a reading.
    if (transportClusterFits_ && transportDisplayText_.isNotEmpty()) {
        const int transportTextX = kTransportX + kTransportButtonSize + kTransportTextGap;
        if (x >= transportTextX && x < transportTextX + kTransportTextWidth)
            return "Playback position (bar.beat.ticks) and tempo (BPM).";
    }

    return {};
}

// ---------------------------------------------------------------------------
void StatusBarComponent::showMessage(const juce::String& msg) {
    transientMessage_ = msg;
    repaint();
    // (Re-)start the single-shot auto-clear timer: 2500 ms, fires once.
    startTimer(2500);
}

// ---------------------------------------------------------------------------
void StatusBarComponent::timerCallback() {
    stopTimer();
    transientMessage_ = {};
    repaint();
}

// ---------------------------------------------------------------------------
void StatusBarComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;

    auto* lnf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());

    // --- Resolve colours: themed if LnF present, otherwise JUCE fallback ---
    juce::Colour bg0, border, textPrimary, textMuted, warningColour, accentColour;
    if (lnf) {
        const auto& c = lnf->getTheme().colors;
        bg0 = c.bg0;
        border = c.border;
        textPrimary = c.textPrimary;
        textMuted = c.textMuted;
        warningColour = c.warning;
        accentColour = c.accent;
    } else {
        bg0 = findColour(juce::DocumentWindow::backgroundColourId);
        border = juce::Colours::grey;
        textPrimary = findColour(juce::Label::textColourId);
        textMuted = textPrimary.withAlpha(0.6f);
        warningColour = juce::Colours::orange;
        accentColour = juce::Colours::cyan;
    }

    const auto bounds = getLocalBounds().toFloat();

    // Background
    g.fillAll(bg0);

    // 1 px top separator
    g.setColour(border);
    g.drawHorizontalLine(0, bounds.getX(), bounds.getRight());

    const int padH = kPadH;
    const int textY = 1;
    const int textH = getHeight() - 2;

    // Reserve space for mute button on the right (28 px wide slot)
    const int muteSlotWidth = kMuteSlotWidth;
    const int rightEdge = getWidth() - muteSlotWidth - padH;

    g.setFont(juce::Font(11.0f));

    if (transientMessage_.isNotEmpty()) {
        // Transient message overrides the normal status text — draw it centred across the
        // full available width (left pad to right edge before the mute button slot).
        g.setColour(textPrimary);
        g.drawText(transientMessage_, padH, textY, rightEdge - padH, textH, juce::Justification::centredLeft, true);
    } else {
        // Normal status: patch name, CPU, voice count.

        // Patch name — left-aligned
        const juce::String patchStr = formatPatch(patchName_);
        g.setColour(textPrimary);
        g.drawText(patchStr, padH, textY, 160, textH, juce::Justification::centredLeft, true);

        // CPU — after patch name, warning colour if > 80 %
        const juce::String cpuStr = formatCpu(cpuPct_ / 100.0f);
        g.setColour(cpuPct_ > 80.0f ? warningColour : textMuted);
        g.drawText(cpuStr, kCpuX, textY, kCpuWidth, textH, juce::Justification::centredLeft, true);

        // Round trip — after CPU, and only while it actually fits before the voice count's own
        // slot (isRoundTripSegmentVisible() — shared with getTooltipForPosition() so the two can
        // never disagree about whether this segment is on screen).
        if (isRoundTripSegmentVisible()) {
            g.setColour(textMuted);
            g.drawText(roundTripText_, kRoundTripX, textY, kRoundTripWidth, textH, juce::Justification::centredLeft,
                       true);
        }

        // Transport cluster readout ("001.1.000   120.0 BPM") — after the play/stop glyph button
        // (see resized() for its bounds), and only while the whole cluster fits before the
        // voice-count slot; transportClusterFits_ is computed once in resized() rather than here
        // because the button is a live child component that resized() must actually hide, not just
        // skip drawing over — see the header comment. Accent while playing, same "obviously
        // running" cue as the button's own glyph colour.
        if (transportClusterFits_ && transportDisplayText_.isNotEmpty()) {
            g.setColour(transportPlaying_ ? accentColour : textMuted);
            const int transportTextX = kTransportX + kTransportButtonSize + kTransportTextGap;
            g.drawText(transportDisplayText_, transportTextX, textY, kTransportTextWidth, textH,
                       juce::Justification::centredLeft, true);
        }

        // Voice count — right-aligned before mute button
        const juce::String voiceStr = formatVoices(voices_);
        g.setColour(textMuted);
        g.drawText(voiceStr, rightEdge - kVoiceSlotWidth, textY, kVoiceSlotWidth, textH,
                   juce::Justification::centredRight, true);
    }
}

// ---------------------------------------------------------------------------
void StatusBarComponent::resized() {
    masterMuteButton_.setBounds(getWidth() - kMuteSlotWidth, 2, 20, getHeight() - 4);

    // Transport cluster fit check — the SAME "does it fit before the voice-count slot" gate the
    // round-trip segment uses in paint(), evaluated here too because the play/stop button is a live
    // child component and paint() alone cannot hide it (see the header's class comment).
    const int rightEdge = getWidth() - kMuteSlotWidth - kPadH;
    transportClusterFits_ = kTransportX + kTransportClusterWidth <= rightEdge - kVoiceSlotWidth - kPadH;
    transportButton_.setVisible(transportClusterFits_);
    if (transportClusterFits_) {
        const int buttonY = (getHeight() - kTransportButtonSize) / 2;
        transportButton_.setBounds(kTransportX, buttonY, kTransportButtonSize, kTransportButtonSize);
    }
}

// ---------------------------------------------------------------------------
juce::String StatusBarComponent::formatCpu(float fraction) {
    // fraction is 0.0 – 1.0 (or 0.0 – 100.0 normalised externally); the spec says
    // "0.756f -> 75.6%", meaning this receives a 0..1 value.
    const float pct = fraction * 100.0f;
    // One decimal place; avoid printing "-0.0%"
    const float clamped = std::max(0.0f, pct);
    return juce::String(clamped, 1) + "%";
}

// ---------------------------------------------------------------------------
juce::String StatusBarComponent::formatVoices(int n) {
    if (n == 1)
        return "1 voice";
    return juce::String(n) + " voices";
}

// ---------------------------------------------------------------------------
juce::String StatusBarComponent::formatPatch(const juce::String& s) {
    if (s.trim().isEmpty())
        return "Untitled";
    return s;
}

// ---------------------------------------------------------------------------
juce::String StatusBarComponent::formatRoundTrip(double milliseconds, bool available) {
    if (!available)
        return juce::String::fromUTF8("RT \xe2\x80\x94"); // em dash: "there is no round trip of ours"
    return "RT " + juce::String(std::max(0.0, milliseconds), 1) + " ms";
}
