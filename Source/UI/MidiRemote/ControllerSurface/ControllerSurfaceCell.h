#pragma once

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "MidiRemote/RemoteModel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// ControllerSurfaceCell.h (docs/control/midi-remote-ui.md#surface-centre): one grid cell
// -- a DISPLAY-ONLY widget (real juce::Slider/juce::Button, setInterceptsMouseClicks(false, false)
// so the real widget's own AppLookAndFeel::drawRotarySlider/drawLinearSlider/drawButtonBackground
// paint it exactly like a module card's own knob, per Source/UI/CLAUDE.md's mockup-fidelity
// convention -- but it is never attached to a parameter and never sends MIDI) that MOVES from
// activity events, never from the mouse. The CELL ITSELF owns real mouse handling: click selects,
// drag reports a grid-cell delta to the owning ControllerSurfaceComponent (which does the actual
// snap/clamp/write-back), Delete/Backspace while selected+focused requests deletion. There is no
// dedicated wheel painter in AppLookAndFeel (confirmed: only rotary/linear/button) -- a
// LinearHorizontal juce::Slider is the honest fallback for ControlKind::wheel.
namespace synth::ui {

class ControllerSurfaceCell
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    ControllerSurfaceCell();
    ~ControllerSurfaceCell() override;

    /** Structural: which control this cell represents, its assignment label, whether it should
     *  paint the MIDI-mapped badge, and the widget's initial displayed value (0..1, same domain as
     *  noteActivity()). */
    void configure(const synth::Control& control, const juce::String& assignmentLabel, bool isWarningLabel,
                   bool isMapped, float initialValue = 0.0f);

    const juce::String& getControlId() const noexcept { return controlId_; }
    const synth::Control& getControl() const noexcept { return control_; }

    /** Test seam: the widget's current displayed value (0..1) -- what configure()'s
     *  initialValue seeded and/or noteActivity() has since driven. */
    float getDisplayedValueForTest() const noexcept { return lastValue_; }
    const juce::String& getAssignmentLabelForTest() const noexcept { return assignmentLabel_; }
    bool isAssignmentWarningForTest() const noexcept { return assignmentIsWarning_; }

    void setSelected(bool selected);
    bool isSelected() const noexcept { return selected_; }

    /** Live activity: absolute sets the widget value directly (0..1); relativeDelta accumulates
     *  onto the last known value (clamped [0,1]); buttonPress/Release sets the button's toggle
     *  state. A control with no activity yet paints at rest (0 / not pressed) --
     *  docs/control/midi-remote-ui.md#surface-centre. No-op (and no repaint) if the decoded value
     *  doesn't actually change the widget's current state. */
    void noteActivity(synth::midi::RemoteEventKind kind, float value);

    /** A control Detect just added pulses (a breathing accent outline, time-bounded to
     *  kDetectPulseMaxMs) until the next message arrives. `sinceMs` is
     *  juce::Time::getMillisecondCounterHiRes() at the moment it started; 0 stops it
     *  (see docs/control/midi-remote-ui.md#detect-mode). */
    void setDetectPulse(double sinceMs);
    /** An existing control lit by Detect: a solid outline for kDetectFlashMs. */
    void flash();
    /** Repaints only while a pulse/flash is live or has just expired -- called from the panel's
     *  existing gated activity tick, never a timer of its own (Source/UI/CLAUDE.md). */
    void tickHighlight();
    bool hasLiveHighlightForTest() const noexcept { return pulseSinceMs_ != 0.0 || flashUntilMs_ != 0.0; }

    /** `mods` distinguishes a plain click (owner replaces the selection with just this
     *  cell) from shift (add) or cmd (toggle) -- see ControllerSurfaceSelection.cpp. */
    std::function<void(const juce::ModifierKeys& mods)> onSelected;
    /** Fired on drag once the pointer has crossed into a new cell -- (dCols, dRows) is the delta
     *  from the drag's START cell, so the owner can compute newCol/newRow = startCol/startRow +
     *  delta and clamp once, rather than accumulating per-pixel drift. */
    std::function<void(int dCols, int dRows)> onDraggedByCells;
    std::function<void()> onDragEnded; // owner commits the move (updateProfile()) here, not per-step

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    static constexpr int kCellSize = 56;
    // The grid's PITCH -- the pixel distance from
    // one cell's origin to the next -- is kCellSize + the owner's inter-cell margin (6 px), not
    // kCellSize alone (setControls()/moveCellToLayout() on ControllerSurfaceComponent both lay
    // cells out on this pitch already). mouseDrag() divides the drag's pixel offset by this to get
    // a whole-cell delta; dividing by kCellSize alone under-counts the pitch and over-reports the
    // delta (e.g. a real 8-cell, 8*62 px drag was reported as round(8*62/56) = 9 cells). The margin
    // is duplicated here (ControllerSurfaceComponent::kCellMargin is the same 6, forwarded from
    // this constant) rather than the cell including ControllerSurfaceComponent.h for it.
    static constexpr int kCellMargin = 6;
    static constexpr int kCellPitch = kCellSize + kCellMargin;
    static constexpr double kDetectPulseMaxMs = 10000.0;
    static constexpr double kDetectFlashMs = 250.0;

private:
    void buildWidgetForKind();

    juce::String controlId_;
    synth::Control control_;
    juce::String assignmentLabel_;
    bool assignmentIsWarning_ = false;
    bool mapped_ = false;
    bool selected_ = false;
    float lastValue_ = 0.0f;
    bool lastPressed_ = false;
    double pulseSinceMs_ = 0.0; // 0 = not pulsing
    double flashUntilMs_ = 0.0; // 0 = not flashing

    std::unique_ptr<juce::Slider> slider_;     // knob/encoder/fader/wheel, style set per kind
    std::unique_ptr<juce::TextButton> button_; // pad/button

    // In the PARENT's coordinate space, not this cell's own local space -- the owner
    // repositions this cell mid-drag (moveCellToLayout()) each time a boundary is crossed, which
    // would otherwise shift the meaning of a cell-local anchor out from under a drag already in
    // progress. See mouseDown()/mouseDrag()'s own comment for the failure this caused.
    juce::Point<int> dragStartMouse_;
    bool isDragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllerSurfaceCell)
};

} // namespace synth::ui
