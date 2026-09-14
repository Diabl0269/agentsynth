// synth::StemSession stem-file naming (FRO55, docs/mixer.md §5.12): each stem is named after the
// ONE TimelineMidiSource/TimelineAudioSource ("Track In"/"Track Audio") track whose signal feeds
// that strip, walked upstream through the graph transitively (through an EQ/Compressor, the way
// buildDefaultAudioChannel's own chain does — Source/Mixer/ChannelFlows/ChannelFlows.cpp), not the strip's own
// graph-node instance name. Split out from StemExportTests.cpp (the file-size cap, and the sum/tap/
// cancel suite there is a different concern from naming) — see that file's own
// NStripsProduceNFilesWithExpectedNamesAndEqualLength for the "zero tracks feed it -> Channel N"
// fallback proof against a rig with no TimelineDoc at all.
//
// Headless/deterministic house rules apply: HostMode::Hosted only, no audio device, no sleeps.
// 48 kHz, 512-sample blocks — only the resulting FILE NAMES matter here, not the rendered audio, so
// every rig below uses the shortest range that still produces at least one file.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/OfflineTransportDriver.h"
#include "Transport/StemExporter.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using synth::BounceOptions;
using synth::StemExporter;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;
constexpr int kRight = ChannelStripModule::kRightBase;
using NodeID = juce::AudioProcessorGraph::NodeID;

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

// Sets node->properties["uuid"] AND mirrors it into the processor via ModuleBase::setNodeUuid —
// the two-step invariant every node-creation call site in this codebase follows (root CLAUDE.md /
// Source/CLAUDE.md: "a node's uuid must be mirrored into its processor at every write site"; see
// also Source/Mixer/ChannelFlows/ChannelFlows.cpp's own addChainNode). StemSession reads the properties copy
// (message thread), so that half is the one that actually matters for these tests — the processor
// mirror is set anyway so a rig built this way isn't a lie about the invariant.
juce::String assignUuid(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    if (node == nullptr)
        return {};
    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    return uuid;
}

void wireStripToMaster(juce::AudioProcessorGraph& graph, NodeID strip, NodeID master) {
    graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
    graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
}

// Track Audio -> Parametric EQ -> Compressor -> Channel Strip, the same chain
// buildDefaultAudioChannel builds (Source/Mixer/ChannelFlows/ChannelFlows.cpp) — proves the naming walk crosses
// an ordinary instrument/macro hop rather than only recognising a direct wire. The strip's own
// output is NOT wired to Master here; callers do that once they know their strip's node id (so two
// channels can be built in a controlled creation order — collectStemStrips sorts by ascending node
// id, and the tests below depend on strip A existing before strip B).
struct TrackChannel {
    NodeID trackAudio, strip;
    juce::String trackUuid;
};

TrackChannel buildTrackChannel(juce::AudioProcessorGraph& graph) {
    TrackChannel channel;
    channel.trackAudio = addFactoryNode(graph, "Track Audio");
    const auto eq = addFactoryNode(graph, "Parametric EQ");
    const auto compressor = addFactoryNode(graph, "Compressor");
    channel.strip = addFactoryNode(graph, "Channel Strip");
    channel.trackUuid = assignUuid(graph, channel.trackAudio);

    graph.addConnection({{channel.trackAudio, 0}, {eq, 0}});
    graph.addConnection({{channel.trackAudio, 1}, {eq, 1}});
    graph.addConnection({{eq, 0}, {compressor, 0}});
    graph.addConnection({{eq, 1}, {compressor, 1}});
    graph.addConnection({{compressor, 0}, {channel.strip, 0}});
    graph.addConnection({{compressor, 1}, {channel.strip, kRight}});
    return channel;
}

BounceOptions oneBeatOptions() {
    BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 1.0;
    options.tailSeconds = 0.0;
    options.sampleRate = kSampleRate;
    options.blockSize = kBlockSize;
    options.bitDepth = 32;
    options.numChannels = kNumChannels;
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

// A live engine + graph, ready for StemExporter::exportStems - callers build whatever strips/tracks
// a test needs, then call finishGraph() once Audio Output/Master are wired.
struct NamingRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    NodeID master, out;

    // Adds Audio Output + Master and wires Master -> Audio Output. Call this FIRST — before any
    // TrackChannel — so ascending node id still matches creation order for the strips built after.
    bool start() {
        engine.initialise();
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        out = addFactoryNode(graph, "Audio Output");
        master = addFactoryNode(graph, "Master");
        if (out == NodeID{} || master == NodeID{})
            return false;
        graph.addConnection({{master, 0}, {out, 0}});
        graph.addConnection({{master, 1}, {out, 1}});
        return true;
    }

    // Constructs the offline driver — must run AFTER every node/connection is in place (it
    // re-prepares the whole graph at the render format; strip pointers stay valid across it, same
    // as StemRig in StemExportTests.cpp).
    bool finish() {
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, kNumChannels);
        return driver != nullptr;
    }

    ~NamingRig() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

} // namespace

// ============================================================================
// 1. Two tracks, "Bass" and "Lead", each with its own channel -> "01 - Bass.wav" / "02 - Lead.wav"
// ============================================================================

TEST(StemExportNamingTest, TwoNamedTracksProduceStemsNamedAfterTheirTracks) {
    NamingRig rig;
    ASSERT_TRUE(rig.start());
    auto& graph = rig.engine.getGraph();

    const auto bass = buildTrackChannel(graph); // built first -> lower strip node id -> "01"
    wireStripToMaster(graph, bass.strip, rig.master);
    const auto lead = buildTrackChannel(graph); // built second -> "02"
    wireStripToMaster(graph, lead.strip, rig.master);

    ASSERT_TRUE(rig.finish());

    TimelineDoc doc;
    const auto bassTrack = doc.addTrack(TrackKind::Audio, "Bass");
    ASSERT_TRUE(doc.setTrackBinding(bassTrack, bass.trackUuid));
    const auto leadTrack = doc.addTrack(TrackKind::Audio, "Lead");
    ASSERT_TRUE(doc.setTrackBinding(leadTrack, lead.trackUuid));

    ScopedTempDir out("agentsynth_stem_naming_bass_lead");
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions(), {}, &doc);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.stemFiles.size(), 2);
    EXPECT_EQ(result.stemFiles[0].getFileName(), "01 - Bass.wav");
    EXPECT_EQ(result.stemFiles[1].getFileName(), "02 - Lead.wav");
}

// ============================================================================
// 2. A strip fed by two tracks (a fan-in) falls back to "Channel N"
// ============================================================================

TEST(StemExportNamingTest, StripFedByTwoTracksFallsBackToChannelN) {
    NamingRig rig;
    ASSERT_TRUE(rig.start());
    auto& graph = rig.engine.getGraph();

    const auto strip = addFactoryNode(graph, "Channel Strip");
    const auto trackA = addFactoryNode(graph, "Track Audio");
    const auto trackB = addFactoryNode(graph, "Track Audio");
    const auto uuidA = assignUuid(graph, trackA);
    const auto uuidB = assignUuid(graph, trackB);
    // Both tracks land on the SAME strip - AudioProcessorGraph sums multiple sources on one input
    // channel, so this is a legal (if unusual) fan-in.
    graph.addConnection({{trackA, 0}, {strip, 0}});
    graph.addConnection({{trackA, 1}, {strip, kRight}});
    graph.addConnection({{trackB, 0}, {strip, 0}});
    graph.addConnection({{trackB, 1}, {strip, kRight}});
    wireStripToMaster(graph, strip, rig.master);

    ASSERT_TRUE(rig.finish());

    TimelineDoc doc;
    const auto trackIdA = doc.addTrack(TrackKind::Audio, "Kick");
    ASSERT_TRUE(doc.setTrackBinding(trackIdA, uuidA));
    const auto trackIdB = doc.addTrack(TrackKind::Audio, "Snare");
    ASSERT_TRUE(doc.setTrackBinding(trackIdB, uuidB));

    ScopedTempDir out("agentsynth_stem_naming_two_feed_one");
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions(), {}, &doc);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.stemFiles.size(), 1);
    EXPECT_EQ(result.stemFiles[0].getFileName(), "01 - Channel 1.wav");
}

// ============================================================================
// 3. A strip fed by no track at all falls back to "Channel N" too
// ============================================================================

TEST(StemExportNamingTest, StripFedByNoTrackFallsBackToChannelN) {
    NamingRig rig;
    ASSERT_TRUE(rig.start());
    auto& graph = rig.engine.getGraph();

    // An orphaned strip - nothing feeds it at all (same shape docs/mixer.md §5.12 already documents
    // as legal: "an orphaned strip ... still gets a stem file").
    const auto strip = addFactoryNode(graph, "Channel Strip");
    wireStripToMaster(graph, strip, rig.master);

    ASSERT_TRUE(rig.finish());

    TimelineDoc doc; // a real, but empty, TimelineDoc - proves "no track reaches it", not "no doc"
    ScopedTempDir out("agentsynth_stem_naming_orphan");
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions(), {}, &doc);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.stemFiles.size(), 1);
    EXPECT_EQ(result.stemFiles[0].getFileName(), "01 - Channel 1.wav");
}

// ============================================================================
// 4. Two DIFFERENT tracks sharing the same user-given name still produce unique stem files - the
//    "NN - " prefix alone guarantees it, so this is a property to assert, not machinery to build.
// ============================================================================

TEST(StemExportNamingTest, DuplicateTrackNamesStillProduceUniqueStemFiles) {
    NamingRig rig;
    ASSERT_TRUE(rig.start());
    auto& graph = rig.engine.getGraph();

    const auto first = buildTrackChannel(graph);
    wireStripToMaster(graph, first.strip, rig.master);
    const auto second = buildTrackChannel(graph);
    wireStripToMaster(graph, second.strip, rig.master);

    ASSERT_TRUE(rig.finish());

    TimelineDoc doc;
    const auto firstTrack = doc.addTrack(TrackKind::Audio, "Bass");
    ASSERT_TRUE(doc.setTrackBinding(firstTrack, first.trackUuid));
    const auto secondTrack = doc.addTrack(TrackKind::Audio, "Bass"); // same name, different track
    ASSERT_TRUE(doc.setTrackBinding(secondTrack, second.trackUuid));

    ScopedTempDir out("agentsynth_stem_naming_duplicate_names");
    const auto result = StemExporter::exportStems(rig.engine, out.dir, oneBeatOptions(), {}, &doc);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_EQ(result.stemFiles.size(), 2);
    EXPECT_EQ(result.stemFiles[0].getFileName(), "01 - Bass.wav");
    EXPECT_EQ(result.stemFiles[1].getFileName(), "02 - Bass.wav");
    EXPECT_NE(result.stemFiles[0].getFullPathName(), result.stemFiles[1].getFullPathName());
    EXPECT_TRUE(result.stemFiles[0].existsAsFile());
    EXPECT_TRUE(result.stemFiles[1].existsAsFile());
}
