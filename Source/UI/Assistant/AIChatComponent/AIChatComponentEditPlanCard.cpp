#include "AIChatComponentEditPlanCard.h"
#include "EditPlanDetailsText.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth {

// Concern: the edit-plan card (layout, details, thumbs feedback) and what its Apply does.

AIChatComponent::EditPlanCard::EditPlanCard(const MessageData& data, std::function<bool()> onApply,
                                            RateCallback rateCallback)
    : planOk(data.planOk)
    , previewLines(data.planPreviewLines)
    , planSummary(data.planDetails)
    , currentRating(data.ratingState)
    , onRate(std::move(rateCallback)) {
    setTitle("Edit plan");

    addAndMakeVisible(headerLabel);
    headerLabel.setText("Edit plan", juce::dontSendNotification);
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setAccessible(false); // the card's own group title says it

    const juce::String preview = previewLines.joinIntoString("\n");
    addAndMakeVisible(previewLabel);
    previewLabel.setText(preview, juce::dontSendNotification);
    previewLabel.setFont(juce::Font(12.0f));
    previewLabel.setMinimumHorizontalScale(1.0f);
    // previewHeight() measures the text at the label's full width, so the label must not narrow it
    // with JUCE's default 5 px side border; with it, a long line wrapped once more than measured and
    // its last line was clipped under the buttons (same fix as the message bubble's label).
    previewLabel.setBorderSize(juce::BorderSize<int>(0));
    previewLabel.setJustificationType(juce::Justification::topLeft);
    previewLabel.setTitle(planOk ? "What this plan changes" : "Why this plan cannot apply");
    previewLabel.setDescription(preview);

    addAndMakeVisible(detailsButton);
    detailsButton.setButtonText("Show details");
    detailsButton.setTooltip("Show the changes this plan makes");
    detailsButton.onClick = [this] {
        isExpanded = !isExpanded;
        if (isExpanded)
            fillDetails();
        detailsButton.setButtonText(isExpanded ? "Hide details" : "Show details");
        detailsFade_.setShown(isExpanded);
        relayout();
    };

    if (planOk && onApply) {
        applyButton = std::make_unique<juce::TextButton>("Apply");
        applyButton->setTitle("Apply edit plan");
        applyButton->setTooltip("Apply this answer to the project (one undo step)");
        // onClick can be invoked on a disabled button (a test, a script), so the guard is here.
        applyButton->onClick = [this, apply = std::move(onApply)] {
            if (!applied && apply())
                showApplied();
        };
        addAndMakeVisible(*applyButton);
        if (data.planApplied)
            showApplied();
    }

    addAndMakeVisible(thumbsUpButton);
    thumbsUpButton.setButtonText(juce::String::fromUTF8("\xF0\x9F\x91\x8D"));
    thumbsUpButton.setTitle("This answer was helpful");
    thumbsUpButton.setTooltip("This answer was helpful");
    thumbsUpButton.onClick = [this] { setRating(AIChatComponent::PatchRatingUiState::Up); };

    addAndMakeVisible(thumbsDownButton);
    thumbsDownButton.setButtonText(juce::String::fromUTF8("\xF0\x9F\x91\x8E"));
    thumbsDownButton.setTitle("This answer missed the mark");
    thumbsDownButton.setTooltip("This answer missed the mark");
    thumbsDownButton.onClick = [this] { setRating(AIChatComponent::PatchRatingUiState::Down); };

    addChildComponent(commentField);
    commentField.setComponentID("patchFeedbackComment");
    commentField.setText(data.ratingComment, juce::dontSendNotification);
    commentField.setTextToShowWhenEmpty("Optional: why? (Enter to send)",
                                        synth::theme::themeOf(*this).colors.textMuted);
    commentField.setTitle("Feedback comment");
    commentField.setTooltip("Optional: say why, then press Enter to send");
    commentField.onReturnKey = [this] { notifyRate(); };
    synth::ui::removeHiddenTabStops(commentField);

    addChildComponent(commentSaveButton);
    commentSaveButton.setButtonText("Send");
    commentSaveButton.setTooltip("Send your feedback comment");
    commentSaveButton.onClick = [this] { notifyRate(); };

    addChildComponent(detailsDisplay);
    detailsDisplay.setMultiLine(true);
    detailsDisplay.setReadOnly(true);
    detailsDisplay.setTitle("Edit plan details");
    detailsDisplay.setTooltip("The changes this plan makes");
    // Filled by fillDetails() the first time the panel opens: laying out a big plan's text is slow.
    synth::ui::removeHiddenTabStops(detailsDisplay);

    commentFade_.onFrame = [this] { relayout(); };
    detailsFade_.onFrame = [this] { relayout(); };
    // A card restored with a rating already picked shows its comment row from the start.
    commentFade_.setShown(currentRating != AIChatComponent::PatchRatingUiState::None);

    applyThemeColours();
}

void AIChatComponent::EditPlanCard::fillDetails() {
    if (detailsFilled)
        return;
    detailsFilled = true;
    detailsDisplay.setText(buildEditPlanDetailsText(planSummary), juce::dontSendNotification);
    planSummary = {};
}

// A change of height needs AIChatComponent::resized(): it sizes each bubble.
void AIChatComponent::EditPlanCard::relayout() {
    if (auto* chat = findParentComponentOfClass<AIChatComponent>())
        chat->resized();
    else
        resized();
}

// Theme tokens, resolved again once the card is parented: at construction a card is not yet in the
// themed window, so themeOf() still answers with the default theme.
void AIChatComponent::EditPlanCard::applyThemeColours() {
    const auto& colours = synth::theme::themeOf(*this).colors;
    headerLabel.setColour(juce::Label::textColourId, planOk ? colours.accent2 : colours.warning);
    previewLabel.setColour(juce::Label::textColourId, colours.textPrimary);
    commentField.setTextToShowWhenEmpty("Optional: why? (Enter to send)", colours.textMuted);
    if (applyButton) {
        applyButton->setColour(juce::TextButton::buttonColourId, applied ? colours.surfaceHi : colours.accent);
        applyButton->setColour(juce::TextButton::textColourOffId, applied ? colours.textMuted : colours.textPrimary);
    }
    thumbsUpButton.setColour(juce::TextButton::buttonColourId, currentRating == AIChatComponent::PatchRatingUiState::Up
                                                                   ? colours.success
                                                                   : colours.surfaceHi);
    thumbsDownButton.setColour(juce::TextButton::buttonColourId,
                               currentRating == AIChatComponent::PatchRatingUiState::Down ? colours.error
                                                                                          : colours.surfaceHi);
    detailsDisplay.setColour(juce::TextEditor::backgroundColourId, colours.bg0.withAlpha(0.6f));
}

// After a successful Apply the plan is in the project: the button stays as a disabled "Applied" so
// the card shows what happened, and undo is the project's own (one step).
void AIChatComponent::EditPlanCard::showApplied() {
    applied = true;
    if (!applyButton)
        return;
    applyButton->setButtonText("Applied");
    applyButton->setTitle("Edit plan applied");
    applyButton->setTooltip("This plan was applied; undo with Cmd+Z");
    applyButton->setEnabled(false);
    applyThemeColours();
}

int AIChatComponent::EditPlanCard::previewHeight(int width) const {
    int height = 0;
    for (const auto& line : previewLines)
        height += AIChatComponent::computeWrappedTextHeight(previewLabel.getFont(), line, juce::jmax(40, width));
    return juce::jmax(16, height);
}

void AIChatComponent::EditPlanCard::resized() {
    auto b = getLocalBounds().reduced(kPadding);
    headerLabel.setBounds(b.removeFromTop(kHeaderHeight));
    b.removeFromTop(kRowGap);
    previewLabel.setBounds(b.removeFromTop(previewHeight(b.getWidth())));
    b.removeFromTop(kRowGap);

    auto buttonRow = b.removeFromTop(kButtonRowHeight);
    detailsButton.setBounds(buttonRow.removeFromLeft(kDetailsButtonWidth).reduced(2));
    if (applyButton)
        applyButton->setBounds(buttonRow.removeFromRight(kApplyButtonWidth).reduced(2));
    b.removeFromTop(kRowGap);

    // The comment row appears only once a rating is picked, so an answer nobody has judged does
    // not invite a comment with nothing to attach it to. The row and the details panel are placed at
    // their full size; while they fade the card's height (getRequiredHeight) is what grows, so they
    // are revealed from the top as it does.
    auto thumbsRow = b.removeFromTop(kFeedbackRowHeight);
    thumbsUpButton.setBounds(thumbsRow.removeFromLeft(40).reduced(2));
    thumbsDownButton.setBounds(thumbsRow.removeFromLeft(40).reduced(2));

    auto rest = b.withHeight(kRowGap + kFeedbackRowHeight + kRowGap + kDetailsHeight);
    rest.removeFromTop(kRowGap);
    auto commentRow = rest.removeFromTop(kFeedbackRowHeight);
    commentSaveButton.setBounds(commentRow.removeFromRight(55).reduced(2));
    commentField.setBounds(commentRow.reduced(2));

    const int commentSlot = commentRowSlot();
    auto detailsArea = b.withHeight(commentSlot + kRowGap + kDetailsHeight);
    detailsArea.removeFromTop(commentSlot + kRowGap);
    detailsDisplay.setBounds(detailsArea);
}

int AIChatComponent::EditPlanCard::commentRowSlot() const {
    return juce::roundToInt(static_cast<float>(kRowGap + kFeedbackRowHeight) * commentFade_.progress());
}

int AIChatComponent::EditPlanCard::getRequiredHeight(int width) const {
    int height = kPadding * 2 + kHeaderHeight + kRowGap + previewHeight(width - kPadding * 2) + kRowGap +
                 kButtonRowHeight + kRowGap + kFeedbackRowHeight;
    height += commentRowSlot();
    height += juce::roundToInt(static_cast<float>(kRowGap + kDetailsHeight) * detailsFade_.progress());
    return height;
}

std::unique_ptr<juce::AccessibilityHandler> AIChatComponent::EditPlanCard::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

void AIChatComponent::EditPlanCard::setRating(AIChatComponent::PatchRatingUiState rating) {
    currentRating = rating;
    applyThemeColours();
    // A rating grows the card (the comment row fades in), and AIChatComponent::resized() is what
    // sizes each bubble, so that is the level that must lay out again.
    commentFade_.setShown(currentRating != AIChatComponent::PatchRatingUiState::None);
    relayout();
    notifyRate();
}

void AIChatComponent::EditPlanCard::notifyRate() {
    if (onRate)
        onRate(currentRating, commentField.getText());
}

// Deliberately no retry loop: the plan was previewed against the live project when it arrived, so
// what can fail here is the project having moved since (or a host build failing). That is worth
// reporting, not worth re-asking the model about.
bool AIChatComponent::applyEditPlan(size_t index) {
    if (index >= messages.size() || messages[index].planApplied)
        return false;
    const auto result = aiService.applyProjectEdit(juce::JSON::parse(messages[index].planJson));
    juce::Logger::writeToLog("Edit plan apply: " + (result.ok ? juce::String("applied") : result.message));
    if (result.ok) {
        messages[index].planApplied = true;
        saveCurrentConversationLocally();
        return true;
    }

    // An Apply that does nothing and says nothing is indistinguishable from a broken button. The
    // redraw is deferred: this runs inside the card's own click, and updateChatDisplay() deletes it.
    messages.push_back({"assistant",
                        "Could not apply this edit plan: " +
                            (result.message.isNotEmpty() ? result.message : juce::String("unknown error")),
                        ""});
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (auto* self = safeThis.getComponent())
            self->updateChatDisplay();
    });
    return false;
}

} // namespace synth
