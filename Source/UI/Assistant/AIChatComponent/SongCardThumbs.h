#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The like and dislike thumbs of a built song card: icon-only buttons drawn with Graphics paths (never a text glyph, so
// they look the same on every system and take the theme's colours).
//
// Position (decided with the song-card redesign, following ChatGPT, Claude and Suno): a small row of the two thumbs,
// left-aligned, under the finished content (after the Steps and Details folds), appearing when the song is built.
// docs/ai/song-steps.md#the-song-card records it.
namespace synth {

class ThumbButton : public juce::Button {
public:
    static constexpr int kSize = 28;

    /** `up` true is the like thumb, false the dislike; a picked thumb is filled in the success or error colour. */
    explicit ThumbButton(bool up);

    bool isUp() const { return up_; }
    void setSelected(bool selected);
    bool isSelected() const { return selected_; }

    /** The thumb's outline inside `area`: up is the hand with its thumb raised, down the same turned over. */
    static juce::Path thumbPath(bool up, juce::Rectangle<float> area);

    void paintButton(juce::Graphics& g, bool hot, bool down) override;

private:
    bool up_;
    bool selected_ = false;
};

} // namespace synth
