// Concern: hearing inputs live -- the per-pass context an audio track's Track Audio node monitors its own input
// from, and the gate of the hand-patched Audio Input module.
// docs/architecture/audio-engine.md#input-monitoring--feedback-guard

#include "AudioEngine.h"
#include "Modules/AudioInputModule.h"
#include <set>

// AUDIO THREAD, right after the device input is published. The map is the same one the input takes read (device
// input number -> packed snapshot index), so a monitored track hears exactly the channels it would record.
// "Recording" is the Record button's engaged flag or any input take capturing: with "wait for Play" the takes start
// from the 10 Hz poll after the transport already rolls, and Auto must not drop the input in between.
void AudioEngine::publishInputMonitorContext() noexcept {
    synth::TransportService::InputMonitorContext context;
    for (std::size_t channel = 0; channel < context.deviceToPacked.size(); ++channel)
        context.deviceToPacked[channel] = inputChannelToSnapshot_[channel].load(std::memory_order_relaxed);
    context.recording = recordingEngaged_.load(std::memory_order_relaxed) || inputRecorder_.isAnyCapturing();
    context.inputDeviceHash = isHosted() ? 0 : inputDeviceHash_.load(std::memory_order_relaxed);
    context.guardReports = &monitorGuardReports_;
    transport.setInputMonitorContextForBlock(context);
}

// The Audio Input module is for hand patches; tracks monitor through their own Track Audio node instead. Its rule:
// it passes the input while it has a cable out of it (patching it onward is asking to hear it) and is silent
// otherwise, so the default patch's unwired Audio Input keeps the mic out of the speakers. The engine-wide feedback
// guard is its backstop: a trip latches the gate shut until every Audio Input is unwired, then wired again -- the
// same "take your hand off and put it back" reset the old armed-track gate had.
void AudioEngine::refreshAudioInputGate() {
    std::set<juce::AudioProcessorGraph::NodeID> inputs;
    for (auto* node : mainProcessorGraph.getNodes())
        if (node != nullptr && dynamic_cast<AudioInputModule*>(node->getProcessor()) != nullptr)
            inputs.insert(node->nodeID);
    bool wired = false;
    if (!inputs.empty())
        for (const auto& connection : mainProcessorGraph.getConnections())
            if (inputs.count(connection.source.nodeID) != 0) {
                wired = true;
                break;
            }
    if (!wired)
        audioInputGuardLatched_.store(false, std::memory_order_relaxed);
    setInputMonitoringEnabled(wired && !audioInputGuardLatched_.load(std::memory_order_relaxed));
}
