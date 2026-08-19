#pragma once

#include "AuthClient.h"
#include "LocalHistoryStore.h"
#include <functional>
#include <memory>
#include <thread>

namespace synth {

/**
 * @class ConversationHistorySource
 * @brief Backend-agnostic interface behind AIChatComponent's history panel (P6-8).
 *
 * One implementation (LocalHistorySource) wraps LocalHistoryStore; one (CloudHistorySource) wraps
 * AuthClient's cloud conversation endpoints. The panel UI (AIChatComponent) is written once
 * against this interface and never itself branches on which backend it's talking to — the CALLER
 * decides which implementation to use, based on synth::isProPlan(AccountSnapshot).
 *
 * Callback-based, not blocking: CloudHistorySource makes a real HTTP call via AuthClient, and
 * nothing on the message thread may block on that (see CLAUDE.md's "no high-frequency logging"
 * invariant for the class of bug a blocked message thread causes here — same risk, different
 * cause). Mirrors AIProvider::sendPrompt()'s existing contract: a callback MAY be invoked on a
 * background thread, so callers hop back to the message thread themselves (SafePointer +
 * juce::MessageManager::callAsync), exactly like AIChatComponent::sendButtonClicked() already does
 * for aiService.sendMessage(). LocalHistorySource's callbacks fire synchronously (file I/O is fast
 * enough not to need offloading), which is also a valid (if degenerate) implementation of that
 * same contract.
 */
class ConversationHistorySource {
public:
    virtual ~ConversationHistorySource() = default;

    struct ListResult {
        bool ok = false;
        std::vector<LocalConversationSummary> conversations; // sorted most-recently-updated first
        // Non-empty only when the cloud backend's account has a pending grace-period deletion
        // (see AuthClient::ListConversationsResult). Always empty for the local backend.
        juce::String deletionScheduledAt;
    };

    virtual void list(std::function<void(ListResult)> callback) = 0;
    virtual void get(const juce::String& id, std::function<void(bool ok, LocalConversation)> callback) = 0;
    virtual void deleteAll(std::function<void(bool ok, int deletedCount)> callback) = 0;
};

/** Wraps LocalHistoryStore against a fixed directory (production default or a test override). */
class LocalHistorySource : public ConversationHistorySource {
public:
    explicit LocalHistorySource(juce::File directory)
        : dir(std::move(directory)) {}

    void list(std::function<void(ListResult)> callback) override {
        ListResult result;
        result.ok = true;
        result.conversations = LocalHistoryStore::list(dir);
        if (callback)
            callback(std::move(result));
    }

    void get(const juce::String& id, std::function<void(bool, LocalConversation)> callback) override {
        LocalConversation conversation;
        bool ok = LocalHistoryStore::get(dir, id, conversation);
        if (callback)
            callback(ok, std::move(conversation));
    }

    void deleteAll(std::function<void(bool, int)> callback) override {
        int count = LocalHistoryStore::deleteAll(dir);
        if (callback)
            callback(true, count);
    }

private:
    juce::File dir;
};

/**
 * @class CloudHistorySource
 * @brief Wraps AuthClient's conversation endpoints against a fixed host + access token.
 *
 * Each call launches a detached worker thread. AuthClient is a small, copyable, stateless
 * protocol client (host/clientId/deviceId strings + an HttpPerformer std::function, no threads of
 * its own — see its class comment), so capturing a COPY of it (plus the access token, plus a
 * heap-allocated cancellation flag kept alive via shared_ptr) into the thread lambda is safe: the
 * thread owns everything it touches and does not reach back into `this`, so detaching rather than
 * joining introduces no dangling-reference risk. The result is delivered via `callback` from that
 * background thread directly — see the class comment on ConversationHistorySource for why the
 * caller, not this class, is responsible for marshalling back to the message thread.
 */
class CloudHistorySource : public ConversationHistorySource {
public:
    CloudHistorySource(juce::String host, juce::String accessToken)
        : authClient(std::move(host))
        , accessToken(std::move(accessToken)) {}

    void list(std::function<void(ListResult)> callback) override {
        auto client = authClient;
        auto token = accessToken;
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        std::thread([client, token, cancelled, callback]() {
            auto res = client.listConversations(token, *cancelled);
            ListResult out;
            out.ok = res.ok;
            out.deletionScheduledAt = res.deletionScheduledAt;
            for (const auto& c : res.conversations)
                out.conversations.push_back({c.id, c.title, c.createdAt, c.updatedAt});
            if (callback)
                callback(std::move(out));
        }).detach();
    }

    void get(const juce::String& id, std::function<void(bool, LocalConversation)> callback) override {
        auto client = authClient;
        auto token = accessToken;
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        std::thread([client, token, id, cancelled, callback]() {
            auto res = client.getConversation(token, id, *cancelled);
            LocalConversation conversation;
            conversation.id = res.id;
            conversation.title = res.title;
            conversation.createdAt = res.createdAt;
            conversation.updatedAt = res.updatedAt;
            for (const auto& m : res.messages)
                conversation.messages.push_back({m.role, m.content, m.createdAt});
            if (callback)
                callback(res.ok, std::move(conversation));
        }).detach();
    }

    void deleteAll(std::function<void(bool, int)> callback) override {
        auto client = authClient;
        auto token = accessToken;
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        std::thread([client, token, cancelled, callback]() {
            auto res = client.deleteAllConversations(token, *cancelled);
            if (callback)
                callback(res.ok, res.deletedCount);
        }).detach();
    }

private:
    AuthClient authClient;
    juce::String accessToken;
};

} // namespace synth
