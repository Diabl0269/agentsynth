// The multi-core render's two halves on their own (docs/architecture/audio-engine.md#multi-core-render-track-workers):
// synth::TrackJobRunner runs every job of a pass exactly once and never before its predecessors, on any number of
// workers; synth::TrackRenderPlan renders a graph byte-identically to juce::AudioProcessorGraph's own sequence
// (fan-in sum order, latency delays, MIDI, bypass, a feedback cable), however its nodes are cut into jobs.
#include "AudioEngine/TrackJobRunner.h"
#include "AudioEngine/TrackRenderPlan.h"
#include <atomic>
#include <bit>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

// ---- the runner ----------------------------------------------------------------------------------------------------

// A DAG of jobs that records the order they ran in: job j depends on every job listed in preds[j].
struct RecordingJobs : synth::TrackJobs {
    std::vector<std::vector<int>> preds, succs;
    std::vector<std::atomic<int>> order, runs;
    std::atomic<int> clock{0};

    explicit RecordingJobs(std::vector<std::vector<int>> predecessors)
        : preds(std::move(predecessors))
        , succs(preds.size())
        , order(preds.size())
        , runs(preds.size()) {
        for (size_t j = 0; j < preds.size(); ++j)
            for (int p : preds[j])
                succs[(size_t)p].push_back((int)j);
    }
    int numJobs() const noexcept override { return (int)preds.size(); }
    int numWaits(int job) const noexcept override { return (int)preds[(size_t)job].size(); }
    std::span<const int> successors(int job) const noexcept override { return succs[(size_t)job]; }
    void runJob(int job) noexcept override {
        volatile double burn = 0.0; // a little work so jobs genuinely overlap on the workers
        for (int i = 0; i < 200; ++i)
            burn = burn + std::sqrt((double)i);
        order[(size_t)job].store(clock.fetch_add(1), std::memory_order_relaxed);
        runs[(size_t)job].fetch_add(1, std::memory_order_relaxed);
    }
};

// The engine's shape: a shared source job, many independent strip jobs fed by it, and one post job after them all.
std::vector<std::vector<int>> trackShape(int strips) {
    std::vector<std::vector<int>> preds(1);
    for (int s = 0; s < strips; ++s)
        preds.push_back({0});
    std::vector<int> post;
    for (int s = 1; s <= strips; ++s)
        post.push_back(s);
    preds.push_back(post);
    return preds;
}

void expectPassesInOrder(int workers, std::vector<std::vector<int>> shape, int passes) {
    synth::TrackJobRunner runner(workers, juce::AudioWorkgroup{}, 512, 48000.0);
    RecordingJobs jobs(std::move(shape));
    for (int pass = 1; pass <= passes; ++pass) {
        ASSERT_TRUE(runner.run(jobs));
        for (size_t j = 0; j < jobs.preds.size(); ++j) {
            ASSERT_EQ(jobs.runs[j].load(), pass) << "job " << j << " pass " << pass;
            for (int p : jobs.preds[j])
                ASSERT_LT(jobs.order[(size_t)p].load(), jobs.order[j].load()) << "job " << j << " ran before " << p;
        }
    }
}

TEST(TrackJobRunnerTest, NoWorkersRunsEveryJobOnTheCaller) { expectPassesInOrder(0, trackShape(8), 50); }

TEST(TrackJobRunnerTest, ManyPassesRunEachJobOnceInDependencyOrder) {
    expectPassesInOrder(1, trackShape(20), 500);
    expectPassesInOrder(3, trackShape(20), 2000);
}

TEST(TrackJobRunnerTest, ChainsAndDiamondsKeepTheirOrder) {
    // 0 -> 1 -> 2 -> 3, plus a diamond 0 -> {4, 5} -> 6 -> 3.
    expectPassesInOrder(3, {{}, {0}, {1}, {2, 6}, {0}, {0}, {4, 5}}, 1000);
}

TEST(TrackJobRunnerTest, EmptyAndOversizePasses) {
    synth::TrackJobRunner runner(2, juce::AudioWorkgroup{}, 512, 48000.0);
    RecordingJobs none({});
    EXPECT_TRUE(runner.run(none));
    RecordingJobs tooMany(std::vector<std::vector<int>>((size_t)synth::TrackJobRunner::kMaxJobs + 1));
    EXPECT_FALSE(runner.run(tooMany));
    for (auto& r : tooMany.runs)
        ASSERT_EQ(r.load(), 0);
}

// ---- the plan against JUCE's own render ----------------------------------------------------------------------------

// Small deterministic processors: their outputs carry fractional values, so any change in summing order or a
// missing delay shows in the bits.
class TestProcessor : public juce::AudioProcessor {
public:
    TestProcessor(int ins, int outs, bool midiIn, bool midiOut)
        : juce::AudioProcessor(
              BusesProperties()
                  .withInput("in", juce::AudioChannelSet::discreteChannels(std::max(ins, 1)), ins > 0)
                  .withOutput("out", juce::AudioChannelSet::discreteChannels(std::max(outs, 1)), outs > 0))
        , midiIn_(midiIn)
        , midiOut_(midiOut) {}
    const juce::String getName() const override { return "Test"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return midiIn_; }
    bool producesMidi() const override { return midiOut_; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    bool midiIn_, midiOut_;
};

// A source: a phase ramp per channel, plus a step for every MIDI event it receives.
class Source final : public TestProcessor {
public:
    explicit Source(float rate)
        : TestProcessor(0, 2, true, false)
        , rate_(rate) {}
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override {
        const float bump = 0.013f * (float)midi.getNumEvents();
        for (int i = 0; i < b.getNumSamples(); ++i) {
            phase_ = std::fmod(phase_ + rate_, 1.0f);
            b.setSample(0, i, phase_ * 0.7f + bump);
            b.setSample(1, i, (1.0f - phase_) * 0.3f - bump);
        }
    }

private:
    float rate_, phase_ = 0.0f;
};

// An effect that reports `latency` and delays its audio by it, so JUCE compensates every parallel path.
class LatentGain final : public TestProcessor {
public:
    LatentGain(float gain, int latency)
        : TestProcessor(2, 2, false, false)
        , gain_(gain)
        , ring_((size_t)std::max(1, latency) * 2, 0.0f) {
        setLatencySamples(latency);
    }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        const int latency = getLatencySamples();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i) {
                const float in = b.getSample(ch, i) * gain_ + 0.1f * b.getSample(1 - ch, i);
                if (latency == 0) {
                    b.setSample(ch, i, in);
                    continue;
                }
                auto& slot = ring_[(size_t)ch * (size_t)latency + (size_t)pos_[ch]];
                b.setSample(ch, i, slot);
                slot = in;
                pos_[ch] = (pos_[ch] + 1) % latency;
            }
    }

private:
    float gain_;
    std::vector<float> ring_;
    int pos_[2] = {0, 0};
};

// A MIDI generator: a note on every `period` samples, at a sample offset that moves block to block.
class NoteMaker final : public TestProcessor {
public:
    explicit NoteMaker(int period)
        : TestProcessor(0, 0, false, true)
        , period_(period) {}
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override {
        midi.clear();
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (++count_ % period_ == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 40 + (count_ / period_) % 24, (juce::uint8)100), i);
    }

private:
    int period_, count_ = 0;
};

using NodeID = juce::AudioProcessorGraph::NodeID;
using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;

// One graph shape: three sources fan in (3 + 1 feeders) to a mixer, a latent branch runs beside a plain one into
// the output, MIDI flows from a generator and the graph's MIDI input, one node is bypassed, and one cable feeds
// back from a later node.
struct EquivalenceRig {
    juce::AudioProcessorGraph graph;
    std::vector<NodeID> ids;

    explicit EquivalenceRig(bool bypassOne) {
        graph.setPlayConfigDetails(2, 2, 48000.0, 512); // first: the IO nodes take their channel counts from it
        auto add = [this](std::unique_ptr<juce::AudioProcessor> p) {
            ids.push_back(graph.addNode(std::move(p))->nodeID);
            return ids.back();
        };
        const auto in = add(std::make_unique<IO>(IO::audioInputNode));
        const auto out = add(std::make_unique<IO>(IO::audioOutputNode));
        const auto midiIn = add(std::make_unique<IO>(IO::midiInputNode));
        const auto midiOut = add(std::make_unique<IO>(IO::midiOutputNode));
        const auto notes = add(std::make_unique<NoteMaker>(97));
        const auto srcA = add(std::make_unique<Source>(0.0123f));
        const auto srcB = add(std::make_unique<Source>(0.0371f));
        const auto srcC = add(std::make_unique<Source>(0.0057f));
        const auto mix = add(std::make_unique<LatentGain>(0.61f, 0));
        const auto latent = add(std::make_unique<LatentGain>(1.37f, 37));
        const auto plain = add(std::make_unique<LatentGain>(0.83f, 0));
        const auto tail = add(std::make_unique<LatentGain>(0.5f, 5));
        const auto bypassed = add(std::make_unique<LatentGain>(3.0f, 0));
        auto connect = [this](NodeID a, int ca, NodeID b, int cb) { graph.addConnection({{a, ca}, {b, cb}}); };
        const int midi = juce::AudioProcessorGraph::midiChannelIndex;
        connect(midiIn, midi, srcA, midi);
        connect(notes, midi, srcB, midi);
        connect(notes, midi, srcC, midi);
        connect(midiIn, midi, srcC, midi);
        connect(notes, midi, midiOut, midi);
        for (int ch = 0; ch < 2; ++ch) {
            connect(srcA, ch, mix, ch);
            connect(srcB, ch, mix, ch);
            connect(srcC, ch, mix, ch);
            connect(in, ch, mix, ch);
            connect(mix, ch, latent, ch);
            connect(mix, ch, plain, ch);
            connect(latent, ch, tail, ch);
            connect(plain, ch, tail, ch);
            connect(srcB, ch, tail, ch);
            connect(tail, ch, bypassed, ch);
            connect(bypassed, ch, out, ch);
            connect(srcC, ch, out, ch);
        }
        connect(tail, 0, srcA, 0);    // feedback: srcA has no audio input, so JUCE refuses it, as does a real patch
        connect(bypassed, 1, mix, 1); // feedback into an earlier node: reads silence in both renders
        if (bypassOne)
            graph.getNodeForId(bypassed)->setBypassed(true);
        graph.prepareToPlay(48000.0, 512);
        graph.rebuild();
    }
};

struct BlockOut {
    std::vector<float> audio;
    std::vector<std::pair<int, int>> midi; // (sample, note)
};

// The same deterministic input every run: a ramp on the audio inputs and a note on the MIDI input.
void fillInput(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi, int block) {
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(ch, i, 0.001f * (float)((block * 7 + i + ch * 3) % 113));
    midi.clear();
    if (block % 3 == 0)
        midi.addEvent(juce::MidiMessage::noteOn(2, 60 + block % 12, (juce::uint8)90), b.getNumSamples() / 2);
}

BlockOut capture(const juce::AudioBuffer<float>& b, const juce::MidiBuffer& midi) {
    BlockOut out;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            out.audio.push_back(b.getSample(ch, i));
    for (const auto m : midi)
        out.midi.emplace_back(m.samplePosition, m.getMessage().getNoteNumber());
    return out;
}

constexpr int kSizes[] = {512, 37, 128, 1, 300, 512, 64, 511};

std::vector<BlockOut> renderWithJuce(bool bypassOne, int blocks) {
    EquivalenceRig rig(bypassOne);
    std::vector<BlockOut> outs;
    juce::AudioBuffer<float> b(2, 512);
    juce::MidiBuffer midi;
    for (int k = 0; k < blocks; ++k) {
        b.setSize(2, kSizes[k % std::size(kSizes)], false, false, true);
        fillInput(b, midi, k);
        rig.graph.processBlock(b, midi);
        outs.push_back(capture(b, midi));
    }
    return outs;
}

std::vector<BlockOut> renderWithPlan(bool bypassOne, int blocks, int workers, bool ownJobs) {
    EquivalenceRig rig(bypassOne);
    std::vector<std::vector<NodeID>> groups;
    if (ownJobs) {
        // The graph's audio and MIDI outputs share one job (a plan refuses them apart); every other node is alone.
        groups.push_back({rig.ids[1], rig.ids[3]});
        for (size_t i = 0; i < rig.ids.size(); ++i)
            if (i != 1 && i != 3)
                groups.push_back({rig.ids[i]});
    } else
        groups.push_back(rig.ids);
    auto plan = synth::TrackRenderPlan::build(rig.graph, groups, 512, 2);
    EXPECT_TRUE(plan->usable()) << plan->problem();
    if (!plan->usable())
        return {};
    synth::TrackJobRunner runner(workers, juce::AudioWorkgroup{}, 512, 48000.0);
    std::vector<BlockOut> outs;
    juce::AudioBuffer<float> b(2, 512);
    juce::MidiBuffer midi;
    for (int k = 0; k < blocks; ++k) {
        b.setSize(2, kSizes[k % std::size(kSizes)], false, false, true);
        fillInput(b, midi, k);
        EXPECT_TRUE(plan->fits(b));
        plan->beginPass(b, midi, nullptr);
        EXPECT_TRUE(runner.run(*plan));
        plan->endPass(b, midi);
        outs.push_back(capture(b, midi));
    }
    return outs;
}

void expectSameBits(const std::vector<BlockOut>& a, const std::vector<BlockOut>& b) {
    ASSERT_EQ(a.size(), b.size());
    for (size_t k = 0; k < a.size(); ++k) {
        ASSERT_EQ(a[k].audio.size(), b[k].audio.size()) << "block " << k;
        for (size_t i = 0; i < a[k].audio.size(); ++i)
            ASSERT_EQ(std::bit_cast<std::uint32_t>(a[k].audio[i]), std::bit_cast<std::uint32_t>(b[k].audio[i]))
                << "block " << k << " sample " << i << ": " << a[k].audio[i] << " vs " << b[k].audio[i];
        ASSERT_EQ(a[k].midi, b[k].midi) << "block " << k;
    }
}

TEST(TrackRenderPlanTest, OneJobMatchesTheGraphBitForBit) {
    const auto reference = renderWithJuce(false, 200);
    double energy = 0.0;
    for (const auto& block : reference)
        for (float v : block.audio)
            energy += (double)v * v;
    ASSERT_GT(energy, 1.0); // the rig really makes sound, so equality is not two silences
    expectSameBits(reference, renderWithPlan(false, 200, 0, false));
}

TEST(TrackRenderPlanTest, EveryNodeItsOwnJobOnWorkersMatchesTheGraph) {
    const auto reference = renderWithJuce(false, 400);
    expectSameBits(reference, renderWithPlan(false, 400, 0, true));
    expectSameBits(reference, renderWithPlan(false, 400, 3, true));
}

TEST(TrackRenderPlanTest, BypassedNodeMatchesTheGraph) {
    expectSameBits(renderWithJuce(true, 200), renderWithPlan(true, 200, 3, true));
}

TEST(TrackRenderPlanTest, RefusesBlocksItWasNotSizedFor) {
    EquivalenceRig rig(false);
    auto plan = synth::TrackRenderPlan::build(rig.graph, {rig.ids}, 256, 2);
    ASSERT_TRUE(plan->usable()) << plan->problem();
    juce::AudioBuffer<float> fits(2, 256), tooLong(2, 257), tooWide(3, 64);
    EXPECT_TRUE(plan->fits(fits));
    EXPECT_FALSE(plan->fits(tooLong));
    EXPECT_FALSE(plan->fits(tooWide));
}

TEST(TrackRenderPlanTest, OutputsInDifferentJobsAreRefused) {
    EquivalenceRig rig(false);
    std::vector<NodeID> rest;
    for (size_t i = 0; i < rig.ids.size(); ++i)
        if (i != 3)
            rest.push_back(rig.ids[i]);
    EXPECT_FALSE(synth::TrackRenderPlan::build(rig.graph, {rest, {rig.ids[3]}}, 512, 2)->usable());
}

TEST(TrackRenderPlanTest, GroupsThatMissOrRepeatANodeAreRefused) {
    EquivalenceRig rig(false);
    auto missing = rig.ids;
    missing.pop_back();
    EXPECT_FALSE(synth::TrackRenderPlan::build(rig.graph, {missing}, 512, 2)->usable());
    EXPECT_FALSE(synth::TrackRenderPlan::build(rig.graph, {rig.ids, {rig.ids.front()}}, 512, 2)->usable());
}

TEST(TrackRenderPlanTest, JobsThatDependOnEachOtherBothWaysAreRefused) {
    // source -> first -> second -> output, with the source and the second gain in one job and the first gain in
    // another: each job feeds the other, a loop no order of jobs can run.
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000.0, 512);
    const auto out = graph.addNode(std::make_unique<IO>(IO::audioOutputNode))->nodeID;
    const auto src = graph.addNode(std::make_unique<Source>(0.01f))->nodeID;
    const auto first = graph.addNode(std::make_unique<LatentGain>(0.5f, 0))->nodeID;
    const auto second = graph.addNode(std::make_unique<LatentGain>(0.7f, 0))->nodeID;
    for (int ch = 0; ch < 2; ++ch) {
        ASSERT_TRUE(graph.addConnection({{src, ch}, {first, ch}}));
        ASSERT_TRUE(graph.addConnection({{first, ch}, {second, ch}}));
        ASSERT_TRUE(graph.addConnection({{second, ch}, {out, ch}}));
    }
    graph.prepareToPlay(48000.0, 512);
    graph.rebuild();
    EXPECT_TRUE(synth::TrackRenderPlan::build(graph, {{src, first}, {second, out}}, 512, 2)->usable());
    EXPECT_FALSE(synth::TrackRenderPlan::build(graph, {{src, second, out}, {first}}, 512, 2)->usable());
}

} // namespace
