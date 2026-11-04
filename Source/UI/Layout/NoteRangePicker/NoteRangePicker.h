#pragma once

#include "Music/MusicalScale.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {

/**
 * Picks a range of notes on a strip of keys that scrolls over the whole keyboard, with a thin overview of the whole
 * keyboard above it. Drag an edge to add or remove notes, drag the middle to move the range (note by note, Shift:
 * octaves), click empty keys to move it there, click a note name to type one. Dragging at either end of the strip
 * scrolls it. With a scale set the edges snap to scale notes. Three Tab stops: the lowest note, the highest note, the
 * whole range. docs/layout/note-range-picker.md.
 */
class NoteRangePicker
    : public juce::Component
    , public juce::TooltipClient {
public:
    static constexpr int kStripHeight = 34;
    static constexpr int kOverviewHeight = 8;
    static constexpr int kOverviewGap = 3;
    static constexpr int kPreferredWidth = 240;
    /** The overview bar, the gap and the strip. A picker given less height shows the strip alone. */
    static constexpr int kPreferredHeight = kOverviewHeight + kOverviewGap + kStripHeight;

    /** The three parts of the picker, in Tab order. */
    enum class Part { LowEdge, HighEdge, Band };

    NoteRangePicker();
    ~NoteRangePicker() override;

    /** Shows `lo`..`hi` (clamped to 0..127, swapped if reversed); does not call onChange. */
    void setRange(int lo, int hi);
    int getLow() const noexcept { return low_; }
    int getHigh() const noexcept { return high_; }

    /** The scale the edges snap to; null or a chromatic scale means every note. The scale is copied. */
    void setScale(const MusicalScale* scale);
    /** The octave number MIDI 60 gets in the note names (4 in the piano roll, 3 in the step grid). */
    void setMiddleCOctave(int octave);

    /** Called after the user changes the range (mouse or keys), never by setRange. */
    std::function<void(int lo, int hi)> onChange;
    /** Called once when a gesture ends with a different range than it began with, and once per key step or typed note
     *  that moved the range; never by setRange. The place to record one undo step. */
    std::function<void(int lo, int hi)> onGestureEnd;

    // ---- Geometry and gestures, for tests ----------------------------------------------------------------
    /** The first white key of the window, in white-key units from C-1 (fractional while scrolling). */
    float getScroll() const noexcept { return scroll_; }
    /** How many white keys the strip shows. */
    int getVisibleKeys() const noexcept { return keys_; }
    /** The centre of `pitch`'s key in this component's coordinates. */
    float keyCentreX(int pitch) const;
    /** The width of one white key. */
    float whiteKeyWidth() const;
    /** The band's bounds as drawn (the strip's rows only). */
    juce::Rectangle<int> bandBounds() const;
    /** Where the overview bar's frame is drawn. */
    juce::Rectangle<float> overviewFrame() const;
    juce::Component& getStop(Part part) noexcept;
    Part getFocusedPart() const;
    /** True while the band or the keys are gliding to their place. */
    bool isMotionRunning() const noexcept { return glideAnim_.isRunning() || fadeAnim_.isRunning(); }
    /** Test seam: lands a running glide on its final frame. */
    void finishMotionForTest();
    /** Test seam: a drag move to `x` (a strip x) as if `dtSeconds` passed since the last one. */
    void dragForTest(float x, double dtSeconds, juce::ModifierKeys mods);
    /** The note editor opened by a click on a note name, and which edge it edits. */
    juce::TextEditor& getNoteEditor() noexcept { return *noteEditor_; }
    bool isEditingNote() const noexcept;
    /** Opens the note editor on an edge, as a click on its name does. */
    void beginNoteEdit(Part edge);

    // ---- juce::Component -------------------------------------------------------------------------------
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    juce::String getTooltip() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class Stop;
    enum class DragKind { None, Overview, Edge, Band };
    struct Drag {
        DragKind kind = DragKind::None;
        Part part = Part::Band;
        float startX = 0.0f;
        int startLow = 0; ///< The range when the gesture began (to tell whether it changed anything).
        int startHigh = 0;
        int anchorLow = 0; ///< The range the band is moved from.
        int anchorHigh = 0;
        int grabPitch = 0; ///< The note under the pointer when a band drag began.
        bool moved = false;
        std::optional<Part> label; ///< A note name the press landed on: a click without a drag types a note.
        double lastTime = 0.0;
    };

    // Positions (the gesture side is in NoteRangePickerGestures.cpp).
    int overviewHeight() const noexcept;
    int stripTop() const noexcept;
    int stripHeight() const noexcept;
    float toX(float units) const;
    float bandX0() const;
    float bandX1() const;
    /** Where an edge's note name goes: inside the band, outside beside its handle, or pinned at the strip's end. */
    enum class LabelSpot { Inside, Outside, Pinned };
    LabelSpot labelSpot(Part edge) const;
    juce::Rectangle<float> labelArea(Part edge) const;
    std::optional<Part> partAt(float x) const;
    std::optional<Part> labelAt(const juce::Point<float>& p) const;

    // Range and view.
    void commit(int lo, int hi, bool notify, float scrollTarget);
    float scrollToShowRange() const;
    float revealTarget(Part part, int lo, int hi) const;
    void scrollTo(float target);
    void setScrollNow(float scroll);
    void scrollBy(float keys);
    bool labelVisible(Part edge) const;
    void finishUserChange(int lo, int hi, Part part);
    void glideTo(float a0, float a1, float scroll);
    void landShown();
    void layoutStops();
    void updateTitle();
    juce::String nameOf(int pitch) const;
    int stepNote(int pitch, int direction) const;
    void finishNoteEdit(bool accept);

    // Gestures.
    void beginDrag(const juce::MouseEvent& e);
    void advanceDrag(float x, juce::ModifierKeys mods, double dt);
    void dragEdge(Part part, float x);
    void dragBand(float x, juce::ModifierKeys mods);
    bool keyPressedOn(Part part, const juce::KeyPress& key);

    void paintOverview(juce::Graphics& g) const;
    void paintKeys(juce::Graphics& g) const;
    void paintBand(juce::Graphics& g) const;

    int low_ = 48;
    int high_ = 72;
    int keys_ = 28;
    float keyW_ = 8.5f;
    float scroll_ = 0.0f; ///< Where the window is (and ends up).
    std::optional<MusicalScale> scale_;
    int middleCOctave_ = 4;
    Drag drag_;
    Part editing_ = Part::LowEdge;

    // What is drawn: the window's left side and the band's two sides, in white-key units, gliding to the above.
    float shownScroll_ = 0.0f;
    float shownA0_ = 0.0f;
    float shownA1_ = 0.0f;
    float fromScroll_ = 0.0f;
    float fromA0_ = 0.0f;
    float fromA1_ = 0.0f;
    float toScroll_ = 0.0f;
    float toA0_ = 0.0f;
    float toA1_ = 0.0f;
    float bandAlpha_ = 1.0f;

    std::unique_ptr<Stop> lowStop_;
    std::unique_ptr<Stop> highStop_;
    std::unique_ptr<Stop> bandStop_;
    std::unique_ptr<juce::TextEditor> noteEditor_;

    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver glideAnim_;
    AnimationDriver fadeAnim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteRangePicker)
};

} // namespace synth::ui
