#include "WelcomeScreenComponent.h"
#include "Branding.h"

namespace synth::ui {

namespace {
constexpr int kCardWidth = 600;
constexpr int kRecentRowHeight = 28;
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
    // Title + subtitle + gap + three action buttons + gap + recents header + recents rows + gap +
    // footer row, plus top/bottom padding. Fixed heights per row rather than FlexBox — this is a
    // small, static layout with no dynamic resizing needs beyond the recents count.
    const int cardHeight = 40 /*top pad*/ + 34 /*title*/ + 8 + 22 /*subtitle*/ + 24 + 40 /*New button*/ + 10 +
                           40 /*Open default*/ + 10 + 40 /*Open existing*/ + 28 + 22 /*recents header*/ + 8 +
                           recentsHeight + 24 + 32 /*footer*/ + 24 /*bottom pad*/;
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

    area.removeFromTop(20);
    auto footer = area.removeFromTop(28);
    versionLabel.setBounds(footer.removeFromLeft(footer.getWidth() / 2));
    showAtLaunchToggle.setBounds(footer.removeFromRight(220));
    footer.removeFromRight(10);
    whatsNewButton.setBounds(footer.removeFromRight(90));
}

} // namespace synth::ui
