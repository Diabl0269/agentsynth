#pragma once

#include "UI/Layout/BottomDockTabModel.h"
#include "UI/Layout/ExitEnterRowSlots.h"
#include "UI/Layout/ExitEnterTimeline.h"
#include <juce_graphics/juce_graphics.h>
#include <vector>

// BottomDockTabMotion.h (docs/layout/animation.md#bottom-dock-tabs): where each tab of the bottom dock's strip is
// while tabs come and go. A tab that leaves (its panel opened in its own window, or went to the main area) shrinks
// toward its centre and then its slot closes; a tab that comes back opens its slot, then grows. It is the shared
// delete-and-undo timeline (ExitEnterTimeline, ExitEnterRowSlots) laid along the strip's x axis. Pure, no
// components and no clock: the dock gives it the elapsed time each frame and places its buttons from placements().
//
// The strip splits its width equally between the offered tabs, so a tab leaving also widens the ones that stay;
// both ride the gap phase, and the widths always add up to the strip's width.
namespace synth::ui {

class BottomDockTabMotion {
public:
    using Tab = DockView;

    /** Where one tab button is drawn now. */
    struct Placement {
        Tab tab = Tab::Timeline;
        juce::Rectangle<int> bounds; // the slot, in the strip's coordinates
        float scale = 1.0f;          // a shrink or grow about the slot's centre (1 under Reduce Motion)
        float alpha = 1.0f;          // a fade under Reduce Motion
        bool leaving = false;        // no longer offered: draw it, never let it be clicked
        bool visible = true;         // false once it has no size or no opacity left
    };

    /** Starts the move from the tabs offered `before` to those offered `after` (both in strip order). False, and
     *  nothing runs, when they are the same or either is empty. Reduce Motion turns the shrink and grow into a fade. */
    bool begin(const std::vector<Tab>& before, const std::vector<Tab>& after, bool reducedMotion);

    /** Puts the motion at `elapsedMs` after it began; true once that is past its end. */
    bool applyAtMs(double elapsedMs);
    /** Lands now: nothing runs any more. */
    void finish();

    bool isRunning() const noexcept { return running_; }
    double totalMs() const noexcept { return timeline_.totalMs(); }
    /** The tabs offered when the motion ends. */
    const std::vector<Tab>& getTarget() const noexcept { return after_; }

    /** One placement per tab drawn now (the staying ones, the leaving ones and the arriving ones), left to right,
     *  filling `strip` exactly. Empty while nothing runs. */
    std::vector<Placement> placements(juce::Rectangle<int> strip) const;

private:
    struct Entry : ExitEnterSlot {
        Tab tab = Tab::Timeline;
    };

    /** The tabs of both sets in one left-to-right order: a leaving tab keeps its old place, an arriving one lands
     *  after the tab that precedes it in the new order. */
    static std::vector<Tab> mergedOrder(const std::vector<Tab>& before, const std::vector<Tab>& after);

    std::vector<Entry> entries_;
    std::vector<Tab> after_;
    size_t beforeCount_ = 0;
    ExitEnterTimeline timeline_;
    float gap_ = 0.0f; // eased progress of the gap phase: 0 = the old widths, 1 = the new ones
    bool reduced_ = false;
    bool running_ = false;
};

} // namespace synth::ui
