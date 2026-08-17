#include "AIChatComponent.h"
#include "../AI/PatchDiff.h"
#include "../Branding.h"

namespace synth {

// Helper function to extract JSON blocks
juce::StringArray extractJSONBlocks(const juce::String& text) {
    juce::StringArray blocks;
    int searchFrom = 0;

    while (true) {
        int start = text.indexOf(searchFrom, "```json");
        if (start == -1)
            break;

        int end = text.indexOf(start + 7, "```");
        if (end == -1)
            break;

        blocks.add(text.substring(start + 7, end).trim());
        searchFrom = end + 3;
    }

    return blocks;
}

namespace {

// Colour for a merge-mode diff line, grouped by PatchChange::Kind (see groupChangesByKind() in
// PatchDiff.h). "+"-prefixed adds are green, "-"-prefixed removals are red/orange; param changes
// and modulation add/remove (which are often a matched pair representing one conceptual "change",
// not an independent add and remove) get a neutral amber rather than fighting for green/red.
juce::Colour colourForKind(PatchChange::Kind kind) {
    using Kind = PatchChange::Kind;
    switch (kind) {
    case Kind::NodeAdded:
    case Kind::ConnectionAdded:
        return juce::Colours::lightgreen;
    case Kind::NodeRemoved:
    case Kind::ConnectionRemoved:
        return juce::Colour(0xFFFF8A65); // orange-red
    case Kind::ParamChanged:
    case Kind::ModulationAdded:
    case Kind::ModulationRemoved:
        return juce::Colour(0xFFFFC107); // amber
    }
    return juce::Colours::white;
}

// Round-trips `raw` through JUCE's JSON formatter for indentation, so the "View JSON" panel isn't
// one unbroken line in a ~280px-wide chat column. Falls back to the raw string on parse failure
// (shouldn't happen — this is a patch that already round-tripped through extractJSONBlocks — but
// must not blank the view if it ever does).
juce::String prettyPrintJson(const juce::String& raw) {
    juce::var parsed = juce::JSON::parse(raw);
    if (parsed.isVoid())
        return raw;
    return juce::JSON::toString(parsed, /*allOnOneLine=*/false);
}

} // namespace

//==============================================================================
class AIChatComponent::PatchCard : public juce::Component {
public:
    // `changes`/`diffAvailable`/`summary` come from AIIntegrationService::computePatchPreview()'s
    // before/after AIStateMapper::graphToJSON() snapshots, computed by the caller in
    // attachPatchPreview() — see docs/AI_Engine.md "Patch Diff Preview". This IS the preview: it's
    // the card's default view, rendered before Apply/Merge is ever clicked. The raw JSON stays
    // available behind the "View JSON" toggle for anyone who wants it.
    //
    // `changes` (synth::computeDiff() output) is used for merge-mode cards, which have stable node
    // identity to diff against. `summary` (synth::summarizePatch() of just the "after" snapshot) is
    // used for replace-mode cards instead: replace mode has no stable node identity between
    // snapshots, so a diff would show the entire prior graph removed and the entire new patch
    // added — technically correct, useless to read. See PatchDiff.h.
    PatchCard(const juce::String& json, std::function<void()> applyCallback, bool isMerge,
              const std::vector<PatchChange>& changes, bool diffAvailable, const PatchSummary& summary,
              AIChatComponent::PatchRatingUiState initialRating, const juce::String& initialComment,
              std::function<void(AIChatComponent::PatchRatingUiState, const juce::String&)> onRateCallback)
        : patchJson(json)
        , onApply(applyCallback)
        , onRate(std::move(onRateCallback))
        , currentRating(initialRating) {

        addAndMakeVisible(headerLabel);
        headerLabel.setText(isMerge ? "Patch Update" : "New Patch", juce::dontSendNotification);
        headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        headerLabel.setColour(juce::Label::textColourId,
                              isMerge ? juce::Colours::lightyellow : juce::Colours::lightgreen);

        addAndMakeVisible(expandButton);
        expandButton.setButtonText("View JSON");
        expandButton.setToggleable(true);
        expandButton.onClick = [this]() {
            isExpanded = !isExpanded;
            expandButton.setButtonText(isExpanded ? "Hide JSON" : "View JSON");
            if (auto* parent = getParentComponent())
                parent->resized();
        };

        addAndMakeVisible(applyButton);
        applyButton.setButtonText(isMerge ? "Merge" : "New Patch");
        applyButton.setColour(juce::TextButton::buttonColourId,
                              isMerge ? juce::Colour(0xFF8B6914) : juce::Colours::darkgreen);
        applyButton.onClick = onApply;

        addAndMakeVisible(thumbsUpButton);
        thumbsUpButton.setButtonText(juce::String::fromUTF8("\xF0\x9F\x91\x8D"));
        thumbsUpButton.setTooltip("This patch was helpful");
        thumbsUpButton.onClick = [this]() { setRating(AIChatComponent::PatchRatingUiState::Up); };

        addAndMakeVisible(thumbsDownButton);
        thumbsDownButton.setButtonText(juce::String::fromUTF8("\xF0\x9F\x91\x8E"));
        thumbsDownButton.setTooltip("This patch missed the mark");
        thumbsDownButton.onClick = [this]() { setRating(AIChatComponent::PatchRatingUiState::Down); };

        addAndMakeVisible(commentField);
        commentField.setComponentID("patchFeedbackComment");
        commentField.setText(initialComment, juce::dontSendNotification);
        commentField.setTextToShowWhenEmpty("Optional: why? (Enter to send)", juce::Colours::grey);
        commentField.onReturnKey = [this]() { notifyRate(); };

        addAndMakeVisible(commentSaveButton);
        commentSaveButton.setButtonText("Send");
        commentSaveButton.setTooltip("Send your feedback comment");
        commentSaveButton.onClick = [this]() { notifyRate(); };

        updateThumbColours();

        addAndMakeVisible(diffDisplay);
        diffDisplay.setMultiLine(true);
        diffDisplay.setReadOnly(true);
        diffDisplay.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.3f));

        if (!diffAvailable) {
            diffLineCount = 1;
            diffDisplay.setText("Preview unavailable - this patch may be rejected when applied.");
        } else if (isMerge) {
            // Grouped by Kind (adds, then removes, then param changes, then connection
            // adds/removes, then modulation adds/removes) so the list doesn't interleave — see
            // groupChangesByKind()'s doc comment. Rendered line-by-line via insertTextAtCaret with
            // the TextEditor's textColourId set per segment (setText() can't colour per-line; this
            // is the same pattern flushDebugLog() uses for insertTextAtCaret, minus the colouring).
            auto grouped = groupChangesByKind(changes);
            if (grouped.empty()) {
                diffLineCount = 1;
                diffDisplay.setText("No changes.");
            } else {
                diffLineCount = (int)grouped.size();
                for (size_t i = 0; i < grouped.size(); ++i) {
                    diffDisplay.setColour(juce::TextEditor::textColourId, colourForKind(grouped[i].kind));
                    diffDisplay.insertTextAtCaret(grouped[i].describe());
                    if (i + 1 < grouped.size())
                        diffDisplay.insertTextAtCaret("\n");
                }
            }
        } else {
            // Replace mode: a plain positive summary of what the new patch contains, not a diff
            // against the old graph (see class doc comment above).
            juce::StringArray lines;
            lines.add("New patch: " + juce::String((int)summary.nodeTypes.size()) +
                      (summary.nodeTypes.size() == 1 ? " module" : " modules"));
            for (const auto& t : summary.nodeTypes)
                lines.add(t);
            if (summary.connectionCount > 0)
                lines.add(juce::String(summary.connectionCount) +
                          (summary.connectionCount == 1 ? " connection" : " connections"));
            diffLineCount = lines.size();
            diffDisplay.setText(lines.joinIntoString("\n"));
        }

        addAndMakeVisible(jsonDisplay);
        jsonDisplay.setMultiLine(true);
        jsonDisplay.setReadOnly(true);
        jsonDisplay.setText(prettyPrintJson(patchJson));
        jsonDisplay.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.3f));
        jsonDisplay.setVisible(false);
    }

    void resized() override {
        auto b = getLocalBounds().reduced(5);
        auto header = b.removeFromTop(25);
        headerLabel.setBounds(header.removeFromLeft(120));

        auto buttons = header.removeFromRight(180);
        applyButton.setBounds(buttons.removeFromRight(80).reduced(2));
        expandButton.setBounds(buttons.removeFromRight(90).reduced(2));

        b.removeFromTop(5);

        // Feedback rows: thumbs are always visible on a patch card, on their own row now that
        // they're single glyphs rather than "Good"/"Bad" labels. The comment field/send button
        // only appear once a rating has been picked, so a patch nobody has judged yet doesn't
        // invite a comment with nothing to attach it to — that second row lives below the thumbs
        // rather than sharing their row, so the comment field has full card width to work with.
        auto thumbsRow = b.removeFromTop(kFeedbackRowHeight);
        thumbsUpButton.setBounds(thumbsRow.removeFromLeft(40).reduced(2));
        thumbsDownButton.setBounds(thumbsRow.removeFromLeft(40).reduced(2));
        bool showComment = currentRating != AIChatComponent::PatchRatingUiState::None;
        commentSaveButton.setVisible(showComment);
        commentField.setVisible(showComment);
        if (showComment) {
            b.removeFromTop(4);
            auto commentRow = b.removeFromTop(kFeedbackRowHeight);
            commentSaveButton.setBounds(commentRow.removeFromRight(55).reduced(2));
            commentField.setBounds(commentRow.reduced(2));
        }
        b.removeFromTop(5);

        if (isExpanded) {
            diffDisplay.setBounds(b.removeFromTop(diffAreaHeight()));
            b.removeFromTop(5);
            jsonDisplay.setVisible(true);
            jsonDisplay.setBounds(b);
        } else {
            diffDisplay.setBounds(b);
            jsonDisplay.setVisible(false);
        }
    }

    int getRequiredHeight() const {
        bool showComment = currentRating != AIChatComponent::PatchRatingUiState::None;
        int height = 35 + kFeedbackRowHeight + (showComment ? 4 + kFeedbackRowHeight : 0) + 5 + diffAreaHeight();
        if (isExpanded)
            height += 5 + kRawJsonHeight;
        return height;
    }

    void setRating(AIChatComponent::PatchRatingUiState rating) {
        currentRating = rating;
        updateThumbColours();
        // A rating being set changes the card's total height (the comment row appears below the
        // thumbs row rather than repurposing it). MessageBubble::resized() repositions this card
        // within its own bounds but doesn't own that bounds' size — AIChatComponent::resized() is
        // what computes each bubble's height via bubble->getRequiredHeight(width) and lays out the
        // whole message list, so that's the level that must relayout, not just the immediate
        // parent. Fall back to resizing this card directly when there's no such ancestor yet (e.g.
        // a unit test constructing PatchCard standalone).
        if (auto* chat = findParentComponentOfClass<AIChatComponent>())
            chat->resized();
        else
            resized();
        notifyRate();
    }

    void notifyRate() {
        if (onRate)
            onRate(currentRating, commentField.getText());
    }

    void updateThumbColours() {
        auto neutral = juce::Colours::darkgrey;
        thumbsUpButton.setColour(juce::TextButton::buttonColourId,
                                 currentRating == AIChatComponent::PatchRatingUiState::Up ? juce::Colours::darkgreen
                                                                                          : neutral);
        thumbsDownButton.setColour(juce::TextButton::buttonColourId,
                                   currentRating == AIChatComponent::PatchRatingUiState::Down ? juce::Colour(0xFF8B3A3A)
                                                                                              : neutral);
    }

private:
    static constexpr int kLineHeight = 16;
    static constexpr int kMinDiffHeight = 24;
    // Caps how tall a very long diff can grow the card; the "View JSON" toggle (which also shows
    // the diff area above the raw JSON, both individually scrollable TextEditors) is the escape
    // hatch rather than letting the message list grow unbounded.
    static constexpr int kMaxDiffHeight = 220;
    // Grew from 160: pretty-printed (indented) JSON runs noticeably taller than the single
    // unbroken line this used to hold.
    static constexpr int kRawJsonHeight = 240;
    static constexpr int kFeedbackRowHeight = 24;

    int diffAreaHeight() const { return juce::jlimit(kMinDiffHeight, kMaxDiffHeight, diffLineCount * kLineHeight + 8); }

    std::function<void(AIChatComponent::PatchRatingUiState, const juce::String&)> onRate;
    AIChatComponent::PatchRatingUiState currentRating = AIChatComponent::PatchRatingUiState::None;

    juce::String patchJson;
    std::function<void()> onApply;
    bool isExpanded = false;
    int diffLineCount = 1;

    juce::Label headerLabel;
    juce::TextButton expandButton;
    juce::TextButton applyButton;
    juce::TextButton thumbsUpButton;
    juce::TextButton thumbsDownButton;
    juce::TextEditor commentField;
    juce::TextButton commentSaveButton;
    juce::TextEditor diffDisplay;
    juce::TextEditor jsonDisplay;
};

//==============================================================================
// PatchCard's sibling, kept to its conventions (header label + a coloured apply button on
// the header row) with the one honest difference: a timeline suggestion has a validated PREVIEW to
// show — "Adds midi track "Bass"; places 1 clip (8 notes) at 0-4 on "Bass"" — rather than raw JSON
// to expand, so the body is that sentence instead of a JSON dump.
//
// The apply callback is EMPTY when the envelope failed validation; the card then shows the reason
// and offers no button, because a suggestion that cannot be applied must still say why (the same
// rule that stops applyPatch swallowing a rejection) but must not look clickable.
class AIChatComponent::TimelineCard : public juce::Component {
public:
    TimelineCard(const juce::String& preview, std::function<void()> applyCallback)
        : previewText(preview) {

        addAndMakeVisible(headerLabel);
        headerLabel.setText("Timeline Changes", juce::dontSendNotification);
        headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        headerLabel.setColour(juce::Label::textColourId, juce::Colours::lightskyblue);

        if (applyCallback) {
            applyButton = std::make_unique<juce::TextButton>();
            applyButton->setButtonText("Apply timeline changes");
            applyButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF1F4E63));
            applyButton->onClick = std::move(applyCallback);
            addAndMakeVisible(*applyButton);
        }

        addAndMakeVisible(previewLabel);
        previewLabel.setText(previewText, juce::dontSendNotification);
        previewLabel.setFont(juce::Font(12.0f));
        previewLabel.setMinimumHorizontalScale(1.0f);
        previewLabel.setJustificationType(juce::Justification::topLeft);
        previewLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
    }

    void resized() override {
        auto b = getLocalBounds().reduced(5);
        auto header = b.removeFromTop(kHeaderHeight);
        if (applyButton)
            applyButton->setBounds(header.removeFromRight(kApplyButtonWidth).reduced(2));
        headerLabel.setBounds(header);
        previewLabel.setBounds(b);
    }

    // Measured against the width the card will actually be laid out at, so the sentence never
    // renders clipped — the same GlyphArrangement measurement MessageBubble does for its own text.
    int getRequiredHeight(int width) const {
        juce::GlyphArrangement ga;
        ga.addJustifiedText(previewLabel.getFont(), previewText, 0.0f, 0.0f,
                            static_cast<float>(juce::jmax(40, width - 10)), juce::Justification::left);
        return kHeaderHeight + juce::jmax(16, static_cast<int>(ga.getBoundingBox(0, -1, true).getHeight())) + 10;
    }

private:
    static constexpr int kHeaderHeight = 25;
    static constexpr int kApplyButtonWidth = 160;

    juce::String previewText;
    juce::Label headerLabel;
    juce::Label previewLabel;
    std::unique_ptr<juce::TextButton> applyButton;
};

//==============================================================================
class AIChatComponent::MessageBubble : public juce::Component {
public:
    MessageBubble(const MessageData& data, std::function<void(const juce::String&)> applyPatch, bool isMerge,
                  std::function<void(const juce::URL&)> urlOpener, const std::vector<PatchChange>& patchDiff,
                  bool patchDiffAvailable, const PatchSummary& patchSummary,
                  std::function<void(AIChatComponent::PatchRatingUiState, const juce::String&)> onRate,
                  std::function<void(const juce::String&)> applyTimelineOps) {
        role = data.role;
        text = data.text;
        responseMs = data.responseMs;

        addAndMakeVisible(textLabel);
        textLabel.setText(text, juce::dontSendNotification);
        textLabel.setMinimumHorizontalScale(1.0f);
        textLabel.setJustificationType(juce::Justification::topLeft);

        if (data.jsonPatch.isNotEmpty()) {
            patchCard = std::make_unique<PatchCard>(
                data.jsonPatch, [applyPatch, json = data.jsonPatch]() { applyPatch(json); }, isMerge, patchDiff,
                patchDiffAvailable, patchSummary, data.ratingState, data.ratingComment, onRate);
            addAndMakeVisible(*patchCard);
        }

        // Independent of the patch card above: a response carrying both gets both cards, and
        // the user applies each on its own terms. No apply callback when the envelope is empty —
        // that is the rejected case, where the preview holds the reason instead of a summary.
        if (data.timelineOpsPreview.isNotEmpty()) {
            std::function<void()> onApply;
            if (data.timelineOpsJson.isNotEmpty() && applyTimelineOps)
                onApply = [applyTimelineOps, envelope = data.timelineOpsJson] { applyTimelineOps(envelope); };
            timelineCard = std::make_unique<TimelineCard>(data.timelineOpsPreview, std::move(onApply));
            addAndMakeVisible(*timelineCard);
        }

        if (data.showUpgradeAction) {
            upgradeButton = std::make_unique<juce::TextButton>();
            upgradeButton->setButtonText("Upgrade to Pro");
            upgradeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF6B4FBB));
            upgradeButton->onClick = [urlOpener] { urlOpener(juce::URL(synth::branding::kUpgradeUrl)); };
            addAndMakeVisible(*upgradeButton);
        }
    }

    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().reduced(2).toFloat();
        bool isUser = (role == "user");

        juce::Colour baseColour = isUser ? juce::Colours::blue : juce::Colours::darkgrey.brighter(0.2f);
        juce::ColourGradient grad(baseColour.withAlpha(0.3f), b.getX(), b.getY(), baseColour.withAlpha(0.1f),
                                  b.getRight(), b.getBottom(), false);

        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, 10.0f);

        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle(b, 10.0f, 1.0f);

        // Role indicator (+ optional elapsed-wait marker on assistant bubbles)
        auto roleBand = b.removeFromTop(12).reduced(5, 0);
        g.setColour(isUser ? juce::Colours::lightblue : juce::Colours::grey);
        g.setFont(juce::Font(10.0f, juce::Font::italic));
        g.drawText(isUser ? "YOU" : "AI", roleBand, juce::Justification::left);

        if (!isUser && responseMs >= 0) {
            g.drawText(AIChatComponent::formatResponseTime(responseMs), roleBand, juce::Justification::right);
        }
    }

    void resized() override {
        auto b = getLocalBounds().reduced(10);

        if (patchCard) {
            patchCard->setBounds(b.removeFromBottom(patchCard->getRequiredHeight()));
            b.removeFromBottom(5);
        }

        if (timelineCard) {
            timelineCard->setBounds(b.removeFromBottom(timelineCard->getRequiredHeight(b.getWidth())));
            b.removeFromBottom(5);
        }

        if (upgradeButton) {
            upgradeButton->setBounds(b.removeFromBottom(kUpgradeButtonHeight));
            b.removeFromBottom(5);
        }

        textLabel.setBounds(b);
    }

    int getRequiredHeight(int width) {
        int contentWidth = width - 20; // Margin
        juce::Font font = textLabel.getFont();

        juce::GlyphArrangement ga;
        ga.addJustifiedText(font, text, 0.0f, 0.0f, (float)contentWidth, juce::Justification::left);

        int textHeight = (int)ga.getBoundingBox(0, -1, true).getHeight();
        int height = textHeight + 25; // Base height for text + role label

        if (patchCard) {
            height += 10 + patchCard->getRequiredHeight();
        }

        if (timelineCard) {
            height += 5 + timelineCard->getRequiredHeight(contentWidth);
        }

        if (upgradeButton) {
            height += 5 + kUpgradeButtonHeight;
        }

        return juce::jmax(40, height);
    }

private:
    static constexpr int kUpgradeButtonHeight = 28;

    juce::String role;
    juce::String text;
    int responseMs = -1;
    juce::Label textLabel;
    std::unique_ptr<PatchCard> patchCard;
    std::unique_ptr<TimelineCard> timelineCard;
    std::unique_ptr<juce::TextButton> upgradeButton;
};

//==============================================================================
AIChatComponent::AIChatComponent(AIIntegrationService& service, juce::ApplicationProperties& props)
    : aiService(service)
    , appProperties(props) {

#ifdef NDEBUG
    juce::Logger::writeToLog("AIChatComponent initialized (Release)");
#else
    juce::Logger::writeToLog("AIChatComponent initialized (Debug)");

    // Add debug components first so tests that iterate children find main components last
    debugConsole.setMultiLine(true);
    debugConsole.setReadOnly(true);
    debugConsole.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
    debugConsole.setColour(juce::TextEditor::textColourId, juce::Colours::lime);
    debugConsole.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    debugConsole.setVisible(false);
    addChildComponent(debugConsole);

    toggleDebugButton.setButtonText("Debug");
    toggleDebugButton.onClick = [this]() {
        debugConsoleVisible = !debugConsoleVisible;
        debugConsole.setVisible(debugConsoleVisible);
        resized();
    };
    addAndMakeVisible(toggleDebugButton);
    juce::Logger::setCurrentLogger(this);
#endif

    // addChildComponent (NOT addAndMakeVisible): AccountRow starts invisible and stays that way
    // until setAccountService() attaches a real AccountService — addAndMakeVisible would force
    // it visible here and immediately override that default. Visibility from then on is
    // controlled entirely by AccountRow's own internal state, not by this component hiding it,
    // so it plays correctly with AIChatComponent's own top-level setVisible() calls from
    // MainComponent.
    addChildComponent(accountRow);

    // Same addChildComponent (not addAndMakeVisible) rationale as accountRow just above: starts
    // invisible/zero-height and stays that way until setAccountService() attaches a real
    // AccountService with a known entitlement.
    addChildComponent(planBadge);

    addAndMakeVisible(viewport);
    viewport.setScrollBarsShown(true, false);
    viewport.setViewedComponent(&messageList);

    addAndMakeVisible(inputField);
    inputField.setReturnKeyStartsNewLine(false);
    inputField.onReturnKey = [this]() { sendButtonClicked(); };
    // Belt and braces for Escape: keyPressed() catches the bubbled press, but a TextEditor with an
    // onEscapeKey handler consumes it before it can bubble, so the two must agree.
    inputField.onEscapeKey = [this]() {
        if (isWaitingForResponse)
            handleUserCancel();
    };
    inputField.addListener(this);
    inputField.setTextToShowWhenEmpty("Ask AI to create or modify a patch...", juce::Colours::grey);
    inputField.setTooltip("Type a message and press Enter or Send");

    addAndMakeVisible(sendButton);
    sendButton.setButtonText("Send");
    sendButton.onClick = [this]() { sendButtonClicked(); };
    sendButton.setTooltip("Send message to AI  (Enter)");

    // Cancel button — hidden until a request is in flight.
    cancelButton.setButtonText("Cancel");
    cancelButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    cancelButton.onClick = [this]() { handleUserCancel(); };
    cancelButton.setTooltip("Cancel the in-flight AI request");
    cancelButton.setVisible(false);
    addChildComponent(cancelButton);

    // Spinner dot — 8×8 ellipse, hidden until waiting.
    spinnerDot.setSize(8, 8);
    spinnerDot.setVisible(false);
    addChildComponent(spinnerDot);

    addAndMakeVisible(newChatButton);
    newChatButton.setButtonText("New Chat");
    newChatButton.setTooltip("Start a new conversation (clears history)");
    newChatButton.onClick = [this]() {
        aiService.clearHistory();
        messages.clear();
        updateChatDisplay();
    };

    addAndMakeVisible(modelPicker);
    modelPicker.setTooltip("Select the AI model to use");
    modelPicker.onChange = [this]() {
        juce::String model = modelPicker.getText();
        aiService.setModel(model);
        appProperties.getUserSettings()->setValue("aiModel", model);
        appProperties.getUserSettings()->saveIfNeeded();
    };

    // Starts invisible (same contract as accountRow/planBadge) — updateHostedModeNotice(), called
    // from refreshModels() below, sets its real visibility once a provider is known.
    addChildComponent(hostedModeNotice);
    hostedModeNotice.setJustificationType(juce::Justification::centredLeft);
    hostedModeNotice.setMinimumHorizontalScale(1.0f);
    hostedModeNotice.setFont(juce::Font(11.0f));
    hostedModeNotice.setText("Hosted mode sends your prompt and current patch to Agent Synth's servers.",
                             juce::dontSendNotification);
    hostedModeNotice.setTooltip("Local (Ollama) mode keeps everything on this machine. See " +
                                juce::String(synth::branding::kWebsiteUrl) + "/privacy for details.");

    refreshModels();

    // Populate history
    for (const auto& msg : aiService.getHistory()) {
        if (msg.role == "system")
            continue;

        juce::String json;
        juce::String cleanText = msg.content;
        int start = msg.content.indexOf("```json");
        if (start != -1) {
            int end = msg.content.indexOf(start + 7, "```");
            if (end != -1) {
                json = msg.content.substring(start + 7, end).trim();
                cleanText = msg.content.substring(0, start) + msg.content.substring(end + 3);
            }
        }
        // showUpgradeAction deliberately left at its default false: a replayed history turn never
        // resurrects the Upgrade button, same as Cancel-button/spinner state being session-only.
        messages.push_back({msg.role, cleanText.trim(), json});
        attachPatchPreview(messages.back());
    }

    updateChatDisplay();
}

AIChatComponent::~AIChatComponent() {
    // Clear the two callback slots this component installed in setAccountService(), so a later
    // publishSnapshot()/setAccessTokenFromWorker() call on a still-alive AccountService (e.g.
    // MainComponent destroys accountService after aiChatComponent) can't copy a callback that
    // captures this half-destroyed object. Belt and braces alongside the SafePointer guard
    // inside those lambdas, which is what actually makes a call arriving mid/after teardown safe.
    if (accountServicePtr != nullptr) {
        accountServicePtr->onStateChanged = nullptr;
        accountServicePtr->onAccessTokenChanged = nullptr;
    }

    stopTimer();
    // Stop any running pulse animation before members are destroyed.
    spinnerDot.stopPulse(vblankUpdater);
#ifndef NDEBUG
    juce::Logger::setCurrentLogger(nullptr);
#endif
}

void AIChatComponent::setAccountService(AccountService* service) {
    accountServicePtr = service;
    accountRow.setAccountService(service);
    planBadge.setAccountService(service);

    if (service == nullptr)
        return;

    // SafePointer guard: AccountService::publishSnapshot()/setAccessTokenFromWorker() copy the
    // std::function out before dispatching it via MessageManager::callAsync(), so clearing these
    // members in ~AIChatComponent() alone cannot stop a call already queued at teardown time —
    // this is what actually makes such a call safe.
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    service->onStateChanged = [safeThis] {
        if (auto* self = safeThis.getComponent()) {
            self->accountRow.refresh();
            self->planBadge.refresh();
        }
    };
    service->onAccessTokenChanged = [safeThis](juce::String token) {
        if (auto* self = safeThis.getComponent())
            self->aiService.setAuthToken(token);
    };
}

void AIChatComponent::timerCallback() {
    if (!isWaitingForResponse)
        return;

    const int elapsed = (int)(juce::Time::getMillisecondCounter() - requestStartMs);
    if (elapsed < kRequestTimeoutMs) {
        refreshWaitingStatusLabel();
        return;
    }

    // Request has timed out.
    cancelRequest();
    messages.push_back({"assistant", "Error: Request timed out after 2 minutes.", ""});
    messages.back().responseMs = elapsed;
    updateChatDisplay();
    inputField.grabKeyboardFocus();
}

void AIChatComponent::refreshWaitingStatusLabel() {
    if (waitingStatusLabel == nullptr || !isWaitingForResponse)
        return;

    const int elapsed = (int)(juce::Time::getMillisecondCounter() - requestStartMs);
    waitingStatusLabel->setText("AI is thinking... " + formatResponseTime(elapsed), juce::dontSendNotification);
}

bool AIChatComponent::keyPressed(const juce::KeyPress& key) {
    // Escape only means "cancel" while something is actually in flight; otherwise let it through
    // so it keeps whatever meaning the enclosing window gives it (closing a panel).
    if (key == juce::KeyPress::escapeKey && isWaitingForResponse) {
        handleUserCancel();
        return true;
    }

    return juce::Component::keyPressed(key);
}

void AIChatComponent::handleUserCancel() {
    const int elapsed = (int)(juce::Time::getMillisecondCounter() - requestStartMs);
    cancelRequest();
    messages.push_back({"assistant", "Cancelled.", ""});
    messages.back().responseMs = elapsed;
    updateChatDisplay();
    inputField.grabKeyboardFocus();
}

void AIChatComponent::cancelRequest() {
    // Actually abandon the request rather than just hiding the UI for it. Until this call existed
    // the HTTP request ran to completion — billed on a metered backend, and (because
    // OllamaProvider drains its queue serially) blocking the next message the user sent. Cleared
    // first so a provider that completes the cancellation synchronously and re-enters here cannot
    // cancel the same id twice.
    const auto cancelling = activeRequestId;
    activeRequestId = {};
    if (cancelling.value != 0)
        aiService.cancelRequest(cancelling);

    // Stop the live thinking-status / timeout timer.
    stopTimer();

    // Stop the pulse animation and hide the spinner.
    spinnerDot.stopPulse(vblankUpdater);
    spinnerDot.setVisible(false);

    // Hide the cancel button, restore normal input state.
    cancelButton.setVisible(false);
    sendButton.setEnabled(true);
    inputField.setReadOnly(false);
    isWaitingForResponse = false;
    waitingStatusLabel = nullptr;
}

void AIChatComponent::resized() {
    auto b = getLocalBounds().reduced(10);

    // Top row: New Chat
    auto topArea = b.removeFromTop(40);
    newChatButton.setBounds(topArea.removeFromLeft(100));

    // Account row: reserved directly above the model-picker row, inside the bottom chrome.
    // Zero height (and invisible) when no AccountService is attached, so every panel/test that
    // never calls setAccountService() sees byte-identical layout to before this feature.
    const int accountRowHeight = accountRow.getPreferredHeight();
    const int accountRowGap = accountRowHeight > 0 ? 5 : 0;

    // Plan badge: same zero-height-when-absent contract as accountRow — reserved only once an
    // AccountService is attached AND its entitlement is known (SignedOut/SigningIn/unknown all
    // collapse to 0, same as accountRow collapsing to 0 with no service).
    const int planBadgeHeight = planBadge.getPreferredHeight();
    const int planBadgeGap = planBadgeHeight > 0 ? 5 : 0;

    // Hosted-mode privacy notice: same zero-height-when-absent contract, reserved only while the
    // active provider is hosted (see updateHostedModeNotice()).
    const int hostedNoticeHeight = hostedModeNotice.isVisible() ? 18 : 0;
    const int hostedNoticeGap = hostedNoticeHeight > 0 ? 5 : 0;

    auto bottomArea = b.removeFromBottom(70 + accountRowHeight + accountRowGap + planBadgeHeight + planBadgeGap +
                                         hostedNoticeHeight + hostedNoticeGap); // Increased height for all four rows

    // Bottom row: Input + Send (+ Cancel when waiting + spinner dot)
    auto inputRow = bottomArea.removeFromBottom(40);

    // Cancel button occupies the same slot as Send — only one is visible at a time.
    // We size both identically so the layout is stable regardless of visibility.
    const auto sendCancelBounds = inputRow.removeFromRight(60);
    sendButton.setBounds(sendCancelBounds);
    cancelButton.setBounds(sendCancelBounds);

    inputRow.removeFromRight(10);

    // Spinner dot: 8×8, vertically centred on the right edge of the input area.
    const int spinnerSize = 8;
    spinnerDot.setBounds(inputRow.removeFromRight(spinnerSize).withSizeKeepingCentre(spinnerSize, spinnerSize));
    inputRow.removeFromRight(4); // gap between spinner and input field

    inputField.setBounds(inputRow);

    // Middle row (above input): Model Picker
    bottomArea.removeFromBottom(5);
    auto modelRow = bottomArea.removeFromBottom(25);
    modelPicker.setBounds(modelRow.removeFromLeft(200));
#ifndef NDEBUG
    toggleDebugButton.setBounds(modelRow.removeFromRight(60));
#endif

    if (hostedNoticeHeight > 0) {
        bottomArea.removeFromBottom(hostedNoticeGap);
        hostedModeNotice.setBounds(bottomArea.removeFromBottom(hostedNoticeHeight));
    }

    if (planBadgeHeight > 0) {
        bottomArea.removeFromBottom(planBadgeGap);
        planBadge.setBounds(bottomArea.removeFromBottom(planBadgeHeight));
    }

    if (accountRowHeight > 0) {
        bottomArea.removeFromBottom(accountRowGap);
        accountRow.setBounds(bottomArea.removeFromBottom(accountRowHeight));
    }

    b.removeFromBottom(10);

#ifndef NDEBUG
    if (debugConsoleVisible) {
        debugConsole.setBounds(b.removeFromBottom(150));
        b.removeFromBottom(5);
    }
#endif

    viewport.setBounds(b);

    // Layout message bubbles and loader
    int y = 0;
    int width = viewport.getMaximumVisibleWidth();
    for (auto* child : messageList.getChildren()) {
        int h = 0;
        if (auto* bubble = dynamic_cast<MessageBubble*>(child)) {
            h = bubble->getRequiredHeight(width);
        } else if (dynamic_cast<juce::Label*>(child)) {
            h = 24;
        }

        if (h > 0) {
            child->setBounds(0, y, width, h);
            y += h + 10;
        }
    }
    messageList.setSize(width, juce::jmax(viewport.getHeight(), y));
}

void AIChatComponent::paint(juce::Graphics& g) {
    auto lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf != nullptr) {
        g.fillAll(lf->getTheme().colors.bg0);
    } else {
        g.fillAll(juce::Colours::darkgrey.darker(0.5f));
    }
}

bool AIChatComponent::shouldUseStructuredOutput(const juce::String& text, const juce::StringArray& moduleTypeNames) {
    // Any real module/effect type name (Chorus, Distortion, Oscillator, ...) means the user is
    // almost certainly talking about the graph, even without an explicit edit verb ("what does
    // the Reverb's decay knob do?"). Deriving this from the module factory registry — rather than
    // a hand-picked handful — means a new module type is covered automatically; the old hardcoded
    // list (oscillator/filter/vca/adsr) silently missed everything else, which is what caused this
    // bug ("Add a chorus between the distortion to the delay" matched none of the four).
    for (const auto& moduleType : moduleTypeNames)
        if (text.containsIgnoreCase(moduleType))
            return true;

    // Generic edit-intent verbs/nouns that show up in a patch-authoring request regardless of
    // which module is named (or when no module is named at all, e.g. "add a filter" without
    // capitalizing on a specific type, or "increase the cutoff"). Bias toward inclusion: a false
    // positive here just spends ~1.5k tokens of unnecessary patch context; a false negative
    // reproduces the original bug (request goes out with no graph context and the model has to
    // guess or ask).
    static const char* kEditIntentWords[] = {
        "patch",   "create", "modify", "sound",    "preset",   "add",  "remove", "delete",
        "connect", "change", "set",    "increase", "decrease", "swap", "insert", "between",
    };
    for (const auto* word : kEditIntentWords)
        if (text.containsIgnoreCase(word))
            return true;

    return false;
}

void AIChatComponent::sendButtonClicked() {
    auto text = inputField.getText().trim();
    if (text.isEmpty())
        return;

    inputField.clear();

    bool useStructuredOutput = shouldUseStructuredOutput(text, AIStateMapper::moduleFactoryTypeNames());

    // Add user message to local state immediately
    messages.push_back({"user", text, ""});
    isWaitingForResponse = true;
    requestStartMs = juce::Time::getMillisecondCounter();
    updateChatDisplay();

    sendButton.setEnabled(false);
    inputField.setReadOnly(true);

    // Show cancel affordance and start the pulse spinner.
    cancelButton.setVisible(true);
    spinnerDot.setVisible(true);
    spinnerDot.startPulse(vblankUpdater);

    // Live thinking-status timer (also enforces the 120 s timeout).
    startTimer(kWaitingStatusIntervalMs);

    const auto requestId = aiService.sendMessage(
        text,
        [this, useStructuredOutput](const AIProvider::AIResponse& aiResponse) {
            juce::Component::SafePointer<AIChatComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis, aiResponse, useStructuredOutput]() {
                if (safeThis.getComponent() == nullptr)
                    return;
                auto* self = safeThis.getComponent();

                if (!self->isWaitingForResponse) {
                    return;
                } // Ignore late responses if a timeout already occurred

                // The request is finished, so there is nothing left to cancel. Clearing the handle
                // before teardown keeps cancelRequest() from asking the provider to cancel a
                // completed id.
                self->activeRequestId = {};
                self->cancelRequest(); // stops timer, spinner, cancel btn, restores input

                // The user already got their "Cancelled." bubble from handleUserCancel(); the
                // provider is just confirming. An error bubble here would contradict it.
                if (aiResponse.error.kind == AIProvider::AIErrorKind::Cancelled)
                    return;

                const int elapsed = (int)(juce::Time::getMillisecondCounter() - self->requestStartMs);

                if (aiResponse.success) {
                    const juce::String& response = aiResponse.content;
                    juce::String json;
                    juce::String cleanText = response;

                    // 1. Try to find JSON between backticks
                    juce::StringArray jsonBlocks = extractJSONBlocks(response);
                    if (!jsonBlocks.isEmpty()) {
                        json = jsonBlocks[0]; // Use the first block found
                        // Attempt to remove the JSON block from the cleanText
                        int start = response.indexOf("```json");
                        if (start != -1) {
                            int end = response.indexOf(start + 7, "```");
                            if (end != -1) {
                                cleanText = response.substring(0, start) + response.substring(end + 3);
                            }
                        }
                    } else if (useStructuredOutput) {
                        // 2. If we requested structured output, the WHOLE response should be JSON
                        // Verify if it's actually JSON
                        juce::var parsed = juce::JSON::parse(response);
                        if (!parsed.isVoid()) {
                            json = response.trim();
                            cleanText = "I've created a new patch based on your request.";
                        }
                    }

                    // The timeline half of the SAME response, extracted independently of the
                    // patch half — a model may send a patch, a timelineOps envelope, or both, and
                    // "timelineOps" is a sibling key, never nested inside the patch. Offered only
                    // when a live timeline is wired in, and under the identical posture the patch
                    // card is under: validate NOW so the user reads a checked summary, apply only
                    // when they click.
                    juce::String timelineOpsJson;
                    juce::String timelineOpsPreview;
#if SYNTH_ENABLE_TIMELINE
                    if (self->aiService.hasTimelineContext()) {
                        const juce::var envelope = AIIntegrationService::extractTimelineOps(response);
                        if (!envelope.isVoid()) {
                            const auto preview = self->aiService.previewTimelineOps(envelope);
                            if (preview.ok) {
                                timelineOpsJson = juce::JSON::toString(envelope);
                                timelineOpsPreview = preview.previewText;
                            } else {
                                // Shown, never swallowed — but with no Apply button, since there is
                                // nothing valid to apply.
                                timelineOpsPreview =
                                    "These timeline changes were rejected and were NOT applied: " + preview.message;
                            }
                        }
                    }
#endif

                    self->messages.push_back({"assistant", cleanText.trim(), json, /*isExpanded=*/false,
                                              /*showUpgradeAction=*/false, timelineOpsJson, timelineOpsPreview});
                    self->messages.back().responseMs = elapsed;
                    self->attachPatchPreview(self->messages.back());
                } else if (aiResponse.error.kind == AIProvider::AIErrorKind::Quota) {
                    // The server's message is already a complete, user-facing sentence — no
                    // "Error: " prefix, same precedent as TrialExhausted/ServiceCapacityExceeded.
                    // showUpgradeAction=true adds the Upgrade-to-Pro button (see MessageBubble).
                    self->messages.push_back({"assistant", aiResponse.error.message, "", false,
                                              /*showUpgradeAction=*/true});
                    self->messages.back().responseMs = elapsed;
                    // The user may have just paid mid-session — check again so a retry right after
                    // upgrading reflects the new plan without restarting the app.
                    if (self->accountServicePtr != nullptr)
                        self->accountServicePtr->refreshEntitlement();
                } else {
                    self->messages.push_back({"assistant", "Error: " + aiResponse.error.message, ""});
                    self->messages.back().responseMs = elapsed;
                }

                self->updateChatDisplay();
                self->inputField.grabKeyboardFocus();
            });
        },
        useStructuredOutput);

    // Only record the handle if we are still waiting. A provider that answers synchronously (test
    // doubles, or the "no provider selected" path) has already run the teardown above by now, and
    // storing the handle here would leave a stale id for the next cancel.
    if (isWaitingForResponse)
        activeRequestId = requestId;
}

void AIChatComponent::attachPatchPreview(MessageData& data) {
    if (data.jsonPatch.isEmpty())
        return;

    // Determine merge mode for this message's patch (used for button text + apply behavior, and
    // to pick the merge/replace branch computePatchPreview() diffs below).
    bool isMerge = false;
    juce::var parsed = juce::JSON::parse(data.jsonPatch);
    if (auto* obj = parsed.getDynamicObject()) {
        juce::String mode = obj->getProperty("mode").toString();
        if (mode == "merge") {
            isMerge = true;
        } else if (mode.isEmpty()) {
            // AI didn't specify mode — infer from user intent + graph state. `data` is already
            // the last element of `messages` (see this method's doc comment), so scan backward
            // from the end for the preceding user turn.
            juce::String userText;
            for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                if (it->role == "user") {
                    userText = it->text;
                    break;
                }
            }
            juce::var ctx = juce::JSON::parse(aiService.getPatchContext());
            bool graphHasNodes = false;
            if (auto* ctxObj = ctx.getDynamicObject()) {
                if (auto* nodes = ctxObj->getProperty("nodes").getArray())
                    graphHasNodes = !nodes->isEmpty();
            }
            if (graphHasNodes && userText.isNotEmpty()) {
                isMerge = userText.containsIgnoreCase("add") || userText.containsIgnoreCase("change") ||
                          userText.containsIgnoreCase("modify") || userText.containsIgnoreCase("tweak") ||
                          userText.containsIgnoreCase("adjust") || userText.containsIgnoreCase("remove") ||
                          userText.containsIgnoreCase("delete") || userText.containsIgnoreCase("make it") ||
                          userText.containsIgnoreCase("more") || userText.containsIgnoreCase("less") ||
                          userText.containsIgnoreCase("brighter") || userText.containsIgnoreCase("warmer") ||
                          userText.containsIgnoreCase("darker");
            }
        }
    }
    data.patchIsMerge = isMerge;

    // The human-readable preview — the PatchCard's default view. Computed from before/after
    // AIStateMapper::graphToJSON() snapshots (never the raw patch JSON): see PatchDiff.h for why
    // that's the only correct way to preview merge-mode auto-wiring, replace-mode deletions, and
    // value rescaling. Merge mode has stable node identity to diff against, so it gets
    // computeDiff(); replace mode does not (PatchDiff.h), so it gets summarizePatch() of the new
    // patch's contents instead — never a diff against the old graph.
    juce::var diffBefore, diffAfter;
    data.patchDiffAvailable = aiService.computePatchPreview(data.jsonPatch, isMerge, diffBefore, diffAfter);
    if (data.patchDiffAvailable) {
        if (isMerge)
            data.patchDiff = computeDiff(diffBefore, diffAfter);
        else
            data.patchSummary = summarizePatch(diffAfter);
    }
}

void AIChatComponent::updateChatDisplay() {
    waitingStatusLabel = nullptr;
    messageList.deleteAllChildren();

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& data = messages[i];
        bool isMerge = data.patchIsMerge;

        auto* bubble = new MessageBubble(
            data,
            [this, isMerge](const juce::String& json) {
                juce::Logger::writeToLog("--- Applying patch (merge=" + juce::String(isMerge ? "true" : "false") +
                                         ") ---");
                juce::Logger::writeToLog("JSON: " + json);

                // Redraws the conversation without destroying the MessageBubble whose callback we
                // may still be inside — updateChatDisplay() calls messageList.deleteAllChildren().
                juce::Component::SafePointer<AIChatComponent> safeThis(this);
                auto refreshLater = [safeThis] {
                    juce::MessageManager::callAsync([safeThis] {
                        if (auto* self = safeThis.getComponent())
                            self->updateChatDisplay();
                    });
                };

                aiService.applyPatchWithRetry(
                    json, isMerge,
                    [safeThis, refreshLater](bool success, const juce::String& error) {
                        auto* self = safeThis.getComponent();
                        if (self == nullptr)
                            return;

                        if (success) {
                            juce::Logger::writeToLog("--- Patch applied ---");
                            return;
                        }

                        // Never swallow a rejection: an Apply/Merge that does nothing and says
                        // nothing is indistinguishable from a broken button.
                        auto reason = error.isNotEmpty() ? error : self->aiService.getLastPatchError();
                        if (reason.isEmpty())
                            reason = "The patch could not be applied.";
                        juce::Logger::writeToLog("--- Patch REJECTED: " + reason + " ---");
                        self->messages.push_back({"assistant",
                                                  "Could not apply this patch after " +
                                                      juce::String(AIIntegrationService::kMaxPatchRetries + 1) +
                                                      " attempts: " + reason,
                                                  ""});
                        refreshLater();
                    },
                    // Retries are bounded and per user click, so announcing each one keeps the wait
                    // legible instead of looking like a hang.
                    [safeThis, refreshLater](const AIIntegrationService::PatchRetryInfo& info) {
                        auto* self = safeThis.getComponent();
                        if (self == nullptr)
                            return;
                        self->messages.push_back({"assistant",
                                                  "That patch was rejected (" + info.error + ") — asking for a fix (" +
                                                      juce::String(info.failedAttempt + 1) + "/" +
                                                      juce::String(info.totalAttempts) + ")…",
                                                  ""});
                        refreshLater();
                    });
            },
            isMerge, urlOpener, data.patchDiff, data.patchDiffAvailable, data.patchSummary,
            [this, i](PatchRatingUiState newRating, const juce::String& comment) {
                if (i >= messages.size())
                    return;
                auto& msg = messages[i];
                msg.ratingState = newRating;
                msg.ratingComment = comment;
                if (newRating != PatchRatingUiState::None) {
                    patchFeedbackStore.record(msg.jsonPatch,
                                              newRating == PatchRatingUiState::Up ? PatchFeedbackStore::Rating::Up
                                                                                  : PatchFeedbackStore::Rating::Down,
                                              comment);
                }
            },
            // Timeline Apply. Deliberately NOT a retry loop like the patch path's: a rejected
            // envelope never reaches this button (the card offers no button at all in that case),
            // so the only failures left here are the ones the live doc/graph moved under — worth
            // reporting, not worth re-asking the model about.
            [this](const juce::String& envelopeJson) {
#if SYNTH_ENABLE_TIMELINE
                juce::Component::SafePointer<AIChatComponent> safeThis(this);
                const auto result = aiService.applyTimelineOps(juce::JSON::parse(envelopeJson));
                if (result.ok)
                    return;

                // Same rule as a rejected patch: an Apply that does nothing and says nothing is
                // indistinguishable from a broken button.
                messages.push_back({"assistant",
                                    "Could not apply these timeline changes: " +
                                        (result.message.isNotEmpty() ? result.message : juce::String("unknown error")),
                                    ""});
                juce::MessageManager::callAsync([safeThis] {
                    if (auto* self = safeThis.getComponent())
                        self->updateChatDisplay();
                });
#else
                juce::ignoreUnused(envelopeJson);
#endif
            });
        messageList.addAndMakeVisible(bubble);
    }

    if (isWaitingForResponse) {
        waitingStatusLabel = new juce::Label();
        messageList.addAndMakeVisible(waitingStatusLabel);
        waitingStatusLabel->setColour(juce::Label::textColourId, juce::Colours::grey);
        refreshWaitingStatusLabel();
    }

    resized();
    scrollToBottom();
}

void AIChatComponent::scrollToBottom() { viewport.setViewPosition(0, messageList.getHeight()); }

juce::String AIChatComponent::formatResponseTime(int ms) {
    if (ms < 0)
        ms = 0;

    if (ms < 1000)
        return juce::String(ms) + "ms";

    if (ms < 60000) {
        const double seconds = (double)ms / 1000.0;
        return juce::String(seconds, 1) + "s";
    }

    const int totalSeconds = ms / 1000;
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return juce::String(minutes) + "m " + juce::String(seconds) + "s";
}

int AIChatComponent::getLastAssistantResponseMs() const {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == "assistant")
            return it->responseMs;
    }
    return -1;
}

juce::String AIChatComponent::getWaitingStatusText() const {
    return waitingStatusLabel != nullptr ? waitingStatusLabel->getText() : juce::String();
}

void AIChatComponent::refreshModels() {
    // Provider identity (unlike its model list) is known synchronously right after
    // setProvider() — no need to wait for the fetch below to resolve.
    updateHostedModeNotice();

    // refreshModels() is called repeatedly over the component's lifetime (once at
    // construction with no provider yet, again after MainComponent installs one, and
    // again whenever Settings triggers a re-fetch) — never just once. Without this
    // clear(), the second call's addItem(..., 1) collides with whatever already holds
    // ID 1 (a previously listed model, or the "Error fetching models" placeholder from
    // a synchronous no-provider callback) and trips ComboBox's duplicate-ID jassert.
    modelPicker.clear(juce::dontSendNotification);
    modelPicker.addItem("Loading models...", 1);
    modelPicker.setSelectedId(1, juce::dontSendNotification);
    modelPicker.setEnabled(false);

    aiService.fetchAvailableModels([this](const juce::StringArray& models, bool success) {
        modelPicker.clear(juce::dontSendNotification);
        modelPicker.setEnabled(true);

        if (success && !models.isEmpty()) {
            for (int i = 0; i < models.size(); ++i) {
                modelPicker.addItem(models[i], i + 1);
            }

            // Select the saved model, current model, or default to first available
            juce::String savedModel = appProperties.getUserSettings()->getValue("aiModel", "");
            juce::String current = aiService.getCurrentModel();

            int index = -1;
            if (savedModel.isNotEmpty() && models.contains(savedModel)) {
                index = models.indexOf(savedModel);
            } else if (current.isNotEmpty()) {
                index = models.indexOf(current);
            }

            if (index != -1) {
                modelPicker.setSelectedId(index + 1, juce::dontSendNotification);
                aiService.setModel(models[index]);
            } else {
                modelPicker.setSelectedId(1, juce::dontSendNotification);
                aiService.setModel(models[0]);
            }
        } else if (success) {
            // Empty-but-successful is not a fetch failure — RemoteProvider's fetchAvailableModels()
            // always resolves this way, because the hosted service picks its own model server-side
            // (see RemoteProvider::fetchAvailableModels()'s doc comment). Showing "Error fetching
            // models" here would be actively misleading to every hosted-mode user.
            modelPicker.addItem("Model chosen automatically", 1);
            modelPicker.setSelectedId(1, juce::dontSendNotification);
            modelPicker.setEnabled(false);
        } else {
            modelPicker.addItem("Error fetching models", 1);
            modelPicker.setSelectedId(1, juce::dontSendNotification);
        }
    });
}

void AIChatComponent::updateHostedModeNotice() {
    const bool hosted = aiService.isCurrentProviderHosted();
    if (hosted) {
        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
            hostedModeNotice.setColour(juce::Label::textColourId, lf->getTheme().colors.textMuted);
    }
    hostedModeNotice.setVisible(hosted);
    resized();
}

#ifndef NDEBUG
void AIChatComponent::appendDebugLog(const juce::String& msg) {
    {
        const juce::ScopedLock sl(logLock);
        pendingLogLines.add(msg);
    }
    bool expected = false;
    if (logFlushScheduled.compare_exchange_strong(expected, true)) {
        juce::Component::SafePointer<AIChatComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent())
                self->flushDebugLog();
        });
    }
}

void AIChatComponent::flushDebugLog() {
    logFlushScheduled.store(false);

    juce::StringArray batch;
    {
        const juce::ScopedLock sl(logLock);
        batch.swapWith(pendingLogLines);
    }

    if (batch.isEmpty())
        return;

    debugConsole.moveCaretToEnd();
    debugConsole.insertTextAtCaret(batch.joinIntoString("\n") + "\n");

    const int maxChars = 8000;
    auto txt = debugConsole.getText();
    if (txt.length() > maxChars) {
        debugConsole.setText(txt.substring(txt.length() - maxChars), juce::dontSendNotification);
        debugConsole.moveCaretToEnd();
    }
}

void AIChatComponent::logMessage(const juce::String& message) { appendDebugLog(message); }
#endif

} // namespace synth
