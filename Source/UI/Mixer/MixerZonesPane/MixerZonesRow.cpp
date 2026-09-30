// Concern: the Zones pane's row, eye toggle, "Show all" link and group heading -- their painting,
// accessibility text and mouse handling.
#include "MixerZonesRow.h"

namespace synth::ui {

namespace {
constexpr int kEyeWidth = 24;
constexpr int kSwatchWidth = 8;
constexpr int kInset = 6;
} // namespace

MixerZonesEye::MixerZonesEye()
    : juce::Button("Visibility") {
    setClickingTogglesState(false);
    setWantsKeyboardFocus(false);
    setDescription("Shows or hides this channel in the mixer. Alt-click shows only this channel.");
}

void MixerZonesEye::clicked(const juce::ModifierKeys& mods) {
    if (mods.isAltDown() && onSoloShow)
        onSoloShow();
    else if (onToggle)
        onToggle();
}

// The Button's toggle state is "shown"; a disabled eye (Master) is drawn dim and never reacts.
void MixerZonesEye::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto* theme = zonesThemeOf(*this);
    const auto text = theme != nullptr ? theme->colors.textPrimary : juce::Colour(0xffEAEEF3);
    const auto accent = theme != nullptr ? theme->colors.accent : juce::Colour(0xff00D1FF);
    float alpha = getToggleState() ? 0.9f : 0.45f;
    if (!isEnabled())
        alpha = 0.3f;
    const auto colour = (highlighted && isEnabled()) ? accent : text.withAlpha(alpha);

    const auto area = juce::Rectangle<float>(14.0f, 9.0f).withCentre(getLocalBounds().toFloat().getCentre());
    juce::Path lid;
    lid.startNewSubPath(area.getX(), area.getCentreY());
    lid.quadraticTo(area.getCentreX(), area.getY() - 4.0f, area.getRight(), area.getCentreY());
    lid.quadraticTo(area.getCentreX(), area.getBottom() + 4.0f, area.getX(), area.getCentreY());
    g.setColour(colour);
    g.strokePath(lid, juce::PathStrokeType(1.2f));
    g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(area.getCentre()));
    if (!getToggleState())
        g.drawLine(area.getX() + 1.0f, area.getBottom() + 1.5f, area.getRight() - 1.0f, area.getY() - 1.5f, 1.4f);
}

MixerZonesLink::MixerZonesLink(const juce::String& text)
    : juce::Button(text) {
    setWantsKeyboardFocus(false);
    setTitle(text);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MixerZonesLink::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto* theme = zonesThemeOf(*this);
    const auto accent = theme != nullptr ? theme->colors.accent : juce::Colour(0xff00D1FF);
    g.setColour(isEnabled() ? (highlighted ? accent.brighter(0.3f) : accent) : accent.withAlpha(0.35f));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centredRight, false);
    if (isEnabled()) {
        const auto width = juce::Font(juce::FontOptions(11.0f)).getStringWidthFloat(getButtonText());
        g.fillRect(static_cast<float>(getWidth()) - width, static_cast<float>(getHeight() / 2 + 7), width, 1.0f);
    }
}

MixerZonesRow::MixerZonesRow(Hooks hooks)
    : hooks_(std::move(hooks)) {
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    addAndMakeVisible(eye_);
    eye_.onToggle = [this] {
        if (hooks_.onToggleHidden)
            hooks_.onToggleHidden(channel_.id);
    };
    eye_.onSoloShow = [this] {
        if (hooks_.onSoloShow)
            hooks_.onSoloShow(channel_.id);
    };
}

void MixerZonesRow::setChannel(const MixerZoneChannel& channel) {
    channel_ = channel;
    const bool master = channel_.kind == MixerZoneChannelKind::Master;
    juce::String zone = channel_.zone == synth::MixerZone::Left    ? "Left zone"
                        : channel_.zone == synth::MixerZone::Right ? "Right zone"
                                                                   : "Scrolling";
    setTitle(channel_.name);
    setDescription(zone + (channel_.hidden ? ", hidden. " : ". ") +
                   "Drag to another group to pin it, or press Alt+Up or Alt+Down. Space shows or hides it.");
    eye_.setToggleState(!channel_.hidden, juce::dontSendNotification);
    eye_.setEnabled(!master);
    eye_.setTitle(channel_.name + " visibility");
    eye_.setTooltip(master ? "Master cannot be hidden"
                           : juce::String(channel_.hidden ? "Show " : "Hide ") + channel_.name +
                                 " (Alt-click: show only this channel)");
    repaint();
}

void MixerZonesRow::setLift(float lift) {
    if (lift == lift_)
        return;
    lift_ = lift;
    repaint();
}

void MixerZonesRow::setKeyboardCursor(bool shown) {
    if (shown == cursor_)
        return;
    cursor_ = shown;
    repaint();
}

void MixerZonesRow::resized() { eye_.setBounds(getLocalBounds().removeFromRight(kEyeWidth)); }

void MixerZonesRow::paint(juce::Graphics& g) {
    const auto* theme = zonesThemeOf(*this);
    const auto surface = theme != nullptr ? theme->colors.surfaceHi : juce::Colour(0xff232833);
    const auto text = theme != nullptr ? theme->colors.textPrimary : juce::Colour(0xffEAEEF3);
    const auto accent = theme != nullptr ? theme->colors.accent : juce::Colour(0xff00D1FF);

    auto bounds = getLocalBounds();
    if (lift_ > 0.0f) {
        g.setColour(surface);
        g.fillRoundedRectangle(bounds.toFloat().reduced(1.0f), 3.0f);
        g.setColour(accent.withMultipliedAlpha(lift_));
        g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), 3.0f, 1.0f);
    } else if (isMouseOver(true)) {
        g.setColour(surface.withAlpha(0.6f));
        g.fillRoundedRectangle(bounds.toFloat().reduced(1.0f), 3.0f);
    }

    if (cursor_) {
        g.setColour(accent);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f, 1.5f);
    }

    const float dim = channel_.hidden ? 0.45f : 1.0f;
    bounds.removeFromRight(kEyeWidth);
    bounds.removeFromLeft(kInset);
    g.setColour(channel_.colour.withMultipliedAlpha(dim));
    g.fillRoundedRectangle(bounds.removeFromLeft(kSwatchWidth).reduced(0, 6).toFloat(), 2.0f);
    bounds.removeFromLeft(kInset);
    g.setColour(text.withMultipliedAlpha(dim));
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(channel_.name, bounds, juce::Justification::centredLeft, true);
}

void MixerZonesRow::mouseDown(const juce::MouseEvent& e) {
    if (hooks_.onGrab && !e.mods.isPopupMenu())
        hooks_.onGrab(channel_.id, e);
}

void MixerZonesRow::mouseDrag(const juce::MouseEvent& e) {
    if (hooks_.onDrag && !e.mods.isPopupMenu())
        hooks_.onDrag(e);
}

void MixerZonesRow::mouseUp(const juce::MouseEvent& e) {
    if (hooks_.onRelease)
        hooks_.onRelease(e);
}

MixerZonesGroupHeader::MixerZonesGroupHeader(synth::MixerZone zone)
    : zone_(zone) {
    setInterceptsMouseClicks(false, false);
    setTitle(titleFor(zone));
}

juce::String MixerZonesGroupHeader::titleFor(synth::MixerZone zone) {
    switch (zone) {
    case synth::MixerZone::Left:
        return "Left zone";
    case synth::MixerZone::Right:
        return "Right zone";
    case synth::MixerZone::Scrolling:
        break;
    }
    return "Scrolling";
}

void MixerZonesGroupHeader::paint(juce::Graphics& g) {
    const auto* theme = zonesThemeOf(*this);
    g.setColour(theme != nullptr ? theme->colors.textMuted : juce::Colour(0xff8A93A0));
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText(getDisplayText(), getLocalBounds().withTrimmedLeft(kInset), juce::Justification::bottomLeft, false);
}

} // namespace synth::ui
