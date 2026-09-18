#include "AI/PatchDiff.h"
#include "AIChatComponent.h"
#include "Branding.h"
#include <thread>

namespace synth {

// Concern: message list rendering -- chat bubbles, patch/timeline cards, and the panel's
// resized() layout loop that positions them (resized() dynamic_casts to MessageBubble*, so it
// has to live alongside its full definition rather than in the general layout code).

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
    // attachPatchPreview() — see docs/ai/patch-preview.md. This IS the preview: it's
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

        // Patch-name accent: success (green) for a brand-new patch, warning (amber) for an
        // in-place update — theme tokens, not raw hex, so the label stays readable against
        // BOTH a dark and a light bubble background (the raw lightgreen/lightyellow this
        // replaced went unreadably low-contrast on a light theme's grey bubble fill). Falls back
        // to the same literals only if no AppLookAndFeel is attached yet (e.g. constructed before
        // this component's owner is parented into a themed window).
        using synth::theme::AppLookAndFeel;
        auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accentColour =
            lf != nullptr ? (isMerge ? lf->getTheme().colors.warning : lf->getTheme().colors.success)
                          : (isMerge ? juce::Colours::lightyellow : juce::Colours::lightgreen);

        addAndMakeVisible(headerLabel);
        headerLabel.setText(isMerge ? "Patch Update" : "New Patch", juce::dontSendNotification);
        headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        headerLabel.setColour(juce::Label::textColourId, accentColour);

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
            diffDisplay.setText("Preview unavailable - this patch may be rejected when applied.");
        } else if (isMerge) {
            // Grouped by Kind (adds, then removes, then param changes, then connection
            // adds/removes, then modulation adds/removes) so the list doesn't interleave — see
            // groupChangesByKind()'s doc comment. Rendered line-by-line via insertTextAtCaret with
            // the TextEditor's textColourId set per segment (setText() can't colour per-line; this
            // is the same pattern flushDebugLog() uses for insertTextAtCaret, minus the colouring).
            auto grouped = groupChangesByKind(changes);
            if (grouped.empty()) {
                diffDisplay.setText("No changes.");
            } else {
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
            diffDisplay.setText(lines.joinIntoString("\n"));
        }
        // Captured AFTER diffDisplay is populated (getText() ignores the per-segment colouring
        // above, which is fine — this is only ever used to MEASURE wrapped height, not to
        // re-render). diffAreaHeight() measures this instead of a diffLineCount*rowHeight
        // estimate: a single long status/preview line (e.g. "Preview unavailable...") WRAPS
        // inside diffDisplay's fixed width, and a line-count estimate doesn't know that and
        // clips it.
        diffText = diffDisplay.getText();

        addAndMakeVisible(jsonDisplay);
        jsonDisplay.setMultiLine(true);
        jsonDisplay.setReadOnly(true);
        jsonDisplay.setText(prettyPrintJson(patchJson));
        jsonDisplay.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha(0.3f));
        jsonDisplay.setVisible(false);
    }

    void resized() override {
        auto b = getLocalBounds().reduced(kCardPadding);

        headerLabel.setBounds(b.removeFromTop(kHeaderLabelHeight));
        b.removeFromTop(kRowGap);

        // View JSON / Apply get their OWN row rather than sharing the header label's row: at a
        // narrow bubble width (see AIChatComponent's ~80%-width bubble gutter) squeezing both
        // buttons alongside the label left "Hide JSON" less than its own text width to render in,
        // so it showed as "View J...". A full-width row gives each button room regardless of how
        // narrow the bubble is.
        auto buttonRow = b.removeFromTop(kButtonRowHeight);
        expandButton.setBounds(buttonRow.removeFromLeft(kExpandButtonWidth).reduced(2));
        buttonRow.removeFromLeft(kRowGap);
        applyButton.setBounds(buttonRow.removeFromRight(kApplyButtonWidth).reduced(2));
        b.removeFromTop(kRowGap);

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
            b.removeFromTop(kRowGap);
            auto commentRow = b.removeFromTop(kFeedbackRowHeight);
            commentSaveButton.setBounds(commentRow.removeFromRight(55).reduced(2));
            commentField.setBounds(commentRow.reduced(2));
        }
        b.removeFromTop(kRowGap);

        if (isExpanded) {
            diffDisplay.setBounds(b.removeFromTop(diffAreaHeight(b.getWidth())));
            b.removeFromTop(kRowGap);
            jsonDisplay.setVisible(true);
            jsonDisplay.setBounds(b);
        } else {
            diffDisplay.setBounds(b);
            jsonDisplay.setVisible(false);
        }
    }

    // `width` must be the width this card will actually be laid out at (MessageBubble passes the
    // same contentWidth it uses for its own text measurement) — diffAreaHeight() below measures
    // the diff/status text's WRAPPED height at that width, so this and resized() must agree on it
    // or the reserved height drifts from what actually renders.
    int getRequiredHeight(int width) const {
        bool showComment = currentRating != AIChatComponent::PatchRatingUiState::None;
        int height = kCardPadding * 2 + kHeaderLabelHeight + kRowGap + kButtonRowHeight + kRowGap + kFeedbackRowHeight +
                     (showComment ? kRowGap + kFeedbackRowHeight : 0) + kRowGap +
                     diffAreaHeight(width - kCardPadding * 2);
        if (isExpanded)
            height += kRowGap + kRawJsonHeight;
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
        using synth::theme::AppLookAndFeel;
        auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour neutral = lf != nullptr ? lf->getTheme().colors.surfaceHi : juce::Colours::darkgrey;
        const juce::Colour upSelected = lf != nullptr ? lf->getTheme().colors.success : juce::Colours::darkgreen;
        const juce::Colour downSelected = lf != nullptr ? lf->getTheme().colors.error : juce::Colour(0xFF8B3A3A);
        thumbsUpButton.setColour(juce::TextButton::buttonColourId,
                                 currentRating == AIChatComponent::PatchRatingUiState::Up ? upSelected : neutral);
        thumbsDownButton.setColour(juce::TextButton::buttonColourId,
                                   currentRating == AIChatComponent::PatchRatingUiState::Down ? downSelected : neutral);
    }

private:
    // 8px-grid spacing/padding used throughout this card's layout.
    static constexpr int kCardPadding = 8;
    static constexpr int kRowGap = 8;
    static constexpr int kHeaderLabelHeight = 20;
    static constexpr int kButtonRowHeight = 28;
    // Wide enough for "Hide JSON" (the longer of the toggle's two labels) with real breathing
    // room, at this card's bold-ish default button font — the fixed-180px-shared-with-header-label
    // math this replaced could squeeze this below its own text width on a narrow bubble.
    static constexpr int kExpandButtonWidth = 96;
    static constexpr int kApplyButtonWidth = 88;
    static constexpr int kMinDiffHeight = 24;
    // Caps how tall a very long diff can grow the card; the "View JSON" toggle (which also shows
    // the diff area above the raw JSON, both individually scrollable TextEditors) is the escape
    // hatch rather than letting the message list grow unbounded.
    static constexpr int kMaxDiffHeight = 220;
    // Grew from 160: pretty-printed (indented) JSON runs noticeably taller than the single
    // unbroken line this used to hold.
    static constexpr int kRawJsonHeight = 240;
    static constexpr int kFeedbackRowHeight = 24;
    // diffDisplay's own left/right internal margins (juce::TextEditor reserves a small inset
    // beyond whatever it's given via setBounds) — subtracted before measuring wrapped height so
    // the estimate is never narrower than what the TextEditor actually renders into.
    static constexpr int kDiffTextInset = 12;

    // Measures diffText's ACTUAL wrapped height at `width` (see AIChatComponent::
    // computeWrappedTextHeight()) rather than a line-count*rowHeight estimate — a long single
    // logical line (the "Preview unavailable..." status message) wraps inside diffDisplay's
    // fixed width, and an estimate that only counts logical lines doesn't see that and clips it.
    int diffAreaHeight(int width) const {
        const int measured = AIChatComponent::computeWrappedTextHeight(diffDisplay.getFont(), diffText,
                                                                       juce::jmax(20, width - kDiffTextInset));
        return juce::jlimit(kMinDiffHeight, kMaxDiffHeight, measured + 8);
    }

    std::function<void(AIChatComponent::PatchRatingUiState, const juce::String&)> onRate;
    AIChatComponent::PatchRatingUiState currentRating = AIChatComponent::PatchRatingUiState::None;

    juce::String patchJson;
    std::function<void()> onApply;
    bool isExpanded = false;
    // The plain text diffDisplay holds, captured once at construction — see its assignment site's
    // doc comment. Used only to measure diffAreaHeight(); never re-rendered from this.
    juce::String diffText;

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

        // Theme tokens, not raw hex — the previous juce::Colours::white.withAlpha(0.8f) preview
        // text was near-invisible on a light theme's light bubble fill, the same class of bug
        // PatchCard's headerLabel had (see its constructor's doc comment).
        using synth::theme::AppLookAndFeel;
        auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour headerColour = lf != nullptr ? lf->getTheme().colors.accent2 : juce::Colours::lightskyblue;
        const juce::Colour previewColour =
            lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white.withAlpha(0.8f);

        addAndMakeVisible(headerLabel);
        headerLabel.setText("Timeline Changes", juce::dontSendNotification);
        headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        headerLabel.setColour(juce::Label::textColourId, headerColour);

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
        previewLabel.setColour(juce::Label::textColourId, previewColour);
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

    // AIChatComponent::resized() reads this to decide which side of the message list gets the
    // gutter (user bubbles hug the right edge, assistant bubbles the left) — see its layout loop.
    bool isUserRole() const { return role == "user"; }

    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().reduced(2).toFloat();
        bool isUser = (role == "user");

        using synth::theme::AppLookAndFeel;
        auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
        // Theme tokens instead of raw blue/darkgrey: the assistant bubble in particular used to be
        // a flat literal grey regardless of theme, which read as a low-contrast block on a light
        // theme. accent tints the user bubble, surfaceHi (the "raised surface" token) tints the
        // assistant one, both still faded through the same alpha gradient as before.
        const juce::Colour baseColour = lf != nullptr
                                            ? (isUser ? lf->getTheme().colors.accent : lf->getTheme().colors.surfaceHi)
                                            : (isUser ? juce::Colours::blue : juce::Colours::darkgrey.brighter(0.2f));
        const juce::Colour borderColour =
            lf != nullptr ? lf->getTheme().colors.border : juce::Colours::white.withAlpha(0.15f);
        const juce::Colour roleColour = lf != nullptr
                                            ? (isUser ? lf->getTheme().colors.accent : lf->getTheme().colors.textMuted)
                                            : (isUser ? juce::Colours::lightblue : juce::Colours::grey);
        const juce::Colour timestampColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::grey;

        juce::ColourGradient grad(baseColour.withAlpha(0.3f), b.getX(), b.getY(), baseColour.withAlpha(0.1f),
                                  b.getRight(), b.getBottom(), false);

        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, 10.0f);

        g.setColour(borderColour);
        g.drawRoundedRectangle(b, 10.0f, 1.0f);

        // Role indicator (+ optional elapsed-wait marker on assistant bubbles). Reserved within
        // the SAME outer padding + role-band height resized() clears for textLabel below, so the
        // role text and the message text never overlap.
        auto content = getLocalBounds().reduced(kOuterPadding);
        auto roleBand = content.removeFromTop(kRoleBandHeight).toFloat();
        g.setColour(roleColour);
        g.setFont(juce::Font(10.0f, juce::Font::italic));
        g.drawText(isUser ? "YOU" : "AI", roleBand, juce::Justification::centredLeft);

        if (!isUser && responseMs >= 0) {
            g.setColour(timestampColour);
            g.drawText(AIChatComponent::formatResponseTime(responseMs), roleBand, juce::Justification::centredRight);
        }
    }

    void resized() override {
        auto b = getLocalBounds().reduced(kOuterPadding);

        // Headroom for the role label/timestamp paint() draws above — see its matching
        // getLocalBounds().reduced(kOuterPadding) + removeFromTop(kRoleBandHeight). Without this,
        // textLabel started at the same y the role band paints into and clipped/overlapped it.
        b.removeFromTop(kRoleBandHeight + kRoleContentGap);

        if (patchCard) {
            patchCard->setBounds(b.removeFromBottom(patchCard->getRequiredHeight(b.getWidth())));
            b.removeFromBottom(kRoleContentGap);
        }

        if (timelineCard) {
            timelineCard->setBounds(b.removeFromBottom(timelineCard->getRequiredHeight(b.getWidth())));
            b.removeFromBottom(kRoleContentGap);
        }

        if (upgradeButton) {
            upgradeButton->setBounds(b.removeFromBottom(kUpgradeButtonHeight));
            b.removeFromBottom(kRoleContentGap);
        }

        textLabel.setBounds(b);
    }

    int getRequiredHeight(int width) {
        int contentWidth = width - kOuterPadding * 2;
        juce::Font font = textLabel.getFont();

        juce::GlyphArrangement ga;
        ga.addJustifiedText(font, text, 0.0f, 0.0f, (float)contentWidth, juce::Justification::left);

        int textHeight = (int)ga.getBoundingBox(0, -1, true).getHeight();
        // Outer padding (top+bottom) + the role band + the gap below it, on top of the wrapped
        // message text — see resized()'s matching reservation.
        int height = textHeight + kOuterPadding * 2 + kRoleBandHeight + kRoleContentGap;

        if (patchCard) {
            height += kRoleContentGap + patchCard->getRequiredHeight(contentWidth);
        }

        if (timelineCard) {
            height += kRoleContentGap + timelineCard->getRequiredHeight(contentWidth);
        }

        if (upgradeButton) {
            height += kRoleContentGap + kUpgradeButtonHeight;
        }

        return juce::jmax(40, height);
    }

private:
    static constexpr int kUpgradeButtonHeight = 28;
    // 8px-grid outer padding (replaces the old ad hoc reduced(10)/reduced(2) mismatch between
    // paint() and resized() that let the role label overlap the message text).
    static constexpr int kOuterPadding = 8;
    static constexpr int kRoleBandHeight = 16;
    static constexpr int kRoleContentGap = 8;

    juce::String role;
    juce::String text;
    int responseMs = -1;
    juce::Label textLabel;
    std::unique_ptr<PatchCard> patchCard;
    std::unique_ptr<TimelineCard> timelineCard;
    std::unique_ptr<juce::TextButton> upgradeButton;
};

void AIChatComponent::resized() {
    auto b = getLocalBounds().reduced(10);

    // 8px-grid gap used for every row boundary in the bottom-chrome stack below, so accountRow /
    // planBadge / upsellButton / the notice strips / the model row all sit a consistent distance
    // apart instead of the ad hoc mix of 4/5px gaps this used to be.
    constexpr int kChromeGap = 8;
    // Width every full-row chrome element below renders at — captured once, before any height
    // (only) slicing, since Rectangle::removeFromTop/removeFromBottom never change the width.
    // Needed up front so hostedModeNotice/downgradeStripLabel can measure their OWN (dynamically
    // set, possibly multi-line) text at the width they'll actually render into before reserving
    // height for it.
    const int chromeWidth = b.getWidth();

    // Top row: New Chat, History
    auto topArea = b.removeFromTop(40);
    newChatButton.setBounds(topArea.removeFromLeft(100));
    topArea.removeFromLeft(kChromeGap);
    historyButton.setBounds(topArea.removeFromLeft(80));

    // Account row: reserved directly above the model-picker row, inside the bottom chrome.
    // Zero height (and invisible) when no AccountService is attached, so every panel/test that
    // never calls setAccountService() sees byte-identical layout to before this feature.
    const int accountRowHeight = accountRow.getPreferredHeight();
    const int accountRowGap = accountRowHeight > 0 ? kChromeGap : 0;

    // Plan badge: same zero-height-when-absent contract as accountRow — reserved only once an
    // AccountService is attached AND its entitlement is known (SignedOut/SigningIn/unknown all
    // collapse to 0, same as accountRow collapsing to 0 with no service).
    const int planBadgeHeight = planBadge.getPreferredHeight();
    const int planBadgeGap = planBadgeHeight > 0 ? kChromeGap : 0;

    // Hosted-mode privacy notice: same zero-height-when-absent contract, reserved only while the
    // active provider is hosted (see updateHostedModeNotice()). Height is MEASURED, not a fixed
    // one-line guess — its text is a full sentence that can wrap at this panel's width, and a
    // fixed height silently truncated it (drawFittedText() derives how many lines it's allowed to
    // wrap across from the label's own height — see AppLookAndFeel::drawLabel()).
    const int hostedNoticeHeight =
        hostedModeNotice.isVisible()
            ? computeWrappedTextHeight(hostedModeNotice.getFont(), hostedModeNotice.getText(), chromeWidth)
            : 0;
    const int hostedNoticeGap = hostedNoticeHeight > 0 ? kChromeGap : 0;

    // P6-8 downgrade notice: same zero-height-when-absent contract, reserved only once
    // updateDowngradeStrip() has something true to say (see its doc comment). Same measured-not-
    // guessed height as hostedModeNotice just above — this one's text also embeds a variable-
    // length date, so a fixed height clipped it whenever the rendered sentence wrapped.
    const int downgradeStripHeight =
        downgradeStripLabel.isVisible()
            ? computeWrappedTextHeight(downgradeStripLabel.getFont(), downgradeStripLabel.getText(), chromeWidth)
            : 0;
    const int downgradeStripGap = downgradeStripHeight > 0 ? kChromeGap : 0;

    // P6-8 upsell strip: just the "Upgrade to Pro" button now (its explanatory text moved to
    // historyButton's tooltip — see the member doc comment), so it only needs a single comfortable
    // click-target row, not the taller label+button row this used to be. Same zero-height-when-absent
    // contract, but starts VISIBLE by default (see the member doc comment) — most callers (including
    // every existing test that never attaches an AccountService) will therefore reserve this space,
    // unlike the other three rows in this stack.
    const int upsellStripHeight = upsellButton.isVisible() ? 32 : 0;
    const int upsellStripGap = upsellStripHeight > 0 ? kChromeGap : 0;

    // Input row (40) + its gap + the model row (24), all on the 8px grid.
    constexpr int kInputRowHeight = 40;
    constexpr int kModelRowHeight = 24;
    auto bottomArea =
        b.removeFromBottom(kInputRowHeight + kChromeGap + kModelRowHeight + accountRowHeight + accountRowGap +
                           planBadgeHeight + planBadgeGap + hostedNoticeHeight + hostedNoticeGap +
                           downgradeStripHeight + downgradeStripGap + upsellStripHeight + upsellStripGap);

    // Bottom row: Input + Send (+ Cancel when waiting + spinner dot)
    auto inputRow = bottomArea.removeFromBottom(kInputRowHeight);

    // Cancel button occupies the same slot as Send — only one is visible at a time.
    // We size both identically so the layout is stable regardless of visibility.
    const auto sendCancelBounds = inputRow.removeFromRight(60);
    sendButton.setBounds(sendCancelBounds);
    cancelButton.setBounds(sendCancelBounds);

    inputRow.removeFromRight(kChromeGap);

    // Spinner dot: 8×8, vertically centred on the right edge of the input area.
    const int spinnerSize = 8;
    spinnerDot.setBounds(inputRow.removeFromRight(spinnerSize).withSizeKeepingCentre(spinnerSize, spinnerSize));
    inputRow.removeFromRight(4); // gap between spinner and input field

    inputField.setBounds(inputRow);

    // Middle row (above input): Model Picker (+ Patch/Arrange selector while its gates hold)
    bottomArea.removeFromBottom(kChromeGap);
    auto modelRow = bottomArea.removeFromBottom(kModelRowHeight);
    modelPicker.setBounds(modelRow.removeFromLeft(200));
    if (modeSelector.isVisible()) {
        modelRow.removeFromLeft(kChromeGap);
        modeSelector.setBounds(modelRow.removeFromLeft(110));
    }
#ifndef NDEBUG
    toggleDebugButton.setBounds(modelRow.removeFromRight(60));
#endif

    if (hostedNoticeHeight > 0) {
        bottomArea.removeFromBottom(hostedNoticeGap);
        hostedModeNotice.setBounds(bottomArea.removeFromBottom(hostedNoticeHeight));
    }

    if (downgradeStripHeight > 0) {
        bottomArea.removeFromBottom(downgradeStripGap);
        downgradeStripLabel.setBounds(bottomArea.removeFromBottom(downgradeStripHeight));
    }

    if (upsellStripHeight > 0) {
        bottomArea.removeFromBottom(upsellStripGap);
        auto upsellArea = bottomArea.removeFromBottom(upsellStripHeight);
        upsellButton.setBounds(upsellArea.removeFromRight(110).reduced(0, 2));
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

    // Layout message bubbles and loader.
    int y = 0;
    const int listWidth = viewport.getMaximumVisibleWidth();
    // Each bubble gets a max width of ~80% of the list, with the rest left as a gutter on the
    // OPPOSITE side from its role — user bubbles hug the right edge, assistant bubbles the left —
    // so a conversation reads as two columns instead of every bubble spanning edge-to-edge with no
    // visual sense of who's speaking.
    constexpr float kBubbleWidthFraction = 0.8f;
    const int bubbleWidth = juce::jmin(listWidth, juce::jmax(160, (int)((float)listWidth * kBubbleWidthFraction)));
    for (auto* child : messageList.getChildren()) {
        int h = 0;
        int w = listWidth;
        int x = 0;
        if (auto* bubble = dynamic_cast<MessageBubble*>(child)) {
            w = bubbleWidth;
            x = bubble->isUserRole() ? listWidth - w : 0;
            h = bubble->getRequiredHeight(w);
        } else if (dynamic_cast<juce::Label*>(child)) {
            h = 24;
        }

        if (h > 0) {
            child->setBounds(x, y, w, h);
            y += h + 10;
        }
    }
    messageList.setSize(listWidth, juce::jmax(viewport.getHeight(), y));
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
                                                  "That patch was rejected (" + info.error + ") - asking for a fix (" +
                                                      juce::String(info.failedAttempt + 1) + "/" +
                                                      juce::String(info.totalAttempts) + ")...",
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
                    // The SERVER conversation id (aiService.getConversationId()), not
                    // currentLocalConversationId — that's this component's own local-history key,
                    // a different identifier the server's ownership check would just 404 on.
                    const juce::String serverConversationId = aiService.getConversationId();
                    const auto storeRating = newRating == PatchRatingUiState::Up ? PatchFeedbackStore::Rating::Up
                                                                                 : PatchFeedbackStore::Rating::Down;

                    // Local log: unconditional fallback, regardless of plan/sync outcome.
                    patchFeedbackStore.record(msg.jsonPatch, storeRating, comment, serverConversationId,
                                              msg.serverMessageId);

                    // P6-9: additionally sync to the server, fire-and-forget, ONLY when this
                    // turn's assistant message has a server-assigned id (Pro + persistence
                    // succeeded when the message was created — see MessageData::serverMessageId)
                    // AND the account is still signed-in Pro right now AND a usable access token
                    // is available. No retry/queueing/error surface by design.
                    if (!msg.serverMessageId.isEmpty() && serverConversationId.isNotEmpty()) {
                        const AccountSnapshot snapshot =
                            accountServicePtr != nullptr ? accountServicePtr->getSnapshot() : AccountSnapshot{};
                        const bool signedIn = accountServicePtr != nullptr && snapshot.state == AccountState::SignedIn;
                        const bool pro = isProPlan(snapshot);
                        const juce::String accessToken =
                            signedIn ? accountServicePtr->getAccessToken() : juce::String();

                        if (signedIn && pro && accessToken.isNotEmpty()) {
                            const juce::String ratingStr = newRating == PatchRatingUiState::Up ? "up" : "down";

                            // Detached background thread, mirrors CloudHistorySource: capture
                            // COPIES only (a small, copyable, stateless AuthClient plus plain
                            // strings), never `this` or any UI state — the thread owns everything
                            // it touches and outlives this callback with no dangling-reference
                            // risk.
                            synth::AuthClient client =
                                testFeedbackHttpPerformer
                                    ? synth::AuthClient(synth::branding::kApiBaseUrl, "synth-desktop",
                                                        testFeedbackHttpPerformer)
                                    : synth::AuthClient(synth::branding::kApiBaseUrl);
                            std::thread([client, accessToken, serverConversationId, messageId = msg.serverMessageId,
                                         ratingStr, comment]() {
                                std::atomic<bool> cancelled{false};
                                client.submitMessageFeedback(accessToken, serverConversationId, messageId, ratingStr,
                                                             comment, cancelled);
                            }).detach();
                        }
                    }
                }
            },
            // Timeline Apply. Deliberately NOT a retry loop like the patch path's: a rejected
            // envelope never reaches this button (the card offers no button at all in that case),
            // so the only failures left here are the ones the live doc/graph moved under — worth
            // reporting, not worth re-asking the model about.
            [this](const juce::String& envelopeJson) {
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

} // namespace synth
