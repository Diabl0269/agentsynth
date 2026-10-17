#pragma once

#include "UI/Chrome/UsageStatsPromptComponent.h"
#include "UI/Layout/IconButton.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

// The app-only startup overlay offering New empty project, Open the factory default project, Open
// an existing project, pick from Recent, plus a low-key contribute request, instead of always
// silently auto-loading the factory "Default" preset. NOT a separate window — MainComponent adds
// this as a full-bounds child (see resized()) so it also occludes the toolbar while shown, since
// none of the toolbar actions make sense until a choice is made. MainComponent constructs and owns
// this ONLY when it owns its own AudioEngine (ownedAudioEngine != nullptr) — a hosted plugin's
// document is host-owned via getStateInformation, so the plugin path must never see this at all.
//
// Usage statistics card: while the owner says the question is still open (setUsageStatsPromptShown(true)), an
// inset panel sits between the subtitle and the start buttons. It never blocks the buttons below it. An answer
// goes out through onUsageStatsChoice; the panel then collapses out (height and fade, 180 ms, easeInCubic) and
// the rest of the card re-forms with it, after a one-line thanks for Share. Reduced Motion and an overlay that is
// not on screen land at once. The card height always includes the panel's current slot, so getCardBounds() and
// resized() stay in step through the collapse.
//
// Logo: the cat mark sits above the title as a transparent cut-out with its neon outline, no tile or border (readable
// on both themes). Each time the overlay is shown it fades in and rises 10 px into place (320 ms, easeOutCubic);
// Reduced Motion lands it at once.
//
// Two modes. Launch (the default): an opaque screen with no way out, since there is nothing to return to; the
// owner picks how to start. Dismissible (setDismissible(true), used when it is reopened over an open project):
// the project stays visible under a dimmed scrim, a close button sits in the card's top-right corner, and
// Escape, that button or a click outside the card report onDismissRequested, which the owner answers by hiding
// the overlay, leaving the project untouched.
//
// Keyboard: Tab and Shift+Tab visit the close button (dismissible only), the three start buttons, the recent
// projects, Contribute and What's New, top to bottom, and wrap inside the overlay (it is a keyboard focus
// container). Escape dismisses only in the dismissible mode.
//
// Pure UI: every action is reported through a callback and MainComponent decides what actually
// happens (including running it through guardUnsavedChanges) — this class knows nothing about
// AudioEngine/PresetManager/RecentProjects beyond the juce::File list it's handed.
class WelcomeScreenComponent : public juce::Component {
public:
    WelcomeScreenComponent();

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;

    /** true: project visible under a scrim, close button shown, Escape / close button / click outside the card
     *  call onDismissRequested. false (the launch default): opaque, nothing dismisses it. */
    void setDismissible(bool dismissible);
    bool isDismissible() const noexcept { return dismissible_; }

    // Most-recent-first, already pruned of anything that no longer exists on disk (see
    // RecentProjects::pruneMissing) — this class shows whatever it's handed with no filtering of
    // its own. Capped display at 5 rows, matching the Load menu's own Recent Projects submenu size
    // in spirit (that one shows all 10; this overlay is a smaller, first-look surface).
    void setRecentProjects(const std::vector<juce::File>& recents);

    // e.g. "Agent Synth v0.13.2" or "Agent Synth - development build" — sourced from
    // synth::whatsnew::kReleaseTag by the caller so there is one place that decides what version
    // string means "this build".
    void setLatestVersionLabel(const juce::String& text);

    // Gives keyboard focus to the first start button (a no-op while the overlay is not on screen),
    // so a keyboard user who reopens it can press Space or Tab straight away.
    void focusFirstControl() { newProjectButton.grabKeyboardFocus(); }

    std::function<void()> onDismissRequested; // dismissible mode only; the owner hides the overlay
    std::function<void()> onNewProject;
    std::function<void()> onOpenDefaultProject;
    std::function<void()> onOpenExistingProject;
    std::function<void(const juce::File&)> onOpenRecentProject;
    std::function<void()> onWhatsNewRequested;
    std::function<void()> onContributeRequested;        // Contribute... button; never hides the overlay
    std::function<void(bool share)> onUsageStatsChoice; // Share (true) / No thanks (false) on the usage statistics card
    std::function<void()> onUsageStatsLearnMoreRequested; // "What we collect" on that card

    /** true opens the card (instantly: it is part of the initial layout) unless it was already answered; false
     *  collapses it out when it is open and idle. Ignored while an answer is being animated. */
    void setUsageStatsPromptShown(bool shown);
    /** On screen right now: open and unanswered, or still animating out. */
    bool isUsageStatsPromptShown() const noexcept {
        return promptState_ == PromptState::asking || promptFraction_ > 0.0f;
    }

    // ---- Test accessors ----
    // Direct accessors rather than getComponentID()+findChildWithID (mirrors
    // FeedbackSettingsTabTests.cpp's getSendButtonForTest() idiom) — simpler than a component-ID
    // lookup when the caller already has the concrete type.
    juce::Button& getCloseButtonForTest() { return closeButton_; }
    juce::Rectangle<int> getCardBoundsForTest() const { return getCardBounds(); }
    juce::Button& getNewProjectButtonForTest() { return newProjectButton; }
    juce::Button& getOpenDefaultButtonForTest() { return openDefaultButton; }
    juce::Button& getOpenExistingButtonForTest() { return openExistingButton; }
    juce::Button& getWhatsNewButtonForTest() { return whatsNewButton; }
    juce::Button& getContributeButtonForTest() { return contributeButton; }
    juce::Label& getContributeLabelForTest() { return contributeLabel; }
    UsageStatsPromptComponent& getUsageStatsPromptForTest() { return usageStatsPrompt_; }
    // The height the card gives the panel and its gap right now (0 once it is gone).
    int getUsageStatsSlotHeightForTest() const { return promptSlotHeight(); }
    float getLogoRevealForTest() const noexcept { return logoReveal_; }
    juce::Rectangle<int> getLogoBoundsForTest() const { return logoBounds_; }
    void completeLogoRevealForTest() { logoAnim_.completeForTest(); }
    int getCardHeightForTest() const { return getCardBounds().getHeight(); }
    int getRecentProjectCountForTest() const { return (int)recentProjectButtons.size(); }
    // Simulates clicking recent row `index` — a no-op (never crashes) if out of range.
    void triggerRecentProjectForTest(int index);

private:
    // Where the card sits within the full-bounds overlay — computed once per layout pass and
    // reused by paint() so the drawn panel and the buttons inside it can never disagree.
    juce::Rectangle<int> getCardBounds() const;
    // Rebuilds recentProjectButtons from recentFiles_ (called by setRecentProjects and by
    // resized(), since the button count/labels don't change between layout passes but their click
    // handlers capture `this` fine either way — kept simple: only setRecentProjects rebuilds).
    void rebuildRecentProjectButtons();

    enum class PromptState { hidden, asking, answered };
    static constexpr int kPromptGap = 14; // between the panel and the start buttons
    int promptSlotHeight() const;
    void handleUsageStatsChoice(bool share);
    void collapseUsageStatsPrompt();
    void finishUsageStatsCollapse();

    static constexpr int kLogoSize = 96;
    static constexpr int kLogoGap = 12; // between the logo and the title
    juce::Image logo_;
    juce::Rectangle<int> logoBounds_;
    float logoReveal_ = 1.0f; // 0 = hidden, 1 = settled; paint-only, never moves the layout
    synth::ui::AnimationDriver logoAnim_;

    UsageStatsPromptComponent usageStatsPrompt_;
    PromptState promptState_ = PromptState::hidden;
    float promptFraction_ = 0.0f; // 1 = fully open, 0 = gone; the card's layout reads only this
    juce::VBlankAnimatorUpdater vblank_{this};
    synth::ui::AnimationDriver promptAnim_;

    bool dismissible_ = false;
    synth::ui::IconButton closeButton_{"Close welcome screen", synth::theme::Glyph::Close,
                                       synth::ui::IconButton::Style::Round};

    juce::TextButton newProjectButton{"New empty project"};
    juce::TextButton openDefaultButton{"Open our default project"};
    juce::TextButton openExistingButton{"Open an existing project..."};
    juce::TextButton whatsNewButton{"What's New..."};
    juce::TextButton contributeButton;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label versionLabel;
    juce::Label recentsHeaderLabel;
    juce::Label noRecentsLabel;
    juce::Label contributeLabel;

    std::vector<juce::File> recentFiles_;
    std::vector<std::unique_ptr<juce::TextButton>> recentProjectButtons;

    // Up to 5 rows shown at once — see setRecentProjects()'s comment.
    static constexpr int kMaxVisibleRecents = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WelcomeScreenComponent)
};

} // namespace synth::ui
