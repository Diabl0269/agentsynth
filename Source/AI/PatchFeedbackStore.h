#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/**
 * P6-3: local, append-only log of thumbs up/down feedback on AI-generated patches. This is the
 * client-only half of P6-3 — there is no server endpoint yet to send it to (packages/conversations'
 * ConversationMessage has no rating field, and the client doesn't hold a conversation id to key one
 * on until P6-8 ships), so entries live only on this device until a future task wires up sync. See
 * docs/AI_Engine.md.
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
    void record(const juce::String& patchJson, Rating rating, const juce::String& comment = {});

private:
    juce::File file;
};

} // namespace synth
