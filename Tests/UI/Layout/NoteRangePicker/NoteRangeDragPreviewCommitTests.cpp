// NoteRangeDragPreviewCommitTests.cpp -- a range drag on the note range picker (an edge, the band, with and without a
// scale): the notes the band is drawn over after the LAST drag event are the range the release hands to onGestureEnd.
#include "../ReorderDragPreviewCommit.h"
#include "Music/MusicalScale.h"
#include "UI/Layout/NoteRangePicker/NoteRangePicker.h"
#include <gtest/gtest.h>

namespace {

using namespace reorder_preview_commit;
using synth::ui::NoteRangePicker;
using Range = std::vector<int>; // {low, high}

struct RangeDragRig {
    NoteRangePicker picker;
    Range gestureEnd;
    int gestureEnds = 0;

    RangeDragRig() {
        picker.setSize(280, 34);
        picker.setRange(48, 60);
        picker.onGestureEnd = [this](int lo, int hi) {
            gestureEnd = {lo, hi};
            ++gestureEnds;
        };
    }

    /** The notes whose keys sit under the drawn band, as the band is drawn once its glide has landed. */
    Range drawnRange() {
        picker.finishMotionForTest();
        const auto band = picker.bandBounds().toFloat();
        Range range;
        for (int pitch = 0; pitch < 128; ++pitch) {
            const float x = picker.keyCentreX(pitch);
            if (x > band.getX() + 0.5f && x < band.getRight() - 0.5f)
                range.push_back(pitch);
        }
        return range.empty() ? Range{-1, -1} : Range{range.front(), range.back()};
    }

    juce::MouseEvent eventAt(float x) {
        const juce::Point<float> p(x, 17.0f);
        const auto mods = juce::ModifierKeys::leftButtonModifier;
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, &picker, &picker, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(),
                                1, false);
    }

    float lowEdgeX() { return picker.keyCentreX(picker.getLow()) - picker.whiteKeyWidth() * 0.5f; }
    float highEdgeX() { return picker.keyCentreX(picker.getHigh()) + picker.whiteKeyWidth() * 0.5f; }

    void dragFrom(float fromX, float toX) {
        gestureEnds = 0;
        const Range before{picker.getLow(), picker.getHigh()};
        Probe<int> probe;
        probe.send = [&](Phase phase, juce::Point<float> p) {
            if (phase == Phase::Down)
                picker.mouseDown(eventAt(p.x));
            else if (phase == Phase::Drag)
                picker.mouseDrag(eventAt(p.x));
            else
                picker.mouseUp(eventAt(p.x));
        };
        probe.shown = [&] { return drawnRange(); };
        probe.committed = [&] { return gestureEnd; };
        const auto outcome = dragAndRelease(probe, {fromX, 17.0f}, straightPath({fromX, 17.0f}, {toX, 17.0f}));
        expectCommitsWhatItPreviewed(outcome, before);
        EXPECT_EQ(gestureEnds, 1) << "one gesture end for the whole drag";
        EXPECT_EQ(outcome.committed, (Range{picker.getLow(), picker.getHigh()}));
    }
};

} // namespace

TEST(NoteRangeDragPreviewCommit, AnEdgeDragCommitsTheRangeTheLastDragEventDrew) {
    RangeDragRig rig;
    rig.dragFrom(rig.highEdgeX() - 1.0f, rig.highEdgeX() + rig.picker.whiteKeyWidth() * 3.4f);
    rig.dragFrom(rig.lowEdgeX() + 1.0f, rig.lowEdgeX() - rig.picker.whiteKeyWidth() * 2.6f);
}

TEST(NoteRangeDragPreviewCommit, AnEdgeDragWithAScaleCommitsTheScaleNotesTheLastDragEventDrew) {
    RangeDragRig rig;
    const auto cMajor = synth::makeScale(0, 0);
    rig.picker.setScale(&cMajor);
    rig.dragFrom(rig.highEdgeX() - 1.0f, rig.highEdgeX() + rig.picker.whiteKeyWidth() * 2.7f);
    rig.dragFrom(rig.lowEdgeX() + 1.0f, rig.lowEdgeX() - rig.picker.whiteKeyWidth() * 1.8f);
}

TEST(NoteRangeDragPreviewCommit, ABandDragCommitsTheRangeTheLastDragEventDrew) {
    RangeDragRig rig;
    const float mid = (rig.lowEdgeX() + rig.highEdgeX()) * 0.5f;
    rig.dragFrom(mid, mid + rig.picker.whiteKeyWidth() * 4.3f);
    const auto cMajor = synth::makeScale(0, 0);
    rig.picker.setScale(&cMajor);
    const float again = (rig.lowEdgeX() + rig.highEdgeX()) * 0.5f;
    rig.dragFrom(again, again - rig.picker.whiteKeyWidth() * 3.1f);
}
