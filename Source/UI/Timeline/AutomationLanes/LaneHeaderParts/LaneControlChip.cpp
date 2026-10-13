// Concern: the lane header control chip -- its paint (picture, hover, lit, grip, accent ring), its names and its keys.
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlChip.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

LaneControlChip::LaneControlChip(std::optional<LaneControl> control, LaneControlKind kind)
    : juce::Button("laneControl")
    , control_(control)
    , kind_(kind) {
    setComponentID(control.has_value() ? juce::String("laneControl.") + laneControlInfo(*control).key
                                       : juce::String("laneControlMore"));
    setWantsKeyboardFocus(true);
    // A click never moves focus off the clips, whose Cmd+X/C/V the app routes by where real focus sits.
    setMouseClickGrabsKeyboardFocus(false);
    setClickingTogglesState(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    applyNames();
}

void LaneControlChip::applyNames() {
    if (!control_.has_value()) {
        const auto text = pencil_ ? juce::String("Editing controls") : juce::String("More actions");
        setTitle(text);
        setTooltip(pencil_ ? "Editing controls: press Esc or Done to finish" : "More actions for this lane");
        return;
    }
    const auto name = laneControlName(*control_, kind_);
    // In edit mode a press shows or hides the control instead of running it, and the names say so.
    if (editing_) {
        setTitle(dimmed_ ? "Show " + name + " on the lane" : "Hide " + name + " from the lane");
        setTooltip(dimmed_ ? name + " (hidden): click to show on the lane" : name + ": click to hide from the lane");
        return;
    }
    setTitle(name);
    setTooltip(laneControlTooltip(*control_, kind_) + detail_);
}

void LaneControlChip::setDetail(const juce::String& detail) {
    if (detail_ == detail)
        return;
    detail_ = detail;
    applyNames();
}

void LaneControlChip::setIconColour(std::optional<juce::Colour> colour) {
    if (iconColour_ == colour)
        return;
    iconColour_ = colour;
    repaint();
}

void LaneControlChip::setLit(bool lit) {
    if (lit_ == lit)
        return;
    lit_ = lit;
    repaint();
}

void LaneControlChip::setEditing(bool editing) {
    if (editing_ == editing)
        return;
    editing_ = editing;
    applyNames();
    repaint();
}

void LaneControlChip::setDimmed(bool dimmed) {
    if (dimmed_ == dimmed)
        return;
    dimmed_ = dimmed;
    applyNames();
    repaint();
}

void LaneControlChip::setPencil(bool pencil) {
    if (pencil_ == pencil)
        return;
    pencil_ = pencil;
    setToggleState(pencil_, juce::dontSendNotification); // a screen reader hears it as pressed
    applyNames();
    repaint();
}

void LaneControlChip::setVisualScale(float scale) {
    if (visualScale_ == scale)
        return;
    visualScale_ = scale;
    repaint();
}

void LaneControlChip::setLifted(bool lifted) {
    if (lifted_ == lifted)
        return;
    lifted_ = lifted;
    repaint();
}

void LaneControlChip::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto& colours = theme.colors;
    const auto full = getLocalBounds().toFloat();
    const float scale = std::max(0.0f, visualScale_) * (lifted_ ? 1.03f : 1.0f);
    g.addTransform(juce::AffineTransform::scale(scale, scale, full.getCentreX(), full.getCentreY()));
    const auto body = juce::Rectangle<float>(std::min(full.getWidth(), full.getHeight()), full.getHeight())
                          .withCentre(full.getCentre());
    const bool accentTint = pencil_ || (editing_ && !dimmed_);
    auto fill = colours.surfaceHi.withAlpha(highlighted || down ? 0.9f : 0.0f);
    if (lit_ || pencil_)
        fill = (lit_ && !pencil_ ? juce::Colour(0xffe0a33d) : colours.accent).withAlpha(0.18f);
    else if (accentTint)
        fill = colours.accent.withAlpha(0.1f);
    const float alpha = (isEnabled() ? 1.0f : 0.4f) * (dimmed_ ? 0.62f : 1.0f);
    g.setOpacity(alpha);
    g.setColour(fill);
    g.fillRoundedRectangle(body, 5.0f);
    g.setColour((pencil_ || lit_ ? colours.accent : colours.border).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(body.reduced(0.5f), 5.0f, 1.0f);

    auto icon = body.withSizeKeepingCentre(14.0f, 14.0f);
    if (editing_ && !isMoreButton()) {
        g.setColour(colours.textMuted.withMultipliedAlpha(alpha));
        for (int i = 0; i < 3; ++i)
            g.fillEllipse(juce::Rectangle<float>(1.6f, 1.6f)
                              .withCentre({body.getX() + 4.0f, body.getCentreY() + (i - 1) * 3.2f}));
        icon = icon.translated(2.0f, 0.0f);
    }
    if (isMoreButton()) {
        const auto colour = colours.textPrimary.withMultipliedAlpha(alpha);
        if (pencil_)
            paintLanePencilIcon(g, icon, colour);
        else
            paintLanePlusIcon(g, icon, colour);
    } else {
        paintLaneControlIcon(g, *control_, icon,
                             iconColour_.value_or(laneControlColour(*control_, theme)).withMultipliedAlpha(alpha));
    }
    g.setOpacity(1.0f);
    synth::ui::paintFocusRing(g, body, *this, 5.0f);
}

// In edit mode a press is the start of a possible drag, so the strip decides between a click and a drag.
void LaneControlChip::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onContextMenu)
            onContextMenu(e.getScreenPosition());
        return;
    }
    if (editing_ && !isMoreButton()) {
        if (onEditPress)
            onEditPress(*this, e);
        return;
    }
    juce::Button::mouseDown(e);
}

void LaneControlChip::mouseDrag(const juce::MouseEvent& e) {
    if (editing_ && !isMoreButton()) {
        if (onEditDrag)
            onEditDrag(*this, e);
        return;
    }
    juce::Button::mouseDrag(e);
}

void LaneControlChip::mouseUp(const juce::MouseEvent& e) {
    if (editing_ && !isMoreButton()) {
        if (onEditRelease)
            onEditRelease(*this, e);
        return;
    }
    juce::Button::mouseUp(e);
}

// Return and Space press at once (juce::Button posts Return's click to the message queue and has no Space handling).
// In edit mode the strip takes Left/Right, Alt+Left/Right, Space and Esc first. Every other key (Up/Down,
// Cmd+Alt+Up/Down) bubbles to the header.
bool LaneControlChip::keyPressed(const juce::KeyPress& key) {
    if (onEditKey && onEditKey(*this, key))
        return true;
    if (key.getModifiers().testFlags(juce::ModifierKeys::allKeyboardModifiers))
        return false;
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        if (isEnabled() && onClick)
            onClick();
        return true;
    }
    return false;
}

} // namespace synth::ui
