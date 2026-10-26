#pragma once

#include "UI/Library/SoundBrowser/ShapeDropTarget.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Lets a component that already takes OS file drops (it derives juce::FileDragAndDropTarget too) take a dragged Sound
 *  the same way: a sound is offered to the file target as `{path}`, so the rules, the highlight and the drop are the
 *  file drop's own. Takes nothing but a sound (a module or snippet drag falls through to the canvas). While a sound
 *  drag is live, isShowingSoundOutline() is true when the file target would take it; the owner paints the faint
 *  outline from it. A component that also takes a dragged Shape returns a ShapeDropTarget from shapeDropTarget(); a
 *  shape is then offered to it the same way, and a component without one refuses every shape. A target that overrides
 *  takesStretchedLoops() is offered a loop as a stretched copy at the song's tempo (and key), written on the drop. */
class SoundDropAdapter
    : public juce::DragAndDropTarget
    , private SoundDragState::Listener {
public:
    SoundDropAdapter();
    ~SoundDropAdapter() override;

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    /** True while a sound or shape drag is live and this component would take it. */
    bool isShowingSoundOutline() const noexcept { return outline_; }
    /** The part of this component that takes a dragged Shape; null (the default) refuses shapes. */
    virtual ShapeDropTarget* shapeDropTarget() { return nullptr; }
    /** True (an audio track) to be offered a loop fitted to the song; false (the default) always gets the original
     * file. */
    virtual bool takesStretchedLoops() const { return false; }

private:
    void soundDragChanged() override;
    juce::FileDragAndDropTarget* fileTarget() { return dynamic_cast<juce::FileDragAndDropTarget*>(this); }

    bool outline_ = false;
};

} // namespace synth::ui
