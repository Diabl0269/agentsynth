#pragma once

#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// FilteredRowFades (docs/layout/animation.md#fading-things-in-and-out): the fade a searchable list gives each of its
// rows as a filter takes it out or brings it back. One FadeVisibility per row, index-aligned with the owner's own row
// list, plus the squeezed slot that lets the list close up: a row that is leaving keeps a slot of `progress()` of its
// height, so the rows below slide while it fades and none ever overlaps its neighbour.
//
// - `isShown(i)` is the LOGICAL answer (false the moment a hide starts): highlight, Return and tests read it, so a
//   row that is fading out can never be picked.
// - A retarget mid-fade (the next keystroke) starts from the row's current opacity, so typing never waits.
// - The first filter pass after `reset()` lands each row on its state; call `markLaidOut()` once it is done.
// - Off screen, or under Animations Off, every change lands before `steer()` returns and `onFrame` never runs.
namespace synth::ui {

class FilteredRowFades {
public:
    /** Called each frame of any row's fade: lay the list out again from `slot()`. */
    std::function<void()> onFrame;

    /** One fade per row, in the owner's row order. The rows must outlive this object's use of them. */
    void reset(const std::vector<juce::Component*>& rows) {
        clear();
        for (auto* row : rows) {
            fades_.push_back(std::make_unique<FadeVisibility>(std::initializer_list<juce::Component*>{row}));
            fades_.back()->onFrame = [this] {
                if (onFrame)
                    onFrame();
            };
        }
        laidOut_ = false;
    }

    /** Drops every fade; call before the rows are destroyed. */
    void clear() { fades_.clear(); }

    /** Marks the first filter pass as done: from now on a change fades instead of landing. */
    void markLaidOut() { laidOut_ = true; }

    /** Steers row `i` towards shown or hidden. */
    void steer(size_t i, bool shown) {
        if (laidOut_)
            fades_[i]->setShown(shown);
        else
            fades_[i]->snapTo(shown);
    }

    bool isShown(size_t i) const { return fades_[i]->isShown(); }
    bool isFading(size_t i) const { return fades_[i]->isFading(); }
    /** True while row `i` takes room in the list: shown, or still fading out. */
    bool occupies(size_t i) const { return fades_[i]->isShown() || fades_[i]->isFading(); }
    /** The height row `i` takes right now when its full height is `full`. */
    int slot(size_t i, int full) const { return juce::roundToInt((float)full * fades_[i]->progress()); }
    float progress(size_t i) const { return fades_[i]->progress(); }

    bool anyFading() const {
        for (const auto& fade : fades_)
            if (fade->isFading())
                return true;
        return false;
    }

private:
    std::vector<std::unique_ptr<FadeVisibility>> fades_;
    bool laidOut_ = false;
};

} // namespace synth::ui
