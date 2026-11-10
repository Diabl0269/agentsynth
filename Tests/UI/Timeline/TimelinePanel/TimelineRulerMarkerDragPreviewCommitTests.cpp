// TimelineRulerMarkerDragPreviewCommitTests.cpp -- a marker drag with snap on: the beat the flag is drawn at after the
// LAST drag event is the beat the release writes to the timeline.
#include "../../Layout/ReorderDragPreviewCommit.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanelTestEvents.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/TimelineRulerComponent.h"
#include <gtest/gtest.h>

namespace {

using namespace reorder_preview_commit;
using Snap = synth::ui::TimelineViewState::Snap;
constexpr double kPixelsPerBeat = 40.0;

struct MarkerDragRig {
    synth::ui::TimelineViewState state;
    synth::TransportService transport;
    synth::TimelineDoc doc;
    AppUndoManager undo;
    synth::ui::TimelineRulerComponent ruler{state};
    synth::MarkerId marker;

    explicit MarkerDragRig(Snap snap) {
        state.pixelsPerBeat = kPixelsPerBeat;
        state.firstVisibleBeat = 0.0;
        state.snap = snap;
        transport.prepare(48000.0, 512);
        ruler.setTransport(&transport);
        ruler.setTimelineDoc(&doc);
        ruler.setUndoManager(&undo);
        ruler.setSize(800, 24);
        marker = doc.addMarker(2.0, "Intro", 0xff000000);
    }

    juce::Point<float> flagCentre() const {
        for (const auto& flag : ruler.buildMarkerFlags())
            if (flag.id == marker)
                return flag.bounds.getCentre();
        return {-1.0f, -1.0f};
    }

    /** The beat the flag is drawn at, from its left edge. */
    double drawnBeat() const {
        for (const auto& flag : ruler.buildMarkerFlags())
            if (flag.id == marker)
                return state.firstVisibleBeat + (double)flag.bounds.getX() / kPixelsPerBeat;
        return -1.0;
    }

    /** Drags the flag by `dx` pixels along a path of several drag events and returns what it showed and wrote. */
    Outcome<double> drag(float dx) {
        const auto press = flagCentre();
        const auto up = press + juce::Point<float>(dx, 0.0f);
        Probe<double> probe;
        probe.send = [&](Phase phase, juce::Point<float> p) {
            if (phase == Phase::Down)
                ruler.mouseDown(makeClickEvent(ruler, p));
            else if (phase == Phase::Drag)
                ruler.mouseDrag(makeDragEvent(ruler, p, press));
            else
                ruler.mouseUp(makeDragEvent(ruler, p, press));
        };
        probe.shown = [&] { return std::vector<double>{drawnBeat()}; };
        probe.committed = [&] { return std::vector<double>{doc.getMarker(marker)->beat}; };
        return dragAndRelease(probe, press, straightPath(press, up));
    }
};

} // namespace

TEST(TimelineRulerMarkerDragPreviewCommit, ASnappedMarkerDropWritesTheBeatTheLastDragEventDrew) {
    for (const Snap snap : {Snap::Quarter, Snap::Eighth, Snap::Bar}) {
        for (const float dx : {163.0f, 57.0f, -41.0f}) {
            MarkerDragRig rig(snap);
            const auto before = std::vector<double>{rig.doc.getMarker(rig.marker)->beat};
            const auto outcome = rig.drag(dx);
            expectCommitsWhatItPreviewed(outcome, before);
            EXPECT_EQ(outcome.committed.front(), rig.doc.getMarker(rig.marker)->beat);
        }
    }
}
