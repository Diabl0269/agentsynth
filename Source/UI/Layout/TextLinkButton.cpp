#include "TextLinkButton.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

TextLinkButton::TextLinkButton(const juce::String& text, juce::Justification justification)
    : juce::Button(text)
    , justification_(justification) {
    setTitle(text);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void TextLinkButton::setJustification(juce::Justification justification) {
    justification_ = justification;
    repaint();
}

void TextLinkButton::paintButton(juce::Graphics& g, bool highlighted, bool) {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        lf->drawTextLink(g, *this, justification_, highlighted);
    else
        synth::theme::paintTextLink(g, *this, synth::theme::themeOf(*this), justification_, highlighted);
}

} // namespace synth::ui
