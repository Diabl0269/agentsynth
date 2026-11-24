#include "AIChatComponentEditPlanCard.h"
#include "AI/PlanPreset.h"
#include "CardActionRow.h"
#include "DetailsPanelStyle.h"
#include "EditPlanDetailsText.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ReadableText.h"

namespace synth {

// Concern: the edit-plan card (layout, details, thumbs feedback) and what its Apply does.

AIChatComponent::EditPlanCard::EditPlanCard(const MessageData& data, std::function<bool()> onApply,
                                            RateCallback rateCallback)
    : planOk(data.planOk)
    , planJson(data.planJson)
    , canSavePreset(data.planOk && planpreset::patchValidates(data.planJson))
    , previewLines(data.planPreviewLines)
    , planSummary(data.planDetails)
    , currentRating(data.ratingState)
    , onRate(std::move(rateCallback)) {
    setTitle("Edit plan");
    {
        // Sizing needs only the first lines: the panel is at most kDetailsHeight tall.
        const auto all = juce::StringArray::fromLines(planSummary.substring(0, kMaxDetailsChars));
        for (int i = 0; i < juce::jmin(all.size(), 40); ++i)
            detailsLines.add(all[i]);
    }

    addAndMakeVisible(headerLabel);
    headerLabel.setMessage("Edit plan", juce::Font(14.0f, juce::Font::bold), juce::Colours::white);
    headerLabel.setTabStop(false);
    headerLabel.setComponentID("planHeaderText");
    headerLabel.setAccessible(false); // the card's own group title says it

    const juce::String preview = previewLines.joinIntoString("\n");
    addAndMakeVisible(previewLabel);
    // previewHeight() measures the text at the editor's width less the 2 px it keeps free at its right edge (see
    // SelectableChatText), so a long line wraps as measured and its last line is not clipped under the buttons.
    previewLabel.setMessage(preview, juce::Font(12.0f), juce::Colours::white);
    previewLabel.setTabStop(false);
    previewLabel.setComponentID("planPreviewText");
    previewLabel.setTitle(planOk ? "What this plan changes" : "Why this plan cannot apply");
    previewLabel.setDescription(preview);

    if (planOk) {
        for (const auto& line : previewLines) {
            auto icon = std::make_unique<synth::ui::StepIconView>();
            icon->setKind(kindForPlanLine(line));
            // Still until Apply, which plays each once in order and settles it into a check.
            icon->setState(data.planApplied ? synth::ui::StepIconState::done : synth::ui::StepIconState::still);
            addAndMakeVisible(*icon);
            rowIcons.push_back(std::move(icon));
        }
    }

    notesLine = buildNotesLine(data.planNotes, data.planSkippedCount);
    if (notesLine.isNotEmpty()) {
        const juce::String all = data.planNotes.joinIntoString("\n");
        addAndMakeVisible(notesLabel);
        notesLabel.setMessage(notesLine, juce::Font(11.0f), juce::Colours::white);
        notesLabel.setTabStop(false);
        notesLabel.setComponentID("planNotesText");
        notesLabel.setTitle("Parts of this plan that were left out or adjusted");
        notesLabel.setDescription(all);
        notesLabel.setTooltip(all);
    }

    addAndMakeVisible(detailsButton);
    detailsButton.setButtonText("Show details");
    detailsButton.setTitle("Show details");
    detailsButton.setTooltip("Show the changes this plan makes");
    detailsButton.onClick = [this] {
        isExpanded = !isExpanded;
        if (isExpanded)
            fillDetails();
        detailsButton.setButtonText(isExpanded ? "Hide details" : "Show details");
        detailsButton.setTitle(isExpanded ? "Hide details" : "Show details");
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

    // Before Apply in child order, so Tab goes Details, Save as preset, Apply, then the thumbs (the order they sit in
    // the row); Enter or Space presses it.
    addChildComponent(savePresetButton);
    savePresetButton.setButtonText("Save as preset");
    savePresetButton.setTitle("Save patch as preset");
    savePresetButton.setTooltip("Save this plan's patch as a preset without applying it to the project");
    savePresetButton.onClick = [this] { startSavePreset(); };
    saveRowShown = canSavePreset || (planOk && applyButton != nullptr);
    if (saveRowShown) {
        saveFade_.snapTo(true);
        if (!canSavePreset) {
            // Never silently missing: a plan that is not a patch of its own shows the button off, and says why.
            savePresetButton.setEnabled(false);
            savePresetButton.setWantsKeyboardFocus(false);
            savePresetButton.setTooltip("Save as preset needs a plan that is a complete patch on its own. This one "
                                        "changes modules already in the project or builds tracks, so it can only be "
                                        "applied.");
        } else if (data.planPresetSaved) {
            showPresetSaved();
        }
    }

    // Apply was added first (its click state is set above); the row's Tab order puts it after Save as preset.
    if (applyButton)
        applyButton->toFront(false);

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
    // The header's accent is a decoration colour that misses the readable bar on some themes' panels: nudged, hue kept.
    const std::vector<juce::Colour> panels{colours.bg0, colours.bg1, colours.surface};
    headerLabel.setTextColour(synth::theme::nudgeToReadable(planOk ? colours.accent2 : colours.warning, panels));
    previewLabel.setTextColour(colours.textPrimary);
    for (auto& icon : rowIcons) {
        icon->setColour(colours.accent);
        icon->setDoneColour(colours.success);
    }
    notesLabel.setTextColour(colours.textMuted);
    synth::ui::cardactions::applyOutlinedStyle(savePresetButton, colours);
    commentField.setTextToShowWhenEmpty("Optional: why? (Enter to send)", colours.textMuted);
    if (applyButton) {
        applyButton->setColour(juce::TextButton::buttonColourId, applied ? colours.surfaceHi : colours.accent);
        // Text on the accent fill falls back to white or black where the theme's text colour does not read on it.
        const auto fill = applied ? colours.surfaceHi : colours.accent;
        applyButton->setColour(juce::TextButton::textColourOffId,
                               synth::theme::readableTextOn(fill, applied ? colours.textMuted : colours.textPrimary));
    }
    thumbsUpButton.setColour(juce::TextButton::buttonColourId, currentRating == AIChatComponent::PatchRatingUiState::Up
                                                                   ? colours.success
                                                                   : colours.surfaceHi);
    thumbsDownButton.setColour(juce::TextButton::buttonColourId,
                               currentRating == AIChatComponent::PatchRatingUiState::Down ? colours.error
                                                                                          : colours.surfaceHi);
    synth::detailspanel::style(detailsDisplay, colours);
}

// A live theme switch reaches the card as a look-and-feel change; the colours above are set values, not read at paint.
void AIChatComponent::EditPlanCard::lookAndFeelChanged() { applyThemeColours(); }

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
    // Each change plays its icon once, in order, and settles into a check.
    for (size_t i = 0; i < rowIcons.size(); ++i)
        rowIcons[i]->playThenSettle((int)i * 180);
}

// The name window is the app's Save preset window (Enter saves, Escape cancels); the chat writes the file. The card
// only turns the button into a disabled "Saved" once a preset exists, as Apply does for an applied plan.
void AIChatComponent::EditPlanCard::startSavePreset() {
    auto* chat = findParentComponentOfClass<AIChatComponent>();
    // onClick can be invoked on the disabled button (a test, a script), so the guard is here too.
    if (chat == nullptr || presetSaved || !canSavePreset)
        return;
    juce::Component::SafePointer<EditPlanCard> safeThis(this);
    chat->savePlanAsPreset(planJson, [safeThis](const juce::String& saved) {
        auto* card = safeThis.getComponent();
        if (card == nullptr)
            return;
        if (saved.isNotEmpty())
            card->showPresetSaved();
    });
}

void AIChatComponent::EditPlanCard::showPresetSaved() {
    presetSaved = true;
    savePresetButton.setButtonText("Saved");
    savePresetButton.setTitle("Preset saved");
    savePresetButton.setTooltip("This plan's patch was saved as a preset; open it from the Open Patch dialog");
    savePresetButton.setEnabled(false);
}

int AIChatComponent::EditPlanCard::previewHeight(int width) const {
    int height = 0;
    for (const auto& line : previewLines)
        height += AIChatComponent::computeWrappedTextHeight(previewLabel.getFont(), line,
                                                            juce::jmax(40, width - textInset() - kEditorSlack));
    return juce::jmax(16, height);
}

songsteps::StepKind AIChatComponent::EditPlanCard::kindForPlanLine(const juce::String& line) {
    using songsteps::StepKind;
    struct Word {
        const char* text;
        StepKind kind;
    };
    static const Word words[] = {
        {"track", StepKind::track},           {"note", StepKind::pattern},          {"clip", StepKind::pattern},
        {"tempo", StepKind::sections},        {"marker", StepKind::sections},       {"lane", StepKind::automation},
        {"automation", StepKind::automation}, {"module", StepKind::module},         {"connection", StepKind::connect},
        {"cable", StepKind::connect},         {"modulation", StepKind::modulation}, {"parameter", StepKind::settings}};
    int best = line.length();
    StepKind kind = StepKind::module;
    for (const auto& word : words) {
        const int at = line.indexOfIgnoreCase(word.text);
        if (at >= 0 && at < best) {
            best = at;
            kind = word.kind;
        }
    }
    return kind;
}

juce::String AIChatComponent::EditPlanCard::buildNotesLine(const juce::StringArray& notes, int skippedCount) {
    if (notes.isEmpty())
        return {};
    const int count = notes.size();
    const bool allSkipped = skippedCount >= count;
    juce::String line = juce::String(count) + (count == 1 ? " detail " : " details ") +
                        (allSkipped ? "skipped: " : "adjusted: ") + notes[0];
    if (count > 1)
        line << " (and " << juce::String(count - 1) << " more)";
    return line;
}

int AIChatComponent::EditPlanCard::notesHeight(int width) const {
    if (notesLine.isEmpty())
        return 0;
    return kRowGap / 2 + AIChatComponent::computeWrappedTextHeight(notesLabel.getFont(), notesLine,
                                                                   juce::jmax(40, width - kEditorSlack));
}

void AIChatComponent::EditPlanCard::resized() {
    auto b = getLocalBounds().reduced(kPadding);
    headerLabel.setBounds(b.removeFromTop(kHeaderHeight));
    b.removeFromTop(kRowGap);
    auto previewArea = b.removeFromTop(previewHeight(b.getWidth()));
    // Each icon sits at the top of its sentence, which wraps at the narrower text width.
    int rowY = previewArea.getY();
    for (size_t i = 0; i < rowIcons.size(); ++i) {
        rowIcons[i]->setBounds(previewArea.getX(), rowY, kIconSize, kIconSize);
        rowY += AIChatComponent::computeWrappedTextHeight(
            previewLabel.getFont(), previewLines[(int)i],
            juce::jmax(40, previewArea.getWidth() - textInset() - kEditorSlack));
    }
    previewLabel.setBounds(previewArea.withTrimmedLeft(textInset()));
    if (notesLine.isNotEmpty()) {
        b.removeFromTop(kRowGap / 2);
        notesLabel.setBounds(b.removeFromTop(notesHeight(b.getWidth()) - kRowGap / 2));
    }
    b.removeFromTop(kRowGap);

    // One row, quiet to loud (CardActionRow): Show details at the left, Save as preset outlined, Apply in the corner.
    // The full-size row first; when the three do not fit (a 280 px chat leaves the card 176 px), the buttons take
    // tighter padding and the link its short text ("Details" / "Hide"; its title and tooltip stay whole).
    using synth::ui::cardactions::fitWidth;
    constexpr int kGaps = 2 * synth::ui::cardactions::kButtonGap;
    auto setLinkText = [this](bool compact) {
        detailsButton.setButtonText(isExpanded ? (compact ? "Hide" : "Hide details")
                                               : (compact ? "Details" : "Show details"));
    };
    setLinkText(false);
    synth::ui::cardactions::Row row;
    row.link = &detailsButton;
    row.linkWidth = detailsButton.getIdealWidth();
    row.middleWidth = saveRowShown ? fitWidth(savePresetButton, 10) : 0;
    row.primaryWidth = applyButton ? fitWidth(*applyButton, 10, 64) : 0;
    if (row.linkWidth + row.middleWidth + row.primaryWidth + kGaps > b.getWidth()) {
        setLinkText(true);
        row.linkWidth = detailsButton.getIdealWidth();
        row.middleWidth = saveRowShown ? fitWidth(savePresetButton, 6) : 0;
        row.primaryWidth = applyButton ? fitWidth(*applyButton, 8, 48) : 0;
    }
    if (saveRowShown)
        row.middle = &savePresetButton;
    if (applyButton)
        row.primary = applyButton.get();
    synth::ui::cardactions::layout(b.removeFromTop(synth::ui::cardactions::kRowHeight), row);
    b.removeFromTop(kRowGap);

    // The comment row appears only once a rating is picked, so an answer nobody has judged does
    // not invite a comment with nothing to attach it to. The row and the details panel are placed at
    // their full size; while they fade the card's height (getRequiredHeight) is what grows, so they
    // are revealed from the top as it does.
    auto thumbsRow = b.removeFromTop(kFeedbackRowHeight);
    thumbsUpButton.setBounds(thumbsRow.removeFromLeft(40).reduced(2));
    thumbsDownButton.setBounds(thumbsRow.removeFromLeft(40).reduced(2));

    const int panelHeight = detailsHeight(b.getWidth());
    auto rest = b.withHeight(kRowGap + kFeedbackRowHeight + kRowGap + panelHeight);
    rest.removeFromTop(kRowGap);
    auto commentRow = rest.removeFromTop(kFeedbackRowHeight);
    commentSaveButton.setBounds(commentRow.removeFromRight(55).reduced(2));
    commentField.setBounds(commentRow.reduced(2));

    const int commentSlot = commentRowSlot();
    auto detailsArea = b.withHeight(commentSlot + kRowGap + panelHeight);
    detailsArea.removeFromTop(commentSlot + kRowGap);
    detailsDisplay.setBounds(detailsArea);
}

int AIChatComponent::EditPlanCard::detailsHeight(int width) const {
    // Before the first open the panel is empty and sized from the summary; once filled the summary is gone, so the
    // lines kept at construction stand in for it (the text shown is those lines, then a note when it was cut).
    return synth::detailspanel::heightFor(
        detailsLines.isEmpty() ? juce::StringArray{"No further details."} : detailsLines, width, kDetailsHeight);
}

int AIChatComponent::EditPlanCard::commentRowSlot() const {
    return juce::roundToInt(static_cast<float>(kRowGap + kFeedbackRowHeight) * commentFade_.progress());
}

int AIChatComponent::EditPlanCard::getRequiredHeight(int width) const {
    int height = kPadding * 2 + kHeaderHeight + kRowGap + previewHeight(width - kPadding * 2) +
                 notesHeight(width - kPadding * 2) + kRowGap + synth::ui::cardactions::kRowHeight + kRowGap +
                 kFeedbackRowHeight;
    height += commentRowSlot();
    height +=
        juce::roundToInt(static_cast<float>(kRowGap + detailsHeight(width - kPadding * 2)) * detailsFade_.progress());
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
    recordApplied(messages[index], result);
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
