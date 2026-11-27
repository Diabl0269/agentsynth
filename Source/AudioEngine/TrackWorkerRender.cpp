// Concern: the multi-core render switch: building and swapping the plan and the worker pool, and the audio
// thread's render through them (TrackWorkerRender.h).
#include "AudioEngine/TrackWorkerRender.h"
#include "AudioEngine/TrackCut.h"

namespace synth {

namespace {
std::vector<std::vector<juce::AudioProcessorGraph::NodeID>> jobGroups(const juce::AudioProcessorGraph& graph) {
    std::vector<std::vector<juce::AudioProcessorGraph::NodeID>> groups;
    for (auto& job : cutIntoTrackJobs(graph).jobs)
        groups.push_back(std::move(job.nodes));
    return groups;
}
} // namespace

TrackWorkerRender::TrackWorkerRender() = default;

TrackWorkerRender::~TrackWorkerRender() { release(); }

void TrackWorkerRender::setWorkerCount(int workers, juce::AudioProcessorGraph& graph) {
    workers = std::max(0, workers);
    if (workers == workers_)
        return;
    workers_ = workers;
    if (workers_ == 0)
        install(nullptr, nullptr, true);
    else if (prepared_)
        rebuild(graph, true);
}

// Called from the device's about-to-start, after the graph's own prepareToPlay, so every node is prepared before
// the plan can reach it. The pool is rebuilt every time: its threads join the workgroup of the device starting now.
void TrackWorkerRender::prepare(juce::AudioProcessorGraph& graph, const juce::AudioWorkgroup& workgroup,
                                double sampleRate, int blockSize, int ioChannels) {
    workgroup_ = workgroup;
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    ioChannels_ = ioChannels;
    prepared_ = true;
    if (workers_ > 0)
        rebuild(graph, true);
}

// The topology broadcast arrives asynchronously, possibly before JUCE's own (also asynchronous) rebuild has
// prepared a node just added; rebuild() runs that first, so the new plan never reaches an unprepared node. Until
// this runs the old plan keeps rendering the old topology, exactly as JUCE's own render sequence does until its
// rebuild lands.
void TrackWorkerRender::graphChanged(juce::AudioProcessorGraph& graph) {
    if (workers_ > 0 && prepared_)
        rebuild(graph, false);
}

void TrackWorkerRender::release() {
    prepared_ = false;
    install(nullptr, nullptr, true);
}

void TrackWorkerRender::rebuild(juce::AudioProcessorGraph& graph, bool newPool) {
    graph.rebuild();
    auto plan = TrackRenderPlan::build(graph, jobGroups(graph), blockSize_, ioChannels_);
    std::unique_ptr<TrackJobRunner> runner;
    if (newPool || runner_ == nullptr)
        runner = std::make_unique<TrackJobRunner>(workers_, workgroup_, blockSize_, sampleRate_, idleSpinMicros_);
    install(std::move(plan), std::move(runner), newPool || runner_ == nullptr);
    ++plansBuilt_;
}

// The audio thread must never see a plan or pool being freed: both live pointers go null first, then the render
// that may already have loaded them is waited out, and only then are the old ones destroyed and the new ones
// published. A block that lands in between renders on the single-thread graph, which is always current.
void TrackWorkerRender::install(std::unique_ptr<TrackRenderPlan> plan, std::unique_ptr<TrackJobRunner> runner,
                                bool replaceRunner) {
    livePlan_.store(nullptr, std::memory_order_seq_cst);
    if (replaceRunner)
        liveRunner_.store(nullptr, std::memory_order_seq_cst);
    drain();
    plan_ = std::move(plan);
    if (replaceRunner)
        runner_ = std::move(runner);
    liveRunner_.store(runner_.get(), std::memory_order_seq_cst);
    livePlan_.store(plan_ != nullptr && plan_->usable() ? plan_.get() : nullptr, std::memory_order_seq_cst);
}

// Started/finished counters rather than an in-flight count, for the same reason as the engine's own render-pass
// drain: a device thread renders back to back, so "has the render that had started finished?" always completes.
void TrackWorkerRender::drain() noexcept {
    const auto target = started_.load(std::memory_order_seq_cst);
    const auto deadline = juce::Time::getMillisecondCounter() + 2000u;
    int spins = 0;
    while (finished_.load(std::memory_order_acquire) < target) {
        if (juce::Time::getMillisecondCounter() > deadline) {
            jassertfalse; // a render outlasted the drain: something is holding the audio thread
            return;
        }
        if (++spins < 1000)
            juce::Thread::yield();
        else
            juce::Thread::sleep(1);
    }
}

// seq_cst on the entry count and both loads pairs with install(): either install sees this render started and
// waits for it, or this render sees the nulled pointers.
bool TrackWorkerRender::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                               juce::AudioPlayHead* playHead) noexcept {
    started_.fetch_add(1, std::memory_order_seq_cst);
    auto* plan = livePlan_.load(std::memory_order_seq_cst);
    auto* runner = liveRunner_.load(std::memory_order_seq_cst);
    bool rendered = false;
    if (plan != nullptr && runner != nullptr && plan->fits(buffer)) {
        plan->beginPass(buffer, midi, playHead);
        rendered = runner->run(*plan);
        if (rendered)
            plan->endPass(buffer, midi);
    }
    finished_.fetch_add(1, std::memory_order_release);
    return rendered;
}

juce::String TrackWorkerRender::planProblem() const { return plan_ != nullptr ? plan_->problem() : juce::String(); }

bool TrackWorkerRender::isActive() const noexcept {
    return livePlan_.load(std::memory_order_relaxed) != nullptr && liveRunner_.load(std::memory_order_relaxed);
}

} // namespace synth
