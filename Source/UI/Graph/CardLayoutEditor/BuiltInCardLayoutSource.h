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
 * ModuleCardLayoutStore), and every write is its own undo step, recorded as it is made. Reaches the editor through a
 * SafePointer and the node by id, never the card, which every write rebuilds. Message thread only.
 * docs/layout/module-card-layout.md#editing-a-layout.
 */
class BuiltInCardLayoutSource final : public CardLayoutEditorSource {
public:
    /** `undo` may be null (no undo step). The node must hold a card drawn from layout data. */
    BuiltInCardLayoutSource(GraphEditor& editor, ::AppUndoManager* undo, juce::AudioProcessorGraph::NodeID nodeId);
    ~BuiltInCardLayoutSource() override;

    bool isAlive() const override;
    /** The module's factory type name ("Filter"), the key of its per-type default and presets. */
    const juce::String& moduleType() const noexcept { return moduleType_; }
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
    /** Writes back the node's override as it stood when this source was made (or clears it, when it had
     *  none), puts back the type's default if an Apply to all or Reset changed it, and rebuilds the card:
     *  a cancelled session leaves the layout as it opened. Recorded as one more undo step (when it
     *  changes anything), so undo brings the cancelled edits back. */
    void restoreOpeningLayout();

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
    /** The graph as it is now when undo is on (the "before" of a write), else void. */
    juce::var snapshotBeforeWrite() const;
    /** Records the difference from `before` to the graph now as one undo step; none when nothing changed. */
    void recordSince(const juce::var& before);

    juce::Component::SafePointer<juce::Component> editor_;
    ::AppUndoManager* undo_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    juce::AudioProcessor* openedOn_ = nullptr; ///< Identity only, never dereferenced unless the node still holds it.
    juce::String moduleType_;
    juce::var openingOverride_;                ///< The node's "cardLayout" JSON at construction; void = none.
    std::optional<CardLayout> openingDefault_; ///< The type's stored default at construction; none = no file.
    bool defaultTouched_ = false;              ///< An Apply to all or Reset wrote or cleared the type's default.
};

} // namespace synth::ui
