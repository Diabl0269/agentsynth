// HostedPluginEditorWindowTests.cpp
//
// Native plugin editor windows for hosted plugins — synth::HostedPluginEditorWindow and
// synth::HostedPluginWindowManager, plus the ModuleComponent "Open Editor" affordance that opens
// them.
//
// Runs against Tests/StubPluginInstance.h, exactly like HostedPluginTests.cpp: there is no real
// VST3/AU binary to load in CI, so every assertion here is about STATE (content component
// identity, sizes, map membership) rather than rendered pixels — see the design note in
// HostedPluginEditorWindow.h. Every HostedPluginEditorWindow constructed directly (not through
// HostedPluginWindowManager::openEditorFor) stays headless for its whole life: the base
// juce::DocumentWindow is built with addToDesktop=false, so no native peer is ever created unless
// something explicitly promotes it. That promotion (FRO100) is openEditorFor()'s own
// addToDesktop() call, gated on HostedPluginWindowManager::setCreatesNativeWindows(true) — false
// by default, so every manager-level test below except group 6 stays exactly as headless as a
// directly-constructed window.
//
// Groups:
//   1. Content — custom editor vs. the GenericAudioProcessorEditor fallback.
//   2. HostedPluginWindowManager — one window per node, refocus, close-on-node-delete, shutdown.
//   3. Instance-change reactions — swap rebuilds, unload closes.
//   4. Resize — the editor drives the window's size.
//   5. ModuleComponent — the card's "Open Editor" button.
//   6. Native-window promotion (FRO100) — setCreatesNativeWindows() gates the real peer.

#include "../StubPluginInstance.h"
#include "AudioEngine/AudioEngine.h"
#include "Plugin/Hosting/HostedPluginEditorWindow.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/HostedPluginWindowManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <chrono>
#include <gtest/gtest.h>

using synth::HostedPluginEditorWindow;
using synth::HostedPluginModule;
using synth::HostedPluginWindowManager;
using synth::test::StubBackend;
using synth::test::StubPluginEditor;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

/** Pumps the JUCE message loop until `predicate` holds or the timeout expires. Same idiom as
 *  HostedPluginTests.cpp — the stub backend's callback is always posted, never fired re-entrantly. */
template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

juce::PluginDescription stubDescription(const juce::String& name = "Stub Plugin", int uid = 0x5754424) {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = "VST3";
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    description.fileOrIdentifier = "/nonexistent/test/path/StubPlugin.vst3";
    return description;
}

/** Loads `backend`'s current stub into `module` and pumps until it publishes. */
void loadAndWait(HostedPluginModule& module, StubBackend& backend, const juce::String& name = "Stub Plugin",
                 int uid = 0x5754424) {
    module.loadPlugin(stubDescription(name, uid), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); })) << "the load never published";
}

} // namespace

// ============================================================================
// 1. Content — custom editor vs. generic fallback
// ============================================================================

TEST(HostedPluginEditorWindowTest, OpensGenericEditorWhenPluginHasNone) {
    StubBackend backend; // default factory: 2-in/2-out, reportsEditor=false
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend);

    HostedPluginEditorWindow window(module, juce::AudioProcessorGraph::NodeID(1));
    EXPECT_TRUE(window.isShowingGenericEditorForTest());
    EXPECT_FALSE(window.isShowingPlaceholderForTest());
    EXPECT_NE(window.getEditorContentForTest(), nullptr);
}

TEST(HostedPluginEditorWindowTest, OpensCustomEditorWhenReported) {
    StubBackend backend([] {
        return std::make_unique<StubPluginInstance>(2, 2, "Custom Editor Plugin", 0x1111, "VST3",
                                                    std::vector<synth::test::StubParamSpec>{}, /*reportsEditor*/ true);
    });
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend, "Custom Editor Plugin", 0x1111);

    HostedPluginEditorWindow window(module, juce::AudioProcessorGraph::NodeID(1));
    EXPECT_FALSE(window.isShowingGenericEditorForTest());
    EXPECT_FALSE(window.isShowingPlaceholderForTest());
    EXPECT_NE(dynamic_cast<StubPluginEditor*>(window.getEditorContentForTest()), nullptr);
}

// ============================================================================
// 2. HostedPluginWindowManager
// ============================================================================

TEST(HostedPluginWindowManagerTest, OnePerNodeAndRefocus) {
    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend);

    HostedPluginWindowManager manager;
    const juce::AudioProcessorGraph::NodeID nodeId(1);

    manager.openEditorFor(&module, nodeId);
    ASSERT_EQ(manager.getOpenWindowCountForTest(), 1);
    auto* firstWindow = manager.getWindowForTest(nodeId);
    ASSERT_NE(firstWindow, nullptr);

    manager.openEditorFor(&module, nodeId);
    EXPECT_EQ(manager.getOpenWindowCountForTest(), 1) << "a second open must refocus, not duplicate";
    EXPECT_EQ(manager.getWindowForTest(nodeId), firstWindow) << "refocus must reuse the same window";
}

TEST(HostedPluginWindowManagerTest, CloseOnNodeDelete) {
    StubBackend backend;
    AudioEngine engine;

    auto module = std::make_unique<HostedPluginModule>();
    auto* modulePtr = module.get();
    const auto nodeId = engine.getGraph().addNode(std::move(module))->nodeID;
    modulePtr->prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(*modulePtr, backend);

    HostedPluginWindowManager manager;
    manager.openEditorFor(modulePtr, nodeId);
    ASSERT_TRUE(manager.hasWindowForTest(nodeId));

    // Mirrors GraphEditor's delete path exactly: graph.removeNode() (destroying modulePtr) runs
    // BEFORE the structure-change hook fires — see HostedPluginWindowManager::pruneClosedNodes's
    // class comment for why it must be safe here without ever touching the module.
    engine.getGraph().removeNode(nodeId);
    manager.pruneClosedNodes(engine.getGraph());

    EXPECT_FALSE(manager.hasWindowForTest(nodeId));
    EXPECT_EQ(manager.getOpenWindowCountForTest(), 0);
}

TEST(HostedPluginWindowManagerTest, NodeDeleteDropsTheEditorBeforeTheInstanceDies) {
    StubBackend backend([] {
        return std::make_unique<StubPluginInstance>(2, 2, "Deletable", 0x4444, "VST3",
                                                    std::vector<synth::test::StubParamSpec>{}, /*reportsEditor*/ true);
    });
    AudioEngine engine;

    auto module = std::make_unique<HostedPluginModule>();
    auto* modulePtr = module.get();
    const auto nodeId = engine.getGraph().addNode(std::move(module))->nodeID;
    modulePtr->prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(*modulePtr, backend, "Deletable", 0x4444);

    HostedPluginWindowManager manager;
    manager.openEditorFor(modulePtr, nodeId);
    auto* window = manager.getWindowForTest(nodeId);
    ASSERT_NE(window, nullptr);
    ASSERT_NE(dynamic_cast<StubPluginEditor*>(window->getEditorContentForTest()), nullptr);

    // The delete path: removeNode destroys the module AND its plugin instance, and nothing has
    // pruned the window yet. The editor was built on that instance, so it must already be gone.
    engine.getGraph().removeNode(nodeId);

    ASSERT_TRUE(manager.hasWindowForTest(nodeId)) << "the prune has not run yet — that is the point";
    EXPECT_TRUE(window->isShowingPlaceholderForTest())
        << "the window still holds an editor built on a destroyed plugin instance";

    manager.pruneClosedNodes(engine.getGraph());
    EXPECT_FALSE(manager.hasWindowForTest(nodeId));
}

TEST(HostedPluginWindowManagerTest, ManagerShutdownClosesAll) {
    StubBackend backend;

    HostedPluginModule moduleA;
    moduleA.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(moduleA, backend, "A", 0x1000);

    HostedPluginModule moduleB;
    moduleB.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(moduleB, backend, "B", 0x2000);

    const juce::AudioProcessorGraph::NodeID nodeA(1);
    const juce::AudioProcessorGraph::NodeID nodeB(2);

    HostedPluginWindowManager manager;
    manager.openEditorFor(&moduleA, nodeA);
    manager.openEditorFor(&moduleB, nodeB);
    ASSERT_EQ(manager.getOpenWindowCountForTest(), 2);

    manager.closeAll();
    EXPECT_EQ(manager.getOpenWindowCountForTest(), 0);
    EXPECT_FALSE(manager.hasWindowForTest(nodeA));
    EXPECT_FALSE(manager.hasWindowForTest(nodeB));

    // Each window's destructor clears HostedPluginModule::onInstanceChanged (see
    // HostedPluginEditorWindow's destructor) — reloading proves neither module was left holding a
    // callback into a destroyed window.
    EXPECT_NO_FATAL_FAILURE(loadAndWait(moduleA, backend, "A2", 0x1001));
    EXPECT_NO_FATAL_FAILURE(loadAndWait(moduleB, backend, "B2", 0x2001));
}

// ============================================================================
// 3. Instance-change reactions
// ============================================================================

TEST(HostedPluginEditorWindowTest, InstanceSwapRebuildsEditor) {
    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend, "First", 0x1);

    HostedPluginEditorWindow window(module, juce::AudioProcessorGraph::NodeID(1));
    ASSERT_TRUE(window.isShowingGenericEditorForTest());

    // Swap in a replacement that reports a custom editor. Captured BEFORE triggering the reload:
    // loadPlugin() sets the new identity synchronously (before the async round-trip even starts),
    // so `getIdentity().name == "Second" && hasInstance()` would be trivially true from the OLD
    // (not-yet-retired) instance on the very first pumpUntil check, before the swap has actually
    // happened. Comparing the instance POINTER instead is race-free.
    auto* oldInstance = module.getActiveInstanceForEditor();
    backend.setFactory([] {
        return std::make_unique<StubPluginInstance>(
            2, 2, "Second", 0x2, "VST3", /*params*/ std::vector<synth::test::StubParamSpec>{}, /*reportsEditor*/ true);
    });
    module.loadPlugin(stubDescription("Second", 0x2), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance() && module.getActiveInstanceForEditor() != oldInstance; }));

    EXPECT_FALSE(window.isShowingGenericEditorForTest());
    EXPECT_FALSE(window.isShowingPlaceholderForTest());
    EXPECT_NE(dynamic_cast<StubPluginEditor*>(window.getEditorContentForTest()), nullptr)
        << "the window must show the NEW instance's editor, not stay on the old (freed) one";
}

TEST(HostedPluginEditorWindowTest, UnloadClosesOrFallsBack) {
    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend);

    HostedPluginEditorWindow window(module, juce::AudioProcessorGraph::NodeID(7));
    bool closeRequested = false;
    juce::AudioProcessorGraph::NodeID requestedId;
    window.onCloseRequested = [&](juce::AudioProcessorGraph::NodeID id) {
        closeRequested = true;
        requestedId = id;
    };

    module.unloadPlugin();
    // Pinned choice: a real unload closes the window. The "really gone, not mid-swap" recheck is
    // deferred to the next message-loop turn — see HostedPluginEditorWindow's class comment.
    ASSERT_TRUE(pumpUntil([&] { return closeRequested; }));
    EXPECT_EQ(requestedId, juce::AudioProcessorGraph::NodeID(7));
    EXPECT_TRUE(window.isShowingPlaceholderForTest()) << "the stale editor must not still be showing";
}

// ============================================================================
// 4. Resize
// ============================================================================

TEST(HostedPluginEditorWindowTest, ResizeRequestHonoured) {
    StubBackend backend([] {
        return std::make_unique<StubPluginInstance>(2, 2, "Resizable Plugin", 0x3333, "VST3",
                                                    std::vector<synth::test::StubParamSpec>{}, /*reportsEditor*/ true);
    });
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend, "Resizable Plugin", 0x3333);

    HostedPluginEditorWindow window(module, juce::AudioProcessorGraph::NodeID(1));
    auto* content = window.getEditorContentForTest();
    ASSERT_NE(dynamic_cast<StubPluginEditor*>(content), nullptr);

    content->setSize(640, 480);

    // Native title bar (set in the constructor) => zero window border, so the window's size tracks
    // the content's exactly — see ResizableWindow::childBoundsChanged/getContentComponentBorder.
    EXPECT_EQ(window.getWidth(), 640);
    EXPECT_EQ(window.getHeight(), 480);
}

// ============================================================================
// 5. ModuleComponent — the "Open Editor" button
// ============================================================================

// A plain TEST (not TEST_F): ModuleComponentTests.cpp's ModuleComponentTest suite is a TEST_F
// fixture, and gtest forbids mixing TEST and TEST_F on the same suite name.
TEST(ModuleComponentHostedPluginTest, CardButtonWiring) {
    StubBackend backend;
    AudioEngine engine;
    GraphEditor graphEditor(engine);

    auto module = std::make_unique<HostedPluginModule>();
    auto* modulePtr = module.get();
    const auto nodeId = engine.getGraph().addNode(std::move(module))->nodeID;
    modulePtr->prepareToPlay(kSampleRate, kBlockSize);

    ModuleComponent comp(modulePtr, nodeId, graphEditor);

    juce::TextButton* openEditorButton = nullptr;
    for (auto* child : comp.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child);
            button != nullptr && button->getComponentID() == "openPluginEditor")
            openEditorButton = button;
    ASSERT_NE(openEditorButton, nullptr) << "a hosted-plugin card must offer an Open Editor button";
    EXPECT_FALSE(openEditorButton->isEnabled()) << "disabled until the module reports hasInstance()";

    loadAndWait(*modulePtr, backend);
    comp.timerCallback(); // enabled state refreshes on the 15 Hz poll — drive it directly
    EXPECT_TRUE(openEditorButton->isEnabled());

    bool fired = false;
    juce::AudioProcessorGraph::NodeID firedNodeId;
    graphEditor.onOpenPluginEditorRequested = [&](juce::AudioProcessorGraph::NodeID id) {
        fired = true;
        firedNodeId = id;
    };

    ASSERT_TRUE(openEditorButton->onClick);
    openEditorButton->onClick(); // headless click — no real mouse event needed
    EXPECT_TRUE(fired);
    EXPECT_EQ(firedNodeId, nodeId);
}

// ============================================================================
// 6. Native-window promotion (FRO100 — the FRO12 follow-up applied here)
// ============================================================================

namespace {

// Overrides both native-window seam points (HostedPluginWindowManager.h's protected
// hasPrimaryDisplayForNativeWindow()/addWindowToDesktop()) so a test can simulate either
// environment deterministically on ANY runner — including a developer's Mac, which always has a
// real display. addWindowToDesktop() deliberately never forwards to the base implementation: this
// must never create an actual native peer, no matter which machine runs the test suite. Mirrors
// DetachablePanelHostTests.cpp's RecordingNativeWindowHost exactly.
class RecordingHostedPluginWindowManager : public HostedPluginWindowManager {
public:
    bool simulatedPrimaryDisplay = true;
    bool addToDesktopCallReached = false;

protected:
    bool hasPrimaryDisplayForNativeWindow() const override { return simulatedPrimaryDisplay; }
    void addWindowToDesktop(HostedPluginEditorWindow&) override {
        addToDesktopCallReached = true; // recorded only -- never creates a real peer
    }
};

} // namespace

TEST(HostedPluginWindowManagerNativeWindowTest, FlagFalseNeverCreatesAPeerEvenWhenADisplayExists) {
    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend);

    HostedPluginWindowManager manager;
    ASSERT_FALSE(manager.isCreatingNativeWindows()) << "false is the default -- every headless test relies on this";

    manager.openEditorFor(&module, juce::AudioProcessorGraph::NodeID(1));
    auto* window = manager.getWindowForTest(juce::AudioProcessorGraph::NodeID(1));
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->getPeer(), nullptr)
        << "the flag is off -- openEditorFor() must never promote the window to a real peer";
}

TEST(HostedPluginWindowManagerNativeWindowTest, FlagTrueButNoPrimaryDisplayStillCreatesNoPeer) {
    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend);

    RecordingHostedPluginWindowManager manager;
    manager.setCreatesNativeWindows(true);
    manager.simulatedPrimaryDisplay = false; // simulates a genuinely headless runner, on ANY machine

    manager.openEditorFor(&module, juce::AudioProcessorGraph::NodeID(1));
    EXPECT_FALSE(manager.addToDesktopCallReached) << "no display -- the promotion call must never be reached";
    auto* window = manager.getWindowForTest(juce::AudioProcessorGraph::NodeID(1));
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->getPeer(), nullptr);
}

TEST(HostedPluginWindowManagerNativeWindowTest, FlagTrueWithAPrimaryDisplayReachesThePromotionCall) {
    StubBackend backend;
    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    loadAndWait(module, backend);

    RecordingHostedPluginWindowManager manager;
    manager.setCreatesNativeWindows(true);
    manager.simulatedPrimaryDisplay = true;

    manager.openEditorFor(&module, juce::AudioProcessorGraph::NodeID(1));
    EXPECT_TRUE(manager.addToDesktopCallReached)
        << "flag on + a display -- production code would promote the window here";
    // The override never forwarded to the real addToDesktop() -- this must still be no real peer,
    // regardless of whether the machine running this test actually has a display.
    auto* window = manager.getWindowForTest(juce::AudioProcessorGraph::NodeID(1));
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->getPeer(), nullptr);
}
