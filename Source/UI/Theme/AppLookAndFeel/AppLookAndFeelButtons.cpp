#include "AppLookAndFeel.h"
#include "UI/Layout/ButtonStateEase.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/IconButton.h"

namespace synth::theme {

using synth::ui::ButtonStateEase;

// Concern: text buttons, drawable (toolbar) buttons, icon buttons, and toggle buttons.

void AppLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;

    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    // Hover, press and toggled-on ease (ButtonStateEase). The off/on fills come from the button's own colours when
    // `backgroundColour` is one of them (a plain TextButton); a caller that passes its own colour keeps it.
    const auto offFill = button.findColour(juce::TextButton::buttonColourId);
    const auto onFill = button.findColour(juce::TextButton::buttonOnColourId);
    const bool ownsFill = backgroundColour == (button.getToggleState() ? onFill : offFill);
    const auto amounts = ButtonStateEase::update(button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    const juce::Colour fill = ButtonStateEase::blend(amounts, [&](bool on, bool hot, bool down) {
        const auto base = ownsFill ? (on ? onFill : offFill) : backgroundColour;
        return down ? base.darker(0.2f) : (hot ? base.brighter(0.12f) : base);
    });

    const float dim = paintsDimmed(button) ? kDisabledControlAlpha : 1.0f;
    g.setColour(fill.withMultipliedAlpha(dim));
    g.fillRoundedRectangle(bounds, m.pillRadius);

    // A juce::TextButton needs its own keyboard-focus indicator: LookAndFeel_V4's default draws
    // none, and Button::paint() only ever passes this function isOver()/isDown()
    // (juce_Button.cpp), never focus state. Reuses drawTextEditorOutline/drawComboBox's own
    // "accent outline when focused, same border weight" convention rather than inventing a new
    // style, so every plain TextButton in the app shows focus. The focused ring is the shared one
    // (thicker on a light theme, where a 1 px accent line beside the grey border read as no ring).
    g.setColour(c.border.withMultipliedAlpha(dim));
    g.drawRoundedRectangle(bounds, m.pillRadius, m.borderWidth);
    synth::ui::paintFocusRing(g, button.getLocalBounds().toFloat(), button, m.pillRadius);
}

juce::Font AppLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return juce::Font(juce::FontOptions((float)juce::jmin(15, buttonHeight - 6)));
}

void AppLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*shouldDrawButtonAsHighlighted*/,
                                    bool /*shouldDrawButtonAsDown*/) {
    g.setFont(getTextButtonFont(button, button.getHeight()));
    const auto textOff = button.findColour(juce::TextButton::textColourOffId);
    const auto textOn = button.findColour(juce::TextButton::textColourOnId);
    const float alpha = !button.isEnabled() ? 0.5f : (paintsDimmed(button) ? kDisabledControlAlpha : 1.0f);
    g.setColour(ButtonStateEase::mix(textOff, textOn, ButtonStateEase::current(button).on).withMultipliedAlpha(alpha));

    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

void AppLookAndFeel::drawDrawableButton(juce::Graphics& g, juce::DrawableButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    // LookAndFeel_V2::drawDrawableButton (the base every V4 build still falls through to) paints
    // its background with an unconditional g.fillAll() keyed only on toggle state - no hover/press
    // distinction and no rounding - so it cannot host the themed pill this component needs. We
    // fully own the background+label paint here instead. The icon itself is a CHILD Drawable
    // positioned by DrawableButton::resized()/getImageBounds() and painted separately — its
    // rest/hover/toggled-on tint comes from the pre-tinted Drawable variants the owner hands to
    // setImages() (MixerIconButton: muted/textPrimary/accent, mirroring the label ladder below), not
    // from anything painted in here. The top bar's buttons are ToolbarButtons, painted by
    // drawToolbarButton instead.
    const auto& c = theme.colors;
    const auto& m = theme.metrics;

    const bool isEnabled = button.isEnabled();
    const bool isHot = shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown; // hover OR press
    const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

    // Background pill. Disabled never fills. Toggled-on reads as "active", not as a big filled
    // button: a light ~13-15% accent wash, with hover/press only nudging it a couple of points
    // stronger — never the heavy ~0.22-0.30-alpha fill this replaced. Off-state hover/press get a
    // soft neutral surface wash instead.
    // Hover, press and toggled-on ease (ButtonStateEase); the rungs below are what each state settles to.
    const auto amounts = ButtonStateEase::update(button, isEnabled && isHot, isEnabled && shouldDrawButtonAsDown);
    const juce::Colour fill = ButtonStateEase::blend(amounts, [&](bool on, bool hot, bool down) {
        if (!isEnabled)
            return juce::Colours::transparentBlack;
        if (on)
            return c.accent.withAlpha(down ? 0.20f : (hot ? 0.15f : 0.13f));
        if (down)
            return c.surfaceHi.withAlpha(0.85f);
        return hot ? c.surface.withAlpha(0.6f) : juce::Colours::transparentBlack;
    });

    if (!fill.isTransparent()) {
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, m.pillRadius);
    }

    // A hairline accent stroke at low alpha reinforces "toggled on" without outlining the whole
    // pill heavily (the old 0.6-alpha ring read as a button, not a state). It fades with the toggle.
    if (isEnabled && amounts.on > 0.0f) {
        g.setColour(c.accent.withAlpha(0.35f * amounts.on));
        g.drawRoundedRectangle(bounds, m.pillRadius, m.borderWidth);
    }

    // Label colour follows the same rest -> hover -> toggled-on ladder as the icon Drawable
    // variants: muted at rest, textPrimary on hover/press, full accent when toggled on, textDisabled
    // when disabled. Computed here from theme tokens directly rather than read off the stock
    // DrawableButton::textColourId/textColourOnId ColourIds (those stay themed in applyTheme()
    // only as a fallback for any stock JUCE draw path that bypasses this LookAndFeel).
    const juce::Colour textColour = ButtonStateEase::blend(amounts, [&](bool on, bool hot, bool) {
        if (!isEnabled)
            return c.textDisabled;
        return on ? c.accent : (hot ? c.textPrimary : c.textMuted);
    });

    // Label geometry: a fixed, readable size docked to the bottom of the strip with a small gap
    // above it — NOT LookAndFeel_V2's cropped-to-16px-then-25%-of-height formula, which is what
    // produced unreadable ~7-9px text at the toolbar's old 36px height.
    if (button.getStyle() == juce::DrawableButton::ImageAboveTextLabel && button.getButtonText().isNotEmpty()) {
        static constexpr float kLabelSize = 11.0f;
        static constexpr float kLabelBottomPad = 6.0f; // balances the ~8px top inset an icon gets from
                                                       // its edgeIndent, so the icon+label block reads
                                                       // centred, not top-heavy.
        static constexpr float kLabelSideInset = 4.0f;

        const auto full = button.getLocalBounds().toFloat();
        const juce::Rectangle<float> textArea(kLabelSideInset, full.getBottom() - kLabelSize - kLabelBottomPad,
                                              juce::jmax(0.0f, full.getWidth() - 2.0f * kLabelSideInset), kLabelSize);

        g.setFont(juce::Font(kLabelSize));
        g.setColour(textColour);
        g.drawFittedText(button.getButtonText(), textArea.toNearestInt(), juce::Justification::centred, 1);
    }

    synth::ui::paintFocusRing(g, bounds, button, m.pillRadius);
}

// ---- the one icon button ---------------------------------------------------------------------
namespace {
// The wash behind an engaged button that has an on-colour (the record button), so "armed" reads from
// across the room and not only from the small glyph.
constexpr float kOnWashAlpha = 0.18f;
constexpr float kBareHoverAlpha = 0.08f;
constexpr float kBarePressAlpha = 0.14f;
constexpr float kBareOnAlpha = 0.13f; // the same lit wash drawDrawableButton gives a toggled-on button
constexpr float kBareOnHoverAlpha = 0.15f;
constexpr float kBareOnPressAlpha = 0.20f;
constexpr float kRoundHoverAlpha = 0.18f;
constexpr float kRoundPressAlpha = 0.28f;
constexpr float kDangerHoverAlpha = 0.15f;
constexpr float kDangerPressAlpha = 0.28f;
constexpr float kFramedHoverBrighten = 0.15f;
} // namespace

namespace {
using IconStyle = synth::ui::IconButton::Style;

// The glyph colour of each settled state: disabled, lit (the on-colour or accent), hot, then rest.
juce::Colour glyphColourFor(const Theme& theme, const synth::ui::IconButton& button, bool on, bool hot) {
    const auto& c = theme.colors;
    if (!button.isEnabled())
        return c.textDisabled;
    if (on)
        return button.getOnTone() == synth::ui::IconButton::OnTone::Plain ? c.textPrimary
                                                                          : button.getOnColour().value_or(c.accent);
    if (hot)
        return button.getStyle() == IconStyle::Danger ? c.error : c.textPrimary;
    return c.textMuted;
}

// Each background layer is a colour per settled state, so hover, press and toggled-on ease (ButtonStateEase).
juce::Colour framedFill(const Theme& theme, bool hot) {
    return hot ? theme.colors.surface.brighter(kFramedHoverBrighten) : theme.colors.surface;
}

juce::Colour framedWash(const synth::ui::IconButton& button, bool on) {
    const bool engaged = on && button.getOnColour().has_value();
    return engaged ? button.getOnColour()->withAlpha(kOnWashAlpha) : juce::Colours::transparentBlack;
}

juce::Colour framedBorder(const Theme& theme, const synth::ui::IconButton& button, bool on) {
    return on && button.getOnColour().has_value() ? *button.getOnColour() : theme.colors.border;
}

// The hover/press wash of the Bare, Round and Danger styles (transparent at rest).
juce::Colour washColour(const Theme& theme, const synth::ui::IconButton& button, bool on, bool hot, bool down) {
    const auto& c = theme.colors;
    switch (button.getStyle()) {
    case IconStyle::Bare:
        // Toggled on (with the accent tone) reads like a lit toolbar toggle: a light accent wash.
        if (button.isEnabled() && on && button.getOnTone() == synth::ui::IconButton::OnTone::Accent &&
            !button.getOnColour().has_value())
            return c.accent.withAlpha(down ? kBareOnPressAlpha : (hot ? kBareOnHoverAlpha : kBareOnAlpha));
        return hot ? c.textPrimary.withAlpha(down ? kBarePressAlpha : kBareHoverAlpha)
                   : juce::Colours::transparentBlack;
    case IconStyle::Round:
        return hot ? c.accent.withAlpha(down ? kRoundPressAlpha : kRoundHoverAlpha) : juce::Colours::transparentBlack;
    case IconStyle::Danger:
        return hot ? c.error.withAlpha(down ? kDangerPressAlpha : kDangerHoverAlpha) : juce::Colours::transparentBlack;
    case IconStyle::Framed:
        break;
    }
    return juce::Colours::transparentBlack;
}

void paintIconButtonBackground(juce::Graphics& g, const synth::ui::IconButton& button, const Theme& theme,
                               const ButtonStateEase::Amounts& amounts) {
    const float radius = theme.metrics.cornerRadiusSmall;
    const auto full = button.getLocalBounds().toFloat();

    if (button.getStyle() == IconStyle::Framed) {
        const auto bounds = full.reduced(1.0f);
        g.setColour(ButtonStateEase::blend(amounts, [&](bool, bool hot, bool) { return framedFill(theme, hot); }));
        g.fillRoundedRectangle(bounds, radius);
        const auto wash = ButtonStateEase::blend(amounts, [&](bool on, bool, bool) { return framedWash(button, on); });
        if (!wash.isTransparent()) {
            g.setColour(wash);
            g.fillRoundedRectangle(bounds, radius);
        }
        g.setColour(
            ButtonStateEase::blend(amounts, [&](bool on, bool, bool) { return framedBorder(theme, button, on); }));
        g.drawRoundedRectangle(bounds, radius, theme.metrics.borderWidth);
        return;
    }
    const auto wash = ButtonStateEase::blend(
        amounts, [&](bool on, bool hot, bool down) { return washColour(theme, button, on, hot, down); });
    if (wash.isTransparent())
        return;
    g.setColour(wash);
    if (button.getStyle() == IconStyle::Round)
        g.fillEllipse(full);
    else
        g.fillRoundedRectangle(full, radius);
}
} // namespace

juce::Colour iconButtonGlyphColour(const Theme& theme, const synth::ui::IconButton& button, bool highlighted,
                                   bool down) {
    return glyphColourFor(theme, button, button.getToggleState(), highlighted || down);
}

void paintIconButton(juce::Graphics& g, const synth::ui::IconButton& button, const Theme& theme, bool highlighted,
                     bool down) {
    using Style = synth::ui::IconButton::Style;
    const bool hot = button.isEnabled() && (highlighted || down);
    const auto amounts = ButtonStateEase::update(button, hot, button.isEnabled() && down);
    paintIconButtonBackground(g, button, theme, amounts);

    const auto full = button.getLocalBounds().toFloat();
    const auto glyphArea = button.getStyle() == Style::Framed ? full.reduced(1.0f) : full;
    synth::theme::paintGlyph(
        g, button.currentGlyph(), glyphArea,
        ButtonStateEase::blend(amounts, [&](bool on, bool h, bool) { return glyphColourFor(theme, button, on, h); }));

    // juce::Button::paint() hands paintButton() only isOver()/isDown(), never focus state, so the
    // ring is drawn here; Button repaints on focus changes itself.
    const bool round = button.getStyle() == Style::Round;
    const float ringRadius = round ? full.getWidth() * 0.5f : theme.metrics.cornerRadiusSmall;
    (button.forceFocusRingForTest ? synth::ui::paintFocusRingAlways : synth::ui::paintFocusRing)(g, full, button,
                                                                                                 ringRadius);
}

void AppLookAndFeel::drawIconButton(juce::Graphics& g, synth::ui::IconButton& button,
                                    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    paintIconButton(g, button, theme, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

void AppLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                                      bool /*shouldDrawButtonAsDown*/) {
    paintToggleButton(g, button, shouldDrawButtonAsHighlighted, button.hasKeyboardFocus(false));
}

void AppLookAndFeel::paintToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool keyboardFocused) {
    if (button.getProperties()[kTogglePillProperty]) {
        paintTogglePill(g, button, shouldDrawButtonAsHighlighted, keyboardFocused);
        return;
    }
    // A disabled or dimmed toggle paints whole at reduced alpha; a dimmed one (still focusable) keeps
    // its focus ring at full strength, outside the layer.
    const bool dimmed = paintsDimmed(button);
    if (dimmed)
        g.beginTransparencyLayer(kDisabledControlAlpha);
    paintTickToggle(g, button, shouldDrawButtonAsHighlighted, keyboardFocused && !dimmed);
    if (dimmed) {
        g.endTransparencyLayer();
        if (keyboardFocused)
            paintTickToggle(g, button, false, true, /*focusRingOnly*/ true);
    }
}

bool AppLookAndFeel::paintsDimmed(const juce::Component& component) {
    if (!component.isEnabled() || (bool)component.getProperties()[kDimmedProperty])
        return true;
    const auto* parent = component.getParentComponent();
    return parent != nullptr && (bool)parent->getProperties()[kDimmedProperty];
}

int AppLookAndFeel::togglePillWidth(const juce::String& text) {
    constexpr int kSidePadding = 10;
    return uiTextWidth(text, kTogglePillFontHeight) + 2 * kSidePadding;
}

// The footer row's small toggle: a rounded pill, filled with the tick colour when on (text in the
// background colour), an outlined surface when off; the accent focus ring hugs the pill.
void AppLookAndFeel::paintTogglePill(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                                     bool keyboardFocused) {
    const auto& c = theme.colors;
    const bool disabled = paintsDimmed(button);
    if (disabled)
        g.beginTransparencyLayer(kDisabledControlAlpha);

    const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const float radius = bounds.getHeight() * 0.5f;
    // Toggled-on and hover ease (ButtonStateEase).
    const auto amounts = ButtonStateEase::update(button, shouldDrawButtonAsHighlighted, false);
    const auto tick = button.findColour(juce::ToggleButton::tickColourId);
    const auto outline = button.findColour(juce::ToggleButton::tickDisabledColourId);
    const auto textOff = button.findColour(juce::ToggleButton::textColourId);
    g.setColour(ButtonStateEase::mix(c.surface, tick, amounts.on));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(ButtonStateEase::mix(outline, tick, amounts.on));
    g.drawRoundedRectangle(bounds, radius, theme.metrics.borderWidth);

    const float textAlpha = juce::jmax(0.9f + 0.1f * amounts.hover, 0.9f + 0.1f * amounts.on);
    g.setColour(ButtonStateEase::mix(textOff, c.bg0, amounts.on).withMultipliedAlpha(textAlpha));
    g.setFont(uiFont(kTogglePillFontHeight));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);

    if (disabled)
        g.endTransparencyLayer();
    if (keyboardFocused)
        synth::ui::paintFocusRingAlways(g, bounds, button, radius);
}

// `focusRingOnly` paints just the ring, over a box a dimmed toggle already painted inside its layer.
void AppLookAndFeel::paintTickToggle(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                                     bool keyboardFocused, bool focusRingOnly) {
    const auto& c = theme.colors;

    // The focus ring stands off the box by a gap (a ticked box is filled with the same accent, so a
    // ring touching it would merge into it). The box shrinks only when the row is too short for the
    // ring to fit inside the component.
    constexpr float kRingGap = 1.0f;
    const float ringExtent = kRingGap + synth::ui::focusRingThickness(theme);
    const float boxSize = juce::jmin(18.0f, (float)button.getHeight() - 2.0f * ringExtent);
    juce::Rectangle<float> box(4.0f, ((float)button.getHeight() - boxSize) * 0.5f, boxSize, boxSize);
    if (focusRingOnly) {
        if (keyboardFocused)
            synth::ui::paintFocusRingAlways(g, box.expanded(ringExtent), button, 4.0f + ringExtent);
        return;
    }

    // Toggled-on and hover ease (ButtonStateEase): the box fills and the tick fades in.
    const auto amounts = ButtonStateEase::update(button, shouldDrawButtonAsHighlighted, false);
    const auto tickColour = button.findColour(juce::ToggleButton::tickColourId);
    g.setColour(ButtonStateEase::mix(c.surface, tickColour, amounts.on));
    g.fillRoundedRectangle(box, 4.0f);

    g.setColour(
        ButtonStateEase::mix(button.findColour(juce::ToggleButton::tickDisabledColourId), tickColour, amounts.on));
    g.drawRoundedRectangle(box, 4.0f, theme.metrics.borderWidth);

    if (amounts.on > 0.0f) {
        g.setColour(c.bg0.withMultipliedAlpha(amounts.on));
        juce::Path tick;
        tick.startNewSubPath(box.getX() + box.getWidth() * 0.25f, box.getCentreY());
        tick.lineTo(box.getX() + box.getWidth() * 0.45f, box.getY() + box.getHeight() * 0.7f);
        tick.lineTo(box.getX() + box.getWidth() * 0.78f, box.getY() + box.getHeight() * 0.28f);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (button.getButtonText().isNotEmpty()) {
        g.setColour(
            button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(0.9f + 0.1f * amounts.hover));
        g.setFont(juce::Font(juce::FontOptions(theme.type.label + 2.0f)));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft((int)boxSize + 10),
                         juce::Justification::centredLeft, 1);
    }

    if (keyboardFocused) {
        g.setColour(c.bg0);
        g.drawRoundedRectangle(box.expanded(kRingGap * 0.5f), 4.0f + kRingGap * 0.5f, kRingGap);
        synth::ui::paintFocusRingAlways(g, box.expanded(ringExtent), button, 4.0f + ringExtent);
    }
}

} // namespace synth::theme
