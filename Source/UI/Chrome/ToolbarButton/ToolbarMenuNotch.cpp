// The menu notch of a toolbar button (ClipSizingButton, SnapModeButton): part of the chip, so it is clipped to the
// chip's rounded border and sits in its lower-right corner, outside the glyph's box. The glyph keeps its full size and
// centred place, and a button with a notch and one without look alike. It follows the chip's press squash.
#include "ToolbarMenuNotch.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// A right triangle whose legs run this far along the chip's right and bottom edges, mixed this far from the accent
// toward the primary text, and dimmed while the button is disabled.
constexpr float kNotchLeg = 5.0f;
constexpr float kNotchTextMix = 0.7f;
constexpr float kNotchDisabledAlpha = 0.4f;
} // namespace

void paintToolbarMenuNotch(juce::Graphics& g, const ToolbarButton& button) {
    const auto& theme = synth::theme::themeOf(button);
    const auto chip = synth::theme::toolbarChipBounds(button);
    const auto squash = button.getChipSquash();
    const auto squashTransform = juce::AffineTransform::scale(squash.x, squash.y, chip.getCentreX(), chip.getCentreY());

    juce::Path chipPath;
    chipPath.addRoundedRectangle(chip, toolbarChipSpec(button.getLayout()).chipRadius);
    chipPath.applyTransform(squashTransform);
    juce::Path notch;
    notch.addTriangle(chip.getRight() - kNotchLeg, chip.getBottom(), chip.getRight(), chip.getBottom(), chip.getRight(),
                      chip.getBottom() - kNotchLeg);
    notch.applyTransform(squashTransform);

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(chipPath);
    const auto colour = theme.colors.accent.interpolatedWith(theme.colors.textPrimary, kNotchTextMix);
    g.setColour(button.isEnabled() ? colour : colour.withMultipliedAlpha(kNotchDisabledAlpha));
    g.fillPath(notch);
}

} // namespace synth::ui
