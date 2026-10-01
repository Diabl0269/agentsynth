#pragma once

#include "AppUndoManager.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorComponent.h"
#include <functional>
#include <memory>

namespace synth::ui {

class PluginKnobPickerTouchCapture;

/**
 * The card layout editor for a hosted plugin's card (docs/control/plugin-card-layout.md#choosing-knobs):
 * CardLayoutEditorComponent over a HostedCardLayoutSource, plus touch-to-add (a parameter moved in the
 * plugin's own editor is ticked). Every edit applies to the scope "Apply to" names at once and is one
 * undo step. The module is reached weakly; once it is gone every edit is a no-op.
 */
class PluginKnobPickerComponent final : public CardLayoutEditorComponent {
public:
    /** `graph`/`nodeId` identify the node for undo; `undoManager` and `store` may be null. `module`
     *  must have a live instance. `shortcuts` may be null. */
    PluginKnobPickerComponent(HostedPluginModule& module, PluginCardLayoutStore* store,
                              juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                              AppUndoManager* undoManager, const ShortcutManager* shortcuts = nullptr);
    ~PluginKnobPickerComponent() override;

    /** Fired once per touch-to-add arming; the card wires it to its own "Open Editor". */
    std::function<void()> onOpenPluginEditorRequested;

    void setTouchToAddArmedForTest(bool armed);
    bool isTouchToAddArmedForTest() const;
    /** Delivers a gesture start for live parameter `parameterIndex` through the real touch capture. */
    void simulateTouchGestureForTest(int parameterIndex);
    juce::AudioProcessorGraph::NodeID getNodeIdForTest() const noexcept { return nodeId_; }

protected:
    int layoutExtraControls(juce::Rectangle<int> area) override;

private:
    void handleParameterTouched(int parameterIndex);
    bool isParameterIndexShown(int parameterIndex) const;

    juce::AudioProcessorGraph::NodeID nodeId_;
    juce::ToggleButton touchToAddToggle_{"Touch in the plugin editor to add"};
    std::unique_ptr<PluginKnobPickerTouchCapture> touchCapture_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginKnobPickerComponent)
};

} // namespace synth::ui
