#pragma once

// The Drum Kit card's pad grid: four pads a row, pad 1 top left, one Tab stop. A click plays a pad and picks it,
// Shift+click and the arrow keys pick it without a sound, Space and Return play the picked pad.
// docs/modules/drum-kit.md#the-card.

#include "Modules/DrumKit/DrumKitModule.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/UIAnimation.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class DrumPadGrid
    : public juce::Component
    , public juce::TooltipClient
    , public synth::testmode::StateSource {
public:
    static constexpr int kColumns = 4;
    static constexpr int kGap = 4;
    static constexpr int kTallPad = 44;
    static constexpr int kShortPad = 38;
    static constexpr int kTallPadLimit = 16; // above this many pads they are short
    static constexpr double kFlashMs = 120.0;
    static constexpr double kRingSlideMs = 120.0;

    /** The grid's height for `count` shown pads. */
    static int heightFor(int count);

    /** The kit must outlive this. */
    explicit DrumPadGrid(DrumKitModule& kit);
    ~DrumPadGrid() override;

    /** The picked pad, 0-based; always one of the shown pads. */
    int picked() const noexcept { return picked_; }
    /** Picks `index` (clamped to the shown pads); `play` also plays it. */
    void pick(int index, bool play);
    /** Called after every pick with the pad and whether it played. */
    std::function<void(int index, bool played)> onPicked;

    /** Re-reads the pad count, names and kinds. */
    void refresh();

    /** Local bounds of pad `index`. */
    juce::Rectangle<int> padBounds(int index) const;
    /** The pad under `p` (local), or -1. */
    int padAt(juce::Point<int> p) const;
    /** What a screen reader says for pad `index`: "Snare pad, built-in sound, 2 of 8". */
    juce::String padTitle(int index) const;
    juce::String padTooltip(int index) const;
    /** True for the 120 ms after pad `index` played from the card (on screen, with Animations on). */
    bool isFlashing(int index) const;
    /** The child that stands for pad `index` to a screen reader (hidden beyond the count). */
    juce::Component& padCell(int index);
    /** Where the selection ring is drawn now (it slides between pads). */
    juce::Rectangle<float> ringBounds() const noexcept { return ringRect_; }

    juce::String getTooltip() override;
    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void addTestModeState(juce::DynamicObject& into) const override;

private:
    class Cell;
    friend class Cell;

    void paintPad(juce::Graphics& g, int index, juce::Rectangle<int> area) const;
    void moveRingTo(int index, bool animate);

    DrumKitModule& kit_;
    std::array<std::unique_ptr<Cell>, synth::drums::kMaxPads> cells_;
    int flashing_ = -1;
    int count_ = 0;
    int picked_ = 0;
    int auditioned_ = -1;
    juce::Rectangle<float> ringRect_;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver ringSlide_;
    AnimationDriver flash_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPadGrid)
};

} // namespace synth::ui
