#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/**
 * P6-3: local, append-only log of thumbs up/down feedback on AI-generated patches. This remains
 * the unconditional fallback log — every rating is recorded here regardless of plan or whether a
 * server sync succeeds. P6-9 (see AIChatComponent's rating callback) additionally syncs a rating
 * to the server, keyed on the optional conversationId/messageId below, but ONLY when the account
 * is signed-in Pro and a server-assigned message id is available (live, same-session assistant
 * messages only — see AIChatComponent::MessageData::serverMessageId); ratings on restored
 * conversations, offline sessions, and free-tier accounts stay local-only. See docs/AI_Engine.md.
 *
 * One JSON object per line (JSON Lines), appended — never rewritten. A user who edits a comment
 * after the fact produces a second line for the same patch/rating rather than mutating the first;
 * this is a log, not a keyed table, so a reader that cares about edits should treat the last entry
 * as authoritative.
 */
class PatchFeedbackStore {
public:
    enum class Rating { Up, Down };

    // Real on-disk location: <user app data>/<kSettingsFolderName>/patch_feedback.jsonl
    PatchFeedbackStore();
    // Test injection point — mirrors DeviceIdStore's file-taking constructor.
    explicit PatchFeedbackStore(juce::File feedbackFile);

    // Appends one record. Creates the parent directory if needed. `patchJson` should be the
    // patch's raw JSON text (the same string PatchCard renders); malformed JSON is stored verbatim
    // under "patchRaw" rather than dropped.
    //
    // `conversationId`/`messageId` (P6-9) are the server-side ids this rating corresponds to, when
    // known — included as "conversationId"/"messageId" fields in the JSON line ONLY when
    // non-empty, so old log lines and offline/signed-out/free-tier entries keep their pre-P6-9
    // shape exactly (no empty-string fields added).
    void record(const juce::String& patchJson, Rating rating, const juce::String& comment = {},
                const juce::String& conversationId = {}, const juce::String& messageId = {});

private:
    juce::File file;
};

} // namespace synth
