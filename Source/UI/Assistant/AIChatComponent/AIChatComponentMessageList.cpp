#include "AIChatComponent.h"
#include "AIChatComponentEditPlanCard.h"
#include "Branding.h"
#include "UI/Assistant/ChatMessageAccessibilityText.h"
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
                  EditPlanCard::RateCallback onRate, std::function<void(bool)> onFoldChanged)
        : onFoldChangedCallback(std::move(onFoldChanged)) {
        role = data.role;
        text = data.text;
        responseMs = data.responseMs;

        // The bubble itself is the list item and speaks the whole message (see
        // createAccessibilityHandler), so the label inside would only repeat it.
        setTitle(synth::ui::describeChatMessageForAccessibility(role, text, data.planOk && data.planJson.isNotEmpty()));
        addAndMakeVisible(textLabel);
        textLabel.setAccessible(false);
        textLabel.setText(text, juce::dontSendNotification);
        // No border: the bubble measures the text at the full content width with no inset, and a
        // label that wraps narrower than it was measured ends the last line with an ellipsis.
        textLabel.setBorderSize(juce::BorderSize<int>(0));
        textLabel.setMinimumHorizontalScale(1.0f);
        textLabel.setJustificationType(juce::Justification::topLeft);
        // The label always holds the whole text at full height; textClip shows the first six lines
        // of it while the message is folded, so nothing is ever cut with an ellipsis.
        addAndMakeVisible(textClip);
        textClip.setInterceptsMouseClicks(false, false);
        textClip.addAndMakeVisible(textLabel);

        unfolded = data.textUnfolded;
        foldFraction = unfolded ? 1.0f : 0.0f;
        foldLink.setWantsKeyboardFocus(true);
        foldLink.onClick = [this] { toggleFold(); };
        updateFoldLinkTexts();
        addChildComponent(foldLink);

        // One card per answer that carries a plan, whatever it holds (a patch, timeline ops, or
        // both); a refused plan still gets its card, showing why, with no Apply.
        if (data.planJson.isNotEmpty()) {
            planCard = std::make_unique<EditPlanCard>(data, std::move(applyPlan), std::move(onRate));
            addAndMakeVisible(*planCard);
        }

        if (data.showUpgradeAction) {
            upgradeButton = std::make_unique<juce::TextButton>();
            upgradeButton->setButtonText("Upgrade to Pro");
            upgradeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF6B4FBB));
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
        const int contentWidth = b.getWidth();

        if (planCard) {
            planCard->setBounds(b.removeFromBottom(planCard->getRequiredHeight(b.getWidth())));
            b.removeFromBottom(kRoleContentGap);
        }

        if (upgradeButton) {
            upgradeButton->setBounds(b.removeFromBottom(kUpgradeButtonHeight));
            b.removeFromBottom(kRoleContentGap);
        }

        const TextMetrics m = measureText(contentWidth);
        textClip.setBounds(b.removeFromTop(visibleTextHeight(m)));
        // One spare line below the measured text so the label's own line count never falls short.
        textLabel.setBounds(0, 0, contentWidth, m.fullHeight + (int)std::ceil(textLabel.getFont().getHeight()));

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

        if (upgradeButton) {
            height += kRoleContentGap + kUpgradeButtonHeight;
        }

        return juce::jmax(40, height);
    }

    // Whether the text is longer than the fold, at this bubble width (test hook).
    bool isFoldable(int width) { return measureText(width - kOuterPadding * 2).foldable; }

private:
    // A message longer than this many lines is folded behind "Show more".
    static constexpr int kFoldLines = 6;
    static constexpr int kFoldLinkHeight = 20;
    static constexpr double kFoldAnimMs = 160.0;

    struct TextMetrics {
        int fullHeight = 0;
        int foldedHeight = 0;
        bool foldable = false;
    };

    TextMetrics measureText(int contentWidth) {
        const juce::Font font = textLabel.getFont();
        TextMetrics m;
        m.fullHeight = AIChatComponent::computeWrappedTextHeight(font, text, contentWidth);
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
    juce::Label textLabel;
    std::unique_ptr<EditPlanCard> planCard;
    std::unique_ptr<juce::TextButton> upgradeButton;

    std::function<void(bool)> onFoldChangedCallback;
    bool unfolded = false;
    float foldFraction = 0.0f; // 0 folded .. 1 unfolded
    juce::Component textClip;
    synth::ui::TextLinkButton foldLink{"Show more"};
    juce::VBlankAnimatorUpdater vblank{this};
    synth::ui::AnimationDriver foldAnim;
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

    // Downgrade notice: same zero-height-when-absent contract, reserved only once
    // updateDowngradeStrip() has something true to say (see its doc comment). Same measured-not-
    // guessed height as hostedModeNotice just above — this one's text also embeds a variable-
    // length date, so a fixed height clipped it whenever the rendered sentence wrapped.
    const int downgradeStripHeight =
        downgradeStripLabel.isVisible()
            ? computeWrappedTextHeight(downgradeStripLabel.getFont(), downgradeStripLabel.getText(), chromeWidth)
            : 0;
    const int downgradeStripGap = downgradeStripHeight > 0 ? kChromeGap : 0;

    // Upsell strip: just the "Upgrade to Pro" button now (its explanatory text moved to
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
            [this, i](bool unfolded) {
                if (i < messages.size())
                    messages[i].textUnfolded = unfolded;
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
