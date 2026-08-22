#include "AI/AIIntegrationService.h"
#include "AI/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MainComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <vector>

namespace synth {
namespace {

/**
 * Structural fingerprint of a graph: the multiset of node type names plus the multiset of
 * connections described by processor name + channel. Deliberately ID-free — restoring a
 * snapshot rebuilds the graph from scratch, so juce NodeIDs are renumbered and comparing
 * them would fail for a graph that is otherwise identical.
 */
struct GraphShape {
    std::multiset<juce::String> nodeTypes;
    std::multiset<juce::String> connections;

    bool operator==(const GraphShape& other) const {
        return nodeTypes == other.nodeTypes && connections == other.connections;
    }
};

GraphShape captureShape(juce::AudioProcessorGraph& g) {
    GraphShape shape;

    for (auto* node : g.getNodes())
        shape.nodeTypes.insert(node->getProcessor()->getName());

    for (const auto& c : g.getConnections()) {
        auto* src = g.getNodeForId(c.source.nodeID);
        auto* dst = g.getNodeForId(c.destination.nodeID);
        shape.connections.insert((src ? src->getProcessor()->getName() : juce::String("?")) + ":" +
                                 juce::String(c.source.channelIndex) + "->" +
                                 (dst ? dst->getProcessor()->getName() : juce::String("?")) + ":" +
                                 juce::String(c.destination.channelIndex));
    }

    return shape;
}

/** Records the AI patch notifications, including their order, so undo/redo can be checked. */
class RecordingListener : public AIIntegrationService::Listener {
public:
    int aboutToApplyCount = 0;
    int appliedCount = 0;
    std::vector<juce::String> events;

    void aiPatchAboutToApply() override {
        ++aboutToApplyCount;
        events.push_back("aboutToApply");
    }

    void aiPatchApplied() override {
        ++appliedCount;
        events.push_back("applied");
    }
};

// A full-replacement patch: three nodes with a distinct shape from the baseline below. Ends in an
// Audio Output reachable from the Oscillator — AIIntegrationService::applyPatch()'s structural
// gate rejects a replace-mode patch that doesn't, and installBaselinePatch() below deliberately
// has no Audio Output, so a bare Oscillator+VCA replace would otherwise pass the gate only by
// accident of the baseline it's replacing, not on its own merits.
constexpr const char* kReplacePatch =
    R"({"nodes":[{"id":10,"type":"Oscillator"},{"id":11,"type":"VCA"},{"id":12,"type":"Audio Output"}],
     "connections":[{"src":10,"srcPort":0,"dst":11,"dstPort":0},{"src":11,"srcPort":0,"dst":12,"dstPort":0}]})";

class AIUndoTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;
    std::unique_ptr<AIIntegrationService> service;

    void SetUp() override {
        graph.clear();
        service = std::make_unique<AIIntegrationService>(graph, &undoManager);
    }

    /**
     * Installs a known starting patch (Oscillator -> Filter) directly through the mapper rather
     * than through the service, so the baseline itself never lands on the undo stack — every test
     * then starts from an empty undo history with a non-empty graph.
     */
    void installBaselinePatch() {
        auto json = juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Filter"}],
             "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})");
        ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, graph, true));
        ASSERT_EQ(graph.getNumNodes(), 2);
        ASSERT_FALSE(undoManager.canUndo());
    }

    /** Merge-mode patches address existing nodes by their live graph uid, not the baseline JSON id. */
    int uidOfNodeNamed(const juce::String& name) const {
        for (auto* node : graph.getNodes())
            if (node->getProcessor()->getName() == name)
                return (int)node->nodeID.uid;
        return -1;
    }
};

TEST_F(AIUndoTest, AiPatchIsUndoable) {
    installBaselinePatch();
    const auto before = captureShape(graph);

    ASSERT_TRUE(service->applyPatch(kReplacePatch));
    ASSERT_EQ(graph.getNumNodes(), 3);
    EXPECT_FALSE(captureShape(graph) == before) << "patch should have changed the graph";

    ASSERT_TRUE(undoManager.undo());

    EXPECT_EQ(graph.getNumNodes(), 2);
    EXPECT_TRUE(captureShape(graph) == before) << "undo must restore the exact pre-patch graph";
}

TEST_F(AIUndoTest, AiPatchIsRedoable) {
    installBaselinePatch();

    ASSERT_TRUE(service->applyPatch(kReplacePatch));
    const auto patched = captureShape(graph);

    ASSERT_TRUE(undoManager.undo());
    ASSERT_FALSE(captureShape(graph) == patched);

    ASSERT_TRUE(undoManager.redo());
    EXPECT_TRUE(captureShape(graph) == patched) << "redo must restore the patched graph";
}

TEST_F(AIUndoTest, AiMergeIsUndoable) {
    installBaselinePatch();
    const auto before = captureShape(graph);

    const int filterUid = uidOfNodeNamed("Filter");
    ASSERT_NE(filterUid, -1);

    // Merge mode (clearExisting == false): add a VCA fed by the existing Filter.
    const juce::String mergePatch = juce::String(R"({"mode":"merge","nodes":[{"id":9001,"type":"VCA"}],)") +
                                    R"("connections":[{"src":)" + juce::String(filterUid) +
                                    R"(,"srcPort":0,"dst":9001,"dstPort":0}]})";

    ASSERT_TRUE(service->applyPatch(mergePatch, /*mergeMode=*/true));
    EXPECT_EQ(graph.getNumNodes(), 3);

    ASSERT_TRUE(undoManager.undo());

    EXPECT_EQ(graph.getNumNodes(), 2);
    EXPECT_TRUE(captureShape(graph) == before) << "undo of a merge must restore the pre-merge graph";
}

TEST_F(AIUndoTest, NodeIdsSurviveUndo) {
    // Undo restores the graph by clearing and rebuilding it from a JSON snapshot. If that renumbers
    // NodeIDs, anything holding an id across the undo (most importantly a merge-mode patch card, which
    // addresses existing nodes by uid) silently stops resolving.
    installBaselinePatch();

    std::multiset<int> uidsBefore;
    for (auto* n : graph.getNodes())
        uidsBefore.insert((int)n->nodeID.uid);

    ASSERT_TRUE(service->applyPatch(kReplacePatch));
    ASSERT_TRUE(undoManager.undo());

    std::multiset<int> uidsAfter;
    for (auto* n : graph.getNodes())
        uidsAfter.insert((int)n->nodeID.uid);

    EXPECT_EQ(uidsBefore, uidsAfter) << "undo must restore node identity, not just node shape";
}

TEST_F(AIUndoTest, MergePatchCanBeReappliedAfterUndo) {
    // Reported by hand-testing: Merge an AI addition, press Cmd+Z, then click Merge again on the same
    // patch card -> nothing happens. The card's JSON references the pre-undo uid of an existing node.
    installBaselinePatch();

    const int filterUid = uidOfNodeNamed("Filter");
    ASSERT_NE(filterUid, -1);

    const juce::String mergePatch = juce::String(R"({"mode":"merge","nodes":[{"id":9001,"type":"VCA"}],)") +
                                    R"("connections":[{"src":)" + juce::String(filterUid) +
                                    R"(,"srcPort":0,"dst":9001,"dstPort":0}]})";

    ASSERT_TRUE(service->applyPatch(mergePatch, /*mergeMode=*/true));
    ASSERT_EQ(graph.getNumNodes(), 3);

    ASSERT_TRUE(undoManager.undo());
    ASSERT_EQ(graph.getNumNodes(), 2);

    // Clicking Merge a second time on the very same card must still work.
    EXPECT_TRUE(service->applyPatch(mergePatch, /*mergeMode=*/true))
        << "re-applying a merge patch after undo must not silently fail";
    EXPECT_EQ(graph.getNumNodes(), 3);
}

TEST_F(AIUndoTest, ReplacePatchCanBeAppliedAfterUndo) {
    // A full-replacement patch carries self-contained ids, so it must apply after an undo regardless
    // of how the restore renumbered the graph.
    installBaselinePatch();

    ASSERT_TRUE(service->applyPatch(kReplacePatch));
    ASSERT_TRUE(undoManager.undo());

    EXPECT_TRUE(service->applyPatch(
        R"({"nodes":[{"id":20,"type":"Oscillator"},{"id":21,"type":"Filter"},{"id":22,"type":"VCA"},
                     {"id":23,"type":"Audio Output"}],
            "connections":[{"src":20,"srcPort":0,"dst":21,"dstPort":0},{"src":21,"srcPort":0,"dst":22,"dstPort":0},
                           {"src":22,"srcPort":0,"dst":23,"dstPort":0}]})"))
        << "a fresh replace patch must apply after an undo";
    EXPECT_EQ(graph.getNumNodes(), 4);
}

TEST_F(AIUndoTest, ListenersFireOnUndo) {
    installBaselinePatch();

    RecordingListener listener;
    service->addListener(&listener);

    ASSERT_TRUE(service->applyPatch(kReplacePatch));
    EXPECT_EQ(listener.aboutToApplyCount, 1);
    EXPECT_EQ(listener.appliedCount, 1);

    // The critical case: undo rebuilds the graph, so the graph editor must be told to drop its
    // module components first and rebuild them afterwards — otherwise it keeps stale pointers
    // into VisualBuffers that applyJSONToGraph has already freed.
    listener.events.clear();
    ASSERT_TRUE(undoManager.undo());

    EXPECT_EQ(listener.aboutToApplyCount, 2);
    EXPECT_EQ(listener.appliedCount, 2);
    ASSERT_EQ(listener.events.size(), 2u);
    EXPECT_EQ(listener.events[0], juce::String("aboutToApply"));
    EXPECT_EQ(listener.events[1], juce::String("applied"));

    // Redo rebuilds the graph the same way, so it must notify too.
    listener.events.clear();
    ASSERT_TRUE(undoManager.redo());

    EXPECT_EQ(listener.aboutToApplyCount, 3);
    EXPECT_EQ(listener.appliedCount, 3);
    ASSERT_EQ(listener.events.size(), 2u);
    EXPECT_EQ(listener.events[0], juce::String("aboutToApply"));
    EXPECT_EQ(listener.events[1], juce::String("applied"));

    service->removeListener(&listener);
}

TEST_F(AIUndoTest, FailedPatchPushesNothing) {
    installBaselinePatch();
    const auto before = captureShape(graph);

    RecordingListener listener;
    service->addListener(&listener);

    EXPECT_FALSE(service->applyPatch("not json at all"));
    EXPECT_FALSE(undoManager.canUndo()) << "an unparseable patch must not leave an undo entry";

    EXPECT_FALSE(service->applyPatch(R"({"nodes":"not-an-array"})"));
    EXPECT_FALSE(undoManager.canUndo()) << "a structurally invalid patch must not leave an undo entry";

    EXPECT_FALSE(service->applyPatch(R"({"nodes":[{"id":1,"type":"NoSuchModule"}],"connections":[]})"));
    EXPECT_FALSE(undoManager.canUndo()) << "an unknown node type must not leave an undo entry";

    // The graph is untouched and no listener was told a patch was coming.
    EXPECT_TRUE(captureShape(graph) == before);
    EXPECT_EQ(listener.aboutToApplyCount, 0);
    EXPECT_EQ(listener.appliedCount, 0);

    service->removeListener(&listener);
}

/** Minimal provider so a MainComponent can be constructed headlessly. */
class StubProvider : public AIProvider {
public:
    juce::String getProviderName() const override { return "Stub"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"stub-model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback) {
            AIResponse response;
            response.success = true;
            response.content = "Stub response.";
            callback(response);
        }
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "stub-model";
    int requestTimeoutMs = 240000;
};

/**
 * Guards the wiring itself: MainComponent must hand its AppUndoManager to the AI service, otherwise
 * every test above still passes while Cmd+Z does nothing in the real app.
 */
TEST(AIUndoWiringTest, MainComponentRoutesAiPatchesThroughUndoManager) {
    ::MainComponent mc(std::make_unique<StubProvider>());

    auto& appUndo = mc.getUndoManager();
    auto& svc = mc.getAiServiceForTest();

    ASSERT_TRUE(svc.applyPatch(kReplacePatch));

    EXPECT_TRUE(appUndo.canUndo()) << "MainComponent must route AI patches through its undo manager";
    EXPECT_EQ(appUndo.getUndoManager().getUndoDescription(), juce::String("AI patch"))
        << "the transaction should carry a human-readable name";
}

/**
 * Headless stand-in for the manual "apply a patch, press Cmd+Z, check the editor still renders"
 * check. Drives a real MainComponent through apply -> undo -> redo, pumping the message loop each
 * time (MainComponent::aiPatchApplied defers updateComponents() via callAsync) and painting after
 * every step. Catches the failure this task is really about: the editor keeping ModuleComponents
 * that point into VisualBuffers the rebuilt graph has already freed.
 */
TEST(AIUndoWiringTest, EditorRebuildsAndRepaintsAcrossUndoAndRedo) {
    ::MainComponent mc(std::make_unique<StubProvider>());
    mc.setSize(1600, 900);

    auto& appUndo = mc.getUndoManager();
    auto& svc = mc.getAiServiceForTest();
    auto& editor = mc.getGraphEditor();

    // Let MainComponent settle any construction-time async work before the baseline snapshot.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    const int baselineModules = editor.getModuleComponents().size();

    auto pumpAndPaint = [&mc] {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        juce::Image img(juce::Image::ARGB, 1600, 900, true);
        juce::Graphics g(img);
        EXPECT_NO_THROW(mc.paint(g));
    };

    // Apply: three modules (Oscillator + VCA + Audio Output).
    ASSERT_TRUE(svc.applyPatch(kReplacePatch));
    pumpAndPaint();
    EXPECT_EQ(editor.getModuleComponents().size(), 3) << "editor should show the patched modules";

    // Undo: the editor must drop the patched components and rebuild the pre-patch ones.
    ASSERT_TRUE(appUndo.undo());
    pumpAndPaint();
    EXPECT_EQ(editor.getModuleComponents().size(), baselineModules)
        << "undo must leave the editor showing the pre-patch modules, not stale ones";

    // Redo: back to the patched state, still rendering.
    ASSERT_TRUE(appUndo.redo());
    pumpAndPaint();
    EXPECT_EQ(editor.getModuleComponents().size(), 3) << "redo must restore the patched modules";
}

} // namespace
} // namespace synth
