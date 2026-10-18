#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentPicker.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kTextIndent = 8.0f;
constexpr float kCaretSize = 10.0f;
constexpr float kCaretMargin = 8.0f;
constexpr float kFontSize = 12.0f;
} // namespace

SamplerInstrumentPicker::SamplerInstrumentPicker()
    : juce::Button("Instrument") {
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setInstrument({}, {});
}

void SamplerInstrumentPicker::setInstrument(const juce::String& name, const juce::String& origin) {
    name_ = name;
    origin_ = name.isEmpty() ? juce::String() : origin;
    setTitle(name.isEmpty() ? juce::String("Instrument, none") : "Instrument, " + name);
    setTooltip("Choose an instrument (Enter)");
    repaint();
}

// A closed combo's look: the shared chip with the name at the left and the shared caret at the right.
void SamplerInstrumentPicker::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat();
    synth::theme::paintChip(g, bounds, theme, {highlighted, down});

    const float caretLeft = bounds.getRight() - kCaretMargin - kCaretSize;
    const auto textArea = juce::Rectangle<float>(bounds.getX() + kTextIndent, bounds.getY(),
                                                 caretLeft - bounds.getX() - kTextIndent, bounds.getHeight());
    const bool empty = name_.isEmpty();
    juce::AttributedString text;
    text.setWordWrap(juce::AttributedString::none);
    text.setJustification(juce::Justification::centredLeft);
    const juce::Font font{juce::FontOptions(kFontSize)};
    text.append(empty ? juce::String("Choose an instrument") : name_, font,
                empty ? theme.colors.textMuted : theme.colors.textPrimary);
    if (origin_.isNotEmpty())
        text.append(juce::String(" ") + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " " + origin_, font,
                    theme.colors.textMuted);
    text.draw(g, textArea);

    const auto caretArea =
        juce::Rectangle<float>(caretLeft, bounds.getCentreY() - kCaretSize * 0.5f, kCaretSize, kCaretSize);
    synth::theme::paintDisclosureChevron(g, caretArea, caret_.openness(), theme.colors.textMuted);
    paintFocusRing(g, bounds, *this, theme.metrics.cornerRadiusSmall);
}

bool SamplerInstrumentPicker::keyPressed(const juce::KeyPress& key) {
    if (isEnabled() && (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)) {
        if (onClick)
            onClick();
        return true;
    }
    return false;
}

} // namespace synth::ui
