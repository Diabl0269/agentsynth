// Concern: the audio track header's Monitor (speaker) button -- its three looks, its words and its accessibility.
#include "UI/Timeline/TrackMonitorButton.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kGlyphSize = 11.0f;
constexpr float kCorner = 3.0f;

// The screen reader reads the title, then this value, so the mode is spoken whenever the button is reached.
class ModeValue : public juce::AccessibilityTextValueInterface {
public:
    explicit ModeValue(TrackMonitorButton& button)
        : button_(button) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return button_.getModeText(); }
    void setValueAsString(const juce::String&) override {}

private:
    TrackMonitorButton& button_;
};

// A speaker: a small box, the cone opening to the right, and two sound arcs.
juce::Path speakerGlyph(juce::Rectangle<float> area) {
    juce::Path path;
    const float x = area.getX(), y = area.getY(), w = area.getWidth(), h = area.getHeight();
    path.startNewSubPath(x, y + h * 0.35f);
    path.lineTo(x + w * 0.25f, y + h * 0.35f);
    path.lineTo(x + w * 0.55f, y + h * 0.1f);
    path.lineTo(x + w * 0.55f, y + h * 0.9f);
    path.lineTo(x + w * 0.25f, y + h * 0.65f);
    path.lineTo(x, y + h * 0.65f);
    path.closeSubPath();
    return path;
}
} // namespace

TrackMonitorButton::TrackMonitorButton()
    : juce::Button("trackMonitorButton") {
    setComponentID("trackMonitorButton");
    setClickingTogglesState(false); // the document is the truth; setState shows it
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Monitor");
    setState(synth::MonitorMode::Auto, false, {});
}

void TrackMonitorButton::setState(synth::MonitorMode mode, bool armed, const juce::String& trackName) {
    const bool changed = mode != mode_ || armed != armed_;
    mode_ = mode;
    armed_ = armed;
    const juce::String name = trackName.isNotEmpty() ? trackName : juce::String("this track");
    setTooltip("Hear this track's input live. Monitor: " + getModeText() +
               ". Click to switch between On and Auto (Auto plays " + name +
               "'s input while it is armed and stopped or recording).");
    if (changed) {
        repaint();
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    }
}

int TrackMonitorButton::getLitLevel() const noexcept {
    if (mode_ == synth::MonitorMode::On)
        return 2;
    return mode_ == synth::MonitorMode::Auto && armed_ ? 1 : 0;
}

juce::String TrackMonitorButton::getModeText() const {
    switch (mode_) {
    case synth::MonitorMode::On:
        return "On";
    case synth::MonitorMode::Off:
        return "Off";
    case synth::MonitorMode::Auto:
        break;
    }
    return armed_ ? "Auto, hearing the input" : "Auto";
}

void TrackMonitorButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const int lit = getLitLevel();
    if (lit == 2) {
        g.setColour(colors.accent);
        g.fillRoundedRectangle(bounds, kCorner);
    } else if (highlighted || down) {
        g.setColour(colors.textPrimary.withAlpha(down ? 0.18f : 0.1f));
        g.fillRoundedRectangle(bounds, kCorner);
    }
    if (lit == 1) {
        g.setColour(colors.accent);
        g.drawRoundedRectangle(bounds, kCorner, 1.2f);
    }

    const auto glyph = juce::Rectangle<float>(kGlyphSize, kGlyphSize).withCentre(bounds.getCentre());
    const auto ink = lit == 2 ? colors.bg0 : (lit == 1 ? colors.accent : colors.textMuted);
    g.setColour(ink);
    g.fillPath(speakerGlyph(glyph));
    if (mode_ == synth::MonitorMode::Off) { // a slash through the waves: Off never plays the input
        g.drawLine(glyph.getRight() - kGlyphSize * 0.35f, glyph.getY(), glyph.getRight(), glyph.getBottom(), 1.2f);
        return;
    }
    juce::Path waves;
    const auto centre = juce::Point<float>(glyph.getX() + kGlyphSize * 0.55f, glyph.getCentreY());
    for (const float radius : {kGlyphSize * 0.2f, kGlyphSize * 0.4f})
        waves.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::MathConstants<float>::pi * 0.25f,
                            juce::MathConstants<float>::pi * 0.75f, true);
    g.strokePath(waves, juce::PathStrokeType(1.1f));
}

// A right-click opens the row's menu (like M/S/R): the button's own down state is never entered, so no click fires.
void TrackMonitorButton::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onPopupMenuRequested)
            onPopupMenuRequested();
        return;
    }
    juce::Button::mouseDown(e);
}

std::unique_ptr<juce::AccessibilityHandler> TrackMonitorButton::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                               [this] {
                                                   if (onClick)
                                                       onClick();
                                               }),
        juce::AccessibilityHandler::Interfaces{std::make_unique<ModeValue>(*this)});
}

} // namespace synth::ui
