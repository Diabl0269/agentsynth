#pragma once

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
// Keyboard: Tab and Shift+Tab visit the three start buttons, the recent projects, Contribute and
// What's New, top to bottom, and wrap inside the overlay (it is a keyboard focus container). There
// is no Escape action: the screen has no cancel, only a choice of how to start.
//
// Pure UI: every action is reported through a callback and MainComponent decides what actually
// happens (including running it through guardUnsavedChanges) — this class knows nothing about
// AudioEngine/PresetManager/RecentProjects beyond the juce::File list it's handed.
class WelcomeScreenComponent : public juce::Component {
public:
    WelcomeScreenComponent();

    void paint(juce::Graphics&) override;
    void resized() override;

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

    std::function<void()> onNewProject;
    std::function<void()> onOpenDefaultProject;
    std::function<void()> onOpenExistingProject;
    std::function<void(const juce::File&)> onOpenRecentProject;
    std::function<void()> onWhatsNewRequested;
    std::function<void()> onContributeRequested; // Contribute... button; never hides the overlay

    // ---- Test accessors ----
    // Direct accessors rather than getComponentID()+findChildWithID (mirrors
    // FeedbackSettingsTabTests.cpp's getSendButtonForTest() idiom) — simpler than a component-ID
    // lookup when the caller already has the concrete type.
    juce::Button& getNewProjectButtonForTest() { return newProjectButton; }
    juce::Button& getOpenDefaultButtonForTest() { return openDefaultButton; }
    juce::Button& getOpenExistingButtonForTest() { return openExistingButton; }
    juce::Button& getWhatsNewButtonForTest() { return whatsNewButton; }
    juce::Button& getContributeButtonForTest() { return contributeButton; }
    juce::Label& getContributeLabelForTest() { return contributeLabel; }
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
