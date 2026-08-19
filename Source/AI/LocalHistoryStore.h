#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** One message in a local conversation's `messages` array. Same field names as
 *  AuthClient::ConversationMessage (role/content/createdAt, see Source/AI/AuthClient.h) so a
 *  history-panel UI reading either backend doesn't have to translate field names. */
struct LocalConversationMessage {
    juce::String role; // "user" or "assistant"
    juce::String content;
    juce::String createdAt; // ISO8601
};

/** Header fields only — id/title/createdAt/updatedAt — surfaced by list() for a fast list view
 *  that never has to parse every conversation's full `messages` array. Same field names as
 *  AuthClient::ConversationSummary. */
struct LocalConversationSummary {
    juce::String id;
    juce::String title; // may be empty (untitled)
    juce::String createdAt;
    juce::String updatedAt;
};

/** Full conversation: header fields flattened alongside `messages`, same shape as
 *  AuthClient::ConversationDetailResult minus its transport-result fields (ok/transportError). */
struct LocalConversation {
    juce::String id;
    juce::String title;
    juce::String createdAt;
    juce::String updatedAt;
    std::vector<LocalConversationMessage> messages;
};

/**
 * @class LocalHistoryStore
 * @brief One JSON file per conversation under
 *        `<userAppData>/<kSettingsFolderName>/History/<id>.json` (P6-8) — the local half of the
 *        local-first/cloud-as-sync history design. Every session writes here regardless of plan;
 *        Pro sessions additionally sync via the server's `x-conversation-id` mechanism
 *        (AIIntegrationService::setConversationId(), see docs/AI_Engine.md).
 *
 * Follows SnippetManager's exact convention (Source/SnippetManager.h): static methods taking an
 * explicit directory (so tests never touch the real per-user location), a
 * getDefaultHistoryDirectory() for production callers, and JSON transform functions kept
 * pure/filesystem-free so they're separately testable.
 *
 * No file-locking, consistent with every other local-storage class in this codebase
 * (SnippetManager, ThemeManager, DeviceIdStore). The multi-instance concurrent-write hazard is
 * avoided by construction instead of locking: one conversation file per app/plugin-instance
 * *session*, keyed by a session-scoped id no other instance ever writes to — mirroring how the
 * cloud side keys history per session via x-conversation-id. list() scanning all files can very
 * rarely race a torn write from another live instance; accepted, same as the classes above.
 */
class LocalHistoryStore {
public:
    // Retention sentinel: "keep forever" — save() skips age-based pruning, but the hard cap below
    // still applies.
    static constexpr int kRetainForever = -1;

    // Engineering backstop against unbounded disk growth, independent of the user's retention
    // setting — not user-facing.
    static constexpr int kHardCapFiles = 2000;

    // Default retention (days), used both as the Settings UI default and whenever a persisted
    // retentionDays value isn't one of the offered choices (see save()'s doc comment).
    static constexpr int kDefaultRetentionDays = 180;

    /** `<userAppData>/<kSettingsFolderName>/History`, created on demand. Mirrors
     *  SnippetManager::getDefaultSnippetsDirectory(). */
    static juce::File getDefaultHistoryDirectory();

    /** A fresh id for a new session-scoped conversation. */
    static juce::String newConversationId();

    // ---- Pure JSON transforms (no filesystem) ------------------------------------------

    static juce::var conversationToVar(const LocalConversation& conversation);

    /** Parses `v` into `out`. Returns false (leaving `out` untouched) when `v` isn't a
     *  well-formed conversation object with a non-empty "id" — same "unreadable means absent"
     *  contract as SnippetManager::loadSnippet(). */
    static bool conversationFromVar(const juce::var& v, LocalConversation& out);

    /** Header-only parse (id/title/createdAt/updatedAt) — skips walking `messages`, for list()'s
     *  fast path. Same failure contract as conversationFromVar(). */
    static bool summaryFromVar(const juce::var& v, LocalConversationSummary& out);

    // ---- Persistence --------------------------------------------------------------------

    /** `dir/<id>.json`, or an empty File when `id` sanitises to nothing. */
    static juce::File fileForId(const juce::File& dir, const juce::String& id);

    /** Rewrites `dir/<id>.json` whole (no locking — see class comment). After a successful write,
     *  prunes conversations older than `retentionDays` by `updatedAt` (kRetainForever skips age
     *  pruning), then enforces kHardCapFiles regardless. Any `retentionDays` outside
     *  {30, 90, 180, 365, kRetainForever} is treated as kDefaultRetentionDays, so a hand-edited
     *  settings value (e.g. a stray 0) can't accidentally prune everything. */
    static bool save(const juce::File& dir, const LocalConversation& conversation, int retentionDays);

    /** Every readable conversation's header fields, sorted by updatedAt descending (most recent
     *  first — ISO8601 timestamps sort lexicographically the same as chronologically). Skips
     *  unreadable/corrupt files rather than surfacing a broken row. */
    static std::vector<LocalConversationSummary> list(const juce::File& dir);

    /** Full conversation including messages. Returns false (out untouched) when missing/corrupt. */
    static bool get(const juce::File& dir, const juce::String& id, LocalConversation& out);

    static bool deleteOne(const juce::File& dir, const juce::String& id);

    /** Deletes every conversation file in `dir`. Returns the number actually deleted. */
    static int deleteAll(const juce::File& dir);

private:
    static void pruneOldConversations(const juce::File& dir, int retentionDays);
    static int sanitiseRetentionDays(int retentionDays);
};

} // namespace synth
