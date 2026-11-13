// Concern: the "transport" key of a project bundle (tempo, time signature, loop) through
// save/load and the autosave sidecar, including rejection of a malformed value.
#include "Modules/OscillatorModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportDoc.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::PatchDocument;
using synth::ProjectBundle;
using synth::TransportDoc;

namespace {

TransportDoc makeCustomTransport() {
    TransportDoc t;
    t.bpm = 96.0;
    t.timeSigNumerator = 3;
    t.timeSigDenominator = 4;
    t.loopStartBeat = 4.0;
    t.loopEndBeat = 16.0;
    t.loopEnabled = true;
    return t;
}

// Everything a save/load call needs, so each test states only what it is about.
struct Doc {
    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    PatchDocument patchDocument;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc midiRemote;

    Doc() { graph.addNode(std::make_unique<OscillatorModule>()); }

    synth::ProjectLoadResult save(const juce::File& dir, const TransportDoc* transport) {
        return ProjectBundle::save(dir, graph, timeline, patchDocument, macros, midiRemote, synth::MixerPanLaw::Balance,
                                   nullptr, transport);
    }
    synth::ProjectLoadResult load(const juce::File& dir, TransportDoc* out) {
        return ProjectBundle::load(dir, graph, timeline, patchDocument, macros, midiRemote, nullptr, nullptr, out);
    }
};

} // namespace

class ProjectBundleTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = synth::userSettingsRootDirectory().getChildFile("agentsynth-projectbundle-transport-tests");
        root.deleteRecursively();
        root.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File bundleDir(const juce::String& name) { return root.getChildFile(name + ProjectBundle::kBundleExtension); }

    // Rewrites the saved project.json's "transport" value (or removes it when `value` is void).
    static void setTransportInFile(const juce::File& dir, const juce::var& value, const char* fileName) {
        const auto file = dir.getChildFile(fileName);
        auto json = juce::JSON::parse(file);
        auto* obj = json.getDynamicObject();
        ASSERT_NE(obj, nullptr);
        if (value.isVoid())
            obj->removeProperty("transport");
        else
            obj->setProperty("transport", value);
        ASSERT_TRUE(file.replaceWithText(juce::JSON::toString(json)));
    }

    // Saves a valid bundle, replaces its transport value, then loads into a fresh empty document whose
    // out-parameter holds a sentinel. Returns whether the load succeeded.
    bool loadWithTransportValue(const juce::var& value, juce::String& message) {
        Doc src;
        const auto dir = bundleDir("Tampered");
        const auto saveResult = src.save(dir, nullptr);
        EXPECT_TRUE(saveResult.ok) << saveResult.message;
        setTransportInFile(dir, value, ProjectBundle::kProjectFileName);

        Doc fresh;
        fresh.graph.clear();
        TransportDoc out = makeCustomTransport();
        const auto result = fresh.load(dir, &out);
        message = result.message;
        if (!result.ok) {
            EXPECT_EQ(fresh.graph.getNumNodes(), 0) << "a rejected load must not touch the graph";
            EXPECT_EQ(out, makeCustomTransport()) << "a rejected load must not touch the out-parameter";
        }
        return result.ok;
    }

    static juce::var validVar() { return makeCustomTransport().toVar(); }

    static juce::var withProperty(const char* key, const juce::var& v) {
        auto var = validVar();
        var.getDynamicObject()->setProperty(key, v);
        return var;
    }

    juce::File root;
};

TEST_F(ProjectBundleTransportTest, SaveWritesTransportKey) {
    Doc doc;
    const auto dir = bundleDir("Key");
    const auto transport = makeCustomTransport();
    ASSERT_TRUE(doc.save(dir, &transport).ok);

    auto json = juce::JSON::parse(dir.getChildFile(ProjectBundle::kProjectFileName));
    ASSERT_TRUE(json.hasProperty("transport"));
    TransportDoc parsed;
    ASSERT_TRUE(parsed.fromVar(json.getProperty("transport", {})));
    EXPECT_EQ(parsed, transport);
}

TEST_F(ProjectBundleTransportTest, TransportRoundTripsThroughSaveAndLoad) {
    Doc doc;
    const auto dir = bundleDir("RoundTrip");
    const auto transport = makeCustomTransport();
    ASSERT_TRUE(doc.save(dir, &transport).ok);

    Doc fresh;
    TransportDoc loaded;
    const auto result = fresh.load(dir, &loaded);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(loaded, transport);
}

TEST_F(ProjectBundleTransportTest, TransportRoundTripsThroughAutosave) {
    Doc doc;
    const auto dir = bundleDir("Autosave");
    const auto transport = makeCustomTransport();
    ASSERT_TRUE(doc.save(dir, nullptr).ok);
    ASSERT_TRUE(ProjectBundle::saveAutosave(dir, doc.graph, doc.timeline, doc.patchDocument, doc.macros, 0,
                                            doc.midiRemote, synth::MixerPanLaw::Balance, nullptr, &transport)
                    .ok);

    Doc fresh;
    TransportDoc loaded;
    const auto result = ProjectBundle::loadAutosave(dir, fresh.graph, fresh.timeline, fresh.patchDocument, fresh.macros,
                                                    fresh.midiRemote, nullptr, nullptr, &loaded);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(loaded, transport);
}

TEST_F(ProjectBundleTransportTest, FileWithoutTheKeyLoadsDefaults) {
    Doc doc;
    const auto dir = bundleDir("Legacy");
    ASSERT_TRUE(doc.save(dir, nullptr).ok);
    EXPECT_FALSE(juce::JSON::parse(dir.getChildFile(ProjectBundle::kProjectFileName)).hasProperty("transport"));

    Doc fresh;
    TransportDoc loaded = makeCustomTransport();
    ASSERT_TRUE(fresh.load(dir, &loaded).ok);
    EXPECT_EQ(loaded, TransportDoc{});
    EXPECT_DOUBLE_EQ(loaded.bpm, 120.0);
    EXPECT_EQ(loaded.timeSigNumerator, 4);
    EXPECT_EQ(loaded.timeSigDenominator, 4);
    EXPECT_DOUBLE_EQ(loaded.loopStartBeat, 0.0);
    EXPECT_DOUBLE_EQ(loaded.loopEndBeat, 4.0);
    EXPECT_FALSE(loaded.loopEnabled);
}

TEST_F(ProjectBundleTransportTest, LoadIgnoresTransportWhenCallerPassesNoOutParameter) {
    Doc doc;
    const auto dir = bundleDir("NoOut");
    const auto transport = makeCustomTransport();
    ASSERT_TRUE(doc.save(dir, &transport).ok);

    Doc fresh;
    EXPECT_TRUE(fresh.load(dir, nullptr).ok);
}

TEST_F(ProjectBundleTransportTest, MalformedTransportRejectsTheWholeFile) {
    juce::String message;
    EXPECT_FALSE(loadWithTransportValue(withProperty("bpm", 2000.0), message));
    EXPECT_TRUE(message.contains("transport")) << message;

    EXPECT_FALSE(loadWithTransportValue(withProperty("timeSigDen", 3), message));
    EXPECT_TRUE(message.contains("transport")) << message;

    EXPECT_FALSE(loadWithTransportValue(withProperty("bpm", "fast"), message));
    EXPECT_TRUE(message.contains("transport")) << message;

    auto missingField = validVar();
    missingField.getDynamicObject()->removeProperty("loopEnabled");
    EXPECT_FALSE(loadWithTransportValue(missingField, message));
    EXPECT_TRUE(message.contains("transport")) << message;

    EXPECT_FALSE(loadWithTransportValue(juce::var("not an object"), message));
    EXPECT_TRUE(message.contains("transport")) << message;
}

TEST_F(ProjectBundleTransportTest, MalformedTransportInAutosaveRejectsTheFile) {
    Doc doc;
    const auto dir = bundleDir("BadAutosave");
    ASSERT_TRUE(doc.save(dir, nullptr).ok);
    ASSERT_TRUE(
        ProjectBundle::saveAutosave(dir, doc.graph, doc.timeline, doc.patchDocument, doc.macros, 0, doc.midiRemote).ok);
    setTransportInFile(dir, withProperty("bpm", -1.0), ProjectBundle::kAutosaveFileName);

    Doc fresh;
    const auto result = ProjectBundle::loadAutosave(dir, fresh.graph, fresh.timeline, fresh.patchDocument, fresh.macros,
                                                    fresh.midiRemote);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("transport")) << result.message;
}

TEST_F(ProjectBundleTransportTest, TransportIsNotStashedInThePatchDocument) {
    Doc doc;
    const auto dir = bundleDir("NoStash");
    const auto transport = makeCustomTransport();
    ASSERT_TRUE(doc.save(dir, &transport).ok);

    Doc fresh;
    TransportDoc loaded;
    ASSERT_TRUE(fresh.load(dir, &loaded).ok);

    // Whatever the patch document would merge into a save must not carry the key: the live value is
    // written by ProjectBundle::save, never replayed from a stash.
    auto* base = new juce::DynamicObject();
    const auto merged = fresh.patchDocument.toVar(juce::var(base));
    EXPECT_FALSE(merged.hasProperty("transport"));

    // A resave with a different live value wins, and the file carries the new one.
    auto changed = transport;
    changed.bpm = 140.0;
    ASSERT_TRUE(fresh.save(dir, &changed).ok);
    TransportDoc reloaded;
    ASSERT_TRUE(fresh.load(dir, &reloaded).ok);
    EXPECT_EQ(reloaded, changed);
}
