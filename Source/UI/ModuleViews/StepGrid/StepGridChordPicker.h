#pragma once

#include "StepGridChords.h"
#include "UI/Layout/CalloutReveal.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/**
 * The Poly Sequencer's chord picker: a popover (juce::CallOutBox) that grows out of a step with the chords that fit the
 * key, then the ones outside it, as chips in rows. One Tab stop: Up and Down move between rows, Left and Right between
 * chips, Enter picks, Esc closes. Hovering a chip lights it; the current chord is accent-highlighted. It holds no
 * pattern: a pick is reported and the popover closes. docs/modules/sequencer-design.md#poly-sequencer-chords.
 */
class StepGridChordPicker
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kWidth = 250;

    StepGridChordPicker(stepgrid::ChordPickerContent content, SequencerChord current);
    ~StepGridChordPicker() override;

    /** A chip was picked (click or Enter); the popover closes after reporting it. */
    std::function<void(SequencerChord chord)> onPick;
    /** The chord under the pointer or the keyboard changed (none: nullopt); only lights a chip today. */
    std::function<void(std::optional<SequencerChord> chord)> onHover;

    const stepgrid::ChordPickerContent& getContent() const noexcept { return content_; }
    /** The chip the keyboard is on, as row and index. */
    int getFocusedRow() const noexcept { return row_; }
    int getFocusedChip() const noexcept { return chip_; }
    /** The chord of the chip the keyboard is on. */
    SequencerChord getFocusedChord() const;
    juce::Rectangle<int> chipBounds(int row, int chip) const;
    /** Picks the chip at `row`, `chip`: the same as clicking it. */
    void pickChip(int row, int chip);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void parentHierarchyChanged() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    struct Placed {
        int row = 0;
        int chip = 0;
        juce::Rectangle<int> bounds;
    };
    struct RowPlace {
        int labelY = 0;
        int labelHeight = 0;
    };

    int layoutAll();
    bool findChip(juce::Point<int> p, int& row, int& chip) const;
    juce::String describe() const;
    void setHover(int row, int chip);

    stepgrid::ChordPickerContent content_;
    SequencerChord current_;
    std::vector<Placed> placed_;
    std::vector<RowPlace> rowPlaces_;
    int outsideHeaderY_ = 0;
    int row_ = 0;
    int chip_ = 0;
    int hoverRow_ = -1;
    int hoverChip_ = -1;
    CalloutReveal reveal_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGridChordPicker)
};

} // namespace synth::ui
