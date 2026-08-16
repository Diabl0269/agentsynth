#pragma once

#include "HostedPluginModule.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/**
 * HostedPluginEditorWindow — a native top-level window hosting a HostedPluginModule's live
 * juce::AudioPluginInstance editor (TL7-5).
 *
 * Owned EXCLUSIVELY by HostedPluginWindowManager, one per node — see that class for the
 * one-window-per-node/refocus rule and the close-on-node-delete pruning that destroys this window
 * when the graph node it belongs to disappears. Never owned by GraphEditor or ModuleComponent, and
 * this class touches neither GraphEditor's paint path nor its 30 Hz animation timer: it is a plain
 * top-level juce::DocumentWindow, laid out and driven entirely by its own content component.
 *
 * -- Content ---------------------------------------------------------------------------------
 *
 * `instance->createEditorIfNeeded()` when the plugin has one (`AudioProcessor::hasEditor()`);
 * `juce::GenericAudioProcessorEditor` otherwise, so every hosted plugin gets SOME editor. Handed to
 * `setContentOwned(editor, true)` — that second argument is
 * resizeToFitWhenContentChangesSize, which is what makes
 * `ResizableWindow::childBoundsChanged` track the editor's own resizes automatically — no
 * childBoundsChanged override needed here. `setResizable()` mirrors the editor's own
 * `isResizable()`. Constructed with `addToDesktop=false` so building one (what every test below
 * does) never creates a native peer; only HostedPluginWindowManager::openEditorFor's
 * `setVisible(true)` does, for real use.
 *
 * -- Reacting to the module (TL7-5) -----------------------------------------------------------
 *
 * Installs itself on `HostedPluginModule::onInstanceChanged` and reacts to EVERY edge (see that
 * field's doc comment for the exact ordering guarantee): rebuild the content against whatever
 * `getActiveInstanceForEditor()` returns right now (nullptr shows a neutral placeholder). If that
 * leaves no instance, the "really gone vs. mid-swap" question can't be answered synchronously (a
 * reload republishes its replacement later in the SAME call that retired the old one — see
 * HostedPluginModule::retireActiveInstance()) — so this defers a recheck to the next message-loop
 * turn via `juce::MessageManager::callAsync`. If the module still has no instance by the time that
 * runs, this really was an unload (or the module is gone entirely), and `onCloseRequested` fires so
 * the manager tears this window down; if an instance has since reappeared, the swap already rebuilt
 * against it and nothing further happens. Pinned choice for a real unload: the window CLOSES rather
 * than staying open on an empty placeholder — see `HostedPluginEditorWindowTests.cpp`,
 * `UnloadClosesOrFallsBack`.
 *
 * Holds the module through a `juce::WeakReference`, not a plain reference: a node deletion in
 * GraphEditor removes it from the AudioProcessorGraph (destroying the HostedPluginModule) BEFORE
 * `GraphEditor::onGraphStructureChanged` ever fires, so by the time
 * `HostedPluginWindowManager::pruneClosedNodes` erases this window, the module behind it may
 * already be freed. Every access here goes through `moduleRef_.get()` for exactly that reason —
 * never a raw dereference of a `HostedPluginModule&`.
 */
class HostedPluginEditorWindow : public juce::DocumentWindow {
public:
    HostedPluginEditorWindow(HostedPluginModule& module, juce::AudioProcessorGraph::NodeID nodeId);
    ~HostedPluginEditorWindow() override;

    void closeButtonPressed() override;

    juce::AudioProcessorGraph::NodeID getNodeId() const noexcept { return nodeId_; }

    /** Fired when the close button is clicked, or when the deferred post-instance-change recheck
     *  (see the class comment) confirms the module's instance is really gone rather than mid-swap.
     *  HostedPluginWindowManager installs this to erase (and so destroy) this window from its map —
     *  see openEditorFor(). This window never destroys itself directly. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onCloseRequested;

    // Testing hooks — see HostedPluginEditorWindowTests.cpp. State, not pixels: none of these
    // require the window to ever have been made visible.
    juce::Component* getEditorContentForTest() const { return getContentComponent(); }
    bool isShowingGenericEditorForTest() const;
    bool isShowingPlaceholderForTest() const;

private:
    // Rebuilds the content component from whatever the module currently reports (a fresh custom or
    // generic editor, or the "no instance" placeholder). Safe to call repeatedly, including with the
    // instance unchanged (setContentOwned no-ops when handed the same pointer it already owns).
    void rebuildContent();

    // Installed on module.onInstanceChanged; see the class comment.
    void instanceChanged();

    juce::WeakReference<HostedPluginModule> moduleRef_;
    juce::AudioProcessorGraph::NodeID nodeId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedPluginEditorWindow)
};

} // namespace synth
