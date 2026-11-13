// Concern: the "+ Add automation..." row's look, name and keys.
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationRow.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

namespace synth::ui {

namespace {
constexpr float kFontSize = 10.0f;
constexpr int kTextPadding = 8; // between the lane stripe's column and the text

struct RowColours {
    juce::Colour surface;
    juce::Colour border;
    juce::Colour text;
    juce::Colour textMuted;
};

RowColours coloursFor(const juce::Component& c) {
    const auto& t = synth::theme::themeOf(c).colors;
    return {t.surface, t.border, t.textPrimary, t.textMuted};
}
} // namespace

AddAutomationRow::AddAutomationRow(synth::TrackId track)
    : juce::Button("addAutomationRow")
    , track_(track) {
    setComponentID("addAutomationRow");
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [this] {
        if (onAddRequested)
            onAddRequested(track_);
    };
    setTrackName({});
}

void AddAutomationRow::setTrackName(const juce::String& trackName) {
    const auto text = "Add automation to " + trackName;
    setTitle(text);
    setTooltip(text);
}

void AddAutomationRow::setCompact(bool compact) {
    if (compact_ == compact)
        return;
    compact_ = compact;
    repaint();
}

// The "+" is a circle outline and a cross, muted until hovered or focused like the row's text.
void AddAutomationRow::paintCompact(juce::Graphics& g, bool highlighted) {
    const auto colours = coloursFor(*this);
    const bool lit = highlighted || hasKeyboardFocus(false);
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(lit ? colours.text : colours.textMuted);
    g.drawEllipse(area, 1.0f);
    const auto c = area.getCentre();
    const float arm = area.getWidth() * 0.22f;
    g.drawLine(c.x - arm, c.y, c.x + arm, c.y, 1.0f);
    g.drawLine(c.x, c.y - arm, c.x, c.y + arm, 1.0f);
    synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, getWidth() / 2.0f);
}

// Muted until hovered or focused, like the lane header's "..." button, so a stack of rows under a long
// list of tracks stays quiet.
void AddAutomationRow::paintButton(juce::Graphics& g, bool highlighted, bool) {
    if (compact_) {
        paintCompact(g, highlighted);
        return;
    }
    const auto colours = coloursFor(*this);
    const auto bounds = getLocalBounds();
    g.fillAll(colours.surface);
    g.setColour(colours.border);
    g.drawHorizontalLine(getHeight() - 1, (float)AutomationLaneHeaderComponent::kIndent, (float)getWidth());

    g.setColour(highlighted || hasKeyboardFocus(false) ? colours.text : colours.textMuted);
    g.setFont(juce::Font(juce::FontOptions(kFontSize)));
    const int left =
        AutomationLaneHeaderComponent::kIndent + AutomationLaneHeaderComponent::kStripeWidth + kTextPadding;
    g.drawText("+ Add automation...", bounds.withTrimmedLeft(left), juce::Justification::centredLeft, true);

    synth::ui::paintFocusRing(g, bounds.toFloat().reduced(1.0f), *this, 3.0f);
}

bool AddAutomationRow::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        if (isEnabled() && onClick)
            onClick();
        return true;
    }
    return juce::Button::keyPressed(key);
}

} // namespace synth::ui
