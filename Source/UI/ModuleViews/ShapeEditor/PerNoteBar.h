#pragma once

// PerNoteBar.h -- the shape editor's per-note settings, in its bottom bar: "Notes move the cursor" (Off, By hand,
// By pitch, Random), Smoothness (Hard to Smooth) and, for By hand, the key strip with its octave buttons. A line of
// hint text fades in under the mode row when the chosen mode cannot work yet (no Pitch cable, or a mono Filter).
#include "NoteKeyStrip.h"
#include "ShapeListAdapter.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** Every change goes to the adapter live, and each click, key step or slider drag is one undo step (choosing By hand
 *  or By pitch also cables the Filter's Pitch input, in the same step). The key and hint rows keep their slots when
 *  hidden, so nothing moves when they fade in or out. The hint follows the cabling at 10 Hz while a mode is on, since a
 *  cable or the Poly switch changed elsewhere is not a change of the shape list. Message thread only. */
class PerNoteBar
    : public juce::Component
    , private juce::Timer {
public:
    static constexpr int kRowHeight = 36;
    static constexpr int kKeyRowHeight = 60;
    static constexpr int kHintHeight = 18;
    static constexpr int kGap = 8;
    static constexpr int kHeight = kRowHeight + kHintHeight + kGap + kKeyRowHeight;

    /** `adapter` must outlive the bar and hasPerNote(). */
    explicit PerNoteBar(ShapeListAdapter& adapter);
    ~PerNoteBar() override;

    /** Re-reads the settings and the shape names from the adapter (a change made elsewhere, such as undo). */
    void refresh(const std::vector<ShapeItem>& shapes);
    /** Fired after the bar wrote to the adapter. */
    std::function<void()> onEdited;

    CardSegmentedSwitch& getModeSwitch() noexcept { return mode_; }
    juce::Slider& getSmoothness() noexcept { return smoothness_; }
    NoteKeyStrip& getKeyStrip() noexcept { return keys_; }
    juce::TextButton& getOctaveDown() noexcept { return octaveDown_; }
    juce::TextButton& getOctaveUp() noexcept { return octaveUp_; }
    juce::Label& getHint() noexcept { return hint_; }
    /** True while the hint line is logically shown, even mid-fade. */
    bool isHintShown() const { return hintFade_->isShown(); }
    /** True while the key row is logically shown (By hand), even mid-fade. */
    bool isKeyRowShown() const { return keyRowFade_->isShown(); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void write(const std::function<void(CustomFilterPerNote&)>& change, bool undoStep);
    void showKeyRow(bool shown);
    void updateHint(const CustomFilterPerNote& perNote);
    void lookAndFeelChanged() override;
    void timerCallback() override;

    ShapeListAdapter& adapter_;
    CardSegmentedSwitch mode_;
    juce::Slider smoothness_;
    NoteKeyStrip keys_;
    juce::TextButton octaveDown_{"<"};
    juce::TextButton octaveUp_{">"};
    juce::Label hint_;
    std::unique_ptr<FadeVisibility> keyRowFade_;
    std::unique_ptr<FadeVisibility> hintFade_;
    bool dragging_ = false;
    bool laidOut_ = false;
    bool refreshed_ = false;
    juce::Rectangle<int> labelArea_, hardArea_, smoothArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerNoteBar)
};

} // namespace synth::ui
