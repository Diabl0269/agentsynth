#pragma once

#include "AppUndoManager.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include <functional>
#include <memory>
#include <optional>

namespace synth::ui {

class PluginKnobPickerTouchCapture;

/**
 * "Add by moving a control in the plugin" (docs/control/plugin-card-layout.md#add-by-moving-a-control-in-the-plugin):
 * while on, every parameter the user moves in the plugin's own window is added to the card, through the same
 * write as the picker's tick (HostedCardLayoutSource::showParameter: one undo step each). It stays on until
 * stopped. The module is held weakly; once it is gone nothing is added. Message thread only.
 */
class PluginTouchToAdd final {
public:
    /** `module` must have a live instance. `store` and `undoManager` may be null; `graph` must outlive this. */
    PluginTouchToAdd(HostedPluginModule& module, PluginCardLayoutStore* store, juce::AudioProcessorGraph& graph,
                     juce::AudioProcessorGraph::NodeID nodeId, AppUndoManager* undoManager);
    ~PluginTouchToAdd();

    /** Fired once from start(), so the owner can open the plugin's editor window. */
    std::function<void()> onRequestOpenEditor;

    /** Starts listening to every parameter of the live instance and requests the editor. Idempotent. */
    void start();
    bool isOn() const noexcept;

    /** Delivers a gesture start for live parameter `parameterIndex` through the real capture. */
    void simulateTouchGestureForTest(int parameterIndex);

private:
    void handleParameterTouched(int parameterIndex);
    std::optional<juce::String> paramIdFor(int parameterIndex) const;

    juce::WeakReference<HostedPluginModule> module_;
    PluginCardLayoutStore* store_;
    juce::AudioProcessorGraph& graph_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    AppUndoManager* undoManager_;
    std::unique_ptr<PluginKnobPickerTouchCapture> capture_;
};

} // namespace synth::ui
