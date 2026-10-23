#pragma once

// ModuleShapesAdapter.h -- what a module's shape list adapter shares: the undo bracket around one edit of the module's
// extra state, and the morph cursor as the module's morphX / morphY parameters (a drag is a host gesture on both and
// one undo step). FilterShapesAdapter and LfoShapesAdapter add their own shapes, axes and presets.
#include "AppUndoManager.h"
#include "Modules/ModuleBase.h"
#include "ShapeListAdapter.h"

namespace synth::ui {

class ModuleShapesAdapter : public ShapeListAdapter {
public:
    /** `undo` may be null (no undo steps). The module and graph must outlive the adapter. */
    ModuleShapesAdapter(ModuleBase& module, AppUndoManager* undo, juce::AudioProcessorGraph& graph,
                        juce::AudioProcessorGraph::NodeID nodeId, juce::String moduleName);

    void beginEdit() override;
    void endEdit() override;

    bool isRestoring() const override { return undo_ != nullptr && undo_->isRestoring(); }
    int restoreSerial() const override { return undo_ != nullptr ? undo_->getRestoreSerial() : 0; }

    bool hasCursor() const override { return true; }
    juce::Point<float> getCursor() const override;
    void setCursor(juce::Point<float> position) override;
    void beginCursorGesture() override;
    void endCursorGesture() override;

    /** The name the window title carries; the card refreshes it before opening. */
    void setModuleName(juce::String name) { moduleName_ = std::move(name); }
    /** Where the window keeps its grid and spectrum choices; the card supplies it before opening. May be null. */
    void setApplicationProperties(juce::ApplicationProperties* properties) noexcept { appProperties_ = properties; }
    juce::ApplicationProperties* getApplicationProperties() const override { return appProperties_; }

protected:
    ModuleBase& module_;
    juce::String moduleName_;

private:
    juce::ApplicationProperties* appProperties_ = nullptr;
    AppUndoManager* undo_;
    juce::AudioProcessorGraph& graph_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    juce::var before_;
    bool editOpen_ = false;
    bool cursorGestureOpen_ = false;
};

} // namespace synth::ui
