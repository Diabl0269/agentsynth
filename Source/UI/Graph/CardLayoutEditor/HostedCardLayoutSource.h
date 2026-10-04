#pragma once

#include "AppUndoManager.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorSource.h"

namespace synth::ui {

/**
 * A hosted plugin instance as the layout editor's source: the instance's parameters (captured once,
 * at construction), its "cardLayout" extra state or the plugin's default in PluginCardLayoutStore,
 * written as the flat version 1 slot list, each write one undo step
 * (AppUndoManager::recordNodeExtraStateChange). The module is held weakly; once it is gone every write
 * is skipped. Message thread only. docs/control/plugin-card-layout.md#choosing-knobs.
 */
class HostedCardLayoutSource final : public CardLayoutEditorSource {
public:
    /** `store` and `undoManager` may be null. `module` must have a live instance. */
    HostedCardLayoutSource(HostedPluginModule& module, PluginCardLayoutStore* store, juce::AudioProcessorGraph& graph,
                           juce::AudioProcessorGraph::NodeID nodeId, AppUndoManager* undoManager);

    bool isAlive() const override { return module_.get() != nullptr; }
    juce::String title() const override;
    juce::String thisScopeText() const override { return "This instance"; }
    juce::String allScopeText() const override;
    juce::String resetText() const override { return "Reset to automatic"; }
    juce::String resetTooltip() const override;
    HiddenRows hiddenRows() const override { return HiddenRows::LeaveTheLayout; }
    bool supportsGroups() const override { return false; }

    std::vector<CardLayoutEditorParam> parameters() const override { return params_; }
    CardLayout currentLayout() const override;
    void apply(const CardLayout& layout, bool allOfType) override;
    CardLayout reset(bool allOfType) override;

    bool hasPresets() const override { return store_ != nullptr; }
    juce::StringArray listPresets() const override;
    bool savePreset(const juce::String& name, const CardLayout& layout) override;
    std::optional<CardLayout> loadPreset(const juce::String& name) const override;
    bool deletePreset(const juce::String& name) override;

    HostedPluginModule* getModule() const { return module_.get(); }

    enum class ParameterState { Unknown, OnCard, Available };
    /** Where `paramId` stands against the card layout this source reads. */
    ParameterState parameterState(const juce::String& paramId) const;
    /** Ticks `paramId` exactly as ticking its row in the picker does (scope "This instance"): one write,
     *  one undo step. False, and nothing written, when it is unknown or already on the card. */
    bool showParameter(const juce::String& paramId);

private:
    void writeOverride(const juce::var& layout);

    juce::WeakReference<HostedPluginModule> module_;
    PluginCardLayoutStore* store_; // not owned; may be null; must outlive this source
    juce::AudioProcessorGraph& graph_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    AppUndoManager* undoManager_; // not owned; may be null
    PluginIdentity identity_;
    juce::String pluginName_;
    std::vector<CardLayoutEditorParam> params_;
};

/** `layout` (slots or sections) as one untitled section, the form the editor works on. */
CardLayout hostedLayoutAsSection(const CardLayout& layout);

} // namespace synth::ui
