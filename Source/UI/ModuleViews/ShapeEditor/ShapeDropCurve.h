#pragma once

// ShapeDropCurve.h -- a curve editor that also takes a Shape dragged from the Sounds view: dropped on the curve it
// replaces the curve of the dot the editor shows (the selected one). Used by the shape window and by the Filter card's
// small curve view; the owner says whether a shape is taken, which dot it lands on and does the replacing.
#include "UI/Library/SoundBrowser/SoundDropAdapter.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <functional>

namespace synth::ui {

class ShapeDropCurveEditor
    : public CurveEditorComponent
    , public SoundDropAdapter
    , private ShapeDropTarget {
public:
    struct DropHooks {
        std::function<bool(synth::shapes::ShapeKind)> accepts;
        /** The name of the dot a drop would replace the curve of; empty when there is none (the drop is refused). */
        std::function<juce::String()> targetName;
        /** Replace that dot's curve with the library shape's, as one undo step. */
        std::function<void(const synth::shapes::LibraryShape&)> dropped;
    };

    DropHooks dropHooks;

    bool isDropOver() const noexcept { return dropOver_; }
    /** What the hint over the curve says while a shape is dragged over it; empty when none is. */
    juce::String getDropHint() const;

    ShapeDropTarget* shapeDropTarget() override { return this; }
    void paintOverChildren(juce::Graphics& g) override;

private:
    bool acceptsShapeDrop(synth::shapes::ShapeKind kind) const override;
    void shapeDragOver(bool over, const juce::String& name) override;
    void shapeDropped(const synth::shapes::LibraryShape& shape) override;

    bool dropOver_ = false;
    juce::String dropName_;
};

} // namespace synth::ui
