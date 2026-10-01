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
    juce::Colour surface{0xff1B1F26};
    juce::Colour border{0xff2A2F38};
    juce::Colour text{0xffEAEEF3};
    juce::Colour textMuted{0xff8A93A0};
};

RowColours coloursFor(const juce::Component& c) {
    RowColours result;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel())) {
        const auto& t = lf->getTheme().colors;
        result = {t.surface, t.border, t.textPrimary, t.textMuted};
    }
    return result;
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

// Muted until hovered or focused, like the lane header's "..." button, so a stack of rows under a long
// list of tracks stays quiet.
void AddAutomationRow::paintButton(juce::Graphics& g, bool highlighted, bool) {
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
