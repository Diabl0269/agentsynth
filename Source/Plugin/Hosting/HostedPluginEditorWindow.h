#pragma once

#include "HostedPluginModule.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/**
 * HostedPluginEditorWindow — a native top-level window hosting a HostedPluginModule's live
 * juce::AudioPluginInstance editor.
 *
 * Owned EXCLUSIVELY by HostedPluginWindowManager, one per node — see that class for the
 * one-window-per-node/refocus rule and the close-on-node-delete pruning. Never owned by GraphEditor
 * or ModuleComponent; it touches neither GraphEditor's paint path nor its 30 Hz animation timer.
 *
 * Content is `instance->createEditorIfNeeded()` when the plugin has one, else
 * `juce::GenericAudioProcessorEditor`, so every hosted plugin gets SOME editor.
 *
 * Installs itself on `HostedPluginModule::onInstanceChanged` and reacts to every edge: rebuild the
 * content against whatever `getActiveInstanceForEditor()` returns right now (nullptr shows a
 * neutral placeholder). If that leaves no instance, the "really gone vs. mid-swap" question can't
 * be answered synchronously (a reload republishes its replacement later in the SAME call that
 * retired the old one — see HostedPluginModule::retireActiveInstance()), so this defers a recheck
 * to the next message-loop turn. If the module still has no instance by then, this really was an
 * unload and `onCloseRequested` fires so the manager tears this window down; if an instance has
 * since reappeared, nothing further happens. See `HostedPluginEditorWindowTests.cpp`,
 * `UnloadClosesOrFallsBack`.
 *
 * Holds the module through a `juce::WeakReference`, not a plain reference: a node deletion in
 * GraphEditor destroys the HostedPluginModule BEFORE `GraphEditor::onGraphStructureChanged` fires,
 * so by the time `HostedPluginWindowManager::pruneClosedNodes` erases this window, the module
 * behind it may already be freed — every access here goes through `moduleRef_.get()`, never a raw
 * dereference.
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
