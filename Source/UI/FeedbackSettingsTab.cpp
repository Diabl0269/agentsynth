#include "FeedbackSettingsTab.h"
#include "../AI/AuthClient.h"
#include "../Branding.h"
#include <atomic>
#include <thread>

FeedbackSettingsTab::FeedbackSettingsTab(synth::AccountService* accountServiceIn)
    : accountService(accountServiceIn) {
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Send Feedback", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

    addAndMakeVisible(descriptionLabel);
    descriptionLabel.setText(
        juce::String::fromUTF8(
            "Bug reports, feature requests, or anything else \xe2\x80\x94 not tied to a specific patch. "
            "Saved on this device, and sent to us too if you're signed in."),
        juce::dontSendNotification);
    descriptionLabel.setJustificationType(juce::Justification::topLeft);
    descriptionLabel.setMinimumHorizontalScale(1.0f);

    addAndMakeVisible(categoryLabel);
    categoryLabel.setText("Category:", juce::dontSendNotification);

    addAndMakeVisible(categoryCombo);
    categoryCombo.addItem("Bug Report", kCategoryBugId);
    categoryCombo.addItem("Feature Request", kCategoryFeatureId);
    categoryCombo.addItem("Other", kCategoryOtherId);
    categoryCombo.setSelectedId(kCategoryOtherId, juce::dontSendNotification);

    addAndMakeVisible(feedbackEditor);
    feedbackEditor.setMultiLine(true);
    feedbackEditor.setReturnKeyStartsNewLine(true);
    feedbackEditor.setTextToShowWhenEmpty("What's on your mind?",
                                          findColour(juce::TextEditor::textColourId).withAlpha(0.5f));
    feedbackEditor.onTextChange = [this] {
        updateSendButtonEnablement();
        statusLabel.setText("", juce::dontSendNotification);
    };

    addAndMakeVisible(sendButton);
    sendButton.onClick = [this] { sendFeedback(); };
    updateSendButtonEnablement();

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
}

void FeedbackSettingsTab::paint(juce::Graphics& g) { g.fillAll(findColour(juce::ResizableWindow::backgroundColourId)); }

void FeedbackSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(4);
    descriptionLabel.setBounds(bounds.removeFromTop(36));
    bounds.removeFromTop(10);

    auto categoryRow = bounds.removeFromTop(25);
    categoryLabel.setBounds(categoryRow.removeFromLeft(70));
    categoryCombo.setBounds(categoryRow.removeFromLeft(160));
    bounds.removeFromTop(10);

    auto bottomRow = bounds.removeFromBottom(30);
    sendButton.setBounds(bottomRow.removeFromLeft(120));
    bottomRow.removeFromLeft(10);
    statusLabel.setBounds(bottomRow);
    bounds.removeFromBottom(8);

    feedbackEditor.setBounds(bounds);
}

void FeedbackSettingsTab::updateSendButtonEnablement() {
    sendButton.setEnabled(feedbackEditor.getText().trim().isNotEmpty());
}

void FeedbackSettingsTab::sendFeedback() {
    const auto text = feedbackEditor.getText().trim();
    if (text.isEmpty())
        return;

    synth::GeneralFeedbackStore::Category category = synth::GeneralFeedbackStore::Category::Other;
    juce::String categoryStr = "other";
    switch (categoryCombo.getSelectedId()) {
    case kCategoryBugId:
        category = synth::GeneralFeedbackStore::Category::Bug;
        categoryStr = "bug";
        break;
    case kCategoryFeatureId:
        category = synth::GeneralFeedbackStore::Category::Feature;
        categoryStr = "feature";
        break;
    default:
        break;
    }

    // Local log: unconditional, regardless of plan/sign-in/sync outcome.
    store.record(text, category);

    // P6-16: additionally sync to the server, fire-and-forget, when signed in with a usable
    // access token. Unlike patch feedback (P6-9) this is NOT Pro-gated -- any signed-in account.
    // Detached background thread, mirrors AIChatComponent's P6-9 sync block: captures COPIES
    // only (a small, copyable, stateless AuthClient plus plain strings), never `this` or any UI
    // state, so the thread owns everything it touches and safely outlives this callback.
    if (accountService != nullptr) {
        const auto snapshot = accountService->getSnapshot();
        const bool signedIn = snapshot.state == synth::AccountState::SignedIn;
        const juce::String accessToken = signedIn ? accountService->getAccessToken() : juce::String();

        if (signedIn && accessToken.isNotEmpty()) {
            synth::AuthClient client = testFeedbackHttpPerformer
                                           ? synth::AuthClient(synth::branding::resolveApiBaseUrl(), "synth-desktop",
                                                               testFeedbackHttpPerformer)
                                           : synth::AuthClient(synth::branding::resolveApiBaseUrl());
            std::thread([client, accessToken, categoryStr, text]() {
                std::atomic<bool> cancelled{false};
                client.submitGeneralFeedback(accessToken, categoryStr, text, cancelled);
            }).detach();
        }
    }

    feedbackEditor.clear();
    updateSendButtonEnablement();
    statusLabel.setText(juce::String::fromUTF8("Thanks \xe2\x80\x94 feedback saved."), juce::dontSendNotification);
}
