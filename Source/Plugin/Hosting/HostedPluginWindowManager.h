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
 */
class HostedPluginWindowManager {
public:
    HostedPluginWindowManager() = default;
    ~HostedPluginWindowManager() { closeAll(); }

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
        window->setVisible(true); // the one call in this class that creates a native peer
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

private:
    std::map<juce::AudioProcessorGraph::NodeID, std::unique_ptr<HostedPluginEditorWindow>> windows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedPluginWindowManager)
};

} // namespace synth
