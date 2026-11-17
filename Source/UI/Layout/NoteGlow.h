#pragma once

#include "UI/Layout/FadeAmount.h"
#include <array>
#include <bitset>
#include <juce_gui_basics/juce_gui_basics.h>

// NoteGlow (docs/layout/animation.md#fading-things-in-and-out): how lit each key of a painted keyboard is. A key that
// lights takes the full amount at once (a played note must answer the click); a key that stops being lit fades out
// with the shared FadeAmount numbers instead of snapping back. One FadeAmount serves every fading key: each key keeps
// the amount it had when it was released and the shared fade scales it to zero, so a key released mid-fade, or lit
// again mid-fade, starts from where it stands.
namespace synth::ui {

class NoteGlow {
public:
    using Amounts = std::array<float, 128>;

    explicit NoteGlow(juce::Component& owner)
        : fade_(owner) {}

    /** The area a frame repaints (the keys), in the owner's coordinates; unset repaints the whole owner. */
    void setRepaintArea(std::function<juce::Rectangle<int>()> area) { fade_.repaintArea = std::move(area); }

    /** Sets the notes that are lit now; keys that just left the set fade out. */
    void set(const std::bitset<128>& lit) {
        const Amounts current = amounts();
        bool released = false;
        for (size_t note = 0; note < lit_.size(); ++note) {
            if (lit.test(note)) {
                from_[note] = 1.0f;
            } else {
                from_[note] = current[note];
                released = released || current[note] > 0.0f;
            }
        }
        lit_ = lit;
        if (!released) {
            fade_.snapTo(false);
            return;
        }
        fade_.snapTo(true);
        fade_.setShown(false);
    }

    /** The amount to paint each key with: 1 lit, fading to 0 after release. */
    Amounts amounts() const {
        Amounts out{};
        const float trail = fade_.value();
        for (size_t note = 0; note < out.size(); ++note)
            out[note] = lit_.test(note) ? 1.0f : from_[note] * trail;
        return out;
    }

    bool isFading() const noexcept { return fade_.isFading(); }

private:
    FadeAmount fade_;
    std::bitset<128> lit_;
    Amounts from_{};
};

} // namespace synth::ui
