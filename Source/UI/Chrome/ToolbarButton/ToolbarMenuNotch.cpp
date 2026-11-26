// The menu notch of a toolbar button (ClipSizingButton, SnapModeButton): part of the chip, in its lower-right corner,
// outside the glyph's box, so the glyph stays centred and a button with a notch and one without look alike. It follows
// the chip's press squash.
#include "ToolbarMenuNotch.h"

#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// The notch is the shared side-menu notch (SideMenuMark.h): the chip's own colour as painted right now
// (toolbarChipColour: the tint at its current hover and lit amounts, over either bar colour it can sit on) pushed
// darker on a light chip and lighter on a dark one, so it holds 3.5:1 in every theme and state.
void paintToolbarMenuNotch(juce::Graphics& g, const ToolbarButton& button) {
    const auto& theme = synth::theme::themeOf(button);
    const auto& c = theme.colors;
    const auto chip = synth::theme::toolbarChipColour(button, theme);
    const auto ground = synth::theme::toolbarGroundColour(button, theme);
    const auto ink =
        sideMenuNotchInk({c.bg0.overlaidWith(ground).overlaidWith(chip), c.bg1.overlaidWith(ground).overlaidWith(chip)},
                         toolbarLitInk(theme, button.getGroup()), button.getLitAmount());
    paintSideMenuNotch(g, synth::theme::toolbarChipBounds(button), toolbarChipSpec(button.getLayout()).chipRadius,
                       button.getChipSquash(), ink, button.isEnabled());
}

} // namespace synth::ui
