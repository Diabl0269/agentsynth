#include "HelpBuildStrip.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kStripPadX = 14;
constexpr int kStripLabelButtonGap = 16;
constexpr int kStripWaveSegments = 48;
constexpr float kStripCorner = 8.0f;
constexpr float kStripWaveAlpha = 0.28f;
constexpr float kStripPi2 = 6.2831853f;

float fracStrip(float v) { return v - std::floor(v); }

float sineAtStrip(float x) { return std::sin(kStripPi2 * x); }
float triangleAtStrip(float x) {
    const float t = fracStrip(x);
    return t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f);
}
float sawAtStrip(float x) { return 2.0f * fracStrip(x + 0.5f) - 1.0f; }
float squareAtStrip(float x) { return fracStrip(x) < 0.5f ? 1.0f : -1.0f; }
} // namespace

HelpBuildStrip::HelpBuildStrip() {
    label_.setText("Agent Synth is free and open source, built by one person. If it has earned a place in "
                   "your setup, you can help build it. No pressure, it stays free either way.",
                   juce::dontSendNotification);
    label_.setFont(juce::Font(juce::FontOptions(13.0f)));
    label_.setJustificationType(juce::Justification::centredLeft);
    label_.setMinimumHorizontalScale(1.0f);
    label_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label_);

    button_.setButtonText("Help build Agent Synth");
    addAndMakeVisible(button_);
}

HelpBuildStrip::~HelpBuildStrip() { driver_.stop(vblank_); }

float HelpBuildStrip::waveformSample(float x01, float shape) {
    const float s = juce::jlimit(0.0f, 4.0f, shape);
    const int a = juce::jmin(3, (int)std::floor(s));
    const float t = s - (float)a;
    const auto at = [x01](int kind) {
        switch (kind % 4) {
        case 0:
            return sineAtStrip(x01);
        case 1:
            return triangleAtStrip(x01);
        case 2:
            return sawAtStrip(x01);
        default:
            return squareAtStrip(x01);
        }
    };
    // Shape 4 is a sine again: the last segment blends the square back into it.
    const float from = at(a);
    const float to = at(a + 1);
    return t >= 1.0f ? to : from + (to - from) * t;
}

void HelpBuildStrip::resized() {
    auto row = getLocalBounds().reduced(kStripPadX, 12);
    const int buttonWidth = HelpBuildButton::preferredWidth(button_.getButtonText());
    button_.setBounds(row.removeFromRight(buttonWidth).withSizeKeepingCentre(buttonWidth, HelpBuildButton::kHeight));
    row.removeFromRight(kStripLabelButtonGap);
    label_.setBounds(row);
}

void HelpBuildStrip::paint(juce::Graphics& g) {
    const auto& c = theme::themeOf(*this).colors;
    const auto panel = getLocalBounds().toFloat().reduced(0.5f);
    juce::Path shape;
    shape.addRoundedRectangle(panel, kStripCorner);

    // The wash: accent into rose across the panel, so the button is the one loud thing.
    g.setGradientFill(juce::ColourGradient(c.accent.withAlpha(0.14f), panel.getX(), panel.getY(),
                                           c.hueRose.withAlpha(0.10f), panel.getRight(), panel.getBottom(), false));
    g.fillPath(shape);

    // The waveform, clipped to the panel: shape 0 to 4 over the cycle (about 3 s per waveform), a sine at rest.
    g.saveState();
    g.reduceClipRegion(shape, {});
    const float morph = looping_ ? phase_ * 4.0f : 0.0f;
    juce::Path wave;
    for (int i = 0; i <= kStripWaveSegments; ++i) {
        const float x01 = (float)i / (float)kStripWaveSegments;
        const float y = panel.getCentreY() - waveformSample(x01 * 4.0f, morph) * panel.getHeight() * 0.26f;
        const float x = panel.getX() + x01 * panel.getWidth();
        if (i == 0)
            wave.startNewSubPath(x, y);
        else
            wave.lineTo(x, y);
    }
    g.setGradientFill(juce::ColourGradient(c.accent.withAlpha(kStripWaveAlpha), panel.getX(), 0.0f,
                                           c.hueRose.withAlpha(kStripWaveAlpha), panel.getRight(), 0.0f, false));
    g.strokePath(wave, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));

    // The glow behind the button: a few soft rings that breathe with the cycle.
    const float breathe = looping_ ? 0.5f - 0.5f * std::cos(kStripPi2 * fracStrip(phase_ * 4.0f)) : 0.4f;
    const auto glowBase = button_.getBounds().toFloat();
    for (int i = 1; i <= 5; ++i) {
        const float a = (0.55f + 0.4f * breathe) * 0.10f * (1.0f - (float)i / 6.0f);
        g.setColour(c.accent.withAlpha(a));
        g.fillRoundedRectangle(glowBase.expanded((float)i * 1.2f), 9.0f + (float)i);
    }
    g.restoreState();

    g.setColour(c.accent.withAlpha(0.50f));
    g.drawRoundedRectangle(panel, kStripCorner, 1.0f);
}

void HelpBuildStrip::setPhaseForTest(float phase) {
    looping_ = true;
    phase_ = phase;
    button_.setMotion(true);
    button_.setPhase(phase);
}

// Motion runs while the strip is on screen and the app animates; otherwise the strip rests (a sine, the button in
// its resting pose) and schedules no frames.
void HelpBuildStrip::updateLoop() {
    const bool allowed = isShowing() && !animationsOff() && !prefersReducedMotion();
    if (allowed == looping_)
        return;
    looping_ = allowed;
    button_.setMotion(looping_);
    if (looping_) {
        startCycle();
    } else {
        driver_.stop(vblank_);
        phase_ = 0.0f;
        repaint();
    }
}

void HelpBuildStrip::startLoopForTest() {
    looping_ = true;
    button_.setMotion(true);
    startCycle();
}

void HelpBuildStrip::completeCycleForTest() { driver_.completeForTest(); }

void HelpBuildStrip::startCycle() {
    driver_.start(
        vblank_, kCycleMs, [](float t) { return t; },
        [this](float t) {
            phase_ = t;
            button_.setPhase(t);
            repaint();
        },
        [this] {
            // The setting may have changed during the cycle: under Animations Off a new cycle would land and call
            // back at once, restarting itself without end, so a cycle restarts only while motion is still allowed.
            if (!looping_)
                return;
            if (animationsOff() || prefersReducedMotion())
                updateLoop();
            else
                startCycle();
        });
}

} // namespace synth::ui
