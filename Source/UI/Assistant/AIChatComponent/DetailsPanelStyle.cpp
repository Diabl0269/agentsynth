#include "DetailsPanelStyle.h"
#include "AIChatComponent.h"

namespace synth::detailspanel {

// Concern: the shared look and sizing of a card's Details panel.

void style(juce::TextEditor& panel, const synth::theme::Colors& colours) {
    panel.setFont(juce::Font(kFontSize));
    panel.setIndents(kIndentX, kIndentY);
    panel.setLineSpacing(kLineSpacing);
    panel.setColour(juce::TextEditor::textColourId, colours.textPrimary);
    panel.setColour(juce::TextEditor::backgroundColourId, colours.bg0.withAlpha(0.6f));
    panel.setColour(juce::TextEditor::outlineColourId, colours.border);
    // Text already in the panel keeps the colour it was added with, so a theme switch recolours it too.
    panel.applyColourToAllText(colours.textPrimary);
}

int heightFor(const juce::StringArray& lines, int width, int maxHeight) {
    const juce::Font font(kFontSize);
    const int textWidth = juce::jmax(40, width - 2 * kIndentX - 12); // 12: the scroll bar's lane
    const int lineHeight = juce::roundToInt(font.getHeight() * kLineSpacing);
    int height = 2 * kIndentY + 4;
    for (const auto& line : lines) {
        const int wrapped =
            line.isEmpty() ? lineHeight : AIChatComponent::computeWrappedTextHeight(font, line, textWidth) - 2;
        height += juce::jmax(lineHeight, juce::roundToInt(static_cast<float>(wrapped) * kLineSpacing));
        if (height >= maxHeight)
            return maxHeight;
    }
    return juce::jlimit(kMinHeight, maxHeight, height);
}

} // namespace synth::detailspanel
