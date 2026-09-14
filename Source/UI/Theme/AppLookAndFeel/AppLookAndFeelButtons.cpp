#include "AppLookAndFeel.h"

namespace synth::theme {

// Concern: text buttons, drawable (toolbar) buttons, and toggle buttons.

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

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, m.pillRadius);

    // Founder review round 4 (T152/T153 macro Configure I/O dialog testing): a juce::TextButton
    // had no visible keyboard-focus indicator anywhere in the app — LookAndFeel_V4's default draws
    // none, and Button::paint() only ever passes this function isOver()/isDown()
    // (juce_Button.cpp), never focus state. Reuses drawTextEditorOutline/drawComboBox's own
    // "accent outline when focused, same border weight" convention rather than inventing a new
    // style, so every plain TextButton in the app (not just this one dialog) now shows focus.
    g.setColour(button.hasKeyboardFocus(true) ? c.accent : c.border);
    g.drawRoundedRectangle(bounds, m.pillRadius, m.borderWidth);
}

juce::Font AppLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return juce::Font(juce::FontOptions((float)juce::jmin(15, buttonHeight - 6)));
}

void AppLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*shouldDrawButtonAsHighlighted*/,
                                    bool /*shouldDrawButtonAsDown*/) {
    g.setFont(getTextButtonFont(button, button.getHeight()));
    const auto colourId =
        button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;
    g.setColour(button.findColour(colourId).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

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
}

void AppLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                                      bool /*shouldDrawButtonAsDown*/) {
    const auto& c = theme.colors;

    const float boxSize = juce::jmin(18.0f, (float)button.getHeight() - 2.0f);
    juce::Rectangle<float> box(4.0f, ((float)button.getHeight() - boxSize) * 0.5f, boxSize, boxSize);

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
}

} // namespace synth::theme
