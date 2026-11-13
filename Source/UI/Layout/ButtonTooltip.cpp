// ButtonTooltip.cpp -- the tooltip rule for buttons: a labelled button's tooltip never repeats its label, an icon-only
// button's tooltip starts with its name.
#include "ButtonTooltip.h"
#include <optional>

namespace synth::ui {

namespace {
// The text after a leading copy of `label` used as a heading, or nullopt when `tip` does not open that way.
std::optional<juce::String> tooltipTextAfterLabel(const juce::String& label, const juce::String& tip) {
    if (label.isEmpty() || !tip.startsWithIgnoreCase(label))
        return std::nullopt;
    const auto rest = tip.substring(label.length());
    if (rest.isEmpty())
        return juce::String();
    for (const juce::String sep : {":", " -", " ("}) {
        if (rest.startsWith(sep))
            // " (" keeps the bracket: "Done (Esc)" leaves "(Esc)".
            return (sep == " (" ? rest.substring(1) : rest.substring(sep.length())).trimStart();
    }
    return std::nullopt;
}

juce::String tooltipCapitalised(const juce::String& text) {
    if (text.isEmpty())
        return text;
    const auto first = juce::CharacterFunctions::toUpperCase(text[0]); // not-ui-text: first letter only
    return juce::String::charToString(first) + text.substring(1);
}
} // namespace

bool buttonShowsText(const juce::Button& button) {
    if (button.getButtonText().trim().isEmpty())
        return false;
    return dynamic_cast<const juce::TextButton*>(&button) != nullptr ||
           dynamic_cast<const juce::ToggleButton*>(&button) != nullptr;
}

bool tooltipEchoesLabel(const juce::String& label, const juce::String& tip) {
    return tooltipTextAfterLabel(label.trim(), tip).has_value();
}

/** A labelled button drops a leading copy of its own words (the explanation reads on from them, so its first letter
    is capitalised); an icon-only button is named first ("Draw pen: ..."), unless the explanation already starts
    with the name ("Close the panel" on Close). Either way the screen reader still hears the name (the title) and then
   this text (the help). */
juce::String buttonTooltipFor(const juce::Button& button, const juce::String& explanation) {
    if (buttonShowsText(button)) {
        if (auto rest = tooltipTextAfterLabel(button.getButtonText().trim(), explanation))
            return tooltipCapitalised(*rest);
        return explanation;
    }
    const auto name = button.getTitle().isNotEmpty() ? button.getTitle() : button.getButtonText();
    if (name.isEmpty() || explanation.isEmpty() || explanation.startsWithIgnoreCase(name))
        return explanation;
    return name + ": " + explanation;
}

void setButtonTooltip(juce::Button& button, const juce::String& explanation) {
    button.setTooltip(buttonTooltipFor(button, explanation));
}

} // namespace synth::ui
