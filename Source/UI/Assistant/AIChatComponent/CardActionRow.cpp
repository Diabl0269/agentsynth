#include "CardActionRow.h"
#include "UI/Layout/TextLinkButton.h"

namespace synth::ui::cardactions {

// Concern: the one placement rule for the action row of every AI card (edit plan, song plan, build steps).
//
// An AI card's actions sit in ONE row, quiet to loud: quiet options (Show details) are text links at the far
// left, and a stop action (Cancel) is an outlined StopButton in the same place; a second action is an outlined button
// left of the primary; the primary is filled with the accent in the right corner. Buttons are 24 px tall in a 28 px
// row, 4 px apart. The row never wraps; at narrow width the link loses its side padding first.
void layout(juce::Rectangle<int> rowRect, const Row& row) {
    const int top = rowRect.getY() + (rowRect.getHeight() - kButtonHeight) / 2;
    int right = rowRect.getRight();
    int used = 0; // what the buttons take, with the gaps between pieces
    auto placeRight = [&](juce::Component* c, int w) {
        if (c == nullptr)
            return;
        c->setBounds(right - w, top, w, kButtonHeight);
        right -= w + kButtonGap;
        used += w + kButtonGap;
    };
    placeRight(row.primary, row.primaryWidth);
    placeRight(row.middle, row.middleWidth);
    if (row.link == nullptr)
        return;
    // The link keeps its ideal width while it fits, then gives up its side padding (the text stays whole).
    const int room = rowRect.getWidth() - used;
    const int minWidth = juce::jmax(0, row.linkWidth - 2 * synth::ui::kTextLinkPadX);
    const int width = juce::jmax(minWidth, juce::jmin(row.linkWidth, room));
    row.link->setBounds(rowRect.getX(), top, width, kButtonHeight);
}

int fitWidth(const juce::TextButton& button, int pad, int minWidth) {
    const juce::Font font(12.0f);
    return juce::jmax(minWidth,
                      juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, button.getButtonText())) + 2 * pad);
}

void applyOutlinedStyle(juce::TextButton& button, const synth::theme::Colors& colours) {
    button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    button.setColour(juce::TextButton::textColourOffId, colours.textPrimary);
    button.setColour(juce::TextButton::textColourOnId, colours.textPrimary);
    // The 1 px border is the look-and-feel's own (colours.border); a transparent fill leaves just the outline.
}

} // namespace synth::ui::cardactions
