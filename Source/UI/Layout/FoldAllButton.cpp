#include "UI/Layout/FoldAllButton.h"

#include "UI/Layout/FocusRing.h"

// Concern: the shared "Collapse all" / "Expand all" strip button (see the header).

namespace synth::ui {

namespace {
constexpr float kTextAlpha = 0.65f;
constexpr float kHoverTextAlpha = 1.0f;
constexpr float kFontHeight = 11.0f;
constexpr float kFocusRingRadius = 3.0f;

juce::String labelFor(bool allFolded) { return allFolded ? "Expand all" : "Collapse all"; }
juce::String tooltipFor(bool allFolded) { return allFolded ? "Unfold every section" : "Fold every section"; }
} // namespace

FoldAllButton::FoldAllButton()
    : juce::Button(labelFor(false)) {
    setTooltip(tooltipFor(false));
}

void FoldAllButton::setAllFolded(bool allFolded) {
    if (allFolded == allFolded_)
        return;
    allFolded_ = allFolded;
    setButtonText(labelFor(allFolded_));
    setTooltip(tooltipFor(allFolded_));
    repaint();
}

// Right-aligned and drawn small, exactly as the library sidebar's is: it is chrome, not a button, and
// must not compete with the section headers.
void FoldAllButton::paintButton(juce::Graphics& g, bool highlighted, bool /*down*/) {
    g.setColour(findColour(juce::Label::textColourId).withAlpha(highlighted ? kHoverTextAlpha : kTextAlpha));
    g.setFont(juce::Font(juce::FontOptions(kFontHeight)));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centredRight);
    paintFocusRing(g, getLocalBounds().toFloat(), *this, kFocusRingRadius);
}

} // namespace synth::ui
