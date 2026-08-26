#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/**
 * P6-10: local, append-only log of free-text feedback NOT tied to any specific AI-generated patch
 * (bug reports, feature requests, general comments) — the general-purpose sibling of
 * PatchFeedbackStore (P6-3, Source/AI/PatchFeedbackStore). Same shape/rationale: one JSON object
 * per line (JSON Lines), appended, never rewritten. No server endpoint exists yet — syncing this
 * follows the same deferred client/server split P6-3/P6-9 already used, so this stays local-only
 * until that follow-up task adds one. See docs/AI_Engine.md.
 */
class GeneralFeedbackStore {
public:
    enum class Category { Bug, Feature, Other };

    // Real on-disk location: <user app data>/<kSettingsFolderName>/general_feedback.jsonl
    GeneralFeedbackStore();
    // Test injection point — mirrors PatchFeedbackStore's file-taking constructor.
    explicit GeneralFeedbackStore(juce::File feedbackFile);

    // Appends one record. Creates the parent directory if needed. Callers are responsible for
    // rejecting empty text before calling this — the store itself records whatever it's given.
    void record(const juce::String& text, Category category = Category::Other);

private:
    juce::File file;
};

} // namespace synth
