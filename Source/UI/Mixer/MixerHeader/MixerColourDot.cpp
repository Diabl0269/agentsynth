// Concern: MixerColourDot's paint, keyboard activation and the press/drag/click split with the header.
#include "MixerColourDot.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kDotWidth = 10.0f;
constexpr float kDotVerticalInset = 2.0f;
constexpr float kDotRadius = 2.0f;
constexpr float kFocusRadius = 3.0f;
} // namespace

MixerColourDot::MixerColourDot()
    : juce::Button("mixerColourDot") {
    setComponentID("mixerColourDot");
    setClickingTogglesState(false);
    setWantsKeyboardFocus(true);
    addShortcut(juce::KeyPress(juce::KeyPress::spaceKey)); // Return is a Button's own
}

void MixerColourDot::setColour(juce::Colour colour) {
    if (colour_ == colour)
        return;
    colour_ = colour;
    repaint();
}

// The dot is centred in the button, so the button can be taller and wider than the painted swatch (a bigger
// target) without the swatch itself growing. A disabled dot (a column with nothing to recolour) paints the same
// swatch with no hover wash, so the column's colour still reads.
void MixerColourDot::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto area = getLocalBounds().toFloat();
    const auto dot =
        area.withSizeKeepingCentre(kDotWidth, juce::jmax(0.0f, area.getHeight() - 2.0f * kDotVerticalInset));

    const bool hot = isEnabled() && (highlighted || down);
    g.setColour(down ? colour_.darker(0.15f) : (hot ? colour_.brighter(0.2f) : colour_));
    g.fillRoundedRectangle(dot, kDotRadius);
    if (hot) {
        const auto edge = laf != nullptr ? laf->getTheme().colors.textPrimary : juce::Colour(0xffEAEEF3);
        g.setColour(edge.withAlpha(0.55f));
        g.drawRoundedRectangle(dot.expanded(1.5f), kDotRadius + 1.0f, 1.0f);
    }
    paintFocusRing(g, area, *this, kFocusRadius);
}

bool MixerColourDot::consumeDragFlag() noexcept {
    const bool wasDrag = suppressClick_;
    suppressClick_ = false;
    return wasDrag;
}

void MixerColourDot::mouseDown(const juce::MouseEvent& e) {
    suppressClick_ = false;
    juce::Button::mouseDown(e);
    if (dragForwarding.onPress && !e.mods.isPopupMenu())
        dragForwarding.onPress(e);
}

void MixerColourDot::mouseDrag(const juce::MouseEvent& e) {
    juce::Button::mouseDrag(e);
    if (dragForwarding.onDrag && !e.mods.isPopupMenu())
        dragForwarding.onDrag(e);
}

// The button decides whether this release is a click while it handles it, so the "was this a drag" answer has to
// be read first and held in suppressClick_ for the click handler. The header's release hook runs last: when the
// press was a drag it commits the reorder, which rebuilds the mixer and destroys this dot, so nothing may follow it.
void MixerColourDot::mouseUp(const juce::MouseEvent& e) {
    const bool wasDrag = dragForwarding.isDragging && dragForwarding.isDragging();
    suppressClick_ = wasDrag;
    juce::Button::mouseUp(e);
    if (wasDrag && dragForwarding.onRelease)
        dragForwarding.onRelease(e);
}

} // namespace synth::ui
