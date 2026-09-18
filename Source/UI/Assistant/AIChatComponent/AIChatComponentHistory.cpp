#include "AIChatComponent.h"
#include "Branding.h"

namespace synth {

// Concern: conversation history -- local/cloud persistence, the history popup, restore/clear,
// and the account plan/quota gating (upsell + downgrade strips) that drives it.

namespace {

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
            // there is no API for that (see this method's doc comment /
            // docs/ai/history.md#restoring). The
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

} // namespace synth
