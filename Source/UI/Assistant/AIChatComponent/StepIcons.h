#pragma once

#include "AI/SongSteps/SongSteps.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

// The icon of one step (docs/ai/song-steps.md): eleven kinds, each moving in its own way while its step runs and
// still otherwise. The painter is a free function so a card row, the edit-plan card and a test draw the same art.
namespace synth::ui {

enum class StepIconState {
    still,   // the kind's art in a fixed pose (an edit-plan row before Apply)
    running, // the kind's art, moving
    fixing,  // the same art, moving, in the warning colour (the caller picks the colour)
    done,    // a check
    waiting, // a dashed ring
    skipped  // a ring with a dash
};

/** Draws the icon into `area` (square). `phase` (0 to 1, looping) is the point in the motion; it is ignored unless the
 *  state is running or fixing. */
void paintStepIcon(juce::Graphics& g, juce::Rectangle<float> area, songsteps::StepKind kind, StepIconState state,
                   float phase, juce::Colour colour);

/** A small component showing one step icon. While running or fixing (and on screen, and motion allowed) it loops its
 *  motion on a self-restarting driver and repaints only its own bounds. */
class StepIconView : public juce::Component {
public:
    StepIconView();
    ~StepIconView() override;

    void setKind(songsteps::StepKind kind);
    void setState(StepIconState state);
    StepIconState getState() const { return state_; }
    songsteps::StepKind getKind() const { return kind_; }
    void setColour(juce::Colour colour);
    /** The colour of the settled check, when it differs from the moving art's (a still icon is accent, a check is
     * green). */
    void setDoneColour(juce::Colour colour);

    /** Runs the motion for one cycle after `delayMs`, then settles into a check; at once under reduced motion. */
    void playThenSettle(int delayMs);

    void paint(juce::Graphics& g) override;
    void parentHierarchyChanged() override { updateLoop(); }
    void visibilityChanged() override { updateLoop(); }

    float phaseForTesting() const { return phase_; }

private:
    void updateLoop();
    void startCycle();

    songsteps::StepKind kind_ = songsteps::StepKind::plan;
    StepIconState state_ = StepIconState::still;
    juce::Colour colour_{juce::Colours::white};
    std::optional<juce::Colour> doneColour_;
    float phase_ = 0.0f;
    bool looping_ = false;
    bool settleAfterCycle_ = false;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver driver_;
};

} // namespace synth::ui
