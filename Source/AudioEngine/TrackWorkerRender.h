#pragma once

#include "AudioEngine/RenderCores.h"
#include "AudioEngine/TrackJobRunner.h"
#include "AudioEngine/TrackRenderPlan.h"
#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace synth {

/** The engine's multi-core render switch: owns the worker pool and the current TrackRenderPlan, and renders a live
 *  device block through them when it is on. Off (zero workers) by default. */
class TrackWorkerRender {
public:
    TrackWorkerRender();
    /** Message thread, no render in flight. */
    ~TrackWorkerRender();

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

private:
    void rebuild(juce::AudioProcessorGraph& graph, bool newPool);
    void install(std::unique_ptr<TrackRenderPlan> plan, std::unique_ptr<TrackJobRunner> runner, bool replaceRunner);
    void drain() noexcept;

    int workers_ = 0;
    int idleSpinMicros_ = TrackJobRunner::kDefaultIdleSpinMicros;
    int plansBuilt_ = 0;
    bool prepared_ = false;
    juce::AudioWorkgroup workgroup_;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    int ioChannels_ = 0;

    std::unique_ptr<TrackRenderPlan> plan_;
    std::unique_ptr<TrackJobRunner> runner_;
    std::atomic<TrackRenderPlan*> livePlan_{nullptr};
    std::atomic<TrackJobRunner*> liveRunner_{nullptr};
    std::atomic<std::uint64_t> started_{0};
    std::atomic<std::uint64_t> finished_{0};
};

} // namespace synth
