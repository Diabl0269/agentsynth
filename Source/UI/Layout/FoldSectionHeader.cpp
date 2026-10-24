// Concern: the folding-section header row shared by the Settings tabs.
#include "UI/Layout/FoldSectionHeader.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kChevronSize = 8;
constexpr float kHeaderTextAlpha = 0.85f;
constexpr float kHeaderHoverTextAlpha = 1.0f;
constexpr float kHeaderRuleAlpha = 0.12f;
constexpr float kSummaryAlpha = 0.6f;
constexpr int kTextInset = 22;
constexpr int kSummaryGap = 12;
constexpr int kRightInset = 8;
} // namespace

void paintFoldSectionHeader(juce::Graphics& g, const juce::Button& button, const juce::String& name,
                            const ChevronTurn& turn, bool hot, const juce::String& summary) {
    const auto textColour = button.findColour(juce::Label::textColourId);
    const bool lit = hot || button.hasKeyboardFocus(false);
    const auto bounds = button.getLocalBounds();
    synth::theme::paintDisclosureChevron(g,
                                         juce::Rectangle<float>(6.0f, (float)(bounds.getHeight() - kChevronSize) * 0.5f,
                                                                (float)kChevronSize, (float)kChevronSize),
                                         turn.openness(), synth::theme::themeOf(button), lit);
    const auto nameFont = juce::Font(juce::FontOptions(12.5f, juce::Font::bold));
    g.setColour(textColour.withAlpha(lit ? kHeaderHoverTextAlpha : kHeaderTextAlpha));
    g.setFont(nameFont);
    g.drawText(name, bounds.withTrimmedLeft(kTextInset), juce::Justification::centredLeft);
    if (summary.isNotEmpty()) {
        const int summaryLeft = kTextInset + juce::GlyphArrangement::getStringWidthInt(nameFont, name) + kSummaryGap;
        g.setColour(textColour.withAlpha(kSummaryAlpha));
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText(summary, bounds.withTrimmedLeft(summaryLeft).withTrimmedRight(kRightInset),
                   juce::Justification::centredLeft);
    }
    g.setColour(textColour.withAlpha(kHeaderRuleAlpha));
    g.fillRect(0, bounds.getBottom() - 1, bounds.getWidth(), 1);
    paintFocusRing(g, bounds.toFloat().reduced(1.0f), button);
}

FoldSectionHeader::FoldSectionHeader(const juce::String& name)
    : juce::Button(name)
    , name_(name) {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    refreshTitle();
    turn_.snapTo(true);
    onClick = [this] {
        if (onOpenRequested)
            onOpenRequested(!open_);
    };
}

void FoldSectionHeader::setSummary(const juce::String& summary) {
    if (summary == summary_)
        return;
    summary_ = summary;
    repaint();
}

void FoldSectionHeader::setOpen(bool open) {
    open_ = open;
    refreshTitle();
    turn_.setOpen(open);
}

// The accessible name carries the state, so a screen reader hears it change.
void FoldSectionHeader::refreshTitle() { setTitle(name_ + (open_ ? ", expanded" : ", collapsed")); }

void FoldSectionHeader::paintButton(juce::Graphics& g, bool hot, bool /*down*/) {
    paintFoldSectionHeader(g, *this, name_, turn_, hot, summary_);
}

} // namespace synth::ui
