#include "WelcomeScreenComponent.h"
#include "Branding.h"
#include "UI/Chrome/HelpBuildStrip.h"
#include "UI/Chrome/UsageStatsCardSlot.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <BinaryData.h>

namespace synth::ui {

namespace {
constexpr int kCardWidth = 600;
constexpr int kRecentRowHeight = 28;
constexpr int kContributeRowHeight = 76; // the "Help build" strip: three wrapped lines plus padding
constexpr double kLogoRevealMs = 320.0;  // fade + rise on show: easeOutCubic
constexpr float kLogoRisePx = 10.0f;
constexpr int kSocialButtonSize = 28;
constexpr int kSocialGap = 4;

// One row of the footer's social links; the order here is the visual and the Tab order.
struct SocialLinkSpec {
    synth::theme::Glyph glyph;
    const char* title;
    const char* tooltip;
    const char* description;
    const char* url;
};

const SocialLinkSpec kSocialLinks[] = {
    {synth::theme::Glyph::SocialDiscord, "Agent Synth on Discord", "Join the Agent Synth Discord",
     "Opens the Agent Synth Discord invite in your browser", synth::branding::kDiscordUrl},
    {synth::theme::Glyph::SocialYouTube, "Agent Synth on YouTube", "Watch Agent Synth on YouTube",
     "Opens the Agent Synth YouTube channel in your browser", synth::branding::kYouTubeUrl},
    {synth::theme::Glyph::SocialGitHub, "Agent Synth on GitHub", "See the Agent Synth source on GitHub",
     "Opens the Agent Synth GitHub repository in your browser", synth::branding::kGitHubUrl},
    {synth::theme::Glyph::SocialTikTok, "Agent Synth on TikTok", "Follow Agent Synth on TikTok",
     "Opens the Agent Synth TikTok page in your browser", synth::branding::kTikTokUrl},
    {synth::theme::Glyph::SocialInstagram, "Agent Synth on Instagram", "Follow Agent Synth on Instagram",
     "Opens the Agent Synth Instagram page in your browser", synth::branding::kInstagramUrl},
};
} // namespace

WelcomeScreenComponent::~WelcomeScreenComponent() = default;

bool WelcomeScreenComponent::isUsageStatsPromptShown() const noexcept { return usageStatsSlot_->isShown(); }

UsageStatsPromptComponent& WelcomeScreenComponent::getUsageStatsPromptForTest() { return usageStatsSlot_->prompt(); }

int WelcomeScreenComponent::getUsageStatsSlotHeightForTest() const { return usageStatsSlot_->slotHeight(); }

juce::Button& WelcomeScreenComponent::getContributeButtonForTest() { return helpStrip_->button(); }

juce::Label& WelcomeScreenComponent::getContributeLabelForTest() { return helpStrip_->label(); }

WelcomeScreenComponent::WelcomeScreenComponent() {
    usageStatsSlot_ = std::make_unique<UsageStatsCardSlot>(*this, [this] {
        resized();
        repaint();
    });
    setOpaque(true);
    // The overlay covers the whole window, so Tab and Shift+Tab cycle inside it rather than
    // escaping to the toolbar and canvas hidden underneath.
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    logo_ = juce::ImageCache::getFromMemory(BinaryData::agentsynthlogo256_png, BinaryData::agentsynthlogo256_pngSize);

    titleLabel.setText(juce::String("Welcome to ") + synth::branding::kProductName, juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("Pick how you'd like to start", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centred);
    subtitleLabel.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.7f));
    addAndMakeVisible(subtitleLabel);

    addChildComponent(usageStatsSlot_->prompt());
    usageStatsSlot_->onChoice = [this](bool share) {
        if (onUsageStatsChoice)
            onUsageStatsChoice(share);
    };
    usageStatsSlot_->onLearnMore = [this] {
        if (onUsageStatsLearnMoreRequested)
            onUsageStatsLearnMoreRequested();
    };
    usageStatsSlot_->onFocusRescue = [this] { newProjectButton.grabKeyboardFocus(); };

    closeButton_.setTitle("Close welcome screen");
    closeButton_.setDescription("Returns to the open project");
    closeButton_.setTooltip("Close (Esc)");
    closeButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    closeButton_.setWantsKeyboardFocus(true); // a Tab stop; Space and Return press it
    closeButton_.onClick = [this] {
        if (onDismissRequested)
            onDismissRequested();
    };
    addChildComponent(closeButton_); // shown only while dismissible

    newProjectButton.setTooltip("Start with an empty project");
    addAndMakeVisible(newProjectButton);
    newProjectButton.onClick = [this] {
        if (onNewProject)
            onNewProject();
    };

    openDefaultButton.setTooltip("Open the factory default project");
    addAndMakeVisible(openDefaultButton);
    openDefaultButton.onClick = [this] {
        if (onOpenDefaultProject)
            onOpenDefaultProject();
    };

    openExistingButton.setTooltip("Choose a project from disk");
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

    helpStrip_ = std::make_unique<HelpBuildStrip>();
    auto& contributeButton = helpStrip_->button();
    contributeButton.setTooltip("Agent Synth is free and open source, built by one person. Contributing helps me spend "
                                "more time building it. Opens agentsynth.app/contribute in your browser.");
    contributeButton.setTitle("Help build Agent Synth");
    contributeButton.setDescription("Opens the Agent Synth contribute page in your browser");
    addAndMakeVisible(*helpStrip_);
    contributeButton.onClick = [this] {
        if (onContributeRequested)
            onContributeRequested();
    };

    whatsNewButton.setTooltip("See what changed in this version");
    addAndMakeVisible(whatsNewButton);
    whatsNewButton.onClick = [this] {
        if (onWhatsNewRequested)
            onWhatsNewRequested();
    };

    for (const auto& link : kSocialLinks) {
        auto button = std::make_unique<IconButton>(link.title, link.glyph, IconButton::Style::Bare);
        button->setTitle(link.title);
        button->setTooltip(link.tooltip);
        button->setDescription(link.description);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button->setWantsKeyboardFocus(true); // a Tab stop; Space and Return press it
        button->onClick = [this, url = juce::String(link.url)] {
            if (onSocialLinkRequested)
                onSocialLinkRequested(url);
        };
        addAndMakeVisible(*button);
        socialButtons_.push_back(std::move(button));
    }

    versionLabel.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.6f));
    addAndMakeVisible(versionLabel);

    rebuildRecentProjectButtons();
}

void WelcomeScreenComponent::visibilityChanged() {
    if (!isShowing()) {
        logoAnim_.stop(vblank_);
        logoReveal_ = 1.0f;
        return;
    }
    if (synth::ui::prefersReducedMotion()) {
        logoAnim_.stop(vblank_);
        logoReveal_ = 1.0f;
        repaint(logoBounds_.expanded(0, (int)kLogoRisePx));
        return;
    }
    logoReveal_ = 0.0f;
    logoAnim_.start(vblank_, kLogoRevealMs, synth::ui::easeOutCubic, [this](float e) {
        logoReveal_ = e;
        repaint(logoBounds_.expanded(0, (int)kLogoRisePx));
    });
}

void WelcomeScreenComponent::setDismissible(bool dismissible) {
    if (dismissible_ == dismissible)
        return;
    dismissible_ = dismissible;
    // Opaque only while it hides the project; a scrim must let the project under it paint.
    setOpaque(!dismissible);
    closeButton_.setVisible(dismissible);
    repaint();
}

bool WelcomeScreenComponent::keyPressed(const juce::KeyPress& key) {
    if (dismissible_ && key == juce::KeyPress::escapeKey) {
        if (onDismissRequested)
            onDismissRequested();
        return true;
    }
    return false;
}

void WelcomeScreenComponent::mouseDown(const juce::MouseEvent& e) {
    // A click on the scrim, outside the card. Clicks on the card's own children never reach here.
    if (dismissible_ && !getCardBounds().contains(e.getPosition()) && onDismissRequested)
        onDismissRequested();
}

void WelcomeScreenComponent::setUsageStatsPromptShown(bool shown) { usageStatsSlot_->setShown(shown); }

void WelcomeScreenComponent::setRecentProjects(const std::vector<juce::File>& recents) {
    recentFiles_ = recents;
    if ((int)recentFiles_.size() > kMaxVisibleRecents)
        recentFiles_.resize((size_t)kMaxVisibleRecents);
    rebuildRecentProjectButtons();
    resized();
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
    // Mirrors resized() row for row (pad, logo, title, subtitle, the usage statistics card slot, three action buttons,
    // recents header, recents rows, contribute row, footer, pad) so the drawn card and its children always agree. Fixed
    // heights rather than FlexBox: a small, static layout whose only variable is the recents count.
    const int cardHeight = 20 /*top pad*/ + kLogoSize + kLogoGap /*logo*/ + 34 /*title*/ + 4 + 22 /*subtitle*/ + 20 +
                           36 /*New*/ + 10 + 36 /*Open default*/ + 10 + 36 /*Open existing*/ + 24 +
                           20 /*recents header*/ + 6 + recentsHeight + 16 + kContributeRowHeight + 20 + 28 /*footer*/ +
                           20 /*bottom pad*/ +
                           usageStatsSlot_->slotHeight() /*usage statistics card + gap, 0 once answered*/;
    // getWidth()/getHeight() can be 0 the moment this runs during construction (setRecentProjects()
    // triggers resized() before the parent has ever called setBounds()) — clamp to 0 rather than
    // let a negative rect flow into every child setBounds() below.
    const int width = juce::jmax(0, juce::jmin(kCardWidth, getWidth() - 40));
    const int height = juce::jmax(0, juce::jmin(cardHeight, getHeight() - 40));
    return juce::Rectangle<int>(0, 0, width, height).withCentre(getLocalBounds().getCentre());
}

void WelcomeScreenComponent::paint(juce::Graphics& g) {
    if (dismissible_)
        g.fillAll(juce::Colours::black.withAlpha(0.6f)); // the open project shows dimmed beneath the card
    else
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));

    auto card = getCardBounds();
    g.setColour(findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(card.toFloat(), 10.0f);
    g.setColour(findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(card.toFloat(), 10.0f, 1.0f);

    if (logo_.isValid() && logoReveal_ > 0.0f) {
        // The artwork is a transparent cut-out with its neon outline, so it sits on the card with no tile or border.
        const auto dest = logoBounds_.toFloat().translated(0.0f, (1.0f - logoReveal_) * kLogoRisePx);
        g.saveState();
        g.setOpacity(logoReveal_);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(logo_, dest, juce::RectanglePlacement::stretchToFit);
        g.restoreState();
    }
}

void WelcomeScreenComponent::resized() {
    auto card = getCardBounds();
    closeButton_.setBounds(juce::Rectangle<int>(28, 28).withRightX(card.getRight() - 10).withY(card.getY() + 10));
    auto area = card.reduced(28, 20);

    logoBounds_ = area.removeFromTop(kLogoSize).withSizeKeepingCentre(kLogoSize, kLogoSize);
    area.removeFromTop(kLogoGap);
    titleLabel.setBounds(area.removeFromTop(34));
    area.removeFromTop(4);
    subtitleLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(20);

    // The card's slot is its panel plus the gap to the buttons; both shrink together, so the buttons below
    // rise by exactly what getCardBounds() took off the card height.
    usageStatsSlot_->layoutSlot(area);

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
    helpStrip_->setBounds(area.removeFromTop(kContributeRowHeight));

    area.removeFromTop(20);
    auto footer = area.removeFromTop(28);
    whatsNewButton.setBounds(footer.removeFromRight(110));
    footer.removeFromRight(10);
    // The social links sit against What's New, 4 px apart, vertically centred in the footer row.
    for (auto it = socialButtons_.rbegin(); it != socialButtons_.rend(); ++it) {
        (*it)->setBounds(
            footer.removeFromRight(kSocialButtonSize).withSizeKeepingCentre(kSocialButtonSize, kSocialButtonSize));
        footer.removeFromRight(kSocialGap);
    }
    footer.removeFromRight(6);
    versionLabel.setBounds(footer);
}

} // namespace synth::ui
