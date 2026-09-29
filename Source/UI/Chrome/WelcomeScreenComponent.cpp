#include "WelcomeScreenComponent.h"
#include "Branding.h"

namespace synth::ui {

namespace {
constexpr int kCardWidth = 600;
constexpr int kRecentRowHeight = 28;
constexpr int kContributeRowHeight = 36;
} // namespace

WelcomeScreenComponent::WelcomeScreenComponent() {
    setOpaque(true);

    titleLabel.setText(juce::String("Welcome to ") + synth::branding::kProductName, juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("Pick how you'd like to start", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centred);
    subtitleLabel.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.7f));
    addAndMakeVisible(subtitleLabel);

    addAndMakeVisible(newProjectButton);
    newProjectButton.onClick = [this] {
        if (onNewProject)
            onNewProject();
    };

    addAndMakeVisible(openDefaultButton);
    openDefaultButton.onClick = [this] {
        if (onOpenDefaultProject)
            onOpenDefaultProject();
    };

    addAndMakeVisible(openExistingButton);
    openExistingButton.onClick = [this] {
        if (onOpenExistingProject)
            onOpenExistingProject();
    };

    recentsHeaderLabel.setText("Recent Projects", juce::dontSendNotification);
    recentsHeaderLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    addAndMakeVisible(recentsHeaderLabel);

    noRecentsLabel.setText("No recent projects yet", juce::dontSendNotification);
    noRecentsLabel.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.6f));
    addAndMakeVisible(noRecentsLabel);

    contributeLabel.setText("Agent Synth is free and open source, built by one person. If it has earned a place in "
                            "your setup, you can help build it. No pressure, it stays free either way.",
                            juce::dontSendNotification);
    contributeLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    contributeLabel.setJustificationType(juce::Justification::centredLeft);
    contributeLabel.setMinimumHorizontalScale(1.0f);
    contributeLabel.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.7f));
    addAndMakeVisible(contributeLabel);

    contributeButton.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x99\xa5 Contribute...")));
    contributeButton.setTooltip("Opens agentsynth.app/contribute in your browser: ways to help build Agent Synth.");
    contributeButton.setTitle("Contribute");
    contributeButton.setDescription("Opens the Agent Synth contribute page in your browser");
    addAndMakeVisible(contributeButton);
    contributeButton.onClick = [this] {
        if (onContributeRequested)
            onContributeRequested();
    };

    addAndMakeVisible(whatsNewButton);
    whatsNewButton.onClick = [this] {
        if (onWhatsNewRequested)
            onWhatsNewRequested();
    };

    addAndMakeVisible(showAtLaunchToggle);
    showAtLaunchToggle.setToggleState(true, juce::dontSendNotification);
    showAtLaunchToggle.onClick = [this] {
        if (onShowAtLaunchChanged)
            onShowAtLaunchChanged(showAtLaunchToggle.getToggleState());
    };

    versionLabel.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.6f));
    addAndMakeVisible(versionLabel);

    rebuildRecentProjectButtons();
}

void WelcomeScreenComponent::setRecentProjects(const std::vector<juce::File>& recents) {
    recentFiles_ = recents;
    if ((int)recentFiles_.size() > kMaxVisibleRecents)
        recentFiles_.resize((size_t)kMaxVisibleRecents);
    rebuildRecentProjectButtons();
    resized();
}

void WelcomeScreenComponent::setShowAtLaunch(bool shouldShow) {
    showAtLaunchToggle.setToggleState(shouldShow, juce::dontSendNotification);
}

void WelcomeScreenComponent::setLatestVersionLabel(const juce::String& text) {
    versionLabel.setText(text, juce::dontSendNotification);
}

void WelcomeScreenComponent::triggerRecentProjectForTest(int index) {
    if (index < 0 || index >= (int)recentProjectButtons.size())
        return;
    if (recentProjectButtons[(size_t)index]->onClick)
        recentProjectButtons[(size_t)index]->onClick();
}

void WelcomeScreenComponent::rebuildRecentProjectButtons() {
    recentProjectButtons.clear();
    for (size_t i = 0; i < recentFiles_.size(); ++i) {
        auto button = std::make_unique<juce::TextButton>(recentFiles_[i].getFileNameWithoutExtension());
        button->setTooltip(recentFiles_[i].getFullPathName());
        const juce::File file = recentFiles_[i];
        button->onClick = [this, file] {
            if (onOpenRecentProject)
                onOpenRecentProject(file);
        };
        addAndMakeVisible(*button);
        recentProjectButtons.push_back(std::move(button));
    }
    const bool hasRecents = !recentFiles_.empty();
    noRecentsLabel.setVisible(!hasRecents);
    for (auto& button : recentProjectButtons)
        button->setVisible(true);
}

juce::Rectangle<int> WelcomeScreenComponent::getCardBounds() const {
    const int recentsHeight = recentFiles_.empty() ? kRecentRowHeight : (int)recentFiles_.size() * kRecentRowHeight;
    // Mirrors resized() row for row (pad, title, subtitle, three action buttons, recents header,
    // recents rows, contribute row, footer, pad) so the drawn card and its children always agree.
    // Fixed heights rather than FlexBox: a small, static layout whose only variable is the recents count.
    const int cardHeight = 20 /*top pad*/ + 34 /*title*/ + 4 + 22 /*subtitle*/ + 20 + 36 /*New*/ + 10 +
                           36 /*Open default*/ + 10 + 36 /*Open existing*/ + 24 + 20 /*recents header*/ + 6 +
                           recentsHeight + 16 + kContributeRowHeight + 20 + 28 /*footer*/ + 20 /*bottom pad*/;
    // getWidth()/getHeight() can be 0 the moment this runs during construction (setRecentProjects()
    // triggers resized() before the parent has ever called setBounds()) — clamp to 0 rather than
    // let a negative rect flow into every child setBounds() below.
    const int width = juce::jmax(0, juce::jmin(kCardWidth, getWidth() - 40));
    const int height = juce::jmax(0, juce::jmin(cardHeight, getHeight() - 40));
    return juce::Rectangle<int>(0, 0, width, height).withCentre(getLocalBounds().getCentre());
}

void WelcomeScreenComponent::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));

    auto card = getCardBounds();
    g.setColour(findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(card.toFloat(), 10.0f);
    g.setColour(findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(card.toFloat(), 10.0f, 1.0f);
}

void WelcomeScreenComponent::resized() {
    auto card = getCardBounds();
    auto area = card.reduced(28, 20);

    titleLabel.setBounds(area.removeFromTop(34));
    area.removeFromTop(4);
    subtitleLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(20);

    newProjectButton.setBounds(area.removeFromTop(36));
    area.removeFromTop(10);
    openDefaultButton.setBounds(area.removeFromTop(36));
    area.removeFromTop(10);
    openExistingButton.setBounds(area.removeFromTop(36));
    area.removeFromTop(24);

    recentsHeaderLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(6);

    if (recentFiles_.empty()) {
        noRecentsLabel.setBounds(area.removeFromTop(kRecentRowHeight));
    } else {
        for (auto& button : recentProjectButtons)
            button->setBounds(area.removeFromTop(kRecentRowHeight).reduced(0, 2));
    }

    area.removeFromTop(16);
    auto contributeRow = area.removeFromTop(kContributeRowHeight);
    contributeButton.setBounds(contributeRow.removeFromRight(120).withSizeKeepingCentre(120, 28));
    contributeRow.removeFromRight(12);
    contributeLabel.setBounds(contributeRow);

    area.removeFromTop(20);
    auto footer = area.removeFromTop(28);
    showAtLaunchToggle.setBounds(footer.removeFromRight(210));
    footer.removeFromRight(10);
    whatsNewButton.setBounds(footer.removeFromRight(110));
    footer.removeFromRight(10);
    versionLabel.setBounds(footer);
}

} // namespace synth::ui
