#pragma once

// PerNoteBar.h -- the shape editor's per-note settings, in its bottom bar: "Notes move the cursor" (Off, By hand,
// By pitch, Random), Smoothness (Hard to Smooth) and, for By hand, the key strip with its octave buttons.
#include "NoteKeyStrip.h"
#include "ShapeListAdapter.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** Every change goes to the adapter live, and each click, key step or slider drag is one undo step. The key row
 *  keeps its slot when it is hidden, so nothing moves when it fades in or out. Message thread only. */
class PerNoteBar : public juce::Component {
public:
    static constexpr int kRowHeight = 36;
    static constexpr int kKeyRowHeight = 60;
    static constexpr int kGap = 8;
    static constexpr int kHeight = kRowHeight + kGap + kKeyRowHeight;

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
    /** True while the key row is logically shown (By hand), even mid-fade. */
    bool isKeyRowShown() const { return keyRowFade_->isShown(); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void write(const std::function<void(CustomFilterPerNote&)>& change, bool undoStep);
    void showKeyRow(bool shown);

    ShapeListAdapter& adapter_;
    CardSegmentedSwitch mode_;
    juce::Slider smoothness_;
    NoteKeyStrip keys_;
    juce::TextButton octaveDown_{"<"};
    juce::TextButton octaveUp_{">"};
    std::unique_ptr<FadeVisibility> keyRowFade_;
    bool dragging_ = false;
    bool laidOut_ = false;
    juce::Rectangle<int> labelArea_, hardArea_, smoothArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerNoteBar)
};

} // namespace synth::ui
