#pragma once

#include "../AI/AccountService.h"
#include "../AI/AuthClient.h"
#include "../GeneralFeedbackStore.h"
#include <juce_gui_basics/juce_gui_basics.h>

// P6-10: the Settings "Feedback" tab — a general, free-text feedback entry point that isn't tied
// to any one AI-generated patch (that's PatchCard's thumbs up/down, P6-3). P6-16 additionally
// syncs each submission to the server, fire-and-forget, gated on sign-in only (not Pro) — see
// GeneralFeedbackStore's doc comment for the local-only log this still writes unconditionally.
//
// NOTE: FeedbackSettingsTab.cpp MUST be added to BOTH the app target AND the test target in
// CMakeLists.txt (same contract PreferencesSettingsTab.h documents).
class FeedbackSettingsTab : public juce::Component {
public:
    // `accountService` is nullable, same "invisible/inert until attached" contract SettingsWindow's
    // own `accountService` parameter documents — nullptr (the default) keeps this tab local-only,
    // which is also what every existing bare `FeedbackSettingsTab tab;` test call site relies on.
    explicit FeedbackSettingsTab(synth::AccountService* accountService = nullptr);
    ~FeedbackSettingsTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Testing hooks -----------------------------------------------------
    // Redirects the underlying store to a caller-owned file so tests never touch the real
    // per-user app-data log — mirrors AIChatComponent::setPatchFeedbackFileForTesting.
    void setFeedbackFileForTesting(const juce::File& file) { store = synth::GeneralFeedbackStore(file); }
    juce::TextEditor& getFeedbackEditorForTest() { return feedbackEditor; }
    juce::ComboBox& getCategoryComboForTest() { return categoryCombo; }
    juce::TextButton& getSendButtonForTest() { return sendButton; }
    juce::String getStatusTextForTest() const { return statusLabel.getText(); }

    // Testing hook (P6-16): installs a fake AuthClient::HttpPerformer for the send callback's
    // locally-constructed AuthClient, mirroring AIChatComponent::setFeedbackHttpPerformerForTesting
    // exactly.
    void setFeedbackHttpPerformerForTesting(synth::AuthClient::HttpPerformer performer) {
        testFeedbackHttpPerformer = std::move(performer);
    }

private:
    void updateSendButtonEnablement();
    void sendFeedback();

    static constexpr int kCategoryBugId = 1;
    static constexpr int kCategoryFeatureId = 2;
    static constexpr int kCategoryOtherId = 3;

    synth::GeneralFeedbackStore store;
    synth::AccountService* accountService = nullptr;
    // P6-16: test injection for the feedback-sync POST sendFeedback() fires on a detached
    // background thread. Empty (falsy) in production, which makes AuthClient's real libcurl
    // transport constructor run instead — same idiom as AIChatComponent's testFeedbackHttpPerformer.
    synth::AuthClient::HttpPerformer testFeedbackHttpPerformer;

    juce::Label titleLabel;
    juce::Label descriptionLabel;
    juce::Label categoryLabel;
    juce::ComboBox categoryCombo;
    juce::TextEditor feedbackEditor;
    juce::TextButton sendButton{"Send Feedback"};
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackSettingsTab)
};
