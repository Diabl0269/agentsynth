// RandombientPanLegsTests.cpp
//
// Kind: jack-raw-mismatch / leg-asymmetry. A saved project (Tests/fixtures/randombient-saw.agsproj: a poly Oscillator
// with unison 7, width 1.0 and random phase -> VCA -> bypassed Filter/Gate/EQ/Compressor -> a Stereo Channel Strip ->
// EQ -> Macro Out -> Master) sounded full with the strip's pan hard left and much quieter hard right. A stereo strip's
// pan is a balance, so the strip's Left output at pan -1 must mirror its Right output at pan +1.
//
// The project opens through the app's own open path (so repairSavedJackEdges runs), a note is held on the Track In, and
// a probe sink per node of the chain reads every output channel offline. The first test LOGS the edges after load and
// a per-node per-channel level table (run it with --gtest_filter='RandombientPanLegs*' and read the output); the second
// is the guard (per-leg level of the mirrored pans within 1.5 dB).

#include "../FakeAudioIODevice.h"
#include "../UniqueTempDir.h"
#include "../fixtures/saved-project/SavedProjectFixture.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "TestPaths.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
using Node = juce::AudioProcessorGraph::Node;

// Project node id (in the saved file) -> uuid; the loaded graph reassigns node ids.
const std::vector<std::pair<int, const char*>> kChain = {
    {18, "5dd854ee-3736-4b47-b75d-97a5ede7c2e6"}, // Oscillator
    {20, "64dda5ca-3405-4576-a138-9c0ce3df8f33"}, // VCA
    {21, "74e729a6-696d-454c-bafd-2f5e67cbc755"}, // Filter (bypassed)
    {22, "9591f378-9c7d-40e0-9abc-bd27c992facf"}, // Gate (bypassed)
    {23, "63de82eb-456e-4923-b1f0-8813cbf0c343"}, // EQ (bypassed)
    {24, "61e241b1-71ea-43ad-9948-cb2e8a44126d"}, // Compressor (bypassed)
    {25, "cf849b6f-5fe0-4328-915b-97f40131fcee"}, // Channel Strip
    {88, "6a924254-a353-43e7-a895-1a3dcc37261a"}, // EQ, bands off
    {27, "22c2171a-2bc9-4799-b4af-b96c0d8e0b8c"}, // Macro Out
};
constexpr const char* kTrackInUuid = "5014d5ca-6fa2-4602-9b36-f04789fdbd7d";

/** A sink that accumulates the sum of squares of every input channel it is given. */
class ProbeSink : public juce::AudioProcessor {
public:
    explicit ProbeSink(int channels)
        : juce::AudioProcessor(
              BusesProperties().withInput("in", juce::AudioChannelSet::discreteChannels(channels), true))
        , sums_((size_t)channels, 0.0) {}
    const juce::String getName() const override { return "ProbeSink"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        for (int ch = 0; ch < b.getNumChannels() && ch < (int)sums_.size(); ++ch) {
            const float* p = b.getReadPointer(ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                sums_[(size_t)ch] += (double)p[i] * p[i];
        }
        samples_ += b.getNumSamples();
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void reset() {
        std::fill(sums_.begin(), sums_.end(), 0.0);
        samples_ = 0;
    }
    std::vector<double> powers() const {
        std::vector<double> out;
        for (double s : sums_)
            out.push_back(samples_ > 0 ? s / (double)samples_ : 0.0);
        return out;
    }

private:
    std::vector<double> sums_;
    long samples_ = 0;
};

using Powers = std::vector<double>; // mean square per channel

struct Rig {
    juce::File bundle = synth::test::uniqueTempPath("randombient", ".agsproj");
    std::unique_ptr<MainComponent> mc;
    std::map<int, ProbeSink*> probes; // by saved-project node id
    std::map<juce::String, int> idByUuid;
    Node* trackIn = nullptr;
    synth::test::FakeAudioIODevice fake{0, 2};

    Rig() {
        const juce::File src(juce::String(TESTS_ROOT_DIR) + "/fixtures/randombient-saw.agsproj");
        if (!src.copyDirectoryTo(bundle))
            return;
        mc = std::make_unique<MainComponent>(std::make_unique<synth::test::SavedProjectFixtureProvider>());
        mc->setSize(1400, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->getTimelinePanel().setSize(1400, 360);
        if (!mc->openProjectForTest(bundle)) {
            mc.reset();
            return;
        }
        auto& g = graph();
        for (auto& [id, uuid] : kChain)
            idByUuid[uuid] = id;
        std::vector<std::pair<Node::Ptr, int>> targets; // collected first: addNode invalidates getNodes() iteration
        for (auto* n : g.getNodes()) {
            const auto uuid = synth::AIStateMapper::ensureNodeUuid(n);
            if (uuid == kTrackInUuid)
                trackIn = n;
            if (auto it = idByUuid.find(uuid); it != idByUuid.end())
                targets.emplace_back(n, it->second);
        }
        for (auto& [n, id] : targets) {
            const int outs = n->getProcessor()->getTotalNumOutputChannels();
            auto probeNode = g.addNode(std::make_unique<ProbeSink>(outs));
            probes[id] = dynamic_cast<ProbeSink*>(probeNode->getProcessor());
            for (int ch = 0; ch < outs; ++ch)
                g.addConnection({{n->nodeID, ch}, {probeNode->nodeID, ch}});
        }
    }
    ~Rig() {
        mc.reset();
        bundle.deleteRecursively();
    }
    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    Node* byProjectId(int id) {
        for (auto* n : graph().getNodes())
            if (auto it = idByUuid.find(synth::AIStateMapper::ensureNodeUuid(n));
                it != idByUuid.end() && it->second == id)
                return n;
        return nullptr;
    }
    bool setParam(int projectId, const juce::String& paramId, float actual) {
        auto* n = byProjectId(projectId);
        if (n == nullptr)
            return false;
        for (auto* p : n->getProcessor()->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r && r->getParameterID() == paramId) {
                r->setValueNotifyingHost(r->convertTo0to1(actual));
                return true;
            }
        return false;
    }
    /** Edges between the chain nodes after load, as "18:3 -> 20:3" with project ids. */
    std::vector<std::string> edges() {
        std::vector<std::string> out;
        for (const auto& c : graph().getConnections()) {
            if (c.source.isMIDI() || c.destination.isMIDI())
                continue;
            auto* s = graph().getNodeForId(c.source.nodeID);
            auto* d = graph().getNodeForId(c.destination.nodeID);
            if (s == nullptr || d == nullptr)
                continue;
            auto si = idByUuid.find(synth::AIStateMapper::ensureNodeUuid(s));
            auto di = idByUuid.find(synth::AIStateMapper::ensureNodeUuid(d));
            if (si == idByUuid.end() || di == idByUuid.end())
                continue;
            out.push_back(std::to_string(si->second) + ":" + std::to_string(c.source.channelIndex) + " -> " +
                          std::to_string(di->second) + ":" + std::to_string(c.destination.channelIndex));
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    /** Holds `pitch` and returns the mean-square per channel at every probe plus the master output (key 0). */
    std::map<int, Powers> play(int root, int chordSize) {
        auto& engine = mc->getAudioEngine();
        engine.audioDeviceAboutToStart(&fake);
        auto* tin = dynamic_cast<TimelineMidiSourceModule*>(trackIn->getProcessor());
        const int intervals[] = {0, 4, 7, 11, 14, 17, 21, 24};
        for (int i = 0; i < chordSize; ++i)
            tin->pushAuditionNote(root + intervals[i], 100, true);
        const int n = synth::test::kFakeDeviceBlockSize;
        std::vector<float> l((size_t)n), r((size_t)n);
        float* outs[] = {l.data(), r.data()};
        double sl = 0, sr = 0;
        long cnt = 0;
        const int warm = 100, measure = 200; // sustained, past the attack
        for (int b = 0; b < warm + measure; ++b) {
            if (b == warm)
                for (auto& [id, p] : probes)
                    p->reset();
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, n, {});
            if (b >= warm) {
                for (int i = 0; i < n; ++i) {
                    sl += (double)l[(size_t)i] * l[(size_t)i];
                    sr += (double)r[(size_t)i] * r[(size_t)i];
                }
                cnt += n;
            }
        }
        std::map<int, Powers> out;
        for (auto& [id, p] : probes)
            out[id] = p->powers();
        out[0] = {sl / (double)cnt, sr / (double)cnt};
        for (int i = 0; i < chordSize; ++i)
            tin->pushAuditionNote(root + intervals[i], 0, false);
        for (int b = 0; b < 60; ++b)
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, n, {});
        engine.audioDeviceStopped();
        return out;
    }

    /** Renders the master output of a 4-note chord held `holdSeconds`, then released for 0.5 s, at the given strip pan
     * (left leg then right leg, one sample per frame). */
    std::array<std::vector<float>, 2> renderMaster(float pan, double holdSeconds) {
        setParam(25, "pan", pan);
        auto& engine = mc->getAudioEngine();
        engine.audioDeviceAboutToStart(&fake);
        auto* tin = dynamic_cast<TimelineMidiSourceModule*>(trackIn->getProcessor());
        const int chord[] = {48, 52, 55, 59};
        for (int note : chord)
            tin->pushAuditionNote(note, 100, true);
        const int n = synth::test::kFakeDeviceBlockSize;
        std::vector<float> l((size_t)n), r((size_t)n);
        float* outs[] = {l.data(), r.data()};
        std::array<std::vector<float>, 2> out;
        const auto render = [&](double seconds) {
            const int blocks = (int)std::ceil(seconds * synth::test::kFakeDeviceSampleRate / n);
            for (int b = 0; b < blocks; ++b) {
                engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, n, {});
                out[0].insert(out[0].end(), l.begin(), l.end());
                out[1].insert(out[1].end(), r.begin(), r.end());
            }
        };
        render(holdSeconds);
        for (int note : chord)
            tin->pushAuditionNote(note, 0, false);
        render(0.5);
        engine.audioDeviceStopped();
        return out;
    }

    /** Undoes the load repair for the poly Oscillator -> VCA right fan: removes right-leg voice cables 1..7. */
    int removeRightVoiceCables() {
        auto* osc = byProjectId(18);
        auto* vca = byProjectId(20);
        auto* oscMod = osc != nullptr ? dynamic_cast<ModuleBase*>(osc->getProcessor()) : nullptr;
        if (oscMod == nullptr || vca == nullptr)
            return -1;
        const int right = oscMod->rightAudioLegChannel();
        int removed = 0;
        for (const auto& c : graph().getConnections())
            if (c.source.nodeID == osc->nodeID && c.destination.nodeID == vca->nodeID &&
                c.source.channelIndex > right && c.source.channelIndex < right + 8)
                if (graph().removeConnection(c))
                    ++removed;
        return removed;
    }

    /** Mean over several roots (a chord of `chordSize` notes on each) so random phase cannot fake a left/right
     * difference. */
    std::map<int, Powers> playAveraged(int chordSize) {
        std::map<int, Powers> acc;
        const int pitches[] = {48, 55, 60, 67, 72};
        for (int p : pitches)
            for (auto& [id, pw] : play(p, chordSize)) {
                auto& a = acc[id];
                a.resize(pw.size(), 0.0);
                for (size_t i = 0; i < pw.size(); ++i)
                    a[i] += pw[i] / 5.0;
            }
        return acc;
    }
};

double db(double meanSquare) { return meanSquare > 1e-20 ? 10.0 * std::log10(meanSquare) : -200.0; }

/** One "ch:dB" cell per audible channel. */
std::string describe(const Powers& p) {
    std::string s;
    for (size_t ch = 0; ch < p.size(); ++ch)
        if (p[ch] > 1e-14) {
            char buf[40];
            std::snprintf(buf, sizeof buf, " %zu:%.1f", ch, db(p[ch]));
            s += buf;
        }
    return s.empty() ? " (silent)" : s;
}

double leg(const std::map<int, Powers>& m, int node, int ch) {
    auto it = m.find(node);
    return it != m.end() && (size_t)ch < it->second.size() ? it->second[(size_t)ch] : 0.0;
}
constexpr int kR = ChannelStripModule::kRightBase;
} // namespace

// Diagnostic: logs the repaired cables and the per-channel dB at every node, for unison width 1 and 0 and strip pan
// -1/0/+1.
TEST(RandombientPanLegs, LogsEdgesAndPerLegLevelsAtEveryNode) {
    Rig rig;
    ASSERT_NE(rig.mc, nullptr);
    ASSERT_NE(rig.trackIn, nullptr);
    if (rig.probes.size() != kChain.size())
        for (auto* n : rig.graph().getNodes())
            std::printf("  node %d '%s' uuid %s\n", (int)n->nodeID.uid, n->getProcessor()->getName().toRawUTF8(),
                        synth::AIStateMapper::ensureNodeUuid(n).toRawUTF8());
    ASSERT_EQ(rig.probes.size(), kChain.size());

    std::printf("[edges after load, project ids]\n");
    for (const auto& e : rig.edges())
        std::printf("  %s\n", e.c_str());

    for (const int chord : {1, 4})
        for (const float width : {1.0f, 0.0f})
            for (const float pan : {-1.0f, 0.0f, 1.0f}) {
                EXPECT_TRUE(rig.setParam(18, "width", width));
                EXPECT_TRUE(rig.setParam(25, "pan", pan));
                const auto m = rig.playAveraged(chord);
                std::printf("[chord %d width %.0f pan %+.0f] per-channel dB (mean of 5 roots)\n", chord, width, pan);
                for (auto& [id, name] : std::vector<std::pair<int, const char*>>{{18, "Osc"},
                                                                                 {20, "VCA"},
                                                                                 {21, "Filter"},
                                                                                 {22, "Gate"},
                                                                                 {23, "EQ"},
                                                                                 {24, "Comp"},
                                                                                 {25, "Strip"},
                                                                                 {88, "EQ88"},
                                                                                 {27, "MacroOut"}})
                    std::printf("  %-8s(%2d):%s\n", name, id, describe(m.at(id)).c_str());
                std::printf("  Master  L %.1f dB  R %.1f dB\n", db(m.at(0)[0]), db(m.at(0)[1]));
            }
}

// Guard shape: the strip's output legs at pan -1 and +1 mirror each other (Left@-1 vs Right@+1) within 1.5 dB, at every
// node after the strip and at the master, for unison width 1.0 (the project's own setting).
TEST(RandombientPanLegs, HardLeftAndHardRightPanMirrorEachOther) {
    Rig rig;
    ASSERT_NE(rig.mc, nullptr);
    ASSERT_EQ(rig.probes.size(), kChain.size());
    ASSERT_TRUE(rig.setParam(18, "width", 1.0f));
    ASSERT_TRUE(rig.setParam(25, "pan", -1.0f));
    const auto left = rig.playAveraged(4);
    ASSERT_TRUE(rig.setParam(25, "pan", 1.0f));
    const auto right = rig.playAveraged(4);
    const auto within = [](double a, double b) { return std::abs(db(a) - db(b)) <= 1.5; };
    EXPECT_TRUE(within(leg(left, 25, 0), leg(right, 25, kR)))
        << "strip: Left@-1 " << db(leg(left, 25, 0)) << " dB vs Right@+1 " << db(leg(right, 25, kR));
    EXPECT_TRUE(within(leg(left, 88, 0), leg(right, 88, 1))) << "EQ 88";
    EXPECT_TRUE(within(leg(left, 27, 0), leg(right, 27, 1))) << "Macro Out";
    EXPECT_TRUE(within(left.at(0)[0], right.at(0)[1]))
        << "master: L@-1 " << db(left.at(0)[0]) << " dB vs R@+1 " << db(right.at(0)[1]);
}

namespace {
double rms(const std::vector<float>& v, size_t from, size_t to) {
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += (double)v[i] * v[i];
    return to > from ? std::sqrt(sum / (double)(to - from)) : 0.0;
}

/** Left half (pan -1), 0.3 s of silence, right half (pan +1), as one stereo 48 kHz 24-bit WAV; prints per-half RMS. */
void writeLeftThenRight(Rig& rig, const juce::File& wav, const char* label) {
    const auto left = rig.renderMaster(-1.0f, 3.0);
    const auto right = rig.renderMaster(1.0f, 3.0);
    const size_t gap = (size_t)(0.3 * synth::test::kFakeDeviceSampleRate);
    const size_t total = left[0].size() + gap + right[0].size();
    juce::AudioBuffer<float> joined(2, (int)total);
    joined.clear();
    for (int ch = 0; ch < 2; ++ch) {
        joined.copyFrom(ch, 0, left[(size_t)ch].data(), (int)left[(size_t)ch].size());
        joined.copyFrom(ch, (int)(left[0].size() + gap), right[(size_t)ch].data(), (int)right[(size_t)ch].size());
    }
    wav.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());
    ASSERT_NE(stream, nullptr) << wav.getFullPathName();
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), synth::test::kFakeDeviceSampleRate, 2, 24, {}, 0));
    ASSERT_NE(writer, nullptr);
    stream.release();
    ASSERT_TRUE(writer->writeFromAudioSampleBuffer(joined, 0, joined.getNumSamples()));
    writer.reset();
    const auto db20 = [](double x) { return x > 1e-9 ? 20.0 * std::log10(x) : -180.0; };
    for (int ch = 0; ch < 2; ++ch) {
        const double l = rms(left[(size_t)ch], 0, left[(size_t)ch].size());
        const double r = rms(right[(size_t)ch], 0, right[(size_t)ch].size());
        std::printf("[%s] %s leg: left-pan half RMS %.5f (%.1f dBFS), right-pan half RMS %.5f (%.1f dBFS)\n", label,
                    ch == 0 ? "L" : "R", l, db20(l), r, db20(r));
    }
}
} // namespace

// Proof clip: the master at strip pan -1 then +1, once as loaded (repair on) and once with the repair undone.
// Runs only with AGENTSYNTH_PROOF_DIR set; writes pan-left-then-right.wav and pan-left-then-right-before-fix.wav.
TEST(RandombientPanLegs, RendersHardLeftThenHardRightClip) {
    const auto dir = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_PROOF_DIR", {});
    if (dir.isEmpty())
        GTEST_SKIP() << "AGENTSYNTH_PROOF_DIR not set";
    const juce::File out(dir);
    ASSERT_TRUE(out.createDirectory());
    {
        Rig rig;
        ASSERT_NE(rig.mc, nullptr);
        ASSERT_EQ(rig.probes.size(), kChain.size());
        writeLeftThenRight(rig, out.getChildFile("pan-left-then-right.wav"), "after-fix");
    }
    {
        Rig rig;
        ASSERT_NE(rig.mc, nullptr);
        ASSERT_EQ(rig.probes.size(), kChain.size());
        const int removed = rig.removeRightVoiceCables();
        std::printf("[before-fix] removed %d right-leg voice cables\n", removed);
        ASSERT_EQ(removed, 7);
        writeLeftThenRight(rig, out.getChildFile("pan-left-then-right-before-fix.wav"), "before-fix");
    }
}
