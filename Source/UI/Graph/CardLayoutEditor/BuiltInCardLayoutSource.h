#pragma once

#include "UI/Graph/CardLayoutEditor/CardLayoutEditorSource.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;
class GraphEditor;
class ModuleComponent;

namespace synth {
class ModuleCardLayoutStore;
}

namespace synth::ui {

/**
 * A built-in module's card as the layout editor's source: its parameters are the ones its card body
 * shows, a write is the node's "cardLayout" property (or the type's default in the editor's bound
 * ModuleCardLayoutStore), and the whole editing session is ONE undo step, recorded when this source is
 * destroyed. Reaches the editor through a SafePointer and the node by id, never the card, which every
 * write rebuilds. Message thread only. docs/layout/module-card-layout.md#editing-a-layout.
 */
class BuiltInCardLayoutSource final : public CardLayoutEditorSource {
public:
    /** `undo` may be null (no undo step). The node must hold a card drawn from layout data. */
    BuiltInCardLayoutSource(GraphEditor& editor, ::AppUndoManager* undo, juce::AudioProcessorGraph::NodeID nodeId);
    ~BuiltInCardLayoutSource() override;

    bool isAlive() const override;
    juce::String title() const override;
    juce::String thisScopeText() const override { return "This module"; }
    juce::String allScopeText() const override;
    juce::String resetText() const override { return "Reset to default"; }
    juce::String resetTooltip() const override;
    HiddenRows hiddenRows() const override { return HiddenRows::StayInPlace; }
    bool supportsGroups() const override { return true; }

    std::vector<CardLayoutEditorParam> parameters() const override;
    CardLayout currentLayout() const override;
    void apply(const CardLayout& layout, bool allOfType) override;
    CardLayout reset(bool allOfType) override;

    bool hasPresets() const override { return store() != nullptr; }
    juce::StringArray listPresets() const override;
    bool savePreset(const juce::String& name, const CardLayout& layout) override;
    std::optional<CardLayout> loadPreset(const juce::String& name) const override;
    bool deletePreset(const juce::String& name) override;

private:
    juce::AudioProcessor* module() const;
    ModuleComponent* card() const;
    ModuleCardLayoutStore* store() const;
    juce::AudioProcessorGraph* graph() const;
    void refreshCard();

    juce::Component::SafePointer<juce::Component> editor_;
    ::AppUndoManager* undo_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    juce::AudioProcessor* openedOn_ = nullptr; ///< Identity only, never dereferenced unless the node still holds it.
    juce::String moduleType_;
    juce::var sessionBefore_;
};

} // namespace synth::ui
