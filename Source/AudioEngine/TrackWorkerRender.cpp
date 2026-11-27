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

TrackWorkerRender::~TrackWorkerRender() {
    release();
    cancelPendingUpdate();
}

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
    graph_ = &graph;
    workgroup_ = workgroup;
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    ioChannels_ = ioChannels;
    prepared_ = true;
    if (workers_ > 0)
        rebuild(graph, true);
}

// The topology broadcast arrives asynchronously, possibly before JUCE's own (also asynchronous) rebuild has
// prepared a node just added; rebuild() runs that first, so the new plan never reaches an unprepared node. Between
// the edit and this call the old plan is no longer current (the edit's JUCE rebuild bumped the serial), so those
// blocks render on the single-thread graph, exactly as with the workers off.
void TrackWorkerRender::graphChanged(juce::AudioProcessorGraph& graph) {
    if (workers_ > 0 && prepared_)
        rebuild(graph, false);
}

void TrackWorkerRender::release() {
    prepared_ = false;
    install(nullptr, nullptr, true);
    unfollowIoNodes();
    graph_ = nullptr;
}

// JUCE's rebuild (Pimpl::handleAsyncUpdate) calls AudioGraphIOProcessor::setParentGraph on every I/O node before it
// hands the new render sequence to the audio thread, and that calls updateHostDisplay(), so this runs inside the
// very call that changes the topology. Counting it there is what keeps a stale plan off the next block. A spurious
// call only costs a block or two on one core until the rebuilt plan lands.
void TrackWorkerRender::audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) {
    topologySerial_.fetch_add(1, std::memory_order_seq_cst);
    if (!rebuilding_.load(std::memory_order_relaxed))
        triggerAsyncUpdate(); // a rebuild with no topology broadcast (graph.rebuild()) still gets its plan
}

void TrackWorkerRender::handleAsyncUpdate() {
    if (graph_ != nullptr && workers_ > 0 && prepared_ && !planIsCurrent())
        rebuild(*graph_, false);
}

void TrackWorkerRender::followIoNodes(juce::AudioProcessorGraph& graph) {
    unfollowIoNodes();
    for (auto* node : graph.getNodes())
        if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(node->getProcessor()) != nullptr) {
            node->getProcessor()->addListener(this);
            ioNodes_.emplace_back(node);
        }
}

void TrackWorkerRender::unfollowIoNodes() {
    for (auto& node : ioNodes_)
        node->getProcessor()->removeListener(this);
    ioNodes_.clear();
}

// The serial is read after graph.rebuild() and before the plan is cut, on the message thread every topology edit runs
// on, so the plan is built from exactly the nodes and cables JUCE's sequence at that serial was.
void TrackWorkerRender::rebuild(juce::AudioProcessorGraph& graph, bool newPool) {
    rebuilding_.store(true, std::memory_order_relaxed);
    followIoNodes(graph);
    graph.rebuild();
    cancelPendingUpdate();
    const auto serial = topologySerial_.load(std::memory_order_seq_cst);
    rebuilding_.store(false, std::memory_order_relaxed);
    auto plan = TrackRenderPlan::build(graph, jobGroups(graph), blockSize_, ioChannels_);
    plan->setTopologySerial(serial);
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
    // Without an I/O node to follow, a topology edit could not be seen in time, so such a plan never goes live.
    const bool live = plan_ != nullptr && plan_->usable() && !ioNodes_.empty();
    livePlan_.store(live ? plan_.get() : nullptr, std::memory_order_seq_cst);
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
// waits for it, or this render sees the nulled pointers. The serial check makes a block that starts after a topology
// edit returned render on the single-thread graph until the plan for that topology is published.
bool TrackWorkerRender::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                               juce::AudioPlayHead* playHead) noexcept {
    started_.fetch_add(1, std::memory_order_seq_cst);
    auto* plan = livePlan_.load(std::memory_order_seq_cst);
    auto* runner = liveRunner_.load(std::memory_order_seq_cst);
    bool rendered = false;
    if (plan != nullptr && runner != nullptr && plan->fits(buffer) &&
        plan->topologySerial() == topologySerial_.load(std::memory_order_seq_cst)) {
        plan->beginPass(buffer, midi, playHead);
        rendered = runner->run(*plan);
        if (rendered)
            plan->endPass(buffer, midi);
    }
    finished_.fetch_add(1, std::memory_order_release);
    return rendered;
}

bool TrackWorkerRender::planIsCurrent() const noexcept {
    const auto* plan = livePlan_.load(std::memory_order_seq_cst);
    return plan != nullptr && plan->topologySerial() == topologySerial_.load(std::memory_order_seq_cst);
}

juce::String TrackWorkerRender::planProblem() const {
    if (plan_ == nullptr)
        return {};
    if (plan_->usable() && ioNodes_.empty())
        return "the graph has no I/O node, so its topology edits cannot be followed";
    return plan_->problem();
}

bool TrackWorkerRender::isActive() const noexcept {
    return livePlan_.load(std::memory_order_relaxed) != nullptr && liveRunner_.load(std::memory_order_relaxed);
}

} // namespace synth
