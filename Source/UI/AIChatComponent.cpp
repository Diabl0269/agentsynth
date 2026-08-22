#include "AIChatComponent.h"
#include "../AI/PatchDiff.h"
#include "../Branding.h"
#include <cmath>
#include <thread>

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

// P6-8: date-only rendering ("18 Aug 2026") for the downgrade strip and history popup rows. Falls
// back to the raw ISO string on parse failure rather than showing nothing — an unreadable-but-
// present date is more useful than a blank one.
juce::String formatReadableDate(const juce::String& iso) {
    if (iso.isEmpty())
        return {};
    auto t = juce::Time::fromISO8601(iso);
    if (t == juce::Time())
        return iso;
    return t.toString(true, false);
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
        // A fresh conversation gets a fresh local-history id — the outgoing conversation's file is
        // left alone (its own save() already captured everything up to this point).
        currentLocalConversationId.clear();
        currentLocalConversationCreatedAt.clear();
        // Also clear the CLOUD conversation id (mirrors the same clear sendButtonClicked() does on
        // a Pro-to-Free downgrade): without this, a Pro user's next message after New Chat would
        // still carry the OLD x-conversation-id, so the server would append the "new" chat's turns
        // onto the previous cloud thread while a separate fresh file starts locally — local and
        // cloud silently diverging. The server mints a fresh id on the next response either way.
        aiService.setConversationId({});
        updateChatDisplay();
    };

    // P6-8: opens the history list/restore/clear popup — see historyButtonClicked(). Tooltip is
    // set by updateUpsellStrip() below (it varies by plan, so setting a static default here would
    // just be overwritten), not here.
    addAndMakeVisible(historyButton);
    historyButton.setButtonText("History");
    historyButton.onClick = [this]() { historyButtonClicked(); };

    addAndMakeVisible(modelPicker);
    modelPicker.setTooltip("Select the AI model to use");
    modelPicker.onChange = [this]() {
        juce::String model = modelPicker.getText();
        aiService.setModel(model);
        appProperties.getUserSettings()->setValue("aiModel", model);
        appProperties.getUserSettings()->saveIfNeeded();
    };

    // Patch/Arrange mode selector — an EXPLICIT routing control (never a keyword heuristic:
    // shouldUseStructuredOutput() stays a patch-path concern), provider-agnostic per the
    // local/remote parity rule. Starts invisible; visible only while the timeline feature is
    // active — see refreshModeControls(), called from refreshModels() below and re-called by
    // MainComponent when the timeline preference toggles. Selection is session-scoped,
    // defaulting to Patch.
    addChildComponent(modeSelector);
    modeSelector.addItem("Patch", kModeSelectorPatchId);
    modeSelector.addItem("Arrange", kModeSelectorArrangeId);
    modeSelector.setSelectedId(kModeSelectorPatchId, juce::dontSendNotification);
    modeSelector.setTooltip("Patch: create or modify the synth patch. "
                            "Arrange: add tracks, notes and automation on the timeline.");

    // Starts invisible (same contract as accountRow/planBadge) — updateHostedModeNotice(), called
    // from refreshModels() below, sets its real visibility once a provider is known.
    addChildComponent(hostedModeNotice);
    hostedModeNotice.setJustificationType(juce::Justification::centredLeft);
    hostedModeNotice.setMinimumHorizontalScale(1.0f);
    hostedModeNotice.setFont(juce::Font(11.0f));
    hostedModeNotice.setText("Hosted mode sends your prompt and current patch to Agent Synth's servers.",
                             juce::dontSendNotification);
    // Tooltip repeats the (possibly ellipsis-truncated) label text in full rather than describing
    // the other mode — hovering a cut-off label should always reveal what it already started
    // saying, never switch topic to something else.
    hostedModeNotice.setTooltip(
        "Hosted mode sends your prompt and current patch to Agent Synth's servers for processing. See " +
        juce::String(synth::branding::kWebsiteUrl) + "/privacy for details.");

    // P6-8 upsell strip. Starts visible (see the member doc comment for why this diverges from
    // accountRow/planBadge/hostedModeNotice's invisible-until-known default) — updateUpsellStrip()
    // below sets its real state, and historyButton's tooltip, from whatever AccountSnapshot is
    // available at this point (none, at construction).
    addChildComponent(upsellButton);
    upsellButton.setButtonText("Upgrade to Pro");
    upsellButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF6B4FBB));
    upsellButton.onClick = [this] { urlOpener(juce::URL(synth::branding::kUpgradeUrl)); };

    // P6-8 downgrade notice — invisible until historyButtonClicked() learns a real grace-period
    // deletion date (see lastDeletionScheduledAt's doc comment); never shown speculatively.
    addChildComponent(downgradeStripLabel);
    downgradeStripLabel.setJustificationType(juce::Justification::centredLeft);
    downgradeStripLabel.setMinimumHorizontalScale(1.0f);
    downgradeStripLabel.setFont(juce::Font(11.0f));

    // Restore the persisted request timeout (falls back to kDefaultRequestTimeoutMs when unset)
    // and push it into aiService immediately, so the active provider is in sync from app startup
    // rather than only once Settings is opened — see AIProvider::setRequestTimeoutMs()'s and
    // AIIntegrationService::setProvider()'s doc comments for why a value has to be pushed even
    // when nothing has changed it yet.
    // getUserSettings() can be null here: a composing owner (MainComponent) constructs this as a
    // member before calling appProperties.setStorageParameters() in its own constructor body (see
    // that class's ORDERING CONTRACT comment on aiChatComponent's declaration) — in that case this
    // falls back to the default, and MainComponent re-reads the real value once its properties file
    // is actually open.
    if (auto* settings = appProperties.getUserSettings())
        requestTimeoutMs = settings->getIntValue("aiRequestTimeoutMs", kDefaultRequestTimeoutMs);
    aiService.setRequestTimeoutMs(requestTimeoutMs);

    refreshModels();
    updateUpsellStrip();

    // Populate history from aiService's own in-memory record (its lifetime spans app restarts
    // within the same session but not across them — see replayMessagesFrom()'s doc comment for why
    // this is shared with restoreConversation()'s history-panel replay).
    {
        std::vector<std::pair<juce::String, juce::String>> pairs;
        for (const auto& msg : aiService.getHistory())
            pairs.push_back({msg.role, msg.content});
        replayMessagesFrom(pairs);
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
    updateUpsellStrip();
    updateDowngradeStrip();

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
            self->updateUpsellStrip();
            // NOT self->updateDowngradeStrip() here — the date it renders is only ever learned
            // from an explicit History-button click (see lastDeletionScheduledAt's doc comment),
            // so a plan/sign-in change alone must not resurrect a stale one. updateDowngradeStrip()
            // is still worth calling: if the account just signed out or went Pro again, its own
            // gate (signedIn && !pro) already hides the strip even with a stale cached date.
            self->updateDowngradeStrip();
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
    if (elapsed < requestTimeoutMs) {
        refreshWaitingStatusLabel();
        return;
    }

    // Request has timed out.
    cancelRequest();
    messages.push_back(
        {"assistant", "Error: Request timed out after " + juce::String(requestTimeoutMs / 60000) + " minutes.", ""});
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

    // EXPLICIT routing, decided by the user's mode selector alone — arrange mode is never
    // inferred from the message text. (arrangeModeActive() is always false when the selector is
    // hidden, including in a SYNTH_ENABLE_TIMELINE=OFF build.) In arrange mode the patch-path
    // keyword heuristic is bypassed entirely: the request carries no patch schema and the
    // response is a timelineOps envelope, not a patch.
    const bool arrangeMode = arrangeModeActive();

    bool useStructuredOutput = !arrangeMode && shouldUseStructuredOutput(text, AIStateMapper::moduleFactoryTypeNames());

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

    // Conversation-id persistence is Pro-only (server-enforced; see RemoteProvider's
    // x-conversation-id header). AIIntegrationService::sendMessage() auto-captures/re-pushes a
    // response id on its own for the common case (a free-plan response never carries one, so
    // there's nothing to gate there) — this only handles the Pro-to-Free downgrade mid-session,
    // where a stale id from an earlier Pro response would otherwise still be sitting in
    // AIIntegrationService and get resent to a now-free account. Clearing it here means
    // RemoteProvider naturally has nothing to send; it never has plan awareness of its own.
    if (accountServicePtr == nullptr || !isProPlan(accountServicePtr->getSnapshot()))
        aiService.setConversationId({});

    // Conversation-id persistence is Pro-only (server-enforced; see RemoteProvider's
    // x-conversation-id header). AIIntegrationService::sendMessage() auto-captures/re-pushes a
    // response id on its own for the common case (a free-plan response never carries one, so
    // there's nothing to gate there) — this only handles the Pro-to-Free downgrade mid-session,
    // where a stale id from an earlier Pro response would otherwise still be sitting in
    // AIIntegrationService and get resent to a now-free account. Clearing it here means
    // RemoteProvider naturally has nothing to send; it never has plan awareness of its own.
    if (accountServicePtr == nullptr || !isProPlan(accountServicePtr->getSnapshot()))
        aiService.setConversationId({});

    AIProvider::CompletionCallback completion = [this, useStructuredOutput,
                                                 arrangeMode](const AIProvider::AIResponse& aiResponse) {
        juce::Component::SafePointer<AIChatComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, aiResponse, useStructuredOutput, arrangeMode]() {
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

                // Arrange mode's whole response body IS the envelope JSON (timeline.generate's
                // output schema) — the timeline card below is the real rendering, so the
                // bubble carries a sentence rather than raw JSON. A rejected envelope still
                // reads as a rejection: the card shows the validator's message. If no envelope
                // was found at all, cleanText keeps the raw body — visible is debuggable,
                // the same never-swallow rule the card itself follows.
                if (arrangeMode) {
                    if (timelineOpsJson.isNotEmpty())
                        cleanText = "Here are the timeline changes I suggest.";
                    else if (timelineOpsPreview.isNotEmpty())
                        cleanText = "I couldn't produce valid timeline changes for that request.";
                }

                self->messages.push_back({"assistant", cleanText.trim(), json, /*isExpanded=*/false,
                                          /*showUpgradeAction=*/false, timelineOpsJson, timelineOpsPreview});
                self->messages.back().responseMs = elapsed;
                // P6-9: only present on a Pro-plan hosted response whose persistence
                // succeeded (see AIResponse::messageId's doc comment) — empty for every other
                // case (local Ollama, free plan, no provider), which is exactly what keeps the
                // later rating-sync check a no-op for those.
                self->messages.back().serverMessageId = aiResponse.messageId;
                self->attachPatchPreview(self->messages.back());

                // P6-8: local-first — every session writes here regardless of plan, right after
                // the assistant turn lands and `messages` reflects the full exchange. Not in
                // AIIntegrationService::sendMessage()'s own callback: that one can run on a
                // provider worker thread and only has aiService.chatHistory (unsplit
                // text+patch), not this component's own text/jsonPatch-split `messages`.
                self->saveCurrentConversationLocally();
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
    };

#if SYNTH_ENABLE_TIMELINE
    // Arrange mode routes to the hosted timeline.generate capability; everything downstream of
    // the response (extraction, preview card, user-gated Apply) is the same seam either way.
    const auto requestId = arrangeMode ? aiService.sendArrangeMessage(text, std::move(completion))
                                       : aiService.sendMessage(text, std::move(completion), useStructuredOutput);
#else
    const auto requestId = aiService.sendMessage(text, std::move(completion), useStructuredOutput);
#endif

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

int AIChatComponent::computeWrappedTextHeight(const juce::Font& font, const juce::String& text, int width) {
    if (text.isEmpty())
        return (int)std::ceil(font.getHeight());

    juce::GlyphArrangement ga;
    ga.addJustifiedText(font, text, 0.0f, 0.0f, (float)juce::jmax(20, width), juce::Justification::left);
    const int wrapped = (int)std::ceil(ga.getBoundingBox(0, -1, true).getHeight());
    // +2px slack for the line-boundary rounding case described in the header doc comment.
    return juce::jmax((int)std::ceil(font.getHeight()), wrapped) + 2;
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
    // setProvider() — no need to wait for the fetch below to resolve. The mode selector's gates
    // don't read the provider (parity rule), but this is still a convenient known resync point
    // for owners that call refreshModels() after wiring things up.
    updateHostedModeNotice();
    refreshModeControls();

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

void AIChatComponent::refreshModeControls() {
#if SYNTH_ENABLE_TIMELINE
    // PROVIDER-AGNOSTIC on purpose (the local/remote parity rule): arrange mode is served by
    // whichever transport the active provider has (AIIntegrationService::sendArrangeMessage), so
    // the selector's gate is the timeline preference alone — never the provider.
    // hasTimelineContext() rides along with the preference switch: with no live doc installed
    // there is nothing to summarise, validate against, or apply to, so offering Arrange would
    // only manufacture the "cannot be checked or applied" failure previewTimelineOps() reports.
    // In the real app the context is installed whenever the feature is on (MainComponent wires
    // both), so this is one gate in practice and a safety net in tests.
    const bool show = aiService.areTimelineToolsEnabled() && aiService.hasTimelineContext();
#else
    const bool show = false;
#endif

    if (modeSelector.isVisible() == show)
        return;

    modeSelector.setVisible(show);
    if (!show)
        modeSelector.setSelectedId(kModeSelectorPatchId, juce::dontSendNotification);
    resized();
}

// ============================================================================
// P6-8: local multi-conversation history + unified history UI + upsell/downgrade strips
// ============================================================================

void AIChatComponent::updateUpsellStrip() {
    static const juce::String kHistoryTooltipBase = "View, restore, or clear saved conversations";
    static const juce::String kUpsellText =
        juce::String::fromUTF8("Your history is saved locally only \xe2\x80\x94 subscribers get automatic "
                               "cloud backup across devices.");

    const bool showUpsell = accountServicePtr == nullptr || !isProPlan(accountServicePtr->getSnapshot());
    upsellButton.setVisible(showUpsell);
    historyButton.setTooltip(showUpsell ? kHistoryTooltipBase + ". " + kUpsellText : kHistoryTooltipBase);
    resized();
}

void AIChatComponent::updateDowngradeStrip() {
    const bool signedIn =
        accountServicePtr != nullptr && accountServicePtr->getSnapshot().state == AccountState::SignedIn;
    const bool pro = accountServicePtr != nullptr && isProPlan(accountServicePtr->getSnapshot());
    const bool show = signedIn && !pro && lastDeletionScheduledAt.isNotEmpty();

    downgradeStripLabel.setVisible(show);
    if (show) {
        downgradeStripLabel.setText(juce::String::fromUTF8("Your subscription has lapsed \xe2\x80\x94 your saved "
                                                           "history will be deleted on ") +
                                        formatReadableDate(lastDeletionScheduledAt) + ".",
                                    juce::dontSendNotification);
    }
    resized();
}

void AIChatComponent::replayMessagesFrom(const std::vector<std::pair<juce::String, juce::String>>& roleContentPairs) {
    messages.clear();
    for (const auto& pair : roleContentPairs) {
        const auto& role = pair.first;
        const auto& content = pair.second;
        if (role == "system")
            continue;

        juce::String json;
        juce::String cleanText = content;
        int start = content.indexOf("```json");
        if (start != -1) {
            int end = content.indexOf(start + 7, "```");
            if (end != -1) {
                json = content.substring(start + 7, end).trim();
                cleanText = content.substring(0, start) + content.substring(end + 3);
            }
        }
        // showUpgradeAction deliberately left at its default false: a replayed turn never
        // resurrects the Upgrade button, same as Cancel-button/spinner state being session-only.
        messages.push_back({role, cleanText.trim(), json});
        attachPatchPreview(messages.back());
    }
}

juce::String AIChatComponent::reconstructMessageContent(const MessageData& data) {
    if (data.jsonPatch.isEmpty())
        return data.text;
    return data.text + "\n```json\n" + data.jsonPatch + "\n```";
}

juce::String AIChatComponent::deriveConversationTitle() const {
    static constexpr int kMaxTitleLength = 60;
    for (const auto& m : messages) {
        if (m.role == "user" && m.text.isNotEmpty()) {
            auto title = m.text.trim();
            if (title.length() > kMaxTitleLength)
                title = title.substring(0, kMaxTitleLength).trim() + juce::String::fromUTF8("\xe2\x80\xa6");
            return title;
        }
    }
    return "New Conversation"; // empty title would render as a blank row in the history popup
}

juce::File AIChatComponent::resolveLocalHistoryDirectory() const {
    return localHistoryDirOverride != juce::File() ? localHistoryDirOverride
                                                   : LocalHistoryStore::getDefaultHistoryDirectory();
}

void AIChatComponent::saveCurrentConversationLocally() {
    if (currentLocalConversationId.isEmpty()) {
        currentLocalConversationId = LocalHistoryStore::newConversationId();
        currentLocalConversationCreatedAt = juce::Time::getCurrentTime().toISO8601(true);
    }

    LocalConversation conversation;
    conversation.id = currentLocalConversationId;
    conversation.createdAt = currentLocalConversationCreatedAt;
    conversation.updatedAt = juce::Time::getCurrentTime().toISO8601(true);
    conversation.title = deriveConversationTitle();

    for (const auto& m : messages) {
        if (m.role != "user" && m.role != "assistant")
            continue;
        conversation.messages.push_back({m.role, reconstructMessageContent(m), conversation.updatedAt});
    }

    const int retentionDays =
        appProperties.getUserSettings()->getIntValue("historyRetentionDays", LocalHistoryStore::kDefaultRetentionDays);
    LocalHistoryStore::save(resolveLocalHistoryDirectory(), conversation, retentionDays);
}

ConversationHistorySource*
AIChatComponent::resolveLocalHistorySource(std::unique_ptr<ConversationHistorySource>& fallbackStorage) {
    if (testLocalHistorySource)
        return testLocalHistorySource.get();
    fallbackStorage = std::make_unique<LocalHistorySource>(resolveLocalHistoryDirectory());
    return fallbackStorage.get();
}

ConversationHistorySource*
AIChatComponent::resolveCloudHistorySource(std::unique_ptr<ConversationHistorySource>& fallbackStorage) {
    if (testCloudHistorySource)
        return testCloudHistorySource.get();
    if (accountServicePtr == nullptr)
        return nullptr;
    auto token = accountServicePtr->getAccessToken();
    if (token.isEmpty())
        return nullptr;
    fallbackStorage = std::make_unique<CloudHistorySource>(synth::branding::kApiBaseUrl, token);
    return fallbackStorage.get();
}

void AIChatComponent::historyButtonClicked() {
    juce::Component::SafePointer<AIChatComponent> safeThis(this);

    // Shared by every "show the local list" path below (not signed in; signed in with no usable
    // token; signed in but Free/lapsed).
    auto showLocalList = [safeThis]() {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        std::unique_ptr<ConversationHistorySource> fallback;
        auto* localSource = self->resolveLocalHistorySource(fallback);
        localSource->list([safeThis](ConversationHistorySource::ListResult result) {
            if (auto* self2 = safeThis.getComponent())
                self2->showHistoryPopup(result.conversations, /*isCloud=*/false);
        });
    };

    const AccountSnapshot snapshot =
        accountServicePtr != nullptr ? accountServicePtr->getSnapshot() : AccountSnapshot{};
    const bool signedIn = accountServicePtr != nullptr && snapshot.state == AccountState::SignedIn;
    const bool pro = isProPlan(snapshot);

    if (!signedIn) {
        showLocalList();
        return;
    }

    std::unique_ptr<ConversationHistorySource> cloudFallback;
    auto* cloudSource = resolveCloudHistorySource(cloudFallback);
    if (cloudSource == nullptr) {
        showLocalList();
        return;
    }

    // Signed in: ALWAYS fire the cloud call, regardless of plan — it's the only source of a
    // pending grace-period deletion date (see lastDeletionScheduledAt's doc comment), and when the
    // plan IS Pro its result doubles as the list itself. Only ever called from this explicit click.
    cloudSource->list([safeThis, pro, showLocalList](ConversationHistorySource::ListResult cloudResult) {
        juce::MessageManager::callAsync([safeThis, cloudResult, pro, showLocalList]() {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;

            self->lastDeletionScheduledAt = cloudResult.ok ? cloudResult.deletionScheduledAt : juce::String();
            self->updateDowngradeStrip();

            if (pro)
                self->showHistoryPopup(cloudResult.conversations, /*isCloud=*/true);
            else
                showLocalList();
        });
    });
}

void AIChatComponent::showHistoryPopup(std::vector<LocalConversationSummary> list, bool isCloud) {
    lastHistoryPopupShown = true;
    lastHistoryPopupWasCloud = isCloud;
    lastHistoryList = list;

    // Real UI is skipped under test (see didShowHistoryPopupForTesting()'s doc comment) — a test
    // only ever reaches this method via simulateHistoryButtonClick() after installing fakes with
    // setHistorySourcesForTesting(), so that's what signals "under test" here, with no separate
    // flag for a future test to forget to set. Actually opening a native juce::PopupMenu window
    // on a headless CI runner with no X server crashes JUCE's XWindowSystem outright (asserts
    // then segfaults) — every test above only asserts on the *ForTesting() state already set
    // above, never on an actual visible menu, so skipping the real window is lossless for them.
    if (testLocalHistorySource != nullptr || testCloudHistorySource != nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Clear my history");
    menu.addSeparator();
    if (list.empty()) {
        menu.addItem(-1, "No saved conversations", false, false);
    } else {
        int itemId = 2;
        for (const auto& summary : list) {
            juce::String title = summary.title.isNotEmpty() ? summary.title : juce::String("Untitled");
            menu.addItem(itemId++, title + "   " + formatReadableDate(summary.updatedAt));
        }
    }

    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&historyButton),
                       [safeThis, list, isCloud](int result) {
                           auto* self = safeThis.getComponent();
                           if (self == nullptr || result <= 0)
                               return;

                           if (result == 1) {
                               self->confirmAndClearHistory();
                               return;
                           }

                           const size_t index = (size_t)(result - 2);
                           if (index < list.size())
                               self->restoreConversation(list[index].id, isCloud);
                       });
}

void AIChatComponent::restoreConversation(const juce::String& id, bool isCloud) {
    juce::Component::SafePointer<AIChatComponent> safeThis(this);

    auto onLoaded = [safeThis, id, isCloud](bool ok, LocalConversation conversation) {
        juce::MessageManager::callAsync([safeThis, ok, conversation, id, isCloud]() {
            auto* self = safeThis.getComponent();
            if (self == nullptr || !ok)
                return;

            std::vector<std::pair<juce::String, juce::String>> pairs;
            for (const auto& m : conversation.messages)
                pairs.push_back({m.role, m.content});
            self->replayMessagesFrom(pairs);

            // aiService's own chatHistory is cleared, NOT re-seeded with the restored turns —
            // there is no API for that (see this method's doc comment / docs/AI_Engine.md). The
            // model has no memory of the restored conversation until new turns accumulate.
            self->aiService.clearHistory();

            // Adopt the restored id so subsequent local (and, for a Pro restore, cloud) saves
            // continue THIS conversation instead of starting a new one.
            self->currentLocalConversationId = id;
            self->currentLocalConversationCreatedAt = conversation.createdAt.isNotEmpty()
                                                          ? conversation.createdAt
                                                          : juce::Time::getCurrentTime().toISO8601(true);
            if (isCloud)
                self->aiService.setConversationId(id);

            self->updateChatDisplay();
        });
    };

    std::unique_ptr<ConversationHistorySource> fallback;
    auto* source = isCloud ? resolveCloudHistorySource(fallback) : resolveLocalHistorySource(fallback);
    if (source == nullptr)
        return;
    source->get(id, onLoaded);
}

void AIChatComponent::confirmAndClearHistory() {
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::WarningIcon)
                       .withTitle("Clear History")
                       .withMessage("Delete your saved conversation history? This cannot be undone.")
                       .withButton("Delete")
                       .withButton("Cancel");
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis](int result) {
        if (result == 1) {
            if (auto* self = safeThis.getComponent())
                self->performClearHistory();
        }
    });
}

void AIChatComponent::performClearHistory() {
    const bool pro = accountServicePtr != nullptr && isProPlan(accountServicePtr->getSnapshot());

    std::unique_ptr<ConversationHistorySource> fallback;
    auto* source = pro ? resolveCloudHistorySource(fallback) : resolveLocalHistorySource(fallback);
    if (source == nullptr)
        return;
    source->deleteAll([](bool, int) {});
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
