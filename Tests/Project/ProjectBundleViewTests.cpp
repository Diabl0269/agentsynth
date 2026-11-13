// Concern: the "view" key of a project bundle (canvas zoom and pan) through save/load and the autosave
// sidecar: present when saved, absent leaves the caller's view alone, malformed rejects the whole file.
#include "Modules/OscillatorModule.h"
#include "PatchDocument.h"
#include "Project/ViewDoc.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::PatchDocument;
using synth::ProjectBundle;
using synth::ViewDoc;

namespace {

ViewDoc customView() {
    ViewDoc v;
    v.zoom = 0.5f;
    v.panX = -200.0f;
    v.panY = 75.0f;
    return v;
}

struct Doc {
    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    PatchDocument patchDocument;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc midiRemote;

    Doc() { graph.addNode(std::make_unique<OscillatorModule>()); }

    synth::ProjectLoadResult save(const juce::File& dir, const ViewDoc* view) {
        return ProjectBundle::save(dir, graph, timeline, patchDocument, macros, midiRemote, synth::MixerPanLaw::Balance,
                                   nullptr, nullptr, view);
    }
    synth::ProjectLoadResult load(const juce::File& dir, ViewDoc* out) {
        return ProjectBundle::load(dir, graph, timeline, patchDocument, macros, midiRemote, nullptr, nullptr, nullptr,
                                   out);
    }
};

} // namespace

class ProjectBundleViewTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = synth::userSettingsRootDirectory().getChildFile("agentsynth-projectbundle-view-tests");
        root.deleteRecursively();
        root.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File bundleDir(const juce::String& name) { return root.getChildFile(name + ProjectBundle::kBundleExtension); }

    static void setViewInFile(const juce::File& dir, const juce::var& value, const char* fileName) {
        const auto file = dir.getChildFile(fileName);
        auto json = juce::JSON::parse(file);
        auto* obj = json.getDynamicObject();
        ASSERT_NE(obj, nullptr);
        if (value.isVoid())
            obj->removeProperty("view");
        else
            obj->setProperty("view", value);
        ASSERT_TRUE(file.replaceWithText(juce::JSON::toString(json)));
    }

    // Saves a valid bundle, replaces its view value, then loads into a fresh empty document whose out-parameter
    // holds a sentinel. A rejected load must touch neither the graph nor the out-parameter.
    bool loadWithViewValue(const juce::var& value, juce::String& message) {
        Doc src;
        const auto dir = bundleDir("Tampered");
        const auto saveResult = src.save(dir, nullptr);
        EXPECT_TRUE(saveResult.ok) << saveResult.message;
        setViewInFile(dir, value, ProjectBundle::kProjectFileName);

        Doc fresh;
        fresh.graph.clear();
        ViewDoc out = customView();
        const auto result = fresh.load(dir, &out);
        message = result.message;
        if (!result.ok) {
            EXPECT_EQ(fresh.graph.getNumNodes(), 0) << "a rejected load must not touch the graph";
            EXPECT_EQ(out, customView()) << "a rejected load must not touch the out-parameter";
        }
        return result.ok;
    }

    juce::File root;
};

TEST_F(ProjectBundleViewTest, SaveWritesViewKeyAndItRoundTrips) {
    Doc doc;
    const auto dir = bundleDir("RoundTrip");
    const auto view = customView();
    ASSERT_TRUE(doc.save(dir, &view).ok);
    EXPECT_TRUE(juce::JSON::parse(dir.getChildFile(ProjectBundle::kProjectFileName)).hasProperty("view"));

    Doc fresh;
    ViewDoc loaded;
    const auto result = fresh.load(dir, &loaded);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(loaded, view);
}

TEST_F(ProjectBundleViewTest, SaveWithoutAViewWritesNoKey) {
    Doc doc;
    const auto dir = bundleDir("NoKey");
    ASSERT_TRUE(doc.save(dir, nullptr).ok);
    EXPECT_FALSE(juce::JSON::parse(dir.getChildFile(ProjectBundle::kProjectFileName)).hasProperty("view"));
}

TEST_F(ProjectBundleViewTest, AbsentViewLeavesTheCallersViewUntouched) {
    Doc doc;
    const auto dir = bundleDir("Legacy");
    ASSERT_TRUE(doc.save(dir, nullptr).ok);

    Doc fresh;
    ViewDoc current = customView();
    ASSERT_TRUE(fresh.load(dir, &current).ok);
    EXPECT_EQ(current, customView()) << "a project that never saved a view must not change the current one";
}

TEST_F(ProjectBundleViewTest, LoadIgnoresViewWhenCallerPassesNoOutParameter) {
    Doc doc;
    const auto dir = bundleDir("NoOut");
    const auto view = customView();
    ASSERT_TRUE(doc.save(dir, &view).ok);

    Doc fresh;
    EXPECT_TRUE(fresh.load(dir, nullptr).ok);
}

TEST_F(ProjectBundleViewTest, ViewRoundTripsThroughAutosave) {
    Doc doc;
    const auto dir = bundleDir("Autosave");
    const auto view = customView();
    ASSERT_TRUE(doc.save(dir, nullptr).ok);
    ASSERT_TRUE(ProjectBundle::saveAutosave(dir, doc.graph, doc.timeline, doc.patchDocument, doc.macros, 0,
                                            doc.midiRemote, synth::MixerPanLaw::Balance, nullptr, nullptr, &view)
                    .ok);

    Doc fresh;
    ViewDoc loaded;
    const auto result = ProjectBundle::loadAutosave(dir, fresh.graph, fresh.timeline, fresh.patchDocument, fresh.macros,
                                                    fresh.midiRemote, nullptr, nullptr, nullptr, &loaded);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(loaded, view);
}

TEST_F(ProjectBundleViewTest, MalformedViewRejectsTheWholeFile) {
    juce::String message;
    EXPECT_FALSE(loadWithViewValue(juce::var("not an object"), message));
    EXPECT_TRUE(message.contains("view")) << message;

    auto badZoom = customView().toVar();
    badZoom.getDynamicObject()->setProperty("zoom", "big");
    EXPECT_FALSE(loadWithViewValue(badZoom, message));
    EXPECT_TRUE(message.contains("view validation failed")) << message;

    auto missingPan = customView().toVar();
    missingPan.getDynamicObject()->removeProperty("panY");
    EXPECT_FALSE(loadWithViewValue(missingPan, message));
    EXPECT_TRUE(message.contains("view validation failed")) << message;
}

TEST_F(ProjectBundleViewTest, OutOfRangeViewClampsInsteadOfRejecting) {
    auto wild = customView().toVar();
    wild.getDynamicObject()->setProperty("zoom", 99.0);
    Doc src;
    const auto dir = bundleDir("Clamp");
    ASSERT_TRUE(src.save(dir, nullptr).ok);
    setViewInFile(dir, wild, ProjectBundle::kProjectFileName);

    Doc fresh;
    ViewDoc loaded;
    ASSERT_TRUE(fresh.load(dir, &loaded).ok);
    EXPECT_FLOAT_EQ(loaded.zoom, ViewDoc::kMaxZoom);
}

TEST_F(ProjectBundleViewTest, ViewIsNotStashedInThePatchDocument) {
    Doc doc;
    const auto dir = bundleDir("NoStash");
    const auto view = customView();
    ASSERT_TRUE(doc.save(dir, &view).ok);

    Doc fresh;
    ViewDoc loaded;
    ASSERT_TRUE(fresh.load(dir, &loaded).ok);
    const auto merged = fresh.patchDocument.toVar(juce::var(new juce::DynamicObject()));
    EXPECT_FALSE(merged.hasProperty("view"));
}
