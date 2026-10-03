#include "AppLookAndFeel.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/IconButton.h"

namespace synth::theme {

// Concern: text buttons, drawable (toolbar) buttons, icon buttons, and toggle buttons.

void AppLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;

    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    juce::Colour fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.darker(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.12f);

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
    const auto colourId =
        button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;
    const float alpha = !button.isEnabled() ? 0.5f : (paintsDimmed(button) ? kDisabledControlAlpha : 1.0f);
    g.setColour(button.findColour(colourId).withMultipliedAlpha(alpha));

    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

void AppLookAndFeel::drawDrawableButton(juce::Graphics& g, juce::DrawableButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    // LookAndFeel_V2::drawDrawableButton (the base every V4 build still falls through to) paints
    // its background with an unconditional g.fillAll() keyed only on toggle state - no hover/press
    // distinction and no rounding - so it cannot host the themed pill this component needs. We
    // fully own the background+label paint here instead. The icon itself is a CHILD Drawable
    // positioned by DrawableButton::resized()/getImageBounds() and painted separately — its
    // rest/hover/toggled-on tint comes from the three pre-tinted Drawable variants
    // MainComponent::applyToolbarIcons() hands to setImages() (muted/textPrimary/accent, mirroring
    // the label ladder below), not from anything painted in here.
    const auto& c = theme.colors;
    const auto& m = theme.metrics;

    const bool isOn = button.getToggleState();
    const bool isEnabled = button.isEnabled();
    const bool isHot = shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown; // hover OR press
    const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

    // Background pill. Disabled never fills. Toggled-on reads as "active", not as a big filled
    // button: a light ~13-15% accent wash, with hover/press only nudging it a couple of points
    // stronger — never the heavy ~0.22-0.30-alpha fill this replaced. Off-state hover/press get a
    // soft neutral surface wash instead.
    juce::Colour fill = juce::Colours::transparentBlack;
    if (isEnabled) {
        if (isOn)
            fill = c.accent.withAlpha(shouldDrawButtonAsDown ? 0.20f : (shouldDrawButtonAsHighlighted ? 0.15f : 0.13f));
        else if (shouldDrawButtonAsDown)
            fill = c.surfaceHi.withAlpha(0.85f);
        else if (shouldDrawButtonAsHighlighted)
            fill = c.surface.withAlpha(0.6f);
    }

    if (!fill.isTransparent()) {
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, m.pillRadius);
    }

    // A hairline accent stroke at low alpha reinforces "toggled on" without outlining the whole
    // pill heavily (the old 0.6-alpha ring read as a button, not a state).
    if (isEnabled && isOn) {
        g.setColour(c.accent.withAlpha(0.35f));
        g.drawRoundedRectangle(bounds, m.pillRadius, m.borderWidth);
    }

    // Label colour follows the same rest -> hover -> toggled-on ladder as the icon Drawable
    // variants: muted at rest, textPrimary on hover/press, full accent when toggled on, textDisabled
    // when disabled. Computed here from theme tokens directly rather than read off the stock
    // DrawableButton::textColourId/textColourOnId ColourIds (those stay themed in applyTheme()
    // only as a fallback for any stock JUCE draw path that bypasses this LookAndFeel).
    juce::Colour textColour = c.textMuted;
    if (!isEnabled)
        textColour = c.textDisabled;
    else if (isOn)
        textColour = c.accent;
    else if (isHot)
        textColour = c.textPrimary;

    // Label geometry: a fixed, readable size docked to the bottom of the strip with a small gap
    // above it — NOT LookAndFeel_V2's cropped-to-16px-then-25%-of-height formula, which is what
    // produced unreadable ~7-9px text at the toolbar's old 36px height.
    if (button.getStyle() == juce::DrawableButton::ImageAboveTextLabel && button.getButtonText().isNotEmpty()) {
        static constexpr float kLabelSize = 11.0f;
        static constexpr float kLabelBottomPad = 6.0f; // balances the ~8px top inset the icon gets
                                                       // from its own edgeIndent (kToolbarIconEdgeIndent
                                                       // in MainComponent::applyToolbarIcons()), so the
                                                       // icon+label block reads centred, not top-heavy.
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
constexpr float kRoundHoverAlpha = 0.18f;
constexpr float kRoundPressAlpha = 0.28f;
constexpr float kDangerHoverAlpha = 0.15f;
constexpr float kDangerPressAlpha = 0.28f;
constexpr float kFramedHoverBrighten = 0.15f;
} // namespace

juce::Colour iconButtonGlyphColour(const Theme& theme, const synth::ui::IconButton& button, bool highlighted,
                                   bool down) {
    const auto& c = theme.colors;
    if (!button.isEnabled())
        return c.textDisabled;
    if (button.getToggleState())
        return button.getOnTone() == synth::ui::IconButton::OnTone::Plain ? c.textPrimary
                                                                          : button.getOnColour().value_or(c.accent);
    if (highlighted || down)
        return button.getStyle() == synth::ui::IconButton::Style::Danger ? c.error : c.textPrimary;
    return c.textMuted;
}

namespace {
void paintIconButtonBackground(juce::Graphics& g, const synth::ui::IconButton& button, const Theme& theme, bool hot,
                               bool down) {
    using Style = synth::ui::IconButton::Style;
    const auto& c = theme.colors;
    const float radius = theme.metrics.cornerRadiusSmall;
    const auto full = button.getLocalBounds().toFloat();

    switch (button.getStyle()) {
    case Style::Framed: {
        const auto bounds = full.reduced(1.0f);
        const bool engaged = button.getToggleState() && button.getOnColour().has_value();
        g.setColour(hot ? c.surface.brighter(kFramedHoverBrighten) : c.surface);
        g.fillRoundedRectangle(bounds, radius);
        if (engaged) {
            g.setColour(button.getOnColour()->withAlpha(kOnWashAlpha));
            g.fillRoundedRectangle(bounds, radius);
        }
        g.setColour(engaged ? *button.getOnColour() : c.border);
        g.drawRoundedRectangle(bounds, radius, theme.metrics.borderWidth);
        break;
    }
    case Style::Bare:
        if (hot) {
            g.setColour(c.textPrimary.withAlpha(down ? kBarePressAlpha : kBareHoverAlpha));
            g.fillRoundedRectangle(full, radius);
        }
        break;
    case Style::Round:
        if (hot) {
            g.setColour(c.accent.withAlpha(down ? kRoundPressAlpha : kRoundHoverAlpha));
            g.fillEllipse(full);
        }
        break;
    case Style::Danger:
        if (hot) {
            g.setColour(c.error.withAlpha(down ? kDangerPressAlpha : kDangerHoverAlpha));
            g.fillRoundedRectangle(full, radius);
        }
        break;
    }
}
} // namespace

void paintIconButton(juce::Graphics& g, const synth::ui::IconButton& button, const Theme& theme, bool highlighted,
                     bool down) {
    using Style = synth::ui::IconButton::Style;
    const bool hot = button.isEnabled() && (highlighted || down);
    paintIconButtonBackground(g, button, theme, hot, down);

    const auto full = button.getLocalBounds().toFloat();
    const auto glyphArea = button.getStyle() == Style::Framed ? full.reduced(1.0f) : full;
    synth::theme::paintGlyph(g, button.currentGlyph(), glyphArea,
                             iconButtonGlyphColour(theme, button, highlighted, down));

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
    const bool on = button.getToggleState();
    const auto tick = button.findColour(juce::ToggleButton::tickColourId);
    g.setColour(on ? tick : c.surface);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(on ? tick : button.findColour(juce::ToggleButton::tickDisabledColourId));
    g.drawRoundedRectangle(bounds, radius, theme.metrics.borderWidth);

    const auto text = on ? c.bg0 : button.findColour(juce::ToggleButton::textColourId);
    g.setColour(text.withMultipliedAlpha(shouldDrawButtonAsHighlighted || on ? 1.0f : 0.9f));
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

    g.setColour(button.getToggleState() ? button.findColour(juce::ToggleButton::tickColourId) : c.surface);
    g.fillRoundedRectangle(box, 4.0f);

    g.setColour(button.getToggleState() ? button.findColour(juce::ToggleButton::tickColourId)
                                        : button.findColour(juce::ToggleButton::tickDisabledColourId));
    g.drawRoundedRectangle(box, 4.0f, theme.metrics.borderWidth);

    if (button.getToggleState()) {
        g.setColour(c.bg0);
        juce::Path tick;
        tick.startNewSubPath(box.getX() + box.getWidth() * 0.25f, box.getCentreY());
        tick.lineTo(box.getX() + box.getWidth() * 0.45f, box.getY() + box.getHeight() * 0.7f);
        tick.lineTo(box.getX() + box.getWidth() * 0.78f, box.getY() + box.getHeight() * 0.28f);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (button.getButtonText().isNotEmpty()) {
        g.setColour(button.findColour(juce::ToggleButton::textColourId)
                        .withMultipliedAlpha(shouldDrawButtonAsHighlighted ? 1.0f : 0.9f));
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
