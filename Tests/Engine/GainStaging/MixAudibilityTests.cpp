// FRO120 gain-staging audit, part 2: two-source audibility.
//
// The incident this guards against summed two oscillators past 0 dBFS at Master; the audio
// device hard-clipped the block and one oscillator vanished. Part 1 (ModuleGainAuditTests.cpp)
// answers "which modules are hidden amplifiers?" in isolation. This file answers the other half:
// "does the ENGINE ever silently erase one source in favour of another, independent of whether
// the final mix clips?" — i.e. is the audibility loss purely a device-clipping artifact (expected,
// quantified here as "obliteration") or something the graph itself is doing.
//
// Real graph path, per the task brief: two tone sources -> independent FX chain -> Channel Strip
// -> Master -> Audio Output, built and rendered through a real hosted AudioEngine exactly the way
// Tests/Mixer/MixerSoloTests.cpp's SoloRig does. Real oscillators (MIDI note-on/gate/envelope
// timing) are deliberately NOT used — the task brief allows generated test tones when "the
// MIDI/note plumbing makes real oscillators awkward", and here it would add attack-ramp and gate
// jitter that has nothing to do with what this test measures. A' saw at 220 Hz and B's square at
// 97 Hz (non-harmonic, so their Goertzel bins never alias into each other) are written directly by
// a tiny stereo-duplicating AudioProcessor source, same pattern as MixerSoloTests.cpp's
// ConstantSource.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
constexpr int kTotalSamples = 22050; // 0.5 s — plenty for an FX chain (incl. Reverb/Delay) to settle
constexpr int kSkipSamples = 4096;
constexpr double kFreqA = 220.0; // saw
constexpr double kFreqB = 97.0;  // square, non-harmonic with A
constexpr double kAmp = 0.5f;

// ---------------------------------------------------------------------------------------------
// A stereo-duplicating tone source (saw or square), same shape as MixerSoloTests.cpp's
// ConstantSource but time-varying. Writing the identical value to both output channels means
// every FX chain below — including Ring Mod's Carrier/Modulator — gets the same signal on both of
// its inputs for free, without any chain needing special-cased wiring.
// ---------------------------------------------------------------------------------------------

class ToneSource : public juce::AudioProcessor {
public:
    enum class Wave { Saw, Square };

    ToneSource(Wave wave, double freqHz, float amp)
        : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true))
        , wave_(wave)
        , freq_(freqHz)
        , amp_(amp) {}

    const juce::String getName() const override { return "Tone"; }
    void prepareToPlay(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        phase_ = 0.0;
    }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        const int n = buffer.getNumSamples();
        for (int i = 0; i < n; ++i) {
            const float v = wave_ == Wave::Saw ? amp_ * float(2.0 * (phase_ - std::floor(phase_ + 0.5)))
                                               : (phase_ < 0.5 ? amp_ : -amp_);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, v);
            phase_ += freq_ / sampleRate_;
            if (phase_ >= 1.0)
                phase_ -= 1.0;
        }
    }
    double getTailLengthSeconds() const override { return 0.0; }
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

private:
    Wave wave_;
    double freq_;
    float amp_;
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
};

// ---------------------------------------------------------------------------------------------
// Chains. Every entry except Filter and RingMod's own class share the plain FX shape (raw ch0/1
// in and out, collapsed Dual I/O); Filter's right leg sits on its own kRightBase block
// (Source/Modules/CLAUDE.md).
// ---------------------------------------------------------------------------------------------

struct ChainSpec {
    juce::String name;
    juce::String factoryType; // "" = None (wire straight through)
    std::vector<int> inCh;    // raw input channels fed from the source's ch0/ch1
    std::vector<int> outCh;   // raw output channels {Left, Right} into the next strip
};

std::vector<ChainSpec> buildChains() {
    const std::vector<int> fx01 = {0, 1};
    return {
        {"None", "", {}, {}},
        {"Filter", "Filter", {0, FilterModule::kRightBase}, {0, FilterModule::kRightBase}},
        {"Distortion", "Distortion", fx01, fx01},
        {"Delay", "Delay", fx01, fx01},
        {"Reverb", "Reverb", fx01, fx01},
        {"Compressor", "Compressor", fx01, fx01},
        {"Chorus", "Chorus", fx01, fx01},
        {"RingMod", "Ring Modulator", fx01, fx01}, // carrier==modulator: ToneSource dupes ch0/ch1
        {"Bitcrusher", "Bitcrusher", fx01, fx01},
    };
}

// ---------------------------------------------------------------------------------------------
// Rig: two tone sources -> independent chain -> Channel Strip -> Master -> Audio Output, built on
// a real hosted AudioEngine (Tests/Mixer/MixerSoloTests.cpp's SoloRig pattern).
// ---------------------------------------------------------------------------------------------

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

void setGainDb(juce::AudioProcessorGraph& graph, NodeID stripId, float db) {
    auto* node = graph.getNodeForId(stripId);
    ASSERT_NE(node, nullptr);
    auto* gain = findParameterByID(node->getProcessor(), "gain");
    ASSERT_NE(gain, nullptr);
    gain->setValueNotifyingHost(gain->getNormalisableRange().convertTo0to1(db));
}

struct MixRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    NodeID stripA, stripB, master;

    // muteB lets the baseline case ("A's level when B is silent") reuse the exact same chain X
    // wiring rather than a separate no-B rig.
    MixRig(const ChainSpec& x, const ChainSpec& y, float gainBDb, bool muteB) {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

        const auto out = addFactoryNode(graph, "Audio Output");
        master = addFactoryNode(graph, "Master");
        stripA = addFactoryNode(graph, "Channel Strip");
        stripB = addFactoryNode(graph, "Channel Strip");
        dynamic_cast<ChannelStripModule*>(graph.getNodeForId(stripA)->getProcessor())
            ->setShape(ChannelStripModule::Shape::Stereo);
        dynamic_cast<ChannelStripModule*>(graph.getNodeForId(stripB)->getProcessor())
            ->setShape(ChannelStripModule::Shape::Stereo);
        setGainDb(graph, stripB, gainBDb);
        if (muteB)
            dynamic_cast<ChannelStripModule*>(graph.getNodeForId(stripB)->getProcessor())->setMuted(true);

        const auto srcA =
            graph.addNode(std::make_unique<ToneSource>(ToneSource::Wave::Saw, kFreqA, (float)kAmp))->nodeID;
        const auto srcB =
            graph.addNode(std::make_unique<ToneSource>(ToneSource::Wave::Square, kFreqB, (float)kAmp))->nodeID;

        wireChain(graph, srcA, x, stripA);
        wireChain(graph, srcB, y, stripB);

        for (NodeID strip : {stripA, stripB}) {
            graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{strip, ChannelStripModule::kRightBase}, {master, MasterModule::kMixRight}});
        }
        graph.addConnection({{master, 0}, {out, 0}});
        graph.addConnection({{master, 1}, {out, 1}});

        engine.prepareForHost(kSampleRate, kBlockSize, 2, 2);
    }

    ~MixRig() { engine.releaseFromHost(); }

    static void wireChain(juce::AudioProcessorGraph& graph, NodeID src, const ChainSpec& chain, NodeID strip) {
        if (chain.factoryType.isEmpty()) {
            graph.addConnection({{src, 0}, {strip, 0}});
            graph.addConnection({{src, 1}, {strip, ChannelStripModule::kRightBase}});
            return;
        }
        const auto fx = addFactoryNode(graph, chain.factoryType);
        for (size_t i = 0; i < chain.inCh.size(); ++i)
            graph.addConnection({{src, (int)(i % 2)}, {fx, chain.inCh[i]}});
        graph.addConnection({{fx, chain.outCh[0]}, {strip, 0}});
        graph.addConnection({{fx, chain.outCh[1]}, {strip, ChannelStripModule::kRightBase}});
    }

    // Renders kTotalSamples of Master's stereo output (channel 0 only — every chain here treats
    // L/R symmetrically since the source itself is mono-duplicated).
    std::vector<float> render() {
        std::vector<float> result(kTotalSamples, 0.0f);
        int rendered = 0;
        while (rendered < kTotalSamples) {
            const int n = std::min(kBlockSize, kTotalSamples - rendered);
            juce::AudioBuffer<float> buffer(2, n);
            buffer.clear();
            juce::MidiBuffer midi;
            engine.processHostBlock(buffer, midi);
            for (int i = 0; i < n; ++i)
                result[(size_t)(rendered + i)] = buffer.getSample(0, i);
            rendered += n;
        }
        return result;
    }
};

// ---------------------------------------------------------------------------------------------
// Single-bin Goertzel magnitude, analysed over the post-skip tail. "A few harmonics is fine" per
// the task brief — this measures the fundamental only, which is enough to answer "is A still
// there" without needing a full FFT.
// ---------------------------------------------------------------------------------------------

double goertzelMagnitude(const std::vector<float>& samples, int skip, double freqHz) {
    const int n = (int)samples.size() - skip;
    if (n <= 0)
        return 0.0;
    const double k = std::round((double)n * freqHz / kSampleRate);
    const double omega = 2.0 * juce::MathConstants<double>::pi * k / (double)n;
    const double coeff = 2.0 * std::cos(omega);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        s0 = samples[(size_t)(skip + i)] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double real = s1 - s2 * std::cos(omega);
    const double imag = s2 * std::sin(omega);
    return std::sqrt(real * real + imag * imag) / ((double)n / 2.0);
}

double peakOf(const std::vector<float>& samples, int skip) {
    double peak = 0.0;
    for (size_t i = (size_t)skip; i < samples.size(); ++i)
        peak = std::max(peak, (double)std::abs(samples[i]));
    return peak;
}

// Simulates the audio device's hard clip at +-1.0 — the failure mode from the FRO120 incident
// (MasterModule itself never clips; the DEVICE did). Returns the clipped buffer.
std::vector<float> hardClip(const std::vector<float>& samples) {
    std::vector<float> out(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
        out[i] = juce::jlimit(-1.0f, 1.0f, samples[i]);
    return out;
}

struct RowResult {
    juce::String chainX, chainY;
    float gainBDb;
    double aMag, bMag, peak;
    bool clipping;
    double aMagAfterClip; // only meaningful when clipping
};

} // namespace

TEST(MixAudibilityTest, ASurvivesEveryChainYAndGainBCombination) {
    const auto chains = buildChains();
    const std::vector<float> gainsBDb = {-12.0f, 0.0f, 6.0f, 12.0f};

    // Baseline: A's fundamental with B silent, per chain X (B's own chain doesn't matter here —
    // held at "None" — since B contributes nothing to the mix).
    std::vector<double> baselineAMag(chains.size(), 0.0);
    for (size_t xi = 0; xi < chains.size(); ++xi) {
        MixRig rig(chains[xi], chains[0], 0.0f, /*muteB=*/true);
        const auto out = rig.render();
        baselineAMag[xi] = goertzelMagnitude(out, kSkipSamples, kFreqA);
        ASSERT_GT(baselineAMag[xi], 1.0e-6) << "chain " << chains[xi].name
                                            << " alone renders silence for A — broken fixture, not a real "
                                               "audibility finding";
    }

    std::vector<RowResult> rows;
    std::vector<std::string> violations;

    for (size_t xi = 0; xi < chains.size(); ++xi) {
        for (size_t yi = 0; yi < chains.size(); ++yi) {
            for (float gainB : gainsBDb) {
                MixRig rig(chains[xi], chains[yi], gainB, /*muteB=*/false);
                const auto out = rig.render();

                const double aMag = goertzelMagnitude(out, kSkipSamples, kFreqA);
                const double bMag = goertzelMagnitude(out, kSkipSamples, kFreqB);
                const double peak = peakOf(out, kSkipSamples);

                RowResult row{chains[xi].name, chains[yi].name, gainB, aMag, bMag, peak, peak > 1.0, 0.0};

                if (peak > 1.0) {
                    const auto clipped = hardClip(out);
                    row.aMagAfterClip = goertzelMagnitude(clipped, kSkipSamples, kFreqA);
                } else {
                    // Engine-level audibility contract: as long as the mix does NOT clip, B must
                    // not push A down by more than 3 dB relative to A playing alone through the
                    // same chain X. This is the "erased inside the engine, not by the device"
                    // check — a real bug if it ever fires.
                    const double floorMag = baselineAMag[xi] * std::pow(10.0, -3.0 / 20.0);
                    if (aMag < floorMag) {
                        std::ostringstream msg;
                        msg << "chainX=" << chains[xi].name.toStdString() << " chainY=" << chains[yi].name.toStdString()
                            << " gainB=" << gainB << "dB: A dropped to " << aMag << " (baseline " << baselineAMag[xi]
                            << ", floor " << floorMag << ") with NO clipping (peak " << peak
                            << ") — B is masking A inside the engine, not the device.";
                        violations.push_back(msg.str());
                    }
                }
                rows.push_back(row);
            }
        }
    }

    // ---- report ----
    juce::File logDir(juce::File(TESTS_ROOT_DIR).getParentDirectory().getChildFile(".buildlogs"));
    logDir.createDirectory();
    juce::FileOutputStream out(logDir.getChildFile("mix-audibility.txt"));
    out.setPosition(0);
    out.truncate();

    std::ostringstream buf;
    buf << "FRO120 mix-audibility matrix: Osc A (220 Hz saw) vs Osc B (97 Hz square) through every "
           "chain X / chain Y / Strip-B-gain combination.\n\n";
    buf << juce::String::formatted("%-11s %-11s %7s %10s %10s %8s %10s\n", "ChainX", "ChainY", "GainB", "|A|", "|B|",
                                   "Peak", "Clip A->")
               .toStdString();
    for (const auto& r : rows) {
        buf << juce::String::formatted("%-11s %-11s %7.1f %10.5f %10.5f %8.3f %10s\n", r.chainX.toRawUTF8(),
                                       r.chainY.toRawUTF8(), r.gainBDb, r.aMag, r.bMag, r.peak,
                                       r.clipping ? juce::String(r.aMagAfterClip, 5).toRawUTF8() : "-")
                   .toStdString();
    }
    buf << "\nBaseline |A| per chain X (B silent):\n";
    for (size_t xi = 0; xi < chains.size(); ++xi)
        buf << juce::String::formatted("  %-11s %10.5f\n", chains[xi].name.toRawUTF8(), baselineAMag[xi]).toStdString();

    if (!violations.empty()) {
        buf << "\nENGINE-LEVEL AUDIBILITY VIOLATIONS (no clipping, A still dropped > 3 dB):\n";
        for (const auto& v : violations)
            buf << "  " << v << "\n";
    }

    const auto text = buf.str();
    out.write(text.data(), text.size());
    out.flush();
    std::cout << text;

    EXPECT_TRUE(violations.empty()) << violations.size()
                                    << " case(s) show A masked by B with no clipping — see "
                                       ".buildlogs/mix-audibility.txt for the full matrix.";
}
