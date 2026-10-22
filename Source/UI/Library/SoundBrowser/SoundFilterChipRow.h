#pragma once

#include "UI/Layout/UIAnimation.h"
#include "UI/Library/SoundBrowser/SoundFilterModel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/** The one row of small removable chips under the search, with "Clear all" at its end. It is ONE Tab stop: Left and
 *  Right move across the chips, Space, Return, Delete or Backspace removes the focused one. A new chip grows in at the
 *  end, a removed one shrinks out and the rest slide along (190 ms; an 80 ms fade under Reduce Motion). */
class SoundFilterChipRow
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kHeight = 28;
    static constexpr const char* kClearAllId = "@clear";

    SoundFilterChipRow();
    ~SoundFilterChipRow() override;

    /** The chips now on; new ones grow in, missing ones shrink out. */
    void setChips(const std::vector<SoundFilterModel::Chip>& chips);
    /** A chip's x was pressed or its key was used. */
    std::function<void(const juce::String& chipId)> onRemove;
    std::function<void()> onClearAll;
    /** The row's grown size changed this frame: its owner lays out again. */
    std::function<void()> onMotionFrame;

    /** 0 with no chips up to 1 with any; follows the grow and shrink so the owner can make room for the row. */
    float rowPresence() const;
    int visibleChipCount() const;
    bool isMotionRunning() const { return anim_.isRunning(); }
    void finishMotionForTest();
    /** Test seams: let the motion run while the row is off screen, and step it to `ms` after its first frame. */
    void setAnimateOffscreenForTest(bool on) { animateOffscreenForTest_ = on; }
    void stepMotionForTest(double ms) { anim_.stepForTest(ms); }
    /** The bounds of the chip with `id` (or the Clear all button) now, and of its x. Empty when absent. */
    juce::Rectangle<int> getChipBounds(const juce::String& id) const;
    juce::Rectangle<int> getRemoveBounds(const juce::String& id) const;
    juce::String getFocusedId() const;
    juce::String getAccessibilityValueText() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    struct Item {
        SoundFilterModel::Chip chip;
        float from = 0.0f, to = 1.0f, shown = 1.0f;
        bool leaving = false;
    };

    int fullWidth(const Item& item) const;
    void relayout();
    void startMotion();
    void activate(int index);
    int indexAtX(int x) const;
    std::vector<int> focusOrder() const;
    juce::String spoken(int index) const;
    void setFocusedItem(int index);

    std::vector<Item> items_; // the chips in order, then (when any chip stays) the Clear all button
    std::vector<juce::Rectangle<int>> bounds_;
    int focused_ = 0;
    int hovered_ = -1;
    bool animateOffscreenForTest_ = false;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver anim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundFilterChipRow)
};

} // namespace synth::ui
