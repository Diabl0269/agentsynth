// Concern: MixerIconButton's glyph ladder and the keyboard wiring every mixer icon button shares.
#include "MixerIconButton.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kIconEdgeIndent = 2;
}

// A Tab stop that acts on Return (a Button's own) and Space (registered here, since JUCE buttons ignore it), the
// toolbar's convention. Keys the button does not use bubble up to the mixer panel, so the column keys keep working.
MixerIconButton::MixerIconButton(const juce::String& name)
    : juce::DrawableButton(name, juce::DrawableButton::ImageFitted) {
    setClickingTogglesState(false);
    setWantsKeyboardFocus(true);
    addShortcut(juce::KeyPress(juce::KeyPress::spaceKey));
    setEdgeIndent(kIconEdgeIndent);
}

void MixerIconButton::setIcon(synth::theme::Icon icon) {
    icon_ = icon;
    hasIcon_ = true;
    applyImages();
}

// The provider is asked each time the tooltip is shown (and each time a screen reader asks for the help text), so
// a tooltip that names a rebindable shortcut follows a rebind without anyone rebuilding the row.
juce::String MixerIconButton::getTooltip() {
    return tooltipProvider ? tooltipProvider() : juce::DrawableButton::getTooltip();
}

// A right-click is never a click on the button: it goes to the row or header the button sits in, so their
// context menus open from anywhere along them.
void MixerIconButton::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu()) {
        juce::DrawableButton::mouseDown(e);
        return;
    }
    if (auto* parent = getParentComponent())
        parent->mouseDown(e.getEventRelativeTo(parent));
}

void MixerIconButton::mouseUp(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu()) {
        juce::DrawableButton::mouseUp(e);
        return;
    }
    if (auto* parent = getParentComponent())
        parent->mouseUp(e.getEventRelativeTo(parent));
}

void MixerIconButton::lookAndFeelChanged() {
    juce::DrawableButton::lookAndFeelChanged();
    applyImages();
}

// Headless-safe: without the themed look and feel (a unit test) or the asset library the button stays blank but
// still works. The three images are clones of one muted base, so the glyph steps through the same
// rest, hover, on ladder as the label text AppLookAndFeel::drawDrawableButton draws.
void MixerIconButton::applyImages() {
    if (!hasIcon_)
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;
    auto base = lf->getIcon(icon_);
    if (base == nullptr)
        return;
    const auto& colours = lf->getTheme().colors;
    auto hover = base->createCopy();
    hover->replaceColour(colours.textMuted, colours.textPrimary);
    auto on = base->createCopy();
    on->replaceColour(colours.textMuted, colours.accent);
    setImages(base.get(), hover.get(), hover.get(), nullptr, on.get(), on.get(), on.get());
}

} // namespace synth::ui
