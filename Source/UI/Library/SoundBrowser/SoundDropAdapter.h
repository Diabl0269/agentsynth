#pragma once

#include "UI/Library/SoundBrowser/SoundDragState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Lets a component that already takes OS file drops (it derives juce::FileDragAndDropTarget too) take a dragged Sound
 *  the same way: a sound is offered to the file target as `{path}`, so the rules, the highlight and the drop are the
 *  file drop's own. Takes nothing but a sound (a module or snippet drag falls through to the canvas). While a sound
 *  drag is live, isShowingSoundOutline() is true when the file target would take it; the owner paints the faint
 *  outline from it. */
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

    /** True while a sound drag is live and this component would take it. */
    bool isShowingSoundOutline() const noexcept { return outline_; }

private:
    void soundDragChanged() override;
    juce::FileDragAndDropTarget* fileTarget() { return dynamic_cast<juce::FileDragAndDropTarget*>(this); }

    bool outline_ = false;
};

} // namespace synth::ui
