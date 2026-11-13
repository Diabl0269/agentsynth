#include "UsageStatsPromptComponent.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kPadX = 14;
constexpr int kPadY = 12;
constexpr int kButtonRowHeight = 30;
constexpr int kButtonWidth = 110;
} // namespace

void UsageStatsPromptComponent::LinkButton::paint(juce::Graphics& g) {
    juce::HyperlinkButton::paint(g);
    paintFocusRing(g, getLocalBounds().toFloat(), *this, 3.0f);
}

void UsageStatsPromptComponent::LinkButton::lookAndFeelChanged() {
    setColour(juce::HyperlinkButton::textColourId, synth::theme::themeOf(*this).colors.accent);
}

UsageStatsPromptComponent::UsageStatsPromptComponent() {
    setTitle("Help make Agent Synth better");
    setDescription("Share anonymous usage statistics: which features and modules get used, and how long sessions "
                   "last. Never your audio, projects, prompts, file names or account. Off unless you say yes, and "
                   "you can turn it off any time in Preferences.");

    headingLabel.setText("Help make Agent Synth better", juce::dontSendNotification);
    headingLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(headingLabel);

    bodyLabel.setText("Share anonymous usage statistics: which features and modules get used, and how long sessions "
                      "last. Never your audio, projects, prompts, file names or account. Off unless you say yes, "
                      "and you can turn it off any time in Preferences.",
                      juce::dontSendNotification);
    bodyLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    bodyLabel.setJustificationType(juce::Justification::topLeft);
    bodyLabel.setMinimumHorizontalScale(1.0f);
    bodyLabel.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.8f));
    addAndMakeVisible(bodyLabel);

    confirmationLabel.setText("Thanks. You can change this in Preferences.", juce::dontSendNotification);
    confirmationLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    confirmationLabel.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(confirmationLabel);

    // Same style and size on purpose: declining is as easy and as visible as agreeing.
    shareButton.setTitle("Share");
    shareButton.setTooltip("Send a daily anonymous summary of feature use");
    shareButton.onClick = [this] {
        if (onChoice)
            onChoice(true);
    };
    addAndMakeVisible(shareButton);

    noThanksButton.setTitle("No thanks");
    noThanksButton.setTooltip("Don't share usage statistics");
    noThanksButton.onClick = [this] {
        if (onChoice)
            onChoice(false);
    };
    addAndMakeVisible(noThanksButton);

    learnMoreButton.setTitle("What we collect");
    learnMoreButton.setTooltip("Open the privacy policy section on usage statistics");
    learnMoreButton.setFont(juce::Font(juce::FontOptions(13.0f)), false, juce::Justification::centredRight);
    learnMoreButton.onClick = [this] {
        if (onLearnMoreRequested)
            onLearnMoreRequested();
    };
    addAndMakeVisible(learnMoreButton);
}

void UsageStatsPromptComponent::showConfirmation(bool show) {
    shareButton.setVisible(!show);
    noThanksButton.setVisible(!show);
    learnMoreButton.setVisible(!show);
    confirmationLabel.setVisible(show);
    confirmationLabel.setAlpha(1.0f);
}

void UsageStatsPromptComponent::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto area = juce::Rectangle<int>(getWidth(), kHeight).toFloat().reduced(0.5f);
    g.setColour(c.accent.withAlpha(0.08f));
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(c.accent.withAlpha(0.55f));
    g.drawRoundedRectangle(area, 8.0f, 1.0f);
}

void UsageStatsPromptComponent::resized() {
    // Always the full-height layout from the top edge: a shrinking panel clips it instead of squeezing it.
    auto area = juce::Rectangle<int>(getWidth(), kHeight).reduced(kPadX, kPadY);
    headingLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);
    bodyLabel.setBounds(area.removeFromTop(54));
    area.removeFromTop(10);
    auto row = area.removeFromTop(kButtonRowHeight);
    confirmationLabel.setBounds(row);
    shareButton.setBounds(row.removeFromLeft(kButtonWidth));
    row.removeFromLeft(10);
    noThanksButton.setBounds(row.removeFromLeft(kButtonWidth));
    learnMoreButton.setBounds(row.removeFromRight(130));
}

} // namespace synth::ui
