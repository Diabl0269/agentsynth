#pragma once

#include "UI/Layout/ExitEnterTimeline.h"
#include <juce_gui_basics/juce_gui_basics.h>

// ExitEnterRowSlots.h (docs/layout/animation.md "Delete and undo animation"): the per-row state and the math a list
// of rows shares when it animates a removal and its undo with ExitEnterTimeline. A row that went away (leaving)
// shrinks toward its centre and then its slot closes; a row an undo brought back (restored) has its slot opened, grows
// back and fades an outline. The list owns its rows and its layout; this only moves the numbers and places a row in
// its slot, so the mod dot's source rows and the port connections panel's rows move exactly alike.
namespace synth::ui {

/** The animated look of one row: its slot height (`current`, `from` -> `to`) and how it is drawn inside the slot. A
 *  list's entry type derives from this. */
struct ExitEnterSlot {
    float from = 0.0f;
    float to = 0.0f;
    float current = 0.0f;
    bool leaving = false;
    bool restored = false; // brought back by an undo or redo: makes room, grows, gets an outline
    float scale = 1.0f;    // a shrink or grow (a fade under Reduce Motion)
    float alpha = 1.0f;
    float outline = 0.0f;
};

namespace exit_enter_slots {

/** Starts a removal / restore motion: slots begin and end at `full` (a leaving row ends at 0, a restored one starts at
 * 0). */
template <class Entries>
void begin(Entries& entries, float full) {
    for (auto& e : entries) {
        e.from = e.leaving ? full : (e.restored ? 0.0f : full);
        e.to = e.leaving ? 0.0f : full;
        e.current = e.from;
    }
}

/** The phases the entries need: an exit when a row leaves, a grow and outline when one is restored, always the gap. */
template <class Entries>
ExitEnterTimeline timelineFor(const Entries& entries) {
    ExitEnterTimeline timeline;
    for (const auto& e : entries) {
        timeline.hasExit = timeline.hasExit || e.leaving;
        timeline.hasEnter = timeline.hasEnter || e.restored;
    }
    timeline.hasGap = true;
    return timeline;
}

/** Where the motion stands `elapsedMs` after it began. */
template <class Entries>
void apply(Entries& entries, const ExitEnterTimeline& timeline, double elapsedMs, bool reducedMotion) {
    const auto frame = timeline.at(elapsedMs);
    for (auto& e : entries) {
        if (!e.leaving && !e.restored)
            continue;
        e.current = e.from + (e.to - e.from) * frame.gap;
        if (e.leaving) {
            e.scale = ExitEnterTimeline::ghostScale(frame.exit, true, reducedMotion);
            e.alpha = ExitEnterTimeline::ghostAlpha(frame.exit, true, reducedMotion);
        } else {
            e.scale = ExitEnterTimeline::ghostScale(frame.grow, false, reducedMotion);
            e.alpha = ExitEnterTimeline::ghostAlpha(frame.grow, false, reducedMotion);
            e.outline = frame.grow >= 1.0f ? 1.0f - frame.outline : 0.0f;
        }
    }
}

/** Every slot at its final place and look (the caller then drops the leaving rows). */
template <class Entries>
void land(Entries& entries, float full) {
    for (auto& e : entries) {
        e.current = e.leaving ? 0.0f : full;
        e.restored = false;
        e.scale = e.alpha = 1.0f;
        e.outline = 0.0f;
    }
}

/** Puts `row` in its slot at `y` and gives it the slot's look; returns the slot's height. */
inline int place(juce::Component& row, const ExitEnterSlot& slot, int y, int width) {
    const int h = juce::roundToInt(slot.current);
    row.setBounds(0, y, width, h);
    row.setVisible(h > 0 && slot.scale > 0.0f && slot.alpha > 0.0f);
    row.setAlpha(slot.alpha);
    if (slot.scale < 1.0f) {
        const auto c = row.getBounds().toFloat().getCentre();
        row.setTransform(juce::AffineTransform::scale(slot.scale, slot.scale, c.x, c.y));
    } else {
        row.setTransform({});
    }
    return h;
}

} // namespace exit_enter_slots

} // namespace synth::ui
