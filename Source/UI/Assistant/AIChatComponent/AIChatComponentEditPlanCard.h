#pragma once

#include "AIChatComponent.h"

namespace synth {

// The one card an answer that changes the project renders: what the plan would do, one line per
// phase (the patch, then the timeline ops), and one Apply that hands the whole plan to
// AIIntegrationService::applyProjectEdit as one undo step. A plan the engine refused shows the
// reason and no Apply. Below that, a details toggle (the per-change list and the plan's JSON) and
// the thumbs feedback the patch card used to carry.
//
// To a screen reader the card is a group named "Edit plan"; the preview label carries the text as
// its description. Every button is a Tab stop with the accent focus ring AppLookAndFeel draws, a
// name and a tooltip. Nested in AIChatComponent so it reads MessageData directly.
class AIChatComponent::EditPlanCard : public juce::Component {
public:
    using RateCallback = std::function<void(AIChatComponent::PatchRatingUiState, const juce::String&)>;

    // `onApply` is ignored for a refused plan (data.planOk false), which gets no Apply button. It
    // returns whether the plan was applied: on true the card turns Apply into a disabled "Applied"
    // (data.planApplied starts a card in that state); on false Apply stays available.
    EditPlanCard(const MessageData& data, std::function<bool()> onApply, RateCallback onRate);

    void resized() override;
    void parentHierarchyChanged() override { applyThemeColours(); }

    // `width` must be the width this card is laid out at (MessageBubble passes its content width);
    // the preview text is measured wrapped at that width, so the two must agree.
    int getRequiredHeight(int width) const;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void setRating(AIChatComponent::PatchRatingUiState rating);
    void notifyRate();
    void applyThemeColours();
    void showApplied();
    int previewHeight(int width) const;
    void relayout();
    int commentRowSlot() const;

    static constexpr int kPadding = 8;
    static constexpr int kRowGap = 8;
    static constexpr int kHeaderHeight = 20;
    static constexpr int kButtonRowHeight = 28;
    static constexpr int kFeedbackRowHeight = 24;
    static constexpr int kDetailsButtonWidth = 110;
    static constexpr int kApplyButtonWidth = 88;
    static constexpr int kDetailsHeight = 240;

    const bool planOk;
    bool applied = false;
    juce::StringArray previewLines;
    bool isExpanded = false;
    AIChatComponent::PatchRatingUiState currentRating = AIChatComponent::PatchRatingUiState::None;
    RateCallback onRate;

    juce::Label headerLabel;
    juce::Label previewLabel;
    juce::TextButton detailsButton;
    std::unique_ptr<juce::TextButton> applyButton;
    juce::TextButton thumbsUpButton;
    juce::TextButton thumbsDownButton;
    juce::TextEditor commentField;
    juce::TextButton commentSaveButton;
    juce::TextEditor detailsDisplay;

    // The feedback comment row and the details panel open and close with a fade and a height tween
    // (160 ms in, 110 ms out); each frame lays the chat out again. Declared after what they wrap.
    synth::ui::FadeVisibility commentFade_{&commentField, &commentSaveButton};
    synth::ui::FadeVisibility detailsFade_{&detailsDisplay};
};

} // namespace synth
