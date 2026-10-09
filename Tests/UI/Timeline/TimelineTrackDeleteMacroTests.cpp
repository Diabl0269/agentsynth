// TimelineTrackDeleteMacroTests.cpp: deleting a track also removes the macro it lives in and the modules only it used,
// in the same undo step, and Cmd+Z brings all of it back in place with nothing extra on the canvas. Drives a real
// off-screen MainComponent through the row menu's Delete Track item, then the app's undo manager, so the real command
// path runs; the unit rule is in Timeline/TrackRemovalSetTests.cpp.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "ProjectBundle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <map>
#include <set>

namespace {

class MockProviderTDM : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockTDM"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// Everything an undo has to give back, as comparable values.
struct State {
    std::map<juce::String, std::pair<int, int>> positions; // node uuid -> x, y
    std::set<juce::String> nodes;
    std::set<juce::String> cables; // "srcUuid:ch>dstUuid:ch"
    juce::var macros;
    juce::var timeline;
};

struct Rig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderTDM>()};

    explicit Rig(int tracks = 2) {
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc.simulateAddAudioTrackClick();
        mc.getTimelinePanel().setSize(1400, 360);
    }

    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }
    GraphEditor& editor() { return mc.getGraphEditor(); }
    synth::MacroSet& macros() { return editor().getMacros(); }
    synth::TimelineDoc& doc() { return mc.getTimelineDoc(); }
    AppUndoManager& undo() { return mc.getUndoManager(); }

    static juce::String uuidOf(const juce::AudioProcessorGraph::Node* node) {
        return node != nullptr ? node->properties["uuid"].toString() : juce::String();
    }
    juce::AudioProcessorGraph::Node* nodeByUuid(const juce::String& uuid) {
        for (auto* node : graph().getNodes())
            if (uuidOf(node) == uuid)
                return node;
        return nullptr;
    }
    juce::String boundUuid(int trackIndex) { return doc().getTracks()[(size_t)trackIndex].bindingUuid; }
    const synth::Macro* macroOfTrack(int trackIndex) { return macros().findByMember(boundUuid(trackIndex)); }
    // The node of type `name` inside the track's macro.
    juce::AudioProcessorGraph::Node* memberNamed(int trackIndex, const juce::String& name) {
        const auto* macro = macroOfTrack(trackIndex);
        if (macro == nullptr)
            return nullptr;
        for (const auto& uuid : macro->members)
            if (auto* node = nodeByUuid(uuid); node != nullptr && node->getProcessor()->getName() == name)
                return node;
        return nullptr;
    }

    // An LFO with a position of its own, modulating `paramId` of `target`.
    juce::AudioProcessorGraph::Node::Ptr addLfoInto(juce::AudioProcessorGraph::Node& target,
                                                    const juce::String& paramId, int x, int y) {
        auto lfo = graph().addNode(synth::AIStateMapper::createModule("LFO"));
        const auto uuid = juce::Uuid().toDashedString();
        lfo->properties.set("uuid", uuid);
        lfo->properties.set("x", x);
        lfo->properties.set("y", y);
        if (auto* module = dynamic_cast<ModuleBase*>(lfo->getProcessor()))
            module->setNodeUuid(uuid);
        editor().updateComponents();
        const int raw = editor().modulationChannelFor(target.nodeID, paramId);
        EXPECT_GE(raw, 0);
        EXPECT_NE(editor().connectModulationSource(lfo->nodeID, 0, target.nodeID, raw, 0.5f).uid, 0u);
        return lfo;
    }

    int countOf(const juce::String& name) {
        int n = 0;
        for (auto* node : graph().getNodes())
            n += node->getProcessor()->getName() == name ? 1 : 0;
        return n;
    }

    State state() {
        State s;
        for (auto* node : graph().getNodes()) {
            const auto uuid = uuidOf(node);
            s.nodes.insert(uuid);
            s.positions[uuid] = {(int)node->properties.getWithDefault("x", 0),
                                 (int)node->properties.getWithDefault("y", 0)};
        }
        for (const auto& c : graph().getConnections())
            s.cables.insert(uuidOf(graph().getNodeForId(c.source.nodeID)) + ":" + juce::String(c.source.channelIndex) +
                            ">" + uuidOf(graph().getNodeForId(c.destination.nodeID)) + ":" +
                            juce::String(c.destination.channelIndex));
        s.macros = macros().toVar();
        s.timeline = doc().toVar();
        return s;
    }

    void deleteTrackRow(int index) {
        auto* header = mc.getTimelinePanel().getTrackHeaderAt(index);
        ASSERT_NE(header, nullptr);
        header->applyContextMenuChoice(synth::ui::TimelineTrackHeaderComponent::kDeleteTrackMenuId);
    }

    // Cards that show on the canvas by themselves: not inside a macro and not part of the output dock.
    std::set<juce::String> freeStandingCards() {
        std::set<juce::String> out;
        for (auto* comp : editor().getModuleComponents()) {
            auto* node = graph().getNodeForId(comp->getNodeId());
            if (comp == nullptr || node == nullptr || !comp->isVisible() ||
                editor().isOutputDockNode(comp->getNodeId()))
                continue;
            out.insert(uuidOf(node));
        }
        return out;
    }
};

void expectSameState(const State& a, const State& b) {
    EXPECT_EQ(a.nodes, b.nodes);
    EXPECT_EQ(a.positions, b.positions) << "every node back at its old position";
    EXPECT_EQ(a.cables, b.cables) << "every cable back";
    EXPECT_TRUE(synth::sameJson(a.macros, b.macros)) << "the macro back with the same members, ports, place and fold";
    EXPECT_TRUE(synth::sameJson(a.timeline, b.timeline)) << "the track back with its lanes and automation";
}

} // namespace

TEST(TimelineTrackDeleteMacro, TheTracksMacroAndItsCardsLeaveWithIt) {
    Rig rig;
    ASSERT_NE(rig.macroOfTrack(0), nullptr);
    const auto victimMacro = rig.macroOfTrack(0)->id;
    const auto keeperMacro = rig.macroOfTrack(1)->id;
    const auto victimUuid = rig.boundUuid(0);
    const int compressorsBefore = rig.countOf("Compressor");

    rig.deleteTrackRow(0);

    EXPECT_EQ(rig.doc().getTracks().size(), 1u);
    EXPECT_EQ(rig.macros().find(victimMacro), nullptr) << "no macro box is left behind";
    EXPECT_NE(rig.macros().find(keeperMacro), nullptr);
    EXPECT_EQ(rig.nodeByUuid(victimUuid), nullptr);
    EXPECT_EQ(rig.countOf("Compressor"), compressorsBefore - 1) << "its Gate, EQ and Compressor went with the macro";
    EXPECT_EQ(rig.macros().size(), 1);
}

TEST(TimelineTrackDeleteMacro, AnLfoOnlyThatTrackUsedGoesAndOneAnotherTrackAlsoUsesStays) {
    Rig rig;
    auto* ownCompressor = rig.memberNamed(0, "Compressor");
    auto* otherCompressor = rig.memberNamed(1, "Compressor");
    ASSERT_NE(ownCompressor, nullptr);
    ASSERT_NE(otherCompressor, nullptr);
    auto onlyThis = rig.addLfoInto(*ownCompressor, "threshold", 40, 40);
    auto shared = rig.addLfoInto(*rig.memberNamed(0, "Compressor"), "ratio", 40, 200);
    ASSERT_NE(rig.editor()
                  .connectModulationSource(
                      shared->nodeID, 0, rig.memberNamed(1, "Compressor")->nodeID,
                      rig.editor().modulationChannelFor(rig.memberNamed(1, "Compressor")->nodeID, "ratio"), 0.5f)
                  .uid,
              0u);
    const auto onlyThisUuid = Rig::uuidOf(onlyThis.get());
    const auto sharedUuid = Rig::uuidOf(shared.get());
    const int attenuvertersBefore = rig.countOf("Attenuverter");
    ASSERT_EQ(attenuvertersBefore, 3);

    rig.deleteTrackRow(0);

    EXPECT_EQ(rig.nodeByUuid(onlyThisUuid), nullptr) << "wired only to the deleted track";
    ASSERT_NE(rig.nodeByUuid(sharedUuid), nullptr) << "another track uses it too";
    EXPECT_EQ(rig.countOf("Attenuverter"), 1)
        << "its link to the other track is intact, the links into the deleted one are gone";

    ASSERT_TRUE(rig.undo().undo());
    EXPECT_NE(rig.nodeByUuid(onlyThisUuid), nullptr);
    EXPECT_EQ(rig.countOf("Attenuverter"), attenuvertersBefore) << "the link to the first track is back";
}

TEST(TimelineTrackDeleteMacro, ALonelyModuleWithNoCableIsLeftAlone) {
    Rig rig;
    auto loner = rig.graph().addNode(synth::AIStateMapper::createModule("LFO"));
    loner->properties.set("uuid", juce::Uuid().toDashedString());
    rig.editor().updateComponents();
    rig.deleteTrackRow(0);
    EXPECT_NE(rig.nodeByUuid(Rig::uuidOf(loner.get())), nullptr);
}

TEST(TimelineTrackDeleteMacro, OneUndoBringsEverythingBackInPlaceAndOneRedoRemovesItAgain) {
    Rig rig;
    auto* compressor = rig.memberNamed(0, "Compressor");
    auto lfo = rig.addLfoInto(*compressor, "threshold", 40, 40);
    ASSERT_NE(lfo, nullptr);
    const auto before = rig.state();
    const auto cardsBefore = rig.freeStandingCards();
    rig.undo().beginNewTransaction();

    rig.deleteTrackRow(0);
    const auto deleted = rig.state();
    EXPECT_LT(deleted.nodes.size(), before.nodes.size());

    ASSERT_TRUE(rig.undo().undo()) << "one step";
    expectSameState(before, rig.state());
    EXPECT_EQ(rig.freeStandingCards(), cardsBefore) << "no extra node sits on the canvas";
    const auto* macro = rig.macroOfTrack(0);
    ASSERT_NE(macro, nullptr) << "the track's node is inside its macro again";
    EXPECT_TRUE(macro->collapsed);
    EXPECT_EQ(rig.freeStandingCards().count(rig.boundUuid(0)), 0u) << "not a loose node over its macro";

    ASSERT_TRUE(rig.undo().redo());
    expectSameState(deleted, rig.state());
    ASSERT_TRUE(rig.undo().undo());
    expectSameState(before, rig.state());
}

TEST(TimelineTrackDeleteMacro, UndoLeavesNoLooseNodeEvenWithoutAnyModulator) {
    Rig rig;
    const auto before = rig.state();
    const auto cardsBefore = rig.freeStandingCards();
    rig.deleteTrackRow(1);
    ASSERT_TRUE(rig.undo().undo());
    expectSameState(before, rig.state());
    EXPECT_EQ(rig.freeStandingCards(), cardsBefore);
    ASSERT_NE(rig.macroOfTrack(1), nullptr);
    EXPECT_TRUE(rig.macroOfTrack(1)->hasMember(rig.boundUuid(1)));
}

// The project as a fresh app opens it after a save, so a restore that left something the file format drops, or a node
// outside its macro, shows up as a difference from the same save taken before the delete.
static State savedAndReopened(Rig& rig, bool* freeStandingMatches = nullptr) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getNonexistentChildFile("fro656-project", ".agsproj", false);
    EXPECT_TRUE(rig.mc.saveProjectForTest(dir));
    Rig loaded(0);
    EXPECT_TRUE(loaded.mc.openProjectForTest(dir));
    if (freeStandingMatches != nullptr)
        *freeStandingMatches = loaded.freeStandingCards() == rig.freeStandingCards();
    dir.deleteRecursively();
    return loaded.state();
}

TEST(TimelineTrackDeleteMacro, ASavedProjectAfterDeleteAndUndoOpensAsItDidWithoutTheDelete) {
    Rig rig;
    rig.addLfoInto(*rig.memberNamed(0, "Compressor"), "threshold", 40, 40);
    const auto before = rig.state();
    const auto reopenedBefore = savedAndReopened(rig);
    rig.deleteTrackRow(0);
    ASSERT_TRUE(rig.undo().undo());
    expectSameState(before, rig.state());

    bool sameCards = false;
    const auto reopenedAfter = savedAndReopened(rig, &sameCards);
    expectSameState(reopenedBefore, reopenedAfter);
    EXPECT_TRUE(sameCards);
}

TEST(TimelineTrackDeleteMacro, TheMacroCardAndTheRowAnimateTogetherAndUndoReversesThem) {
    ReducedMotionGuard guard(false);
    Rig rig;
    rig.mc.getTimelinePanel().forceTrackGlideForTest(true);
    auto& glide = rig.editor().getCardGlideForTest();
    glide.setForceAnimateForTest(true);
    ASSERT_NE(rig.macroOfTrack(0), nullptr);

    rig.deleteTrackRow(0);
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning()) << "the row shrinks";
    EXPECT_GE(glide.exitGhostCount(), 1) << "and the macro card shrinks with it";
    glide.finish();
    rig.mc.getTimelinePanel().getTrackListMotionForTest().finishNow();

    ASSERT_TRUE(rig.undo().undo());
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning()) << "the row grows back";
    EXPECT_GE(glide.enterGhostCount(), 1) << "with the macro card";
}
