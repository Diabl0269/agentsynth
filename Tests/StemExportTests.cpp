// synth::StemExporter / synth::StemSession — offline stem export (P9-8, docs/mixer.md §5.12): one
// render pass through the same offline path BounceExporter uses, writing one audio file per mixer
// channel strip instead of one file for the whole mix.
//
//   • the tap        -- ChannelStripModule::setStemTapBuffer copies the strip's FINAL output (post
//                        gain/pan/mute/solo) into the armed buffer, on every processBlock exit path
//   • N files         -- one per ChannelStrip node, stable "NN - <name>.<ext>" names, equal length
//   • THE correctness -- sum(stems) reproduces the PRE-MASTER mix: proven with a non-unity Master
//                        gain against a normal BounceExporter::bounce of the same patch/range
//   • post-fader      -- non-default strip gain/pan prove the tap sits after the strip's own fader
//   • mute/solo       -- every strip is ALWAYS written; a muted or non-soloed strip's stem is
//                        silent, never absent, and the sum property still holds; solo/mute state is
//                        unchanged by the export
//   • cancel/failure  -- no stem files left behind, pre-existing files untouched, every tap disarmed
//
// Headless/deterministic house rules apply: HostMode::Hosted only, no audio device, no sleeps.
// 48 kHz, 512-sample blocks, 120 BPM (the transport's own default) => 24000 samples/beat.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/MasterModule.h"
#include "../Source/Transport/BounceExporter.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "../Source/Transport/StemExporter.h"
#include "../Source/Transport/StemSession.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

using synth::BounceExporter;
using synth::BounceOptions;
using synth::StemExporter;
using synth::StemResult;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;
constexpr int kRight = ChannelStripModule::kRightBase;
using NodeID = juce::AudioProcessorGraph::NodeID;

// ============================================================================
// Shared fixture plumbing — mirrors MixerSoloTests.cpp's SoloRig / ConstantSource and
// BounceExporterTests.cpp's Fixture, duplicated locally per this codebase's own test-file
// convention (each render fixture stays self-contained and readable on its own).
// ============================================================================

// A stereo source that writes one constant to both outputs.
class ConstantSource : public juce::AudioProcessor {
public:
    explicit ConstantSource(float value)
        : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true))
        , value_(value) {}

    const juce::String getName() const override { return "Constant"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), value_, buffer.getNumSamples());
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
    float value_;
};

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

MasterModule* masterAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<MasterModule*>(node->getProcessor()) : nullptr;
}

void setParam(juce::AudioProcessor& processor, const juce::String& id, float value) {
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (ranged->getParameterID() == id) {
                ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
                return;
            }
    FAIL() << "no parameter " << id;
}

constexpr float kSourceA = 0.3f;
constexpr float kSourceB = -0.15f;
constexpr float kGainADb = 3.0f;
constexpr float kPanA = -0.4f;
constexpr float kGainBDb = -6.0f;
constexpr float kPanB = 0.7f;
constexpr float kMasterGainDb = 6.0f; // non-unity - the whole point of the correctness test

float expectedStripSample(float source, float gainDb, float pan, bool leftLeg) {
    const float gain = juce::Decibels::decibelsToGain(gainDb, ChannelStripModule::kMinGainDb);
    float panL = 1.0f, panR = 1.0f;
    // The real law, not a reimplementation - so this test stays correct if the balance law itself
    // ever changes (see ChannelStripTests.cpp for the module-level proof of the law).
    ModuleBase::panGains(pan, panL, panR);
    return source * gain * (leftLeg ? panL : panR);
}

// Two strips (A, B) fed by constant sources, landing on Master's Mix - nothing feeds Direct, which
// is what makes "sum(stems) == the pre-Master mix" the same thing as "sum(stems) * masterGain ==
// a normal bounce of this patch" below. Params (strip gain/pan, Master gain) are set BEFORE the
// OfflineTransportDriver is constructed (which reprepares the graph), so every smoothed value starts
// already AT its target - no ramp transient to account for in a sample-exact comparison.
struct StemRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    NodeID stripA, stripB, master, out;

    bool build() {
        engine.initialise();
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

        out = addFactoryNode(graph, "Audio Output");
        master = addFactoryNode(graph, "Master");
        stripA = addFactoryNode(graph, "Channel Strip");
        stripB = addFactoryNode(graph, "Channel Strip");
        if (out == NodeID{} || master == NodeID{} || stripA == NodeID{} || stripB == NodeID{})
            return false;

        const auto srcA = graph.addNode(std::make_unique<ConstantSource>(kSourceA))->nodeID;
        const auto srcB = graph.addNode(std::make_unique<ConstantSource>(kSourceB))->nodeID;

        for (auto [src, strip] : {std::pair{srcA, stripA}, std::pair{srcB, stripB}}) {
            graph.addConnection({{src, 0}, {strip, 0}});
            graph.addConnection({{src, 1}, {strip, kRight}});
            graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
        }
        graph.addConnection({{master, 0}, {out, 0}});
        graph.addConnection({{master, 1}, {out, 1}});

        auto* a = stripAt(graph, stripA);
        auto* b = stripAt(graph, stripB);
        auto* m = masterAt(graph, master);
        if (a == nullptr || b == nullptr || m == nullptr)
            return false;
        setParam(*a, "gain", kGainADb);
        setParam(*a, "pan", kPanA);
        setParam(*b, "gain", kGainBDb);
        setParam(*b, "pan", kPanB);
        setParam(*m, "gain", kMasterGainDb);

        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, kNumChannels);
        return true;
    }

    ~StemRig() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

BounceOptions oneBeatOptions(int bitDepth = 32) {
    BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 1.0; // 120 bpm default -> 24000 samples -> ceil(24000/512) = 47 whole blocks
    options.tailSeconds = 0.0;
    options.sampleRate = kSampleRate;
    options.blockSize = kBlockSize;
    options.bitDepth = bitDepth;
    options.numChannels = kNumChannels;
    return options;
}

BounceOptions oneBeatOptionsWithTail(double tailSeconds) {
    BounceOptions options = oneBeatOptions();
    options.tailSeconds = tailSeconds;
    return options;
}

struct ScopedTempDir {
    explicit ScopedTempDir(const juce::String& name)
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        dir.deleteRecursively();
        dir.createDirectory();
    }
    ~ScopedTempDir() { dir.deleteRecursively(); }

    juce::File dir;
};

struct ScopedTempFile {
    explicit ScopedTempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedTempFile() { file.deleteFile(); }

    juce::File file;
};

struct WavContents {
    bool ok = false;
    juce::int64 lengthInSamples = 0;
    juce::AudioBuffer<float> audio;
};

WavContents readWav(const juce::File& file) {
    WavContents out;
    if (!file.existsAsFile())
        return out;
    auto input = file.createInputStream();
    if (input == nullptr)
        return out;
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(input.release(), /*deleteStreamIfOpeningFails=*/true));
    if (reader == nullptr)
        return out;
    out.lengthInSamples = reader->lengthInSamples;
    out.audio.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
    out.audio.clear();
    reader->read(&out.audio, 0, (int)reader->lengthInSamples, 0, true, true);
    out.ok = true;
    return out;
}

} // namespace

// ============================================================================
// 1. N strips -> exactly N files, expected names, equal length
// ============================================================================

TEST(StemExportTest, NStripsProduceNFilesWithExpectedNamesAndEqualLength) {
    StemRig rig;
    ASSERT_TRUE(rig.build());

    ScopedTempDir out("agentsynth_stems_names");
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions());
    ASSERT_TRUE(result.ok) << result.message;

    ASSERT_EQ(result.stemFiles.size(), 2);
    EXPECT_EQ(result.stemFiles[0].getFileName(), "01 - Channel Strip.wav");
    EXPECT_EQ(result.stemFiles[1].getFileName(), "02 - Channel Strip.wav");
    for (auto& f : result.stemFiles)
        EXPECT_TRUE(f.existsAsFile()) << f.getFullPathName();

    const auto wavA = readWav(result.stemFiles[0]);
    const auto wavB = readWav(result.stemFiles[1]);
    ASSERT_TRUE(wavA.ok);
    ASSERT_TRUE(wavB.ok);
    EXPECT_EQ(wavA.lengthInSamples, wavB.lengthInSamples);
    EXPECT_EQ(wavA.lengthInSamples, result.samplesWritten);
    EXPECT_GT(wavA.lengthInSamples, (juce::int64)0);
}

TEST(StemExportTest, NoStripsShowsAClearMessageInsteadOfRenderingNothing) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    addFactoryNode(graph, "Audio Output");
    auto driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, kNumChannels);

    EXPECT_FALSE(StemExporter::hasChannelStrips(engine));

    ScopedTempDir out("agentsynth_stems_none");
    const auto result = StemExporter::exportStems(engine, out.dir, oneBeatOptions());
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, StemExporter::kNoChannelsMessage);
    EXPECT_TRUE(result.stemFiles.isEmpty());
    // No folder side effect from a failed setup either.
    EXPECT_EQ(out.dir.getNumberOfChildFiles(juce::File::findFilesAndDirectories), 0);

    engine.releaseFromHost();
    engine.shutdown();
}

// ============================================================================
// 2. THE correctness check: sum(stems) reproduces the PRE-MASTER mix, proven against a
//    NON-UNITY Master gain - a post-Master tap would fail this by exactly the Master gain factor.
// ============================================================================

TEST(StemExportTest, StemsSumToThePreMasterMixEvenWithNonUnityMasterGain) {
    ScopedTempDir stemsOut("agentsynth_stems_sum");
    ScopedTempFile bounceOut("agentsynth_stems_sum_bounce.wav");

    StemResult stems;
    {
        StemRig rig;
        ASSERT_TRUE(rig.build());
        stems = StemExporter::exportStems(rig.engine, stemsOut.dir, oneBeatOptions());
        ASSERT_TRUE(stems.ok) << stems.message;
    }

    // A FRESH rig for the reference bounce - same starting DSP state as the stem export's own fresh
    // rig (BounceExporterTests.DeterministicByteIdentical uses the same two-fresh-fixtures idiom for
    // the same reason: reusing one live engine for two renders would carry the first's ramp/phase
    // state into the second).
    {
        StemRig rig;
        ASSERT_TRUE(rig.build());
        const auto bounce = BounceExporter::bounce(rig.engine, bounceOut.file, oneBeatOptions());
        ASSERT_TRUE(bounce.ok) << bounce.message;
    }

    ASSERT_EQ(stems.stemFiles.size(), 2);
    const auto wavA = readWav(stems.stemFiles[0]);
    const auto wavB = readWav(stems.stemFiles[1]);
    const auto wavMix = readWav(bounceOut.file);
    ASSERT_TRUE(wavA.ok);
    ASSERT_TRUE(wavB.ok);
    ASSERT_TRUE(wavMix.ok);
    ASSERT_EQ(wavA.lengthInSamples, wavMix.lengthInSamples);
    ASSERT_EQ(wavB.lengthInSamples, wavMix.lengthInSamples);

    const float masterGain = juce::Decibels::decibelsToGain(kMasterGainDb, MasterModule::kMinGainDb);
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int i = 0; i < wavMix.lengthInSamples; ++i) {
            const float sum = wavA.audio.getSample(ch, i) + wavB.audio.getSample(ch, i);
            ASSERT_NEAR(sum * masterGain, wavMix.audio.getSample(ch, i), 1.0e-5f)
                << "channel " << ch << " sample " << i
                << " - a post-Master tap would be off by the Master gain factor here";
        }
    }
}

// ============================================================================
// 2b. The tail phase (StemSession::stepTail) is exercised at all - oneBeatOptions() above always
//     sets tailSeconds = 0.0, which would leave the tail loop completely uncovered. A ConstantSource
//     doesn't care whether the transport is playing or stopped, so the tail's extra blocks are
//     expected to carry on with exactly the same per-strip values as the range - proving both that
//     stem lengths grow by the tail and that the sum property still holds all the way through it.
// ============================================================================

TEST(StemExportTest, TailBlocksAreCapturedAndTheSumPropertyHoldsThroughTheTail) {
    constexpr double kTailSeconds = 0.25; // -> 12000 samples -> ceil(12000/512) = 24 tail blocks
    ScopedTempDir stemsOut("agentsynth_stems_tail");
    ScopedTempFile bounceOut("agentsynth_stems_tail_bounce.wav");

    StemResult stems;
    {
        StemRig rig;
        ASSERT_TRUE(rig.build());
        stems = StemExporter::exportStems(rig.engine, stemsOut.dir, oneBeatOptionsWithTail(kTailSeconds));
        ASSERT_TRUE(stems.ok) << stems.message;
    }
    {
        StemRig rig;
        ASSERT_TRUE(rig.build());
        const auto bounce = BounceExporter::bounce(rig.engine, bounceOut.file, oneBeatOptionsWithTail(kTailSeconds));
        ASSERT_TRUE(bounce.ok) << bounce.message;
    }

    ASSERT_EQ(stems.stemFiles.size(), 2);
    const auto wavA = readWav(stems.stemFiles[0]);
    const auto wavB = readWav(stems.stemFiles[1]);
    const auto wavMix = readWav(bounceOut.file);
    ASSERT_TRUE(wavA.ok);
    ASSERT_TRUE(wavB.ok);
    ASSERT_TRUE(wavMix.ok);

    // Range alone (oneBeatOptions(), no tail) is 47 blocks; the tail adds 24 more.
    constexpr juce::int64 kExpectedBlocks = 47 + 24;
    const juce::int64 expectedLength = kExpectedBlocks * (juce::int64)kBlockSize;
    EXPECT_EQ(wavA.lengthInSamples, expectedLength);
    EXPECT_EQ(wavB.lengthInSamples, expectedLength);
    ASSERT_EQ(wavA.lengthInSamples, wavMix.lengthInSamples);
    ASSERT_EQ(wavB.lengthInSamples, wavMix.lengthInSamples);
    ASSERT_EQ(wavA.lengthInSamples, stems.samplesWritten);

    const float masterGain = juce::Decibels::decibelsToGain(kMasterGainDb, MasterModule::kMinGainDb);
    for (int ch = 0; ch < kNumChannels; ++ch) {
        for (int i = 0; i < wavMix.lengthInSamples; ++i) {
            const float sum = wavA.audio.getSample(ch, i) + wavB.audio.getSample(ch, i);
            ASSERT_NEAR(sum * masterGain, wavMix.audio.getSample(ch, i), 1.0e-5f)
                << "channel " << ch << " sample " << i << " (tail region: " << (i >= 47 * kBlockSize) << ")";
        }
    }

    // The tail region itself carries the same per-strip values as the range - a stopped transport
    // doesn't silence a time-invariant source, and nothing here has its own tail-only behaviour.
    const float expectedAL = expectedStripSample(kSourceA, kGainADb, kPanA, /*leftLeg=*/true);
    const float expectedAR = expectedStripSample(kSourceA, kGainADb, kPanA, /*leftLeg=*/false);
    for (juce::int64 i = 47 * (juce::int64)kBlockSize; i < wavA.lengthInSamples; ++i) {
        EXPECT_NEAR(wavA.audio.getSample(0, i), expectedAL, 1.0e-5f) << "tail sample " << i;
        EXPECT_NEAR(wavA.audio.getSample(1, i), expectedAR, 1.0e-5f) << "tail sample " << i;
    }
}

// ============================================================================
// 3. Post-fader: each stem carries its own strip's non-default gain/pan, analytically
// ============================================================================

TEST(StemExportTest, EachStemCarriesItsOwnStripsGainAndPanPostFader) {
    StemRig rig;
    ASSERT_TRUE(rig.build());

    ScopedTempDir out("agentsynth_stems_postfader");
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions());
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.stemFiles.size(), 2);

    const auto wavA = readWav(result.stemFiles[0]);
    const auto wavB = readWav(result.stemFiles[1]);
    ASSERT_TRUE(wavA.ok);
    ASSERT_TRUE(wavB.ok);

    const float expectedAL = expectedStripSample(kSourceA, kGainADb, kPanA, /*leftLeg=*/true);
    const float expectedAR = expectedStripSample(kSourceA, kGainADb, kPanA, /*leftLeg=*/false);
    const float expectedBL = expectedStripSample(kSourceB, kGainBDb, kPanB, /*leftLeg=*/true);
    const float expectedBR = expectedStripSample(kSourceB, kGainBDb, kPanB, /*leftLeg=*/false);

    ASSERT_GT(wavA.lengthInSamples, (juce::int64)0);
    for (int i = 0; i < wavA.lengthInSamples; ++i) {
        ASSERT_NEAR(wavA.audio.getSample(0, i), expectedAL, 1.0e-5f) << "strip A left, sample " << i;
        ASSERT_NEAR(wavA.audio.getSample(1, i), expectedAR, 1.0e-5f) << "strip A right, sample " << i;
        ASSERT_NEAR(wavB.audio.getSample(0, i), expectedBL, 1.0e-5f) << "strip B left, sample " << i;
        ASSERT_NEAR(wavB.audio.getSample(1, i), expectedBR, 1.0e-5f) << "strip B right, sample " << i;
    }
}

// ============================================================================
// 4. Muted strip: its file exists and is silent; the sum property still holds
// ============================================================================

TEST(StemExportTest, MutedStripWritesASilentStemAndTheSumPropertyStillHolds) {
    StemRig rig;
    ASSERT_TRUE(rig.build());
    stripAt(rig.engine.getGraph(), rig.stripB)->setMuted(true);

    ScopedTempDir out("agentsynth_stems_muted");
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions());
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.stemFiles.size(), 2);

    const auto wavA = readWav(result.stemFiles[0]);
    const auto wavB = readWav(result.stemFiles[1]);
    ASSERT_TRUE(wavA.ok);
    ASSERT_TRUE(wavB.ok);

    const float expectedAL = expectedStripSample(kSourceA, kGainADb, kPanA, true);
    const float expectedAR = expectedStripSample(kSourceA, kGainADb, kPanA, false);
    for (int i = 0; i < wavA.lengthInSamples; ++i) {
        EXPECT_NEAR(wavA.audio.getSample(0, i), expectedAL, 1.0e-5f);
        EXPECT_NEAR(wavA.audio.getSample(1, i), expectedAR, 1.0e-5f);
        EXPECT_EQ(wavB.audio.getSample(0, i), 0.0f) << "muted strip must write silence, sample " << i;
        EXPECT_EQ(wavB.audio.getSample(1, i), 0.0f);
    }
}

// ============================================================================
// 5. Solo during export: every strip is still written; non-soloed stems are silent; state
//    (solo, mute) is unchanged afterwards
// ============================================================================

TEST(StemExportTest, SoloedStripDuringExportWritesEveryStripButOnlySoloedOnesAreAudible) {
    StemRig rig;
    ASSERT_TRUE(rig.build());
    auto& graph = rig.engine.getGraph();
    ASSERT_TRUE(rig.engine.setChannelStripSoloed(rig.stripA, true));
    ASSERT_EQ(rig.engine.getSoloedStripCount(), 1);

    ScopedTempDir out("agentsynth_stems_solo");
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions());
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.stemFiles.size(), 2) << "a soloed strip must not change which strips get written";

    const auto wavA = readWav(result.stemFiles[0]); // stripA - soloed, passes
    const auto wavB = readWav(result.stemFiles[1]); // stripB - not soloed, gated silent
    ASSERT_TRUE(wavA.ok);
    ASSERT_TRUE(wavB.ok);

    const float expectedAL = expectedStripSample(kSourceA, kGainADb, kPanA, true);
    const float expectedAR = expectedStripSample(kSourceA, kGainADb, kPanA, false);
    for (int i = 0; i < wavA.lengthInSamples; ++i) {
        EXPECT_NEAR(wavA.audio.getSample(0, i), expectedAL, 1.0e-5f);
        EXPECT_NEAR(wavA.audio.getSample(1, i), expectedAR, 1.0e-5f);
        EXPECT_EQ(wavB.audio.getSample(0, i), 0.0f) << "non-soloed strip must be silent, sample " << i;
        EXPECT_EQ(wavB.audio.getSample(1, i), 0.0f);
    }

    // Solo state and mute params are unchanged after export.
    EXPECT_TRUE(stripAt(graph, rig.stripA)->isSoloed());
    EXPECT_FALSE(stripAt(graph, rig.stripB)->isSoloed());
    EXPECT_FALSE(stripAt(graph, rig.stripA)->isMuted());
    EXPECT_FALSE(stripAt(graph, rig.stripB)->isMuted());
    EXPECT_EQ(rig.engine.getSoloedStripCount(), 1);
}

// ============================================================================
// 6. Cancel/failure: no stem files left behind, pre-existing files untouched, every tap disarmed
// ============================================================================

TEST(StemExportTest, CancellationLeavesNoStemFilesAndDisarmsEveryTap) {
    StemRig rig;
    ASSERT_TRUE(rig.build());

    ScopedTempDir out("agentsynth_stems_cancel");
    const auto sentinel = out.dir.getChildFile("keep-me.txt");
    sentinel.replaceWithText("pre-existing");

    int calls = 0;
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions(), [&calls](double) {
        ++calls;
        return calls < 5; // cancel a handful of blocks in, well before the range ends
    });

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("cancel")) << result.message;
    EXPECT_TRUE(result.stemFiles.isEmpty());

    const auto children = out.dir.findChildFiles(juce::File::findFilesAndDirectories, false);
    ASSERT_EQ(children.size(), 1) << "a cancelled export must leave no stem files behind";
    EXPECT_EQ(children[0], sentinel) << "and must never touch a file that was already there";
    EXPECT_EQ(sentinel.loadFileAsString(), "pre-existing");

    // Every tap is disarmed - a subsequent normal render (the fixture's own driver) writes nothing
    // into memory the session already freed.
    auto& graph = rig.engine.getGraph();
    EXPECT_FALSE(stripAt(graph, rig.stripA)->isStemTapArmedForTest());
    EXPECT_FALSE(stripAt(graph, rig.stripB)->isStemTapArmedForTest());
    rig.driver->renderBlocks(1); // must not crash - proves nothing is still armed
}

// A destination that is already a FILE (not a folder) fails before anything is rendered or touched,
// and disarms whatever it may have armed on the way there.
TEST(StemExportTest, DestinationThatIsAlreadyAFileFailsCleanlyAndDisarmsEveryTap) {
    StemRig rig;
    ASSERT_TRUE(rig.build());

    ScopedTempFile blocker("agentsynth_stems_blocker");
    blocker.file.replaceWithText("not a folder");

    const auto result = StemExporter::exportStems(rig.engine, blocker.file, oneBeatOptions());
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("folder")) << result.message;
    EXPECT_TRUE(result.stemFiles.isEmpty());
    EXPECT_EQ(blocker.file.loadFileAsString(), "not a folder") << "the pre-existing file must be untouched";

    auto& graph = rig.engine.getGraph();
    EXPECT_FALSE(stripAt(graph, rig.stripA)->isStemTapArmedForTest());
    EXPECT_FALSE(stripAt(graph, rig.stripB)->isStemTapArmedForTest());
}

// ============================================================================
// 7. The tap itself, at the module level - exact post-fader behaviour on every exit path, with a
//    preallocated buffer only (no allocation on the audio thread, the same discipline
//    Tests/ChannelStripTests.cpp's own bypass/mute suite already pins for processBlock as a whole).
// ============================================================================

namespace {
juce::AudioBuffer<float> stripInputForTap(float left, float right) {
    juce::AudioBuffer<float> buffer(ChannelStripModule::kNumChannels, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, left);
        buffer.setSample(kRight, i, right);
    }
    return buffer;
}
} // namespace

TEST(StemExportTest, TapCapturesTheNormalPostFaderOutput) {
    ChannelStripModule strip;
    strip.prepareToPlay(kSampleRate, kBlockSize);
    setParam(strip, "gain", 6.0f);
    setParam(strip, "pan", -1.0f); // hard left: right leg silenced

    juce::AudioBuffer<float> tap(2, kBlockSize);
    tap.clear();
    strip.setStemTapBuffer(&tap);
    ASSERT_TRUE(strip.isStemTapArmedForTest());

    auto buffer = stripInputForTap(0.25f, 0.25f);
    juce::MidiBuffer midi;
    // Enough blocks for the 20 ms gain/pan ramp to settle (same idiom ChannelStripTests uses).
    for (int i = 0; i < 40; ++i) {
        auto scratch = buffer;
        strip.processBlock(scratch, midi);
    }
    strip.processBlock(buffer, midi);

    const float plus6 = juce::Decibels::decibelsToGain(6.0f);
    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_NEAR(tap.getSample(0, i), 0.25f * plus6, 1.0e-5f) << "tap left, sample " << i;
        EXPECT_NEAR(tap.getSample(1, i), 0.0f, 1.0e-5f) << "tap right (panned out), sample " << i;
    }

    strip.setStemTapBuffer(nullptr);
    ASSERT_FALSE(strip.isStemTapArmedForTest());
}

TEST(StemExportTest, TapCapturesSilenceWhenMutedAndWhenSoloGated) {
    synth::TransportService transport;
    ChannelStripModule strip;
    strip.setPlayHead(&transport);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> tap(2, kBlockSize);
    juce::MidiBuffer midi;

    // Muted.
    strip.setMuted(true);
    tap.setSize(2, kBlockSize);
    tap.clear();
    strip.setStemTapBuffer(&tap);
    auto muted = stripInputForTap(0.5f, 0.5f);
    strip.processBlock(muted, midi);
    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_EQ(tap.getSample(0, i), 0.0f);
        EXPECT_EQ(tap.getSample(1, i), 0.0f);
    }
    strip.setMuted(false);

    // Solo-gated (someone else is soloed, this strip is not).
    transport.setMixerSoloActiveForBlock(true);
    tap.clear();
    auto gated = stripInputForTap(0.5f, 0.5f);
    strip.processBlock(gated, midi);
    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_EQ(tap.getSample(0, i), 0.0f);
        EXPECT_EQ(tap.getSample(1, i), 0.0f);
    }

    strip.setStemTapBuffer(nullptr);
}
