#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The like and dislike thumbs of a built song card: icon-only buttons drawn with Graphics paths (never a text glyph, so
// they look the same on every system and take the theme's colours).
//
// Position: a small row of the two thumbs, right-aligned to the card content's right edge, under the finished content
// (after the Steps and Details folds), appearing when the song is built. They sit at the right end because the chat
// apps (ChatGPT, Claude) put the rating at the right end of the action row under an answer, and the edit-plan card's
// rating row sits on the same side so every rating row agrees. docs/ai/song-steps.md#the-song-card records it.
namespace synth {

class ThumbButton : public juce::Button {
public:
    static constexpr int kSize = 28;
    static constexpr int kGap = 4; // between the two thumbs of a row

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
