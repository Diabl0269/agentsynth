#pragma once

#include "Music/MusicalScale.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {

/**
 * Picks a range of notes on a strip of four octaves: drag an edge to add or remove notes, drag the middle to move the
 * whole range. With a scale set the edges snap to scale notes and the scale's keys are drawn lighter. The strip scrolls
 * by octaves to keep the range in view. Three Tab stops: the lowest note, the highest note, the whole range; Left and
 * Right move the focused part by one scale note (Shift: an octave, the range always by an octave, Option: one note).
 * Edges and the middle glide 120 ms to their place. docs/layout/note-range-picker.md.
 */
class NoteRangePicker
    : public juce::Component
    , public juce::TooltipClient {
public:
    static constexpr int kPreferredWidth = 240;
    static constexpr int kPreferredHeight = 34;

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

    // ---- Geometry, for tests ---------------------------------------------------------------------------
    /** The first C of the four octaves shown. */
    int getWindowBase() const noexcept { return base_; }
    /** The centre of `pitch`'s key in this component's coordinates. */
    float keyCentreX(int pitch) const;
    /** The width of one white key. */
    float whiteKeyWidth() const;
    juce::Rectangle<int> bandBounds() const;
    juce::Component& getStop(Part part) noexcept;
    Part getFocusedPart() const;
    /** True while the band is gliding to its place. */
    bool isMotionRunning() const noexcept { return glideAnim_.isRunning() || fadeAnim_.isRunning(); }
    /** Test seam: lands a running glide on its final frame. */
    void finishMotionForTest();

    // ---- juce::Component -------------------------------------------------------------------------------
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    juce::String getTooltip() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class Stop;
    struct Drag {
        bool active = false;
        Part part = Part::Band;
        float startX = 0.0f;
        int startLow = 0;
        int startHigh = 0;
    };

    std::optional<Part> partAt(float x) const;
    bool keyPressedOn(Part part, const juce::KeyPress& key);
    int stepNote(int pitch, int direction) const;
    void commit(int lo, int hi, bool notify);
    void dragEdge(Part part, float x);
    void dragBand(const juce::MouseEvent& e);
    void afterRangeChanged();
    void glideTo(float x0, float x1);
    void layoutStops();
    void updateTitle();
    void paintKeys(juce::Graphics& g) const;
    void paintBand(juce::Graphics& g) const;
    juce::String nameOf(int pitch) const;

    int low_ = 48;
    int high_ = 72;
    int base_ = 36;
    std::optional<MusicalScale> scale_;
    int middleCOctave_ = 4;
    Drag drag_;
    Part hover_ = Part::Band;
    bool hoverOnBand_ = false;
    bool hoverOnKeys_ = false;

    float shownX0_ = 0.0f;
    float shownX1_ = 0.0f;
    float fromX0_ = 0.0f;
    float fromX1_ = 0.0f;
    float bandAlpha_ = 1.0f;

    std::unique_ptr<Stop> lowStop_;
    std::unique_ptr<Stop> highStop_;
    std::unique_ptr<Stop> bandStop_;

    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver glideAnim_;
    AnimationDriver fadeAnim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteRangePicker)
};

} // namespace synth::ui
