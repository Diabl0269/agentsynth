#pragma once

#include "AppUndoManager.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorComponent.h"

namespace synth::ui {

/**
 * The card layout editor for a hosted plugin's card (docs/control/plugin-card-layout.md#choosing-knobs):
 * CardLayoutEditorComponent over a HostedCardLayoutSource: the list of every plugin parameter, ticked to show
 * on the card. Every edit applies to the scope "Apply to" names at once and is one undo step. The module is
 * reached weakly; once it is gone every edit is a no-op. ("Add by moving a control in the plugin" is the
 * card's own mode, PluginTouchToAdd, not part of this list.)
 */
class PluginKnobPickerComponent final : public CardLayoutEditorComponent {
public:
    /** `graph`/`nodeId` identify the node for undo; `undoManager` and `store` may be null. `module`
     *  must have a live instance. `shortcuts` may be null. */
    PluginKnobPickerComponent(HostedPluginModule& module, PluginCardLayoutStore* store,
                              juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                              AppUndoManager* undoManager, const ShortcutManager* shortcuts = nullptr);
    ~PluginKnobPickerComponent() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginKnobPickerComponent)
};

} // namespace synth::ui
