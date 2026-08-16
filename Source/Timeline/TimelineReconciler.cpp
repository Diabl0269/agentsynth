#include "TimelineReconciler.h"

#include "../Plugin/Hosting/HostedPluginModule.h"
#include "AutomationBinding.h"
#include <map>

namespace synth {

bool TimelineReconciler::reconcile(TimelineDoc& doc, const juce::AudioProcessorGraph& graph) {
    // uuid -> processor, built once: TL7-6's per-lane check needs the processor (to test whether the
    // node is a HostedPluginModule), not just whether the uuid exists.
    std::map<juce::String, juce::AudioProcessor*> processorByUuid;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            processorByUuid.emplace(uuid, node->getProcessor());
    }

    auto uuidResolves = [&processorByUuid](const juce::String& uuid) { return processorByUuid.count(uuid) > 0; };

    // TL7-6: a lane's node existing is not the whole story once that node is a HostedPluginModule —
    // its parameter SET can have changed shape since the lane was created (a plugin update). See
    // Source/Timeline/AutomationBinding.h for the keying/fallback/orphan rule this delegates to.
    // Non-plugin nodes are the UNCHANGED path: the node resolving is the whole story for orphaning,
    // exactly as it was before TL7-6 — a lane whose paramId has gone missing on one of OUR OWN
    // modules is merely unbound, never orphaned by this pass.
    auto laneResolves = [&processorByUuid](const juce::String& uuid, const juce::String& paramId, int paramIndexHint) {
        const auto found = processorByUuid.find(uuid);
        if (found == processorByUuid.end())
            return false;
        if (dynamic_cast<HostedPluginModule*>(found->second) == nullptr)
            return true;
        return !resolveLaneParameter(found->second, paramId, paramIndexHint).orphaned;
    };

    return doc.reconcileBindings(uuidResolves, laneResolves);
}

} // namespace synth
