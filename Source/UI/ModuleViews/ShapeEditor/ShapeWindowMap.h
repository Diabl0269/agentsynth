#pragma once

// ShapeWindowMap.h -- the shape editor window's morph map, which also takes a Shape dragged from the Sounds view:
// over a dot it stamps that dot's shape, over empty space it adds a shape there.
#include "UI/Library/SoundBrowser/SoundDropAdapter.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include <functional>

namespace synth::ui {

class ShapeWindowMap
    : public MorphMap
    , public SoundDropAdapter
    , private ShapeDropTarget {
public:
    /** What the map asks of the editor for a dragged library shape. */
    struct DropHooks {
        std::function<bool(synth::shapes::ShapeKind)> accepts;
        /** Dropped on dot `dot` (-1: on empty space at map position `at`, each coordinate 0..1). */
        std::function<void(const synth::shapes::LibraryShape&, int dot, juce::Point<float> at)> dropped;
    };

    DropHooks dropHooks;

    /** The dot a drop at the pointer would stamp (-1: it would add a shape, or no drag is over the map). */
    int getDropDot() const noexcept { return dropOver_ ? dropDot_ : -1; }
    bool isDropOver() const noexcept { return dropOver_; }
    /** What the hint over the map says while a shape is dragged over it; empty when none is. */
    juce::String getDropHint() const;

    ShapeDropTarget* shapeDropTarget() override { return this; }
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    bool acceptsShapeDrop(synth::shapes::ShapeKind kind) const override;
    void shapeDragOver(bool over, const juce::String& name) override;
    void shapeDropped(const synth::shapes::LibraryShape& shape) override;
    void trackPointer(juce::Point<int> local);

    bool dropOver_ = false;
    juce::String dropName_;
    juce::Point<int> dropPoint_;
    int dropDot_ = -1;
};

} // namespace synth::ui
