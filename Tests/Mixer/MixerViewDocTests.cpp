// MixerViewDocTests.cpp: the MixerViewDoc value type and its project-file round trip -- zones and
// hidden channels survive save and load by their stable ids, and a missing, malformed or dangling
// "mixerView" never blocks opening a project. The AI-facing refusal of the key is in
// AIPatchValidationTests.
#include "../UI/Mixer/MixerZonesTestRig.h"
#include "ProjectBundle.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <gtest/gtest.h>

using synth::MixerViewDoc;
using synth::MixerZone;

TEST(MixerViewDocTests, DefaultsAreScrollingAndShown) {
    MixerViewDoc doc;
    EXPECT_EQ(doc.getZone("x"), MixerZone::Scrolling);
    EXPECT_FALSE(doc.isHidden("x"));
    EXPECT_TRUE(doc.isEmpty());
}

TEST(MixerViewDocTests, ANewProjectPinsMasterRight) {
    const auto doc = MixerViewDoc::forNewProject();
    EXPECT_EQ(doc.getZone(MixerViewDoc::kMasterId), MixerZone::Right);
    EXPECT_EQ(doc.getHiddenCount(), 0);
}

TEST(MixerViewDocTests, MasterCannotBeHiddenAndScrollingClearsAZone) {
    MixerViewDoc doc;
    doc.setHidden(MixerViewDoc::kMasterId, true);
    EXPECT_FALSE(doc.isHidden(MixerViewDoc::kMasterId));
    doc.setZone("a", MixerZone::Left);
    doc.setZone("a", MixerZone::Scrolling);
    EXPECT_TRUE(doc.isEmpty());
}

TEST(MixerViewDocTests, VarRoundTripKeepsZonesAndHiddenAndIsDeterministic) {
    MixerViewDoc doc;
    doc.setZone("b", MixerZone::Right);
    doc.setZone("a", MixerZone::Left);
    doc.setHidden("z", true);
    doc.setHidden("direct", true);

    MixerViewDoc copy;
    ASSERT_TRUE(copy.fromVar(doc.toVar()));
    EXPECT_TRUE(copy == doc);
    EXPECT_EQ(juce::JSON::toString(copy.toVar()), juce::JSON::toString(doc.toVar()));
}

TEST(MixerViewDocTests, FromVarIsAllOrNothing) {
    MixerViewDoc doc;
    doc.setZone("keep", MixerZone::Left);
    const auto before = doc;
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(R"({"zones":{"a":"sideways"},"hidden":[]})")));
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(R"({"zones":{"a":"left"},"hidden":[1]})")));
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(R"([1,2])")));
    EXPECT_TRUE(doc == before) << "a rejected value leaves the document untouched";
    EXPECT_TRUE(doc.fromVar(juce::JSON::parse(R"({})"))) << "missing members just mean empty";
    EXPECT_TRUE(doc.isEmpty());
}

TEST(MixerViewDocTests, RetainOnlyDropsDanglingIdsButKeepsTheFixedOnes) {
    MixerViewDoc doc;
    doc.setZone("alive", MixerZone::Left);
    doc.setZone("gone", MixerZone::Right);
    doc.setZone(MixerViewDoc::kMasterId, MixerZone::Right);
    doc.setHidden("gone2", true);
    doc.setHidden(MixerViewDoc::kDirectId, true);
    doc.retainOnly({"alive"});
    EXPECT_EQ(doc.getZone("alive"), MixerZone::Left);
    EXPECT_EQ(doc.getZone("gone"), MixerZone::Scrolling);
    EXPECT_EQ(doc.getZone(MixerViewDoc::kMasterId), MixerZone::Right);
    EXPECT_FALSE(doc.isHidden("gone2"));
    EXPECT_TRUE(doc.isHidden(MixerViewDoc::kDirectId));
}

TEST(MixerViewDocTests, BusOrderRoundTripsThroughVarAndDropsEmptyAndDuplicateIds) {
    MixerViewDoc doc;
    doc.setBusOrder({"b", "", "a", "b"});
    EXPECT_EQ(doc.getBusOrder(), (std::vector<juce::String>{"b", "a"}));
    EXPECT_FALSE(doc.isEmpty());

    MixerViewDoc copy;
    ASSERT_TRUE(copy.fromVar(doc.toVar()));
    EXPECT_EQ(copy.getBusOrder(), (std::vector<juce::String>{"b", "a"})) << "the saved order is kept, not sorted";
    EXPECT_TRUE(copy == doc);
    MixerViewDoc other;
    EXPECT_TRUE(other != doc) << "operator== sees the bus order";
}

TEST(MixerViewDocTests, FromVarRejectsANonStringBusOrderItemAndLeavesTheDocumentUntouched) {
    MixerViewDoc doc;
    doc.setBusOrder({"keep"});
    const auto before = doc;
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(R"({"busOrder":["a",1]})")));
    EXPECT_FALSE(doc.fromVar(juce::JSON::parse(R"({"busOrder":"a"})")));
    EXPECT_TRUE(doc == before);
    EXPECT_TRUE(doc.fromVar(juce::JSON::parse(R"({"zones":{}})")));
    EXPECT_TRUE(doc.getBusOrder().empty()) << "a missing key means no saved order";
}

TEST(MixerViewDocTests, RetainOnlyPrunesDeadBusIds) {
    MixerViewDoc doc;
    doc.setBusOrder({"a", "gone", "b"});
    doc.retainOnly({"a", "b"});
    EXPECT_EQ(doc.getBusOrder(), (std::vector<juce::String>{"a", "b"}));
}

TEST(MixerViewDocTests, OrderBusesPutsSavedIdsFirstAndUnknownOnesAfterInInputOrder) {
    MixerViewDoc doc;
    doc.setBusOrder({"c", "a"});
    EXPECT_EQ(doc.orderBuses({"x", "a", "y", "c"}), (std::vector<juce::String>{"c", "a", "x", "y"}));
    EXPECT_EQ(MixerViewDoc().orderBuses({"x", "a"}), (std::vector<juce::String>{"x", "a"}));
}

namespace {

struct LoadedProject {
    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    synth::PatchDocument patch;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc midi;
    MixerViewDoc view;
    synth::ProjectLoadResult result;
};

struct TempBundle {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("mixerview-" + juce::Uuid().toString())
                         .withFileExtension(synth::ProjectBundle::kBundleExtension);
    ~TempBundle() { dir.deleteRecursively(); }
};

} // namespace

// The stable ids are the strips' node uuids, so a project saved from one graph and loaded into a
// brand-new one (fresh NodeIDs) finds the same zones and hidden flags on the same channels.
TEST(MixerViewDocTests, ProjectSaveAndLoadRoundTripsZonesAndHiddenChannelsByStableId) {
    MixerZonesRig r(3);
    const auto a = r.stripId(0);
    const auto b = r.stripId(1);
    const auto c = r.stripId(2);
    r.panel->pinChannel(a, MixerZone::Left);
    r.panel->pinChannel(b, MixerZone::Right);
    r.panel->setChannelHidden(c, true);
    r.panel->setChannelHidden(MixerViewDoc::kDirectId, true);
    ASSERT_TRUE(a.isNotEmpty() && b.isNotEmpty() && c.isNotEmpty());

    TempBundle bundle;
    synth::MidiRemoteProjectDoc midi;
    auto& liveGraph = r.mc.getAudioEngine().getGraph();
    ASSERT_TRUE(synth::ProjectBundle::save(bundle.dir, liveGraph, r.mc.getTimelineDoc(),
                                           r.mc.getGraphEditor().getPatchDocument(), r.mc.getGraphEditor().getMacros(),
                                           midi, synth::MixerPanLaw::Balance, &r.panel->getViewDoc())
                    .ok);
    EXPECT_TRUE(bundle.dir.getChildFile("project.json").loadFileAsString().contains("\"mixerView\""));

    LoadedProject loaded;
    loaded.result = synth::ProjectBundle::load(bundle.dir, loaded.graph, loaded.timeline, loaded.patch, loaded.macros,
                                               loaded.midi, nullptr, &loaded.view);
    ASSERT_TRUE(loaded.result.ok) << loaded.result.message.toStdString();
    EXPECT_EQ(loaded.view.getZone(a), MixerZone::Left);
    EXPECT_EQ(loaded.view.getZone(b), MixerZone::Right);
    EXPECT_TRUE(loaded.view.isHidden(c));
    EXPECT_TRUE(loaded.view.isHidden(MixerViewDoc::kDirectId));
    EXPECT_EQ(loaded.view.getZone(MixerViewDoc::kMasterId), MixerZone::Right);
    bool foundLoadedStrip = false;
    for (auto* node : loaded.graph.getNodes())
        foundLoadedStrip = foundLoadedStrip || node->properties["uuid"].toString() == a;
    EXPECT_TRUE(foundLoadedStrip) << "the id names a node that exists in the loaded graph";

    // The autosave sidecar carries the same key.
    ASSERT_TRUE(synth::ProjectBundle::saveAutosave(
                    bundle.dir, liveGraph, r.mc.getTimelineDoc(), r.mc.getGraphEditor().getPatchDocument(),
                    r.mc.getGraphEditor().getMacros(), 0, midi, synth::MixerPanLaw::Balance, &r.panel->getViewDoc())
                    .ok);
    LoadedProject recovered;
    recovered.result =
        synth::ProjectBundle::loadAutosave(bundle.dir, recovered.graph, recovered.timeline, recovered.patch,
                                           recovered.macros, recovered.midi, nullptr, &recovered.view);
    ASSERT_TRUE(recovered.result.ok);
    EXPECT_TRUE(recovered.view == loaded.view);
}

TEST(MixerViewDocTests, AProjectWithoutTheKeyLoadsWithEveryColumnScrollingAndShown) {
    MixerZonesRig r(1);
    TempBundle bundle;
    synth::MidiRemoteProjectDoc midi;
    ASSERT_TRUE(synth::ProjectBundle::save(bundle.dir, r.mc.getAudioEngine().getGraph(), r.mc.getTimelineDoc(),
                                           r.mc.getGraphEditor().getPatchDocument(), r.mc.getGraphEditor().getMacros(),
                                           midi)
                    .ok);
    EXPECT_FALSE(bundle.dir.getChildFile("project.json").loadFileAsString().contains("mixerView"));

    LoadedProject loaded;
    loaded.view.setZone("stale", MixerZone::Left);
    loaded.result = synth::ProjectBundle::load(bundle.dir, loaded.graph, loaded.timeline, loaded.patch, loaded.macros,
                                               loaded.midi, nullptr, &loaded.view);
    ASSERT_TRUE(loaded.result.ok);
    EXPECT_TRUE(loaded.view.isEmpty()) << "an older project keeps its all-scrolling layout";
}

TEST(MixerViewDocTests, AMalformedOrDanglingKeyNeverBlocksOpeningTheProject) {
    MixerZonesRig r(1);
    TempBundle bundle;
    synth::MidiRemoteProjectDoc midi;
    MixerViewDoc view;
    view.setZone("no-such-node", MixerZone::Left);
    ASSERT_TRUE(synth::ProjectBundle::save(bundle.dir, r.mc.getAudioEngine().getGraph(), r.mc.getTimelineDoc(),
                                           r.mc.getGraphEditor().getPatchDocument(), r.mc.getGraphEditor().getMacros(),
                                           midi, synth::MixerPanLaw::Balance, &view)
                    .ok);
    {
        LoadedProject loaded;
        loaded.result = synth::ProjectBundle::load(bundle.dir, loaded.graph, loaded.timeline, loaded.patch,
                                                   loaded.macros, loaded.midi, nullptr, &loaded.view);
        ASSERT_TRUE(loaded.result.ok);
        EXPECT_EQ(loaded.view.getZone("no-such-node"), MixerZone::Scrolling) << "ids with no node are pruned";
    }

    auto file = bundle.dir.getChildFile("project.json");
    auto json = juce::JSON::parse(file);
    json.getDynamicObject()->setProperty("mixerView", "garbage");
    ASSERT_TRUE(file.replaceWithText(juce::JSON::toString(json)));
    LoadedProject loaded;
    loaded.result = synth::ProjectBundle::load(bundle.dir, loaded.graph, loaded.timeline, loaded.patch, loaded.macros,
                                               loaded.midi, nullptr, &loaded.view);
    EXPECT_TRUE(loaded.result.ok);
    EXPECT_TRUE(loaded.view.isEmpty());
}

TEST(MixerViewDocTests, NewPatchStartsAgainWithMasterPinnedRightAndNothingHidden) {
    MixerZonesRig r(2);
    r.panel->pinChannel(MixerViewDoc::kMasterId, MixerZone::Left);
    r.panel->setChannelHidden(r.stripId(0), true);
    r.mc.newPatchForTest();
    EXPECT_TRUE(r.panel->getViewDoc() == MixerViewDoc::forNewProject());
    r.mc.simulateAddAudioTrackClick(); // the channels the fresh project's mixer lists
    r.panel->rebuild();
    ASSERT_NE(r.panel->getMasterColumnForTest(), nullptr);
    EXPECT_TRUE(r.panel->getMasterColumnForTest()->isVisible());
    EXPECT_EQ(r.panel->getColumnZoneForTest(r.panel->getColumnCount() - 1), MixerZone::Right);
    EXPECT_EQ(r.panel->getColumnCount(), 3) << "the new track, Direct and Master: nothing is hidden";
}
