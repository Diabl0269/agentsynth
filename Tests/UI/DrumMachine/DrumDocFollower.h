#pragma once

// DrumDocFollower.h -- what the timeline panel does for the drum machine in the app: every document change (an edit,
// an undo, a redo) reaches DrumMachineComponent::refreshFromDoc() from inside the change. Tests that cover motion
// across an undo or a drop attach one, since calling refreshFromDoc() by hand after the change hides the order of
// events.
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/DrumMachine/DrumMachineComponent.h"

namespace drum_test {

struct DocFollower : synth::TimelineDoc::Listener {
    DocFollower(synth::TimelineDoc& d, synth::ui::DrumMachineComponent& g)
        : doc(d)
        , grid(g) {
        doc.addListener(this);
    }
    ~DocFollower() override { doc.removeListener(this); }
    void timelineChanged(const synth::TimelineDoc&) override { grid.refreshFromDoc(); }

    synth::TimelineDoc& doc;
    synth::ui::DrumMachineComponent& grid;
};

} // namespace drum_test
