#pragma once

#include "HostedPluginEditorWindow.h"
#include "HostedPluginModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <memory>

namespace synth {

/**
 * HostedPluginWindowManager — owns every open HostedPluginEditorWindow, one per graph node.
 *
 * Owned by MainComponent (AppUI), never by GraphEditor: this keeps every plugin editor window
 * entirely outside GraphEditor's paint() / 30 Hz animation timer.
 *
 * `pruneClosedNodes()`, wired to `GraphEditor::onGraphStructureChanged`, is a pure
 * `AudioProcessorGraph::getNodeForId()` lookup — it never dereferences the HostedPluginModule behind
 * a node that no longer exists, since `graph.removeNode()` has usually already destroyed it before
 * this fires. Erasing the map entry destroys the corresponding HostedPluginEditorWindow, whose own
 * destructor is safe in that state too (it holds the module through a juce::WeakReference).
 *
 * `closeAll()` destroys every open window. MainComponent calls it as the FIRST line of its own
 * destructor — before `graphEditor.detachAllModuleComponents()` and `audioEngine.shutdown()` — so
 * every editor is torn down while the graph and its nodes are still fully alive. As a second,
 * independent line of defence, MainComponent declares its HostedPluginWindowManager member AFTER
 * `audioEngine`/`ownedAudioEngine` and `graphEditor`: members are destroyed in REVERSE declaration
 * order, so the manager is torn down before the engine/graph members even without the explicit call.
 * Losing either mechanism silently would only show up as an intermittent crash on app close.
 *
 * FRO100 (the same FRO12 follow-up bug, fixed here): `HostedPluginEditorWindow` is built
 * `addToDesktop=false` (a deliberate headless-test seam), and JUCE only ever creates a native peer
 * from a TopLevelWindow constructor's own `addToDesktop=true`, `recreateDesktopWindow()`/
 * `lookAndFeelChanged()` when a peer already exists, or an explicit `addToDesktop()` call — never
 * from `setVisible()` alone. `setCreatesNativeWindows(bool)` gates that explicit call, mirroring
 * `DetachablePanelHost::setCreatesNativeWindows()`: false (the default, and every headless test's
 * value) leaves `openEditorFor()` exactly as before; true — set once, right after construction, by
 * `Main.cpp`'s `MainWindow` and `PluginEditor.cpp`'s `AgentSynthPluginEditor`, the app's and
 * plugin's only real `MainComponent` construction sites — makes `openEditorFor()` call
 * `window->addToDesktop()` (gated additionally on a primary display existing, for a genuinely
 * headless runner) before `setVisible(true)`.
 */
class HostedPluginWindowManager {
public:
    HostedPluginWindowManager() = default;
    virtual ~HostedPluginWindowManager() { closeAll(); }

    /** When true, `openEditorFor()` gives the freshly built window a REAL native top-level window
     *  (a peer), so it actually shows up on screen — see the FRO100 class-comment paragraph above.
     *  Defaults to false so every headless test stays exactly as before. */
    void setCreatesNativeWindows(bool shouldCreate) noexcept { createsNativeWindows_ = shouldCreate; }
    bool isCreatingNativeWindows() const noexcept { return createsNativeWindows_; }

    /** Opens the editor window for `nodeId`'s HostedPluginModule, or brings the existing one to
     *  front if it's already open (one window per node). `module` must be the live processor
     *  currently at `nodeId` — the caller (MainComponent) resolves it via
     *  `graph.getNodeForId(nodeId)->getProcessor()` right before calling this, mirroring every other
     *  NodeID -> live-module lookup in this codebase. A no-op if `module` is nullptr. */
    void openEditorFor(HostedPluginModule* module, juce::AudioProcessorGraph::NodeID nodeId) {
        if (module == nullptr)
            return;

        auto existing = windows_.find(nodeId);
        if (existing != windows_.end()) {
            existing->second->toFront(true);
            return;
        }

        auto window = std::make_unique<HostedPluginEditorWindow>(*module, nodeId);
        window->onCloseRequested = [this](juce::AudioProcessorGraph::NodeID id) { closeAllForNode(id); };
        // A DocumentWindow's default position is the screen origin, i.e. top-left UNDER the menu
        // bar and behind the app's main window — "Open Editor did nothing" to the user. Centre it
        // at its content size and bring it forward, in that order, around the addToDesktop() call
        // that promotes it to a real native peer (see the FRO100 class-comment paragraph). Guarded
        // on a display existing: centreWithSize dereferences getPrimaryDisplay(), which is NULL on
        // a headless test runner (Linux CI has no display server; this crashed there and nowhere
        // else) — the same guard the promotion itself needs, so both share it.
        if (hasPrimaryDisplayForNativeWindow())
            window->centreWithSize(juce::jmax(1, window->getWidth()), juce::jmax(1, window->getHeight()));
        // FRO100: promote the window to a real native peer BEFORE setVisible(true) — setVisible()
        // alone never creates one (see setCreatesNativeWindows()'s doc comment above). window's
        // bounds are already the just-centred ones, and TopLevelWindow::addToDesktop() reads the
        // component's CURRENT bounds to size/position the native peer, so they survive unchanged.
        if (createsNativeWindows_ && hasPrimaryDisplayForNativeWindow())
            addWindowToDesktop(*window);
        window->setVisible(true);
        window->toFront(true);
        windows_.emplace(nodeId, std::move(window));
    }

    /** Destroys the editor window open for `nodeId`, if any. Safe to call when there is none. Named
     *  "AllForNode" (rather than e.g. closeEditor) because the one-per-node rule makes "all" and
     *  "the one" the same set, and the plural form is what a future multi-window-per-node change
     *  (were one ever needed) would keep meaning. */
    void closeAllForNode(juce::AudioProcessorGraph::NodeID nodeId) { windows_.erase(nodeId); }

    /** Destroys every open window. See the class comment's "Shutdown / member order" section for
     *  why MainComponent calls this explicitly, in addition to relying on declaration order. */
    void closeAll() { windows_.clear(); }

    /** Drops (and so destroys) every window whose node no longer exists in `graph`. Wire this to
     *  GraphEditor::onGraphStructureChanged; see the class comment for why this must stay a pure
     *  graph lookup rather than ever touching the module a removed node used to carry. */
    void pruneClosedNodes(juce::AudioProcessorGraph& graph) {
        for (auto it = windows_.begin(); it != windows_.end();) {
            if (graph.getNodeForId(it->first) == nullptr)
                it = windows_.erase(it);
            else
                ++it;
        }
    }

    // Testing hooks — see HostedPluginEditorWindowTests.cpp.
    int getOpenWindowCountForTest() const { return (int)windows_.size(); }
    bool hasWindowForTest(juce::AudioProcessorGraph::NodeID nodeId) const {
        return windows_.find(nodeId) != windows_.end();
    }
    HostedPluginEditorWindow* getWindowForTest(juce::AudioProcessorGraph::NodeID nodeId) const {
        auto it = windows_.find(nodeId);
        return it != windows_.end() ? it->second.get() : nullptr;
    }

protected:
    // ---- Native-window seam (HostedPluginEditorWindowTests.cpp's "native window" group) ----
    // openEditorFor() calls these two, in that order, to decide whether the freshly built window
    // gets a real native peer and to perform that call. Split into two overridable points (rather
    // than folding the display check into the first) so a test subclass can simulate "no primary
    // display" or "call reached" deterministically on ANY runner — including a developer's Mac,
    // which always has a real display — without this base implementation ever creating one. Mirrors
    // DetachablePanelHost's identical seam (DetachablePanelHost.h) exactly.
    virtual bool hasPrimaryDisplayForNativeWindow() const {
        return juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
    }
    virtual void addWindowToDesktop(HostedPluginEditorWindow& window) {
        // The flag-less TopLevelWindow overload — it derives its style flags from
        // getDesktopWindowStyleFlags() (native title bar, matching what HostedPluginEditorWindow's
        // constructor already configured via setUsingNativeTitleBar) rather than us guessing them
        // again here.
        window.addToDesktop();
    }

private:
    bool createsNativeWindows_ = false;
    std::map<juce::AudioProcessorGraph::NodeID, std::unique_ptr<HostedPluginEditorWindow>> windows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedPluginWindowManager)
};

} // namespace synth
