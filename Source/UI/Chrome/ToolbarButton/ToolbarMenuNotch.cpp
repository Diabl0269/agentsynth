// The menu notch of a toolbar button (ClipSizingButton, SnapModeButton): part of the chip, in its lower-right corner,
// outside the glyph's box, so the glyph stays centred and a button with a notch and one without look alike. It follows
// the chip's press squash.
#include "ToolbarMenuNotch.h"

#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// The notch is the shared side-menu notch (SideMenuMark.h): 8 px a leg, in the theme ink that reads on the colour the
// chip is painted in right now (toolbarChipColour: the tint at its current hover and lit amounts), over either bar
// colour it can sit on, easing to the lit glyph's ink as the button lights, so it holds 3:1 in every theme and state.
void paintToolbarMenuNotch(juce::Graphics& g, const ToolbarButton& button) {
    const auto& theme = synth::theme::themeOf(button);
    const auto& c = theme.colors;
    const auto chip = synth::theme::toolbarChipColour(button, theme);
    const auto ink = sideMenuNotchInk(c, {c.bg0.overlaidWith(chip), c.bg1.overlaidWith(chip)},
                                      toolbarLitInk(theme, button.getGroup()), button.getLitAmount());
    paintSideMenuNotch(g, synth::theme::toolbarChipBounds(button), toolbarChipSpec(button.getLayout()).chipRadius,
                       button.getChipSquash(), ink, button.isEnabled());
}

} // namespace synth::ui
