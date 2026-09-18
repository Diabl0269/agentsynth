// Timeline operations: the trust-boundary seam for AI-authored timeline/automation edits —
// extract -> validate (untrusted, via TimelineOps::validate) -> preview to the user -> the user
// clicks Apply -> apply through the host callback. A deliberate mirror of the patch card's flow: a
// timelineOps envelope is a SIBLING of a patch suggestion, never nested inside one, so a single
// response may legitimately carry both — each half gets its own gate and its own button. (Also
// documented in docs/AI_Engine_patch_safety.md §9 "Sibling, never nested".)
#include "AIIntegrationService.h"

namespace synth {

// Runs exactly the same extraction applyPatch() does (extractJsonFromResponse: fenced block, bare
// braces, or the whole body), then hands back the parsed ROOT — envelope and patch share one JSON
// object when the model sends both, so this is the same var the patch path parses.
juce::var AIIntegrationService::extractTimelineOps(const juce::String& response) {
    const juce::var parsed = juce::JSON::parse(extractJsonFromResponse(response));
    // Presence, not well-formedness: a malformed "timelineOps" must reach previewTimelineOps() and
    // be reported, never be quietly dropped as if the model had asked for nothing (the same rule
    // that keeps applyPatch from swallowing a rejected patch).
    return TimelineOps::carriesOps(parsed) ? parsed : juce::var();
}

// The preview step: on success, result.previewText is what the chat card shows the user before
// they agree to anything.
TimelineOpsResult AIIntegrationService::previewTimelineOps(const juce::var& envelope) const {
    if (timelineDoc == nullptr)
        return {false, "This build has no timeline wired in, so timeline changes cannot be checked or applied.", {}};

    return TimelineOps::validate(envelope, *timelineDoc, audioGraph);
}

TimelineOpsResult AIIntegrationService::applyTimelineOps(const juce::var& envelope) {
    if (!timelineOpsApply)
        return {false, "Timeline changes cannot be applied from here.", {}};

    const auto result = timelineOpsApply(envelope);
    // One log per Apply click — user-click frequency, so it stays within the no-high-frequency
    // logging rule, and in Debug builds it lands in AIChatComponent's console.
    juce::Logger::writeToLog(juce::String("applyTimelineOps ") + (result.ok ? "applied: " : "rejected: ") +
                             result.message);
    return result;
}

} // namespace synth
