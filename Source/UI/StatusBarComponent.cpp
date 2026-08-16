#include "StatusBarComponent.h"
#include "Theme/AppLookAndFeel.h"

// ---------------------------------------------------------------------------
StatusBarComponent::StatusBarComponent() { addAndMakeVisible(masterMuteButton_); }

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
    juce::Colour bg0, border, textPrimary, textMuted, warningColour;
    if (lnf) {
        const auto& c = lnf->getTheme().colors;
        bg0 = c.bg0;
        border = c.border;
        textPrimary = c.textPrimary;
        textMuted = c.textMuted;
        warningColour = c.warning;
    } else {
        bg0 = findColour(juce::DocumentWindow::backgroundColourId);
        border = juce::Colours::grey;
        textPrimary = findColour(juce::Label::textColourId);
        textMuted = textPrimary.withAlpha(0.6f);
        warningColour = juce::Colours::orange;
    }

    const auto bounds = getLocalBounds().toFloat();

    // Background
    g.fillAll(bg0);

    // 1 px top separator
    g.setColour(border);
    g.drawHorizontalLine(0, bounds.getX(), bounds.getRight());

    const int padH = 6;
    const int textY = 1;
    const int textH = getHeight() - 2;

    // Reserve space for mute button on the right (28 px wide slot)
    const int muteSlotWidth = 28;
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
        g.drawText(cpuStr, 170, textY, 60, textH, juce::Justification::centredLeft, true);

        // TL6-8 round trip — after CPU, and only while it actually fits before the voice count's
        // own slot. A cramped window drops this segment rather than overlapping two readings.
        const int roundTripX = 236;
        const int roundTripW = 90;
        if (roundTripText_.isNotEmpty() && roundTripX + roundTripW <= rightEdge - 80 - padH) {
            g.setColour(textMuted);
            g.drawText(roundTripText_, roundTripX, textY, roundTripW, textH, juce::Justification::centredLeft, true);
        }

        // Voice count — right-aligned before mute button
        const juce::String voiceStr = formatVoices(voices_);
        g.setColour(textMuted);
        g.drawText(voiceStr, rightEdge - 80, textY, 80, textH, juce::Justification::centredRight, true);
    }
}

// ---------------------------------------------------------------------------
void StatusBarComponent::resized() { masterMuteButton_.setBounds(getWidth() - 28, 2, 20, getHeight() - 4); }

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
