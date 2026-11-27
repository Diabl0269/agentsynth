#pragma once

#include "AudioEngine/RenderCores.h"
#include "AudioEngine/TrackJobRunner.h"
#include "AudioEngine/TrackRenderPlan.h"
#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace synth {

/** The engine's multi-core render switch: owns the worker pool and the current TrackRenderPlan, and renders a live
 *  device block through them when it is on.
 *
 *  A plan never renders a topology other than the one JUCE's own render sequence has: every JUCE rebuild of the
 *  graph (synchronous inside addNode/removeConnection/..., or asynchronous) bumps a topology serial in the same call,
 *  through the graph I/O nodes' processor-changed callback (JUCE re-parents every I/O node on each rebuild), and the
 *  audio thread renders a block on the plan only while the plan's serial still matches; otherwise the block takes
 *  the single-thread graph until the rebuilt plan is published. */
class TrackWorkerRender
    : private juce::AudioProcessorListener
    , private juce::AsyncUpdater {
public:
    TrackWorkerRender();
    /** Message thread, no render in flight. */
    ~TrackWorkerRender() override;

    /** Message thread. 0 turns the worker path off; takes effect at once when prepared, else at the next prepare. */
    void setWorkerCount(int workers, juce::AudioProcessorGraph& graph);
    int workerCount() const noexcept { return workers_; }
    /** Message thread. How long an idle worker polls before it sleeps; applies to the next pool (device start or a
     *  count change). */
    void setIdleSpinMicros(int micros) noexcept { idleSpinMicros_ = std::max(0, micros); }
    /** Message thread: how many plans have been installed so far (each device start, count change and graph edit). */
    int plansBuilt() const noexcept { return plansBuilt_; }

    /** Message thread, graph just prepared, no render running: starts the pool and builds the plan. */
    void prepare(juce::AudioProcessorGraph& graph, const juce::AudioWorkgroup& workgroup, double sampleRate,
                 int blockSize, int ioChannels);
    /** Message thread: the graph's topology changed; rebuilds the plan. */
    void graphChanged(juce::AudioProcessorGraph& graph);
    /** Message thread: drops the plan and the pool (and the node references the plan holds). */
    void release();

    /** Audio thread. False when the caller must render the graph itself (off, no plan, or the block does not fit). */
    bool render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead) noexcept;

    /** Message thread: why the current plan cannot render; empty when it can or there is none. */
    juce::String planProblem() const;
    /** Message thread: true when a usable plan and a pool are installed. */
    bool isActive() const noexcept;
    /** True while the installed plan matches the graph's last rebuild, so the next block renders on the workers. */
    bool planIsCurrent() const noexcept;

private:
    // Any thread JUCE rebuilds the graph on: counts the rebuild and schedules the plan's.
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void handleAsyncUpdate() override;
    void followIoNodes(juce::AudioProcessorGraph& graph);
    void unfollowIoNodes();
    void rebuild(juce::AudioProcessorGraph& graph, bool newPool);
    void install(std::unique_ptr<TrackRenderPlan> plan, std::unique_ptr<TrackJobRunner> runner, bool replaceRunner);
    void drain() noexcept;

    int workers_ = 0;
    int idleSpinMicros_ = TrackJobRunner::kDefaultIdleSpinMicros;
    int plansBuilt_ = 0;
    bool prepared_ = false;
    juce::AudioProcessorGraph* graph_ = nullptr;
    juce::AudioWorkgroup workgroup_;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    int ioChannels_ = 0;

    std::unique_ptr<TrackRenderPlan> plan_;
    std::unique_ptr<TrackJobRunner> runner_;
    std::atomic<TrackRenderPlan*> livePlan_{nullptr};
    std::atomic<TrackJobRunner*> liveRunner_{nullptr};
    std::vector<juce::AudioProcessorGraph::Node::Ptr> ioNodes_; // the I/O nodes whose rebuild callback is followed
    std::atomic<std::uint64_t> topologySerial_{0};
    std::atomic<bool> rebuilding_{false};
    std::atomic<std::uint64_t> started_{0};
    std::atomic<std::uint64_t> finished_{0};
};

} // namespace synth
