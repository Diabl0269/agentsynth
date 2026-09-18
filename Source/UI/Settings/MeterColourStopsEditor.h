#pragma once

#include "UI/Mixer/MeterColourStops.h"
#include "UI/Mixer/MixerMeterScale.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// MeterColourStopsEditor.h -- FRO147 ("Preferences > Metering > Appearance" equivalent, docs/
// mixer.md's Meters section / docs/layout/colour-overrides.md's meter colours): a vertical -60..+3 dB
// scale (the SAME taper/tick marks MixerMeter itself draws -- MixerMeterScale.h) with a live
// preview bar painted through MeterColourStops::forEachBand across the full range, and one
// draggable handle per stop beside it.
//
// This component owns its OWN working copy of a MeterColourStops -- it never touches
// ApplicationProperties or AppLookAndFeel itself, exactly like CableSwatchRow/NoteSwatchRow (the
// existing Appearance-tab swatch rows) stay ignorant of persistence. AppearanceSettingsTab is the
// one place that knows how to save an edit and push it live (see its own FRO147 comment on why
// that push goes through MainComponent's settings-file reload rather than a direct pointer).
//
// Interaction:
//  - Drag a handle vertically: 0.5 dB snap (kDbSnap), clamped to [-60, +3], and clamped further so
//    it can never cross a neighbour (clampDbForIndex leaves at least kDbSnap between stops). The
//    FLOOR stop -- index 0, the lowest dbFrom, "the colour from -inf" -- never drags.
//  - Click a handle's swatch: fires onColourPickerRequested rather than opening a picker itself
//    (mirrors AppearanceSettingsTab::openCableColourPicker/openNoteColourPicker already routing
//    through the owner). The swatch is also the row's most natural grab point, so a PRESS there
//    only commits to "click" (and opens the picker) if the pointer never moves more than
//    kSwatchDragThresholdPx before mouseUp -- move past that, and it drags the handle exactly like
//    a press anywhere else on the row (see mouseDown/mouseDrag/mouseUp).
//  - Click anywhere else in the editor that isn't a handle: adds a stop at that dB, coloured from
//    the band already showing there (brightened so a same-colour add is still visible), up to
//    MeterColourStops::kMaxStops. Refused (no-op) if a stop already sits at that snapped dB.
//  - Select a handle (click, or Tab to the editor + arrow keys), then Delete/Backspace or the
//    owner's "Remove" button (removeSelectedStop()): removes it. Never the floor; never below 1
//    remaining stop.
//  - Up/Down while a handle is selected: nudge kNudgeDb (Shift = kShiftNudgeDb), same clamp rules
//    as a drag. A no-op (but still consumed) on the floor.
//
// Accessibility: a single juce::AccessibilityHandler (juce::AccessibilityRole::slider, mirroring
// MixerMeter's own MeterValueInterface idiom) describing the CURRENTLY SELECTED handle's dB and
// colour -- selecting a different handle changes what gets announced, the same "one adjustable
// value, selection moves which value it is" shape a themed list/slider already reads as.
namespace synth::ui {

class MeterColourStopsEditor
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    MeterColourStopsEditor();
    ~MeterColourStopsEditor() override = default;

    /** Replaces the working stop set wholesale (initial load, "Reset to Theme", or a theme switch
     *  while no override is pinned) and clears the selection -- the old selected INDEX may no
     *  longer mean the same stop. Does not itself fire onChanged (the caller already knows). */
    void setStops(const MeterColourStops& stops);
    const MeterColourStops& getStops() const noexcept { return stops_; }

    /** Owner hook: fired after any edit that moves/adds/removes/recolours/nudges a stop.
     *  `committed` is false for a still-in-progress drag frame (write-through only -- see
     *  MeterColourStops.h's writeMeterColourStopsOverride) and true at a gesture's end (mouse up,
     *  a keyboard nudge, add, remove, or a colour picker preview/commit) -- the owner persists
     *  with saveMeterColourStopsOverride() on true. */
    std::function<void(const MeterColourStops&, bool committed)> onChanged;

    /** Fired on a swatch click -- `index` into getStops(), `screenBounds` the swatch's own
     *  screen-space bounds (a CallOutBox anchor), `current` its live colour. This component never
     *  constructs a picker itself; the owner opens one and calls setStopColour() from its
     *  preview/commit callbacks. */
    std::function<void(int index, juce::Rectangle<int> screenBounds, juce::Colour current)> onColourPickerRequested;

    /** Recolours stop `index` in place (no dB change) -- the owner's ColourPickerPopup preview/
     *  commit callback lands here. */
    void setStopColour(int index, juce::Colour colour, bool committed);

    /** Removes the selected stop. No-op when nothing is selected, the floor (index 0) is
     *  selected, or only one stop remains -- wired to the owner's "Remove" button as well as this
     *  component's own Delete/Backspace handling. */
    void removeSelectedStop();

    static constexpr float kDbSnap = 0.5f;
    static constexpr float kNudgeDb = 0.5f;
    static constexpr float kShiftNudgeDb = 3.0f;
    /** A press on a handle's swatch stays a "click" (opens the colour picker on mouseUp) until the
     *  pointer moves at least this many px from the press point -- past that it commits to a drag,
     *  same as a press anywhere else on the row. */
    static constexpr int kSwatchDragThresholdPx = 4;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // ---- Testing hooks: real-mouse-path (docs/testing.md's "Test the real mouse path") ----
    int getSelectedIndexForTest() const noexcept { return selected_; }
    void selectForTest(int index) { selectIndex(index); }
    juce::Rectangle<int> getHandleBoundsForTest(int index) const { return handleBounds(index); }
    juce::Rectangle<int> getSwatchBoundsForTest(int index) const { return swatchBounds(index); }
    int getHandleCountForTest() const noexcept { return (int)stops_.getStops().size(); }
    float dbForYForTest(int y) const noexcept { return yToDb(y); }
    int yForDbForTest(float db) const noexcept { return dbToY(db); }
    static float snapDbForTest(float db) noexcept { return snapDb(db); }

private:
    enum class HitZone { None, Swatch, Draggable };
    struct Hit {
        int index = -1;
        HitZone zone = HitZone::None;
    };

    int barTop() const noexcept;
    int barBottom() const noexcept;
    float yToDb(int y) const noexcept;
    int dbToY(float db) const noexcept;
    juce::Rectangle<int> handleBounds(int index) const;
    juce::Rectangle<int> swatchBounds(int index) const;
    // Named hitTestStop, not hitTest -- juce::Component::hitTest(int, int) is a different overload
    // (hit-shape for mouse dispatch, 2 raw ints), and shadowing its name here only invites the
    // wrong one to be called by accident.
    Hit hitTestStop(juce::Point<int> localPos) const;

    void selectIndex(int index);
    float clampDbForIndex(int index, float requestedDb) const;
    void setStopDb(int index, float db, bool committed);
    void addStopAt(float db);
    void notify(bool committed);
    static float snapDb(float db) noexcept;

    MeterColourStops stops_;
    int selected_ = -1;
    int dragIndex_ = -1;
    // The dragged stop's OWN dbFrom at mouseDown, so mouseUp can tell "a real drag happened" from
    // "pressed and released without moving" -- a plain click on a handle body selects it and must
    // not fire a committed (or any) onChanged.
    float dragStartDb_ = 0.0f;
    // >= 0 while a press that started ON A SWATCH is still undecided between "click" (open the
    // picker) and "drag" (move the handle) -- see mouseDown/mouseDrag/mouseUp and the class
    // comment above. Holds the pressed stop's index; -1 once the gesture resolves either way.
    int pendingSwatchClickIndex_ = -1;
    juce::Point<int> swatchPressPos_;

    static constexpr int kScaleWidth = 40;
    static constexpr int kPreviewBarWidth = 8;
    static constexpr int kPreviewBarGap = 4;
    static constexpr int kHandleHeight = 22;
    static constexpr int kSwatchSize = 14;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterColourStopsEditor)
};

} // namespace synth::ui
