#include "AIChatComponent.h"
#include "AIChatComponentEditPlanCard.h"
#include "Branding.h"
#include "BuildStepsCard.h"
#include "SelectableChatText.h"
#include "SongPlanCard.h"
#include "UI/Assistant/ChatMessageAccessibilityText.h"
#include "UI/Assistant/ChatPalette.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/Layout/UIAnimation.h"
#include <thread>

namespace synth {

// Concern: message list rendering -- chat bubbles (each with at most one edit-plan card, defined in
// AIChatComponentEditPlanCard.cpp), and the panel's resized() layout loop that positions them (resized() dynamic_casts
// to MessageBubble*, so it has to live alongside its full definition rather than in the general layout code).

//==============================================================================
class AIChatComponent::MessageBubble : public juce::Component {
public:
    MessageBubble(const MessageData& data, std::function<bool()> applyPlan, std::function<void()> onUpgrade,
                  EditPlanCard::RateCallback onRate, std::function<void(bool)> onFoldChanged, SongHooks songHooks = {})
        : onFoldChangedCallback(std::move(onFoldChanged)) {
        role = data.role;
        text = data.text;
        responseMs = data.responseMs;

        // The bubble itself is the list item and speaks the whole message (see
        // createAccessibilityHandler); the text view inside is a Tab stop for selecting and copying.
        setTitle(synth::ui::describeChatMessageForAccessibility(
            role, text, data.planOk && data.planJson.isNotEmpty() && !data.song));
        // The text is a read-only editor (SelectableChatText): the mouse selects it, Cmd+C copies, Cmd+A selects all,
        // and a right click offers Copy. It always holds the whole text at full height; textClip shows the first six
        // lines of it while the message is folded, so nothing is ever cut with an ellipsis.
        textView.setMessage(text, juce::Label().getFont(), themeColors().textPrimary);
        textView.setTitle("Message text");
        textView.setComponentID("chatMessageText");
        textView.setTooltip("Drag to select, Cmd+C to copy");
        textView.onFocusChanged = [this] { repaint(); };
        addAndMakeVisible(textClip);
        textClip.setInterceptsMouseClicks(false, true);
        textClip.addAndMakeVisible(textView);

        unfolded = data.textUnfolded;
        foldFraction = unfolded ? 1.0f : 0.0f;
        foldLink.setWantsKeyboardFocus(true);
        foldLink.onClick = [this] { toggleFold(); };
        updateFoldLinkTexts();
        addChildComponent(foldLink);

        // One card per answer that carries a plan, whatever it holds (a patch, timeline ops, or
        // both); a refused plan still gets its card, showing why, with no Apply. A song message
        // has its song card instead: the built song is already applied, and that card carries the
        // details and the thumbs the edit-plan card would.
        if (data.song)
            createSongCard(data.song, std::move(songHooks), std::move(onRate));
        else if (data.planJson.isNotEmpty()) {
            planCard = std::make_unique<EditPlanCard>(data, std::move(applyPlan), std::move(onRate));
            addAndMakeVisible(*planCard);
        }

        if (data.showUpgradeAction) {
            upgradeButton = std::make_unique<juce::TextButton>();
            upgradeButton->setButtonText("Upgrade to Pro");
            applyUpgradeColours();
            upgradeButton->onClick = std::move(onUpgrade);
            addAndMakeVisible(*upgradeButton);
        }
    }

    ~MessageBubble() override { foldAnim.stop(vblank); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::listItem);
    }

    // AIChatComponent::resized() reads this to decide which side of the message list gets the
    // gutter (user bubbles hug the right edge, assistant bubbles the left) — see its layout loop.
    bool isUserRole() const { return role == "user"; }

    void lookAndFeelChanged() override { applyThemeColours(); }
    void parentHierarchyChanged() override { applyThemeColours(); }

    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().reduced(2).toFloat();
        bool isUser = (role == "user");

        // Every bubble colour comes from the theme's tokens (ChatPalette.h): the user bubble tints with the accent, the
        // assistant one with the raised surface, both faded through the same alpha gradient on every theme.
        const auto palette = synth::ui::chatPaletteFor(*this);
        const juce::Colour baseColour = isUser ? palette.userBubble : palette.assistantBubble;
        const juce::Colour borderColour = themeColors().border;
        const juce::Colour roleColour = isUser ? palette.userRole : palette.mutedText;
        const juce::Colour timestampColour = palette.mutedText;

        juce::ColourGradient grad(baseColour.withAlpha(0.3f), b.getX(), b.getY(), baseColour.withAlpha(0.1f),
                                  b.getRight(), b.getBottom(), false);

        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, 10.0f);

        g.setColour(borderColour);
        g.drawRoundedRectangle(b, 10.0f, 1.0f);

        // Role indicator (+ optional elapsed-wait marker on assistant bubbles). Reserved within
        // the SAME outer padding + role-band height resized() clears for textView below, so the
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

    void applyThemeColours() {
        textView.setTextColour(themeColors().textPrimary);
        applyUpgradeColours();
    }

    void paintOverChildren(juce::Graphics& g) override {
        if (textView.focusedByKeyboard())
            synth::ui::paintFocusRingAlways(g, getLocalBounds().reduced(2).toFloat(), *this, 10.0f);
    }

    void applyUpgradeColours() {
        if (upgradeButton == nullptr)
            return;
        const auto palette = synth::ui::chatPaletteFor(*this);
        upgradeButton->setColour(juce::TextButton::buttonColourId, palette.upgradeFill);
        upgradeButton->setColour(juce::TextButton::textColourOffId, palette.upgradeText);
    }

    const synth::theme::Colors& themeColors() const {
        static const synth::theme::Colors defaults;
        const auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        return lf != nullptr ? lf->getTheme().colors : defaults;
    }

    void resized() override {
        auto b = getLocalBounds().reduced(kOuterPadding);

        // Headroom for the role label/timestamp paint() draws above — see its matching
        // getLocalBounds().reduced(kOuterPadding) + removeFromTop(kRoleBandHeight). Without this,
        // textView started at the same y the role band paints into and clipped/overlapped it.
        b.removeFromTop(kRoleBandHeight + kRoleContentGap);
        const int contentWidth = b.getWidth();

        if (planCard) {
            planCard->setBounds(b.removeFromBottom(planCard->getRequiredHeight(b.getWidth())));
            b.removeFromBottom(kRoleContentGap);
        }

        if (songCard) {
            songCard->setBounds(b.removeFromBottom(songCard->getRequiredHeight(b.getWidth())));
            b.removeFromBottom(kRoleContentGap);
        }

        if (upgradeButton) {
            upgradeButton->setBounds(b.removeFromBottom(kUpgradeButtonHeight));
            b.removeFromBottom(kRoleContentGap);
        }

        const TextMetrics m = measureText(contentWidth);
        textClip.setBounds(b.removeFromTop(visibleTextHeight(m)));
        // One spare line below the measured text so the label's own line count never falls short.
        textView.setBounds(0, 0, contentWidth, m.fullHeight + (int)std::ceil(textView.getFont().getHeight()));

        foldLink.setVisible(m.foldable);
        if (m.foldable)
            foldLink.setBounds(b.removeFromTop(kFoldLinkHeight));
    }

    int getRequiredHeight(int width) {
        int contentWidth = width - kOuterPadding * 2;
        const TextMetrics m = measureText(contentWidth);
        int textHeight = visibleTextHeight(m) + (m.foldable ? kFoldLinkHeight : 0);
        // Outer padding (top+bottom) + the role band + the gap below it, on top of the wrapped
        // message text — see resized()'s matching reservation.
        int height = textHeight + kOuterPadding * 2 + kRoleBandHeight + kRoleContentGap;

        if (planCard)
            height += kRoleContentGap + planCard->getRequiredHeight(contentWidth);

        if (songCard)
            height += kRoleContentGap + songCard->getRequiredHeight(contentWidth);

        if (upgradeButton) {
            height += kRoleContentGap + kUpgradeButtonHeight;
        }

        return juce::jmax(40, height);
    }

    // The song card of this message, whichever the session's phase calls for; re-read from the session by
    // refreshSong().
    SongCardBase* getSongCard() const { return songCard; }
    bool showsPlanCard() const { return songPlanCard != nullptr; }

    // Brings the card up to date with its session. A change of phase that swaps the card (plan to build) is a rebuild
    // of the whole list, done by the caller; this only refreshes the card showing.
    void refreshSong() {
        if (songPlanCard)
            songPlanCard->refresh();
        if (buildCard)
            buildCard->refresh();
    }

    // Whether this bubble shows an edit-plan card (test hook).
    bool hasEditPlanCard() const { return planCard != nullptr; }

    // Whether the text is longer than the fold, at this bubble width (test hook).
    bool isFoldable(int width) { return measureText(width - kOuterPadding * 2).foldable; }

private:
    void createSongCard(const std::shared_ptr<SongSession>& session, SongHooks hooks,
                        EditPlanCard::RateCallback onRate) {
        const auto phase = session->phase;
        const bool planning = phase == SongSession::Phase::ready || phase == SongSession::Phase::replanning ||
                              phase == SongSession::Phase::dismissed;
        if (planning) {
            SongPlanCard::Actions actions{hooks.onBuild, hooks.onReplan, hooks.onCancelPlan};
            songPlanCard = std::make_unique<SongPlanCard>(*session, std::move(actions));
            songCard = songPlanCard.get();
        } else {
            BuildStepsCard::Actions actions;
            actions.onCancel = hooks.onCancelBuild;
            actions.onListToggled = [session](bool open) { session->listOpen = open; };
            actions.onRate = [session, onRate = std::move(onRate)](int rating, const juce::String& comment) {
                session->rating = rating;
                session->ratingComment = comment;
                if (onRate)
                    onRate(static_cast<PatchRatingUiState>(rating), comment);
            };
            buildCard = std::make_unique<BuildStepsCard>(*session, std::move(actions));
            songCard = buildCard.get();
        }
        songCard->onLayoutChanged = [this] { relayoutList(); };
        addAndMakeVisible(*songCard);
    }

    // A message longer than this many lines is folded behind "Show more".
    static constexpr int kFoldLines = 6;
    static constexpr int kEditorRightEdgeSpace = 2; // juce::TextEditor's own right-edge reserve
    static constexpr int kFoldLinkHeight = 20;
    static constexpr double kFoldAnimMs = 160.0;

    struct TextMetrics {
        int fullHeight = 0;
        int foldedHeight = 0;
        bool foldable = false;
    };

    TextMetrics measureText(int contentWidth) {
        const juce::Font font = textView.getFont();
        TextMetrics m;
        // The editor keeps 2 px free at its right edge, so it wraps at that narrower width; measuring at the full width
        // can count one line fewer than it shows and clip the last one (the closing quote of the built-song line).
        m.fullHeight = AIChatComponent::computeWrappedTextHeight(font, text, contentWidth - kEditorRightEdgeSpace);
        const float lineHeight = font.getHeight();
        const int lines = juce::jmax(1, (int)std::lround((float)(m.fullHeight - 2) / lineHeight));
        m.foldable = lines > kFoldLines;
        m.foldedHeight = (int)std::ceil(lineHeight * (float)kFoldLines);
        return m;
    }

    // Height of the text area now: the folded height, the full height, or between them while the
    // fold animation runs.
    int visibleTextHeight(const TextMetrics& m) const {
        if (!m.foldable)
            return m.fullHeight;
        return m.foldedHeight + (int)std::lround((float)(m.fullHeight - m.foldedHeight) * foldFraction);
    }

    void updateFoldLinkTexts() {
        const juce::String label = unfolded ? "Show less" : "Show more";
        const juce::String tip = unfolded ? "Show less of this message" : "Show the whole message";
        foldLink.setButtonText(label);
        foldLink.setTitle(tip);
        foldLink.setTooltip(tip);
        foldLink.repaint();
    }

    void relayoutList() {
        if (auto* chat = findParentComponentOfClass<AIChatComponent>())
            chat->resized();
        else
            resized();
    }

    void toggleFold() {
        unfolded = !unfolded;
        updateFoldLinkTexts();
        if (onFoldChangedCallback)
            onFoldChangedCallback(unfolded);

        const float from = foldFraction;
        const float to = unfolded ? 1.0f : 0.0f;
        if (isShowing()) {
            // Same 160 ms ease-out as the other short UI motion; each frame re-runs the list's layout,
            // which asks this bubble for its (interpolated) height.
            foldAnim.start(
                vblank, kFoldAnimMs, synth::ui::easeOutCubic,
                [this, from, to](float t) {
                    foldFraction = from + (to - from) * t;
                    relayoutList();
                },
                [this, to] {
                    foldFraction = to;
                    relayoutList();
                });
        } else {
            foldAnim.stop(vblank);
            foldFraction = to;
            relayoutList();
        }
    }

    static constexpr int kUpgradeButtonHeight = 28;
    // 8px-grid outer padding (replaces the old ad hoc reduced(10)/reduced(2) mismatch between
    // paint() and resized() that let the role label overlap the message text).
    static constexpr int kOuterPadding = 8;
    static constexpr int kRoleBandHeight = 16;
    static constexpr int kRoleContentGap = 8;

    juce::String role;
    juce::String text;
    int responseMs = -1;
    SelectableChatText textView;
    std::unique_ptr<EditPlanCard> planCard;
    std::unique_ptr<SongPlanCard> songPlanCard;
    std::unique_ptr<BuildStepsCard> buildCard;
    SongCardBase* songCard = nullptr;
    std::unique_ptr<juce::TextButton> upgradeButton;

    std::function<void(bool)> onFoldChangedCallback;
    bool unfolded = false;
    float foldFraction = 0.0f; // 0 folded .. 1 unfolded
    juce::Component textClip;
    synth::ui::TextLinkButton foldLink{"Show more"};
    juce::VBlankAnimatorUpdater vblank{this};
    synth::ui::AnimationDriver foldAnim;
};

namespace {
// A banner strip's height while it fades: its natural height scaled by how far the fade has got, never
// below 1 px while it is still on screen (0 means "not there" to the layout below).
int stripHeight(int natural, const synth::ui::FadeVisibility& fade) {
    return juce::jmax(1, juce::roundToInt(static_cast<float>(natural) * fade.progress()));
}
} // namespace

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
    topArea.removeFromLeft(kChromeGap);
    exportButton.setBounds(topArea.removeFromLeft(100));

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
            ? stripHeight(computeWrappedTextHeight(hostedModeNotice.getFont(), hostedModeNotice.getText(), chromeWidth),
                          hostedNoticeFade_)
            : 0;
    const int hostedNoticeGap = hostedNoticeHeight > 0 ? kChromeGap : 0;

    // Downgrade notice: same zero-height-when-absent contract, reserved only once
    // updateDowngradeStrip() has something true to say (see its doc comment). Same measured-not-
    // guessed height as hostedModeNotice just above — this one's text also embeds a variable-
    // length date, so a fixed height clipped it whenever the rendered sentence wrapped.
    const int downgradeStripHeight =
        downgradeStripLabel.isVisible()
            ? stripHeight(
                  computeWrappedTextHeight(downgradeStripLabel.getFont(), downgradeStripLabel.getText(), chromeWidth),
                  downgradeFade_)
            : 0;
    const int downgradeStripGap = downgradeStripHeight > 0 ? kChromeGap : 0;

    // Upsell strip: just the "Upgrade to Pro" button now (its explanatory text moved to
    // historyButton's tooltip — see the member doc comment), so it only needs a single comfortable
    // click-target row, not the taller label+button row this used to be. Same zero-height-when-absent
    // contract, but starts VISIBLE by default (see the member doc comment) — most callers (including
    // every existing test that never attaches an AccountService) will therefore reserve this space,
    // unlike the other three rows in this stack.
    const int upsellStripHeight = upsellButton.isVisible() ? stripHeight(32, upsellFade_) : 0;
    const int upsellStripGap = upsellStripHeight > 0 ? kChromeGap : 0;

    // Input row (40) + its gap + the model row (24), all on the 8px grid.
    const int inputRowHeight = currentInputRowHeight();
    constexpr int kSendRowHeight = 40; // Send, Cancel and the dot keep their size, at the bottom of the row
    constexpr int kModelRowHeight = 24;
    auto bottomArea =
        b.removeFromBottom(inputRowHeight + kChromeGap + kModelRowHeight + accountRowHeight + accountRowGap +
                           planBadgeHeight + planBadgeGap + hostedNoticeHeight + hostedNoticeGap +
                           downgradeStripHeight + downgradeStripGap + upsellStripHeight + upsellStripGap);

    // Bottom row: Input + Send (+ Cancel when waiting + spinner dot)
    auto inputRow = bottomArea.removeFromBottom(inputRowHeight);

    // Cancel button occupies the same slot as Send — only one is visible at a time.
    // We size both identically so the layout is stable regardless of visibility.
    auto sendCancelBounds = inputRow.removeFromRight(60);
    sendCancelBounds = sendCancelBounds.removeFromBottom(kSendRowHeight);
    sendButton.setBounds(sendCancelBounds);
    cancelButton.setBounds(sendCancelBounds);

    inputRow.removeFromRight(kChromeGap);

    // Spinner dot: 8×8, vertically centred on the right edge of the input area.
    const int spinnerSize = 8;
    spinnerDot.setBounds(inputRow.removeFromRight(spinnerSize)
                             .removeFromBottom(kSendRowHeight)
                             .withSizeKeepingCentre(spinnerSize, spinnerSize));
    inputRow.removeFromRight(4); // gap between spinner and input field

    inputField.setBounds(inputRow);
    // The wrap depends on the width just set: a change of line count lands here without an ease.
    updateInputRowHeight(true);

    // Middle row (above input): Model Picker
    bottomArea.removeFromBottom(kChromeGap);
    auto modelRow = bottomArea.removeFromBottom(kModelRowHeight);
    modelPicker.setBounds(modelRow.removeFromLeft(200));
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

    // Layout message bubbles and loader. Growing the list past the viewport shows the scrollbar and
    // shrinking it hides it, which changes the width the bubbles wrap at, so a second pass runs when
    // the width the first one used is no longer the viewport's.
    for (int pass = 0; pass < 2; ++pass) {
        const int listWidth = viewport.getMaximumVisibleWidth();
        layoutMessageList(listWidth);
        if (viewport.getMaximumVisibleWidth() == listWidth)
            break;
    }
}

void AIChatComponent::layoutMessageList(int listWidth) {
    int y = 0;
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
        } else if (child == waitingSpinner) {
            continue; // sits inside the waiting label's row below
        } else if (dynamic_cast<juce::Label*>(child)) {
            h = 24;
            if (waitingSpinner != nullptr && child == waitingStatusLabel) {
                waitingSpinner->setTopLeftPosition(2, y + (h - waitingSpinner->getHeight()) / 2);
                x = kWaitingDotGutter;
                w -= kWaitingDotGutter;
            }
        }

        if (h > 0) {
            child->setBounds(x, y, w, h);
            y += h + 10;
        }
    }
    messageList.setSize(listWidth, juce::jmax(viewport.getHeight(), y));
}

void AIChatComponent::updateChatDisplay() {
    dropWaitingRow();
    messageList.deleteAllChildren();

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& data = messages[i];
        if (data.song)
            refreshUsesLeft(*data.song);
        auto* bubble = new MessageBubble(
            data, [this, i] { return applyEditPlan(i); }, [this] { openUpgradePage(); },
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
                    patchFeedbackStore.record(msg.planJson, storeRating, comment, serverConversationId,
                                              msg.serverMessageId);

                    // Additionally sync to the server, fire-and-forget, ONLY when this
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
                            signedIn ? accountServicePtr->accessTokenForRequest() : juce::String();

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
            [this, i](bool unfolded) {
                if (i < messages.size())
                    messages[i].textUnfolded = unfolded;
            },
            makeSongHooks(i));
        messageList.addAndMakeVisible(bubble);
    }

    if (isWaitingForResponse && songBuildIndex < 0) {
        // The thinking row: a pulsing dot and the elapsed-time line, in the chat itself. The first time a wait starts
        // it fades in; a rebuild during the same wait keeps it showing.
        waitingSpinner = new SpinnerDot();
        waitingSpinner->setSize(8, 8);
        waitingSpinner->setComponentID("chatThinkingDot");
        messageList.addAndMakeVisible(waitingSpinner);
        waitingStatusLabel = new juce::Label();
        waitingStatusLabel->setComponentID("chatThinkingLabel");
        messageList.addAndMakeVisible(waitingStatusLabel);
        waitingStatusLabel->setColour(juce::Label::textColourId, synth::ui::chatPaletteFor(*this).mutedText);
        refreshWaitingStatusLabel();
        waitingSpinner->startPulse(vblankUpdater);
        if (!waitingRowShown_) {
            waitingFade_ = std::make_unique<synth::ui::FadeVisibility>(
                std::vector<juce::Component*>{waitingSpinner, waitingStatusLabel});
            waitingSpinner->setVisible(false);
            waitingStatusLabel->setVisible(false);
            waitingFade_->setShown(true);
            waitingRowShown_ = true;
        }
    }

    updateExportButton();
    resized();
    scrollToBottom();
}

AIChatComponent::SongHooks AIChatComponent::makeSongHooks(size_t i) {
    SongHooks hooks;
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    // The card is deleted by the redraw these trigger, so each runs on the next message-loop turn.
    auto later = [safeThis](std::function<void(AIChatComponent&)> action) {
        juce::MessageManager::callAsync([safeThis, action] {
            if (auto* self = safeThis.getComponent())
                action(*self);
        });
    };
    hooks.onBuild = [later, i] { later([i](AIChatComponent& c) { c.buildSongAt(i); }); };
    hooks.onCancelPlan = [later, i] { later([i](AIChatComponent& c) { c.dismissSong(i); }); };
    hooks.onCancelBuild = [later] { later([](AIChatComponent& c) { c.cancelSongBuild(); }); };
    hooks.onReplan = [later, i](int length, int detail) {
        later([i, length, detail](AIChatComponent& c) { c.replanSong(i, length, detail, {}, {}); });
    };
    return hooks;
}

bool AIChatComponent::refreshSongBubble(size_t index) {
    if (index >= messages.size() || !messages[index].song)
        return false;
    auto* bubble = dynamic_cast<MessageBubble*>(messageList.getChildComponent((int)index));
    if (bubble == nullptr)
        return false;
    const auto phase = messages[index].song->phase;
    const bool planning = phase == SongSession::Phase::ready || phase == SongSession::Phase::replanning ||
                          phase == SongSession::Phase::dismissed;
    // The card showing is of the other kind (the phase moved on): rebuild the list so the right one takes its place.
    if (bubble->getSongCard() == nullptr || bubble->showsPlanCard() != planning) {
        updateChatDisplay();
        return true;
    }
    refreshUsesLeft(*messages[index].song);
    bubble->refreshSong();
    resized();
    return true;
}

bool AIChatComponent::hasEditPlanCardForTesting(size_t index) const {
    auto* bubble = dynamic_cast<MessageBubble*>(messageList.getChildComponent((int)index));
    return bubble != nullptr && bubble->hasEditPlanCard();
}

juce::Component* AIChatComponent::getSongCardForTesting(size_t index) const {
    if (index >= messages.size())
        return nullptr;
    auto* bubble = dynamic_cast<MessageBubble*>(messageList.getChildComponent((int)index));
    return bubble != nullptr ? bubble->getSongCard() : nullptr;
}

void AIChatComponent::scrollToBottom() { viewport.setViewPosition(0, messageList.getHeight()); }

} // namespace synth
