#pragma once

// CableEndGlide.h
//
// The overlay that lets a modulation cable's end glide, not jump, when a card's More row folds or unfolds (the end
// moves between the knob it lands on and the More button over the row's fade time), and when a tab or a Drum Kit pad
// is picked (between the knob and its parked place, see ModuleComponent::parkedCableEnd). Pure
// paint state: no card bounds ever change. The canvas owns it (graph_editor_paint::CanvasMemo) and
// GraphEditor::buildVisibleCables() runs applyTo() after the knob re-anchor, like the other cable overlays.
// docs/layout/cables.md#the-end-glides-when-the-more-row-toggles.

#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class ModuleComponent;

class CableEndGlide {
public:
    using VisibleCable = graph_editor_types::VisibleCable;

    /** `cablesNow` returns the cables as drawn right now; `onFrame` asks the canvas to repaint them. */
    CableEndGlide(juce::VBlankAnimatorUpdater& updater, std::function<const std::vector<VisibleCable>&()> cablesNow,
                  std::function<void()> onFrame);
    ~CableEndGlide();

    /** Call just before `card`'s More row changes state (`unfolding`: the row is about to open). Records where each
     *  cable landing on the card is drawn now and starts the glide; lands at once when nothing animates. */
    void begin(ModuleComponent& card, bool unfolding);
    /** Call just before a pick moves `card`'s cable ends at once (a tab, a Drum Kit pad): each end glides from where it
     *  is drawn now to where it lands after the pick, over the fade-in time. */
    void beginMove(ModuleComponent& card) { begin(card, /*unfolding*/ true); }

    /** Moves the end of every gliding cable between its recorded point and its live anchor. */
    void applyTo(std::vector<VisibleCable>& cables) const;

    bool isLive() const noexcept { return !entries_.empty(); }
    bool isDriverRunningForTest() const noexcept { return driver_.isRunning(); }
    /** Test seams: step to eased progress `e` (0..1) with no VBlank; end the glide. */
    void advanceForTest(float e);
    void completeForTest() { driver_.completeForTest(); }
    void finish();

private:
    struct Entry {
        juce::Component::SafePointer<ModuleComponent> card;
        graph_editor_types::CableId id;
        juce::Point<float> oldRelative; // the end's old place, relative to its card, so a moving card carries it
        // Where a cable on a knob that also has a gutter jack ends up when its row folds (card-relative): the glide
        // runs to it, since the jack only rejoins the gutter when the fade ends.
        std::optional<juce::Point<float>> foldedRelative;
    };

    juce::VBlankAnimatorUpdater& updater_;
    std::function<const std::vector<VisibleCable>&()> cablesNow_;
    std::function<void()> onFrame_;
    synth::ui::AnimationDriver driver_;
    std::vector<Entry> entries_;
    float progress_ = 0.0f;
};
