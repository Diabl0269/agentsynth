// AutomationLanesDragMakeRoomTests.cpp -- a modulator row dragged over another lane (of its own track or another one)
// makes room there: that lane opens a gap of the row's height at the drop's slot, everything below slides down, and the
// gap the row left in its own lane closes. Esc closes every gap and the row glides back; a lane that refuses the row
// opens none; the drop keeps everything where it stands; Reduce Motion and Animations off land at once; the keyboard's
// move into another lane glides. Against the real saved project (CrossTrack); the tweens are stepped by hand.
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesCrossTrackFixture.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include <cstdlib>
#include <functional>

using namespace cross_track_test;
using synth::ui::ModulatorRow;

namespace {

// A real press on a row's title, then drags to screen ys; the release (or Esc) is left to the caller.
struct RowDrag {
    ModulatorRow& row;
    juce::Point<float> pressLocal{(float)ModulatorRow::kIndent + 30.0f, 5.0f};
    juce::Point<int> pressScreen;
    int lastY;

    explicit RowDrag(ModulatorRow& r)
        : row(r)
        , pressScreen(r.localPointToGlobal(pressLocal.toInt())) {
        lastY = pressScreen.y;
        row.mouseDown(makeClickEvent(row, pressLocal, automation_lanes_test::leftButton()));
    }
    juce::MouseEvent at(int screenY) {
        const auto local = row.getLocalPoint(nullptr, juce::Point<int>(pressScreen.x, screenY)).toFloat();
        return makeDragEvent(row, local, pressLocal, automation_lanes_test::leftButton());
    }
    void moveTo(int screenY) {
        for (int i = 1; i <= 6; ++i)
            row.mouseDrag(at(lastY + (screenY - lastY) * i / 6));
        lastY = screenY;
    }
    void release() { row.mouseUp(at(lastY)); }
};

// The KeyMove project with the added LFO's row moved to track 3's Threshold lane, every motion landed: dragging it up
// over track 1's (now empty) Ratio lane crosses two tracks.
struct MakeRoom : KeyMove {
    MakeRoom() {
        if (mc == nullptr)
            return;
        host().moveModulatorToLane(rows(0, "ratio").front(), thresholdC, 0);
        panel().refreshModulators();
        settle();
        mc->getUndoManager().clearUndoHistory();
    }
    void settle() {
        lanes().stepBlockMotionForTest(1.0f);
        lanes().stepModulatorGlideForTest(1.0f);
        synth::ui::FadeVisibility::stepAllForTest(1.0f);
    }
    ModulatorRow* lfoRow() { return lanes().modulatorRowFor(thresholdC, 0); }
    int headerY(synth::LaneId lane) { return panel().laneHeaderForTest(lane)->getY(); }
    int trackY(int index) { return panel().getTrackHeaderAt(index)->getY(); }
    // Just above the bottom of `lane`'s header: over the lane, at its first slot.
    int overFirstSlot(synth::LaneId lane) {
        auto& header = *panel().laneHeaderForTest(lane);
        return header.localPointToGlobal(juce::Point<int>(0, header.getHeight())).y - 8;
    }
    // Where every lane header and track header stands, in on-screen order.
    std::vector<int> positions() {
        return {headerY(ratioA), headerY(attackA), headerY(thresholdB), headerY(thresholdC), trackY(1), trackY(2)};
    }
};

std::vector<int> shifted(std::vector<int> ys, size_t from, int by) {
    for (size_t i = from; i < ys.size(); ++i)
        ys[i] += by;
    return ys;
}

} // namespace

// Regression test for FRO1479: a row dragged over another track's lane showed only a drop line; nothing below moved
// until the drop, and then everything jumped.
TEST(AutomationLanesDragMakeRoom, ARowOverAnotherTracksLaneOpensAGapThereAndTheLanesAndTracksBelowSlide) {
    MakeRoom s;
    ASSERT_NE(s.mc, nullptr);
    FadeAnimateGuard animate;
    auto* row = s.lfoRow();
    ASSERT_NE(row, nullptr) << s.layout();
    const int h = row->getHeight();
    const auto before = s.positions();
    const auto extrasBefore = s.lanes().extraHeights();

    RowDrag drag(*row);
    drag.moveTo(s.overFirstSlot(s.ratioA));
    EXPECT_TRUE(s.lanes().isBlockMotionAnimating()) << "the gap glides open";
    s.lanes().stepBlockMotionForTest(0.5f);
    const auto half = s.positions();
    EXPECT_GT(half[1], before[1]) << "Attack is sliding down";
    EXPECT_LT(half[1], before[1] + h) << "not there yet: it glides";

    s.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_EQ(s.positions(), shifted(before, 1, h)) << "everything below Ratio's gap stands one row lower";
    const auto extras = s.lanes().extraHeights();
    ASSERT_EQ(extras.size(), 3u);
    EXPECT_EQ(extras[0], extrasBefore[0] + h) << "track 1 makes room";
    EXPECT_EQ(extras[1], extrasBefore[1]);
    EXPECT_EQ(extras[2], extrasBefore[2] - h) << "track 3's gap closed";
    drag.release();
}

TEST(AutomationLanesDragMakeRoom, EscapeClosesTheGapAndTheRowGlidesBackToItsOwnSlot) {
    MakeRoom s;
    ASSERT_NE(s.mc, nullptr);
    FadeAnimateGuard animate;
    auto* row = s.lfoRow();
    ASSERT_NE(row, nullptr);
    const int restY = row->getY();
    const auto before = s.positions();

    RowDrag drag(*row);
    drag.moveTo(s.overFirstSlot(s.ratioA));
    s.lanes().stepBlockMotionForTest(1.0f);
    ASSERT_NE(s.positions(), before);
    ASSERT_TRUE(s.lanes().sendModulatorDragEscapeForTest());
    EXPECT_TRUE(s.lanes().isBlockMotionAnimating()) << "the gaps glide shut";
    EXPECT_TRUE(s.lanes().isModulatorGlideAnimating()) << "the row glides back";
    EXPECT_NE(s.lfoRow()->getY(), restY) << "from where it hung";
    drag.release();

    s.settle();
    EXPECT_EQ(s.positions(), before);
    EXPECT_EQ(s.lfoRow()->getY(), restY);
    EXPECT_FALSE(s.lfoKeyOn(s.thresholdC).isEmpty()) << "nothing moved\n" << s.layout();
    EXPECT_FALSE(s.mc->getUndoManager().canUndo());
}

TEST(AutomationLanesDragMakeRoom, TheDropKeepsEverythingWhereTheGapPutItAndTheRowIsWholeInItsNewLane) {
    MakeRoom s;
    ASSERT_NE(s.mc, nullptr);
    FadeAnimateGuard animate;
    auto* row = s.lfoRow();
    ASSERT_NE(row, nullptr);
    const int h = row->getHeight();
    const auto before = s.positions();

    RowDrag drag(*row);
    drag.moveTo(s.overFirstSlot(s.ratioA));
    s.lanes().stepBlockMotionForTest(1.0f);
    drag.release();
    ASSERT_FALSE(s.lfoKeyOn(s.ratioA).isEmpty()) << s.layout();
    EXPECT_EQ(s.positions(), shifted(before, 1, h)) << "no jump on the drop";
    EXPECT_EQ(s.lanes().modulatorRowHeightFor(s.ratioA, 0), h) << "it takes the whole gap at once";

    s.settle();
    EXPECT_EQ(s.positions(), shifted(before, 1, h));
}

TEST(AutomationLanesDragMakeRoom, UnderReduceMotionAndWithAnimationsOffTheGapOpensAndClosesAtOnce) {
    for (const auto mode : {synth::ui::AnimationMode::reduced, synth::ui::AnimationMode::off}) {
        MakeRoom s;
        ASSERT_NE(s.mc, nullptr);
        FadeAnimateGuard animate(mode);
        auto* row = s.lfoRow();
        ASSERT_NE(row, nullptr);
        const int h = row->getHeight();
        const auto before = s.positions();

        RowDrag drag(*row);
        drag.moveTo(s.overFirstSlot(s.ratioA));
        EXPECT_FALSE(s.lanes().isBlockMotionAnimating()) << (int)mode;
        EXPECT_EQ(s.positions(), shifted(before, 1, h)) << (int)mode;
        ASSERT_TRUE(s.lanes().sendModulatorDragEscapeForTest());
        EXPECT_FALSE(s.lanes().isModulatorGlideAnimating()) << (int)mode;
        EXPECT_EQ(s.positions(), before) << (int)mode;
        drag.release();
    }
}

TEST(AutomationLanesDragMakeRoom, ALaneThatRefusesTheRowOpensNoGap) {
    MakeRoom s;
    ASSERT_NE(s.mc, nullptr);
    FadeAnimateGuard animate;
    auto* comp = nodeByUuid(*s.mc, s.compressor[0]);
    auto* lfo = nodeByUuid(*s.mc, s.lfoUuid);
    auto& editor = s.mc->getGraphEditor();
    editor.connectModulationSource(lfo->nodeID, 0, comp->nodeID, editor.modulationChannelFor(comp->nodeID, "attack"),
                                   0.5f);
    s.panel().refreshModulators();
    s.settle();
    ASSERT_FALSE(s.lfoKeyOn(s.attackA).isEmpty()) << s.layout();
    auto* row = s.lfoRow();
    ASSERT_NE(row, nullptr);
    const auto before = s.positions();
    const auto extrasBefore = s.lanes().extraHeights();

    RowDrag drag(*row);
    drag.moveTo(s.overFirstSlot(s.attackA)); // Attack already has this LFO
    s.lanes().stepBlockMotionForTest(1.0f);
    EXPECT_EQ(s.positions(), before);
    EXPECT_EQ(s.lanes().extraHeights(), extrasBefore);
    drag.release();
}

// Regression test for FRO1479: the keyboard's move into another lane put the row there at once, without the glide a
// move inside its own lane has.
TEST(AutomationLanesDragMakeRoom, TheKeyboardsMoveIntoAnotherLaneGlidesTheRowFromWhereItStood) {
    MakeRoom s;
    ASSERT_NE(s.mc, nullptr);
    FadeAnimateGuard animate;
    auto* row = s.lfoRow();
    ASSERT_NE(row, nullptr);
    const int fromY = row->getY();

    ASSERT_TRUE(s.lanes().moveModulatorBy(s.thresholdC, s.lfoKeyOn(s.thresholdC), -1)) << s.layout();
    auto* moved = s.lanes().modulatorRowFor(s.thresholdB, s.lanes().modulatorCount(s.thresholdB) - 1);
    ASSERT_NE(moved, nullptr) << s.layout();
    ASSERT_EQ(moved->getInfo().sourceUuid, s.lfoUuid);
    EXPECT_TRUE(s.lanes().isModulatorGlideAnimating());
    EXPECT_EQ(moved->getY(), fromY) << "it starts where it stood";
    EXPECT_GT(moved->getHeight(), 0) << "drawn whole while it travels";

    s.settle();
    EXPECT_LT(moved->getY(), fromY) << "and lands in the lane above";
    EXPECT_FALSE(s.lanes().isModulatorGlideAnimating());
}

// ---- DragMakeRoomSurfaces: every drag that can land on more than one target, mid-drag ----
//
// Each row is a drag that can drop in more than one place. Mid-drag (before the release), the neighbour it passes or
// lands before stands one item's height from where it stood, so the room the drop needs is there before the drop. A
// new multi-target drag (another lane kind, another list that reorders across owners) is one more row here; a surface
// that shifts nothing until its drop fails by name.
namespace {

struct MidDragShift {
    int shift = 0; // how far the neighbour moved, px (sign ignored)
    int item = 0;  // the dragged item's height, px
};

struct MakeRoomSurface {
    const char* name;
    std::function<MidDragShift(MakeRoom&)> run;
};

// A press at the middle of a lane header and drags through its own mouse handlers; release is the caller's.
struct LaneHeaderDrag {
    synth::ui::AutomationLaneHeaderComponent& header;
    juce::Point<float> pressLocal;
    juce::Point<int> pressScreen;
    explicit LaneHeaderDrag(synth::ui::AutomationLaneHeaderComponent& h)
        : header(h)
        , pressLocal(40.0f, (float)h.getHeight() * 0.5f)
        , pressScreen(h.localPointToGlobal(pressLocal.toInt())) {
        header.mouseDown(makeClickEvent(header, pressLocal, automation_lanes_test::leftButton()));
    }
    juce::MouseEvent at(int y) {
        const auto local = header.getLocalPoint(nullptr, juce::Point<int>(pressScreen.x, y)).toFloat();
        return makeDragEvent(header, local, pressLocal, automation_lanes_test::leftButton());
    }
    void moveBy(int dy) {
        for (int i = 1; i <= 6; ++i)
            header.mouseDrag(at(pressScreen.y + dy * i / 6));
    }
    void release(int dy) { header.mouseUp(at(pressScreen.y + dy)); }
};

const std::vector<MakeRoomSurface>& makeRoomSurfaces() {
    static const std::vector<MakeRoomSurface> surfaces = {
        {"mod row, own lane",
         [](MakeRoom& s) {
             s.host().addNewModulator(s.compressor[2], "threshold", "LFO", 0);
             s.panel().refreshModulators();
             s.settle();
             auto* first = s.lanes().modulatorRowFor(s.thresholdC, 0);
             auto* second = s.lanes().modulatorRowFor(s.thresholdC, 1);
             const int h = first->getHeight();
             const int before = second->getY();
             RowDrag drag(*first);
             drag.moveTo(drag.pressScreen.y + h * 3 / 2);
             s.settle();
             const int after = second->getY();
             drag.release();
             return MidDragShift{after - before, h};
         }},
        {"mod row, another lane of its track",
         [](MakeRoom& s) {
             s.host().moveModulatorToLane(s.rows(2, "threshold").front(), s.ratioA, 0);
             s.panel().refreshModulators();
             s.settle();
             auto* row = s.lanes().modulatorRowFor(s.ratioA, 0);
             const int h = row->getHeight();
             const int before = s.headerY(s.attackA);
             RowDrag drag(*row);
             drag.moveTo(s.overFirstSlot(s.attackA));
             s.settle();
             const int after = s.headerY(s.attackA); // its own lane's gap closed above Attack
             drag.release();
             return MidDragShift{after - before, h};
         }},
        {"mod row, another track",
         [](MakeRoom& s) {
             auto* row = s.lfoRow();
             const int h = row->getHeight();
             const int before = s.headerY(s.thresholdB);
             RowDrag drag(*row);
             drag.moveTo(s.overFirstSlot(s.ratioA));
             s.settle();
             const int after = s.headerY(s.thresholdB);
             drag.release();
             return MidDragShift{after - before, h};
         }},
        {"lane header, its own track",
         [](MakeRoom& s) {
             const int item = s.headerY(s.attackA) - s.headerY(s.ratioA);
             const int before = s.headerY(s.attackA);
             LaneHeaderDrag drag(*s.panel().laneHeaderForTest(s.ratioA));
             drag.moveBy(item * 3 / 2);
             s.lanes().stepLaneGlideForTest();
             const int after = s.headerY(s.attackA);
             drag.release(item * 3 / 2);
             return MidDragShift{after - before, item};
         }},
    };
    return surfaces;
}

} // namespace

TEST(DragMakeRoomSurfaces, EveryMultiTargetDragShiftsItsNeighbourByTheItemsHeightBeforeTheDrop) {
    for (const auto& surface : makeRoomSurfaces()) {
        MakeRoom s;
        ASSERT_NE(s.mc, nullptr);
        FadeAnimateGuard animate;
        const auto got = surface.run(s);
        EXPECT_GT(got.item, 0) << surface.name;
        EXPECT_EQ(std::abs(got.shift), got.item) << surface.name << ": the neighbour did not make room mid-drag";
    }
}
