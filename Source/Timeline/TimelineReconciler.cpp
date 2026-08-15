#include "TimelineReconciler.h"

#include <set>

namespace synth {

bool TimelineReconciler::reconcile(TimelineDoc& doc, const juce::AudioProcessorGraph& graph) {
    std::set<juce::String> liveUuids;
    for (auto* node : graph.getNodes()) {
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            liveUuids.insert(uuid);
    }

    return doc.reconcileBindings([&liveUuids](const juce::String& uuid) { return liveUuids.count(uuid) > 0; });
}

} // namespace synth
