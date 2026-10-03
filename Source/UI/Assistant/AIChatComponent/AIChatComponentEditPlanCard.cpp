#include "AIChatComponentEditPlanCard.h"
#include "UI/Layout/DialogKeyboard.h"

namespace synth {

// Concern: the edit-plan card (layout, details, thumbs feedback) and what its Apply does.

namespace {

// Round-trips `raw` through JUCE's JSON formatter for indentation, so the details panel isn't one
// unbroken line in a ~280px-wide chat column. Falls back to the raw string on a parse failure.
juce::String prettyPrintJson(const juce::String& raw) {
    const juce::var parsed = juce::JSON::parse(raw);
    return parsed.isVoid() ? raw : juce::JSON::toString(parsed, /*allOnOneLine=*/false);
}

} // namespace

AIChatComponent::EditPlanCard::EditPlanCard(const MessageData& data, std::function<void()> onApply,
                                            RateCallback rateCallback)
    : planOk(data.planOk)
    , previewLines(data.planPreviewLines)
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
    previewLabel.setJustificationType(juce::Justification::topLeft);
    previewLabel.setTitle(planOk ? "What this plan changes" : "Why this plan cannot apply");
    previewLabel.setDescription(preview);

    addAndMakeVisible(detailsButton);
    detailsButton.setButtonText("Show details");
    detailsButton.setTooltip("Show the module changes and the raw JSON behind this plan");
    detailsButton.onClick = [this] {
        isExpanded = !isExpanded;
        detailsButton.setButtonText(isExpanded ? "Hide details" : "Show details");
        if (auto* chat = findParentComponentOfClass<AIChatComponent>())
            chat->resized();
        else
            resized();
    };

    if (planOk && onApply) {
        applyButton = std::make_unique<juce::TextButton>("Apply");
        applyButton->setTitle("Apply edit plan");
        applyButton->setTooltip("Apply this answer to the project (one undo step)");
        applyButton->onClick = std::move(onApply);
        addAndMakeVisible(*applyButton);
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
    commentField.setTextToShowWhenEmpty("Optional: why? (Enter to send)", juce::Colours::grey);
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
    detailsDisplay.setTooltip("The changes this plan makes, then its JSON");
    detailsDisplay.setText((data.planDetails.isNotEmpty() ? data.planDetails + "\n\n" : juce::String()) +
                           prettyPrintJson(data.planJson));
    synth::ui::removeHiddenTabStops(detailsDisplay);

    applyThemeColours();
}

// Theme tokens, resolved again once the card is parented: at construction a card is not yet in the
// themed window, so getLookAndFeel() is still the default and only the fallbacks would apply.
void AIChatComponent::EditPlanCard::applyThemeColours() {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto colours = lf != nullptr ? lf->getTheme().colors : synth::theme::Theme{}.colors;
    headerLabel.setColour(juce::Label::textColourId, planOk ? colours.accent2 : colours.warning);
    previewLabel.setColour(juce::Label::textColourId, colours.textPrimary);
    if (applyButton)
        applyButton->setColour(juce::TextButton::buttonColourId, colours.accent);
    thumbsUpButton.setColour(juce::TextButton::buttonColourId, currentRating == AIChatComponent::PatchRatingUiState::Up
                                                                   ? colours.success
                                                                   : colours.surfaceHi);
    thumbsDownButton.setColour(juce::TextButton::buttonColourId,
                               currentRating == AIChatComponent::PatchRatingUiState::Down ? colours.error
                                                                                          : colours.surfaceHi);
    detailsDisplay.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.3f));
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
    // not invite a comment with nothing to attach it to.
    auto thumbsRow = b.removeFromTop(kFeedbackRowHeight);
    thumbsUpButton.setBounds(thumbsRow.removeFromLeft(40).reduced(2));
    thumbsDownButton.setBounds(thumbsRow.removeFromLeft(40).reduced(2));
    const bool showComment = currentRating != AIChatComponent::PatchRatingUiState::None;
    commentField.setVisible(showComment);
    commentSaveButton.setVisible(showComment);
    if (showComment) {
        b.removeFromTop(kRowGap);
        auto commentRow = b.removeFromTop(kFeedbackRowHeight);
        commentSaveButton.setBounds(commentRow.removeFromRight(55).reduced(2));
        commentField.setBounds(commentRow.reduced(2));
    }

    detailsDisplay.setVisible(isExpanded);
    if (isExpanded) {
        b.removeFromTop(kRowGap);
        detailsDisplay.setBounds(b.removeFromTop(kDetailsHeight));
    }
}

int AIChatComponent::EditPlanCard::getRequiredHeight(int width) const {
    const bool showComment = currentRating != AIChatComponent::PatchRatingUiState::None;
    int height = kPadding * 2 + kHeaderHeight + kRowGap + previewHeight(width - kPadding * 2) + kRowGap +
                 kButtonRowHeight + kRowGap + kFeedbackRowHeight;
    if (showComment)
        height += kRowGap + kFeedbackRowHeight;
    if (isExpanded)
        height += kRowGap + kDetailsHeight;
    return height;
}

std::unique_ptr<juce::AccessibilityHandler> AIChatComponent::EditPlanCard::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

void AIChatComponent::EditPlanCard::setRating(AIChatComponent::PatchRatingUiState rating) {
    currentRating = rating;
    applyThemeColours();
    // A rating grows the card (the comment row appears), and AIChatComponent::resized() is what
    // sizes each bubble, so that is the level that must lay out again.
    if (auto* chat = findParentComponentOfClass<AIChatComponent>())
        chat->resized();
    else
        resized();
    notifyRate();
}

void AIChatComponent::EditPlanCard::notifyRate() {
    if (onRate)
        onRate(currentRating, commentField.getText());
}

// Deliberately no retry loop: the plan was previewed against the live project when it arrived, so
// what can fail here is the project having moved since (or a host build failing). That is worth
// reporting, not worth re-asking the model about.
void AIChatComponent::applyEditPlan(const juce::String& planJson) {
    const auto result = aiService.applyProjectEdit(juce::JSON::parse(planJson));
    juce::Logger::writeToLog("Edit plan apply: " + (result.ok ? juce::String("applied") : result.message));
    if (result.ok)
        return;

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
}

} // namespace synth
