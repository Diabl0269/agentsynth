// The menu notch of a toolbar button (ClipSizingButton, SnapModeButton): part of the chip, in its lower-right corner,
// outside the glyph's box, so the glyph stays centred and a button with a notch and one without look alike. It follows
// the chip's press squash.
#include "ToolbarMenuNotch.h"

#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// The chip's group-colour alpha the notch must read on: the rest and lit ends of the chip's tint, both of them.
constexpr float kChipAlphaRest = 0.10f;
constexpr float kChipAlphaHover = 0.26f;
} // namespace

// The notch is the shared side-menu notch (SideMenuMark.h): 8 px a leg, in whichever theme ink reads best on the chip's
// tint at rest and on hover, so it holds 3:1 in every theme.
void paintToolbarMenuNotch(juce::Graphics& g, const ToolbarButton& button) {
    const auto& theme = synth::theme::themeOf(button);
    const auto hue = toolbarGroupHue(theme, button.getGroup());
    const auto& c = theme.colors;
    const auto ink = sideMenuNotchInk(
        c, {c.bg0.overlaidWith(hue.withAlpha(kChipAlphaRest)), c.bg0.overlaidWith(hue.withAlpha(kChipAlphaHover)),
            c.bg1.overlaidWith(hue.withAlpha(kChipAlphaRest)), c.bg1.overlaidWith(hue.withAlpha(kChipAlphaHover))});
    paintSideMenuNotch(g, synth::theme::toolbarChipBounds(button), toolbarChipSpec(button.getLayout()).chipRadius,
                       button.getChipSquash(), ink, button.isEnabled());
}

} // namespace synth::ui
