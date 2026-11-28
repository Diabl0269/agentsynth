#pragma once

#include "AudioEngine/TrackJobRunner.h"
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <span>
#include <vector>

namespace synth {

/** The graph's render rebuilt over the same nodes as juce::AudioProcessorGraph's own sequence (same node order,
 *  same per-node processBlock calls, same fan-in summing order and latency delays), cut into jobs that
 *  TrackJobRunner may run on different threads. Built on the message thread; rendered on the audio thread. */
class TrackRenderPlan final : public TrackJobs {
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    /** Message thread, `graph` prepared. `groups` puts every node in exactly one job. Never null: check usable(). */
    static std::unique_ptr<TrackRenderPlan> build(const juce::AudioProcessorGraph& graph,
                                                  const std::vector<std::vector<NodeID>>& groups, int maxBlockSize,
                                                  int maxIoChannels);
    ~TrackRenderPlan() override;

    /** Empty when the plan can render; otherwise why not. */
    const juce::String& problem() const noexcept { return problem_; }
    bool usable() const noexcept { return problem_.isEmpty(); }
    /** Audio thread: true when a block of this size and width can be rendered. */
    bool fits(const juce::AudioBuffer<float>& io) const noexcept;
    /** The graph topology serial the plan was built at (TrackWorkerRender's count of JUCE rebuilds); set once on the
     *  message thread before the plan is published, read by the audio thread. */
    void setTopologySerial(std::uint64_t serial) noexcept { topologySerial_ = serial; }
    std::uint64_t topologySerial() const noexcept { return topologySerial_; }

    /** Audio thread, before the jobs run; `io` and `midi` are read by the jobs and must stay alive until endPass. */
    void beginPass(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead) noexcept;
    /** Audio thread, after every job has run: writes the graph's audio and MIDI output into `io` and `midi`. */
    void endPass(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi) noexcept;

    int numJobs() const noexcept override;
    int numWaits(int job) const noexcept override;
    std::span<const int> successors(int job) const noexcept override;
    void runJob(int job) noexcept override;

    struct Step;
    struct Job;
    struct DelayLine;

private:
    TrackRenderPlan();
    void renderStep(Step& step, Job& job) noexcept;

    juce::String problem_;
    std::uint64_t topologySerial_ = 0;
    int maxBlockSize_ = 0;
    int maxIoChannels_ = 0;
    std::vector<std::unique_ptr<Step>> steps_;
    std::vector<std::unique_ptr<Job>> jobs_;
    std::vector<std::unique_ptr<DelayLine>> delays_;
    juce::AudioBuffer<float> slots_;
    juce::AudioBuffer<float> audioOut_;
    juce::MidiBuffer midiOut_;

    int numSamples_ = 0;
    const juce::AudioBuffer<float>* audioIn_ = nullptr;
    const juce::MidiBuffer* midiIn_ = nullptr;
    juce::AudioPlayHead* playHead_ = nullptr;
    int outChannels_ = 0;
    bool audioOutClear_ = true;

    friend struct TrackRenderPlanBuilder;
};

} // namespace synth
