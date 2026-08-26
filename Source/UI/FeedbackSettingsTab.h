#pragma once

#include "../GeneralFeedbackStore.h"
#include <juce_gui_basics/juce_gui_basics.h>

// P6-10: the Settings "Feedback" tab — a general, free-text feedback entry point that isn't tied
// to any one AI-generated patch (that's PatchCard's thumbs up/down, P6-3). Local-only for now: see
// GeneralFeedbackStore's doc comment for why no server sync exists yet.
//
// NOTE: FeedbackSettingsTab.cpp MUST be added to BOTH the app target AND the test target in
// CMakeLists.txt (same contract PreferencesSettingsTab.h documents).
class FeedbackSettingsTab : public juce::Component {
public:
    FeedbackSettingsTab();
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

private:
    void updateSendButtonEnablement();
    void sendFeedback();

    static constexpr int kCategoryBugId = 1;
    static constexpr int kCategoryFeatureId = 2;
    static constexpr int kCategoryOtherId = 3;

    synth::GeneralFeedbackStore store;

    juce::Label titleLabel;
    juce::Label descriptionLabel;
    juce::Label categoryLabel;
    juce::ComboBox categoryCombo;
    juce::TextEditor feedbackEditor;
    juce::TextButton sendButton{"Send Feedback"};
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeedbackSettingsTab)
};
