#include "AudioEngine/AuditionMonitor.h"
#include "AudioEngine/ConnectionIndex.h"
#include "Modules/ModuleBase.h"
#include <vector>

namespace synth {

namespace {
void setFlag(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id, bool on) {
    if (auto* node = graph.getNodeForId(id))
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setAuditionAudible(on);
}
} // namespace

AuditionMonitor::AuditionMonitor(juce::AudioProcessorGraph& graph, std::function<bool()> playbackRunning)
    : graph_(graph)
    , playbackRunning_(std::move(playbackRunning)) {}

AuditionMonitor::~AuditionMonitor() { stopTimer(); }

// The flagged set is every node reachable by following cables forward from the source, so a send bus a strip feeds and
// the Master are audible too. Cables are read once per run; a cable drawn mid-run is picked up by the next one.
void AuditionMonitor::begin(NodeID source) {
    clear();
    const ConnectionIndex index(graph_);
    std::vector<NodeID> pending{source};
    flagged_.insert(source);
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        for (const auto& connection : index.outOf(node))
            if (flagged_.insert(connection.destination.nodeID).second)
                pending.push_back(connection.destination.nodeID);
    }
    for (const auto id : flagged_)
        setFlag(graph_, id, true);
    startTimer(kPollMs);
}

void AuditionMonitor::end() {
    if (!isActive())
        return;
    ended_ = true;
    clearAtMs_ = juce::Time::getMillisecondCounter() + (juce::uint32)kTailMs;
}

void AuditionMonitor::clear() {
    stopTimer();
    for (const auto id : flagged_)
        setFlag(graph_, id, false);
    flagged_.clear();
    ended_ = false;
}

// Polled while a run is active or its tail rings: the transport starting ends it, and so does the tail running out.
void AuditionMonitor::timerCallback() {
    const bool tailOver = ended_ && (juce::int32)(juce::Time::getMillisecondCounter() - clearAtMs_) >= 0;
    if (tailOver || (playbackRunning_ && playbackRunning_()))
        clear();
}

} // namespace synth
