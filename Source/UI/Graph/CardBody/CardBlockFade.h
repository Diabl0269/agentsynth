#pragma once

// CardBlockFade (docs/layout/animation.md#fading-things-in-and-out): one part of a module card that appears and
// disappears in place (the Show Scope panel, the response view, the LFO's custom wave section, a card view, the
// More row's controls) with the shared FadeVisibility, and takes its height in step with the fade so the card and
// the neighbours it makes room for slide instead of jumping. It adds no animator: FadeVisibility's own driver runs,
// `progress()` is the shown amount, and each frame of it re-lays the card out.
//
// The card's height follows the fade, but the neighbours are made room for ONCE per direction (a card growing asks
// for its final footprint when the fade starts, a card shrinking gives the room back when the fade has ended):
// asking per frame would push and pull every neighbour 10 times a second and rebuild the cables with it.
#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace synth {

class CardBlockFade {
public:
    CardBlockFade() = default;

    /** Starts managing `targets` (not empty; the first one stands for the block's visibility). `relayout` re-measures
     *  the card from the block's `height()`; it runs every frame of a fade and when one has ended. `settled` runs
     *  once when a fade-out has ended, to give the room back. Both may be empty. */
    void attach(std::vector<juce::Component*> targets, std::function<void()> relayout, std::function<void()> settled) {
        fade_ = std::make_unique<synth::ui::FadeVisibility>(std::move(targets));
        fade_->onFrame = std::move(relayout);
        fade_->onHidden = std::move(settled);
    }

    /** Drops the fade; call before the targets are destroyed. */
    void reset() { fade_.reset(); }

    bool isAttached() const noexcept { return fade_ != nullptr; }
    bool isFading() const noexcept { return fade_ != nullptr && fade_->isFading(); }
    /** True while the block is shown or fading in. */
    bool isShown() const { return fade_ != nullptr && fade_->isShown(); }
    /** How much of the block shows: 1 when it is shown and `atFinal` (the card's final footprint), 0 when it is hidden
     *  and `atFinal`, `progress()` otherwise. 0 without a fade. */
    float fraction(bool atFinal = false) const {
        if (fade_ == nullptr)
            return 0.0f;
        return atFinal ? (fade_->isShown() ? 1.0f : 0.0f) : fade_->progress();
    }

    /** The part of `full` px the block takes now. */
    int height(int full, bool atFinal = false) const {
        return static_cast<int>(static_cast<float>(full) * fraction(atFinal) + 0.5f);
    }

    /** Fades the block in or out and keeps the card in step. `settling` is the owner's "measure at the final
     *  footprint" flag that `height(.., atFinal)` is read with; `relayout` re-measures the card and `makeRoom` lets
     *  its neighbours make room (or come back). When the change lands at once (the card is off screen, Animations
     *  Off) this is just `relayout` then `makeRoom`, as it always was. */
    void setShown(bool shown, bool& settling, const std::function<void()>& relayout,
                  const std::function<void()>& makeRoom) {
        if (fade_ == nullptr)
            return;
        fade_->setShown(shown);
        if (!fade_->isFading()) {
            relayout();
            makeRoom();
            return;
        }
        if (shown) {
            settling = true;
            relayout();
            makeRoom();
            settling = false;
        }
        relayout();
    }

    synth::ui::FadeVisibility* get() const noexcept { return fade_.get(); }

private:
    std::unique_ptr<synth::ui::FadeVisibility> fade_;
};

} // namespace synth
