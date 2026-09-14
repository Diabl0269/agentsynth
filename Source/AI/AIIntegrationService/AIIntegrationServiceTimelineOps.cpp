// Timeline operations: the trust-boundary seam for AI-authored timeline/automation edits —
// extract -> validate (untrusted, via TimelineOps::validate) -> apply through the host callback.
#include "AIIntegrationService.h"

namespace synth {

juce::var AIIntegrationService::extractTimelineOps(const juce::String& response) {
    const juce::var parsed = juce::JSON::parse(extractJsonFromResponse(response));
    // Presence, not well-formedness: a malformed "timelineOps" must reach previewTimelineOps() and
    // be reported, never be quietly dropped as if the model had asked for nothing (the same rule
    // that keeps applyPatch from swallowing a rejected patch).
    return TimelineOps::carriesOps(parsed) ? parsed : juce::var();
}

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
