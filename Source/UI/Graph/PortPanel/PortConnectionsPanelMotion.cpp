// PortConnectionsPanelMotion.cpp -- the panel's row motion: a removed row shrinks away and the rows below close the
// gap; a row an undo brings back is made room for, grows and gets a fading outline (the shared ExitEnterTimeline, one
// phase after another). The "Disconnect all" footer opens and closes its slot with the gap phase. Everything lands at
// once when the panel is not on screen. docs/layout/animation.md#delete-and-undo-animation.

#include "PortConnectionsPanel.h"

#include "UI/Layout/ReducedMotion.h"
#include <algorithm>

namespace synth::ui {

void PortConnectionsPanel::animateLayout() {
    const bool changing =
        footer_.current != footer_.to ||
        std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.leaving || e.restored; });
    if (changing)
        startRemovalMotion();
    else
        layoutRows();
}

void PortConnectionsPanel::startRemovalMotion() {
    exit_enter_slots::begin(entries_, (float)PortConnectionRow::kHeight);
    for (auto& e : entries_) {
        if (e.leaving) {
            e.row->setEnabled(false); // a row on its way out is not a control any more
            e.row->setInterceptsMouseClicks(false, false);
        }
    }
    footer_.from = footer_.current;
    if (!(isShowing() || forceAnimate_)) {
        landMotion();
        return;
    }
    reducedMotion_ = prefersReducedMotion();
    timeline_ = exit_enter_slots::timelineFor(entries_);
    motionActive_ = true;
    applyTimelineAtMs(0.0);
    const double total = timeline_.totalMs();
    anim_.start(
        updater_, total, [](float t) { return t; }, [this, total](float t) { applyTimelineAtMs((double)t * total); },
        [this] { landMotion(); });
}

void PortConnectionsPanel::applyTimelineAtMs(double elapsedMs) {
    if (!motionActive_)
        return;
    exit_enter_slots::apply(entries_, timeline_, elapsedMs, reducedMotion_);
    footer_.current = footer_.from + (footer_.to - footer_.from) * timeline_.at(elapsedMs).gap;
    layoutRows();
    repaint();
}

// Every row at its final place and look: the rows that left are gone, the ones that came back are ordinary rows.
void PortConnectionsPanel::landMotion() {
    const bool wasActive =
        motionActive_ || footer_.current != footer_.to ||
        std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.leaving || e.restored; });
    if (!wasActive)
        return;
    motionActive_ = false;
    exit_enter_slots::land(entries_, (float)PortConnectionRow::kHeight);
    footer_.current = footer_.to;
    finishLeaving();
    layoutRows();
    repaint();
}

void PortConnectionsPanel::finishLeaving() {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.leaving; }),
                   entries_.end());
}

} // namespace synth::ui
