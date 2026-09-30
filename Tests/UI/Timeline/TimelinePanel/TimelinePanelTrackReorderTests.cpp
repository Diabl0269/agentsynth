// TimelinePanelTrackReorderTests.cpp: dragging a track header row in the timeline's track list
// -- the shared reorder behaviour: the lifted row stays under the grab point, the drop
// reorders the doc in one undo step, Esc cancels, a sub-threshold press is a click, and a rebuild
// mid-drag discards the gesture. Real hand-built mouse sequences on the real panel; an off-screen
// owner lands every glide instantly (no VBlank), so positions are asserted exactly.
#include "../../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "MainComponent/MainComponent.h"
#include "TimelinePanelTestEvents.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

namespace {

class MockProviderTRO : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockTRO"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct TrackDragRig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderTRO>()};
    synth::ui::TimelinePanelComponent* panel = nullptr;

    explicit TrackDragRig(int tracks) {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc.simulateAddAudioTrackClick();
        panel = &mc.getTimelinePanel();
        panel->setSize(1400, 700); // tall: the pointer stays clear of the autoscroll edge zones
    }

    std::vector<synth::TrackId> trackOrder() {
        std::vector<synth::TrackId> ids;
        for (const auto& track : mc.getTimelineDoc().getTracks())
            ids.push_back(track.id);
        return ids;
    }
    int rowHeight() { return panel->getTrackHeaderAt(0)->getHeight(); }
};

// Hand-built events at header-list y positions. The pressed row moves under the pointer during a
// drag (exactly as live), so the row-local position is re-derived from the list y at every event.
struct RowDrag {
    juce::Component& list;
    synth::ui::TimelineTrackHeaderComponent& row;
    int pressY;

    juce::Point<float> local(int listY) const {
        return row.getLocalPoint(&list, juce::Point<float>(3.0f, (float)listY));
    }
    void down() { row.mouseDown(makeClickEvent(row, local(pressY))); }
    void dragTo(int y) { row.mouseDrag(makeDragEvent(row, local(y), local(pressY))); }
    void up(int y) { row.mouseUp(makeClickEvent(row, local(y))); }
};

RowDrag dragOf(TrackDragRig& r, int index, int grabInRow = 10) {
    auto* row = r.panel->getTrackHeaderAt(index);
    return RowDrag{r.panel->getTrackHeaderListForTest(), *row, row->getY() + grabInRow};
}

} // namespace

TEST(TimelinePanelTrackReorderTests, DraggingPastTwoRowsReordersTheDocInOneUndoStep) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    ASSERT_EQ(original.size(), 3u);
    const int rh = r.rowHeight();
    auto drag = dragOf(r, 0);

    drag.down();
    drag.dragTo(drag.pressY + rh + 6);
    EXPECT_TRUE(r.panel->isTrackReorderActiveForTest());
    drag.dragTo(drag.pressY + 2 * rh + 20);
    EXPECT_EQ(r.trackOrder(), original) << "nothing moves in the doc until the drop";
    EXPECT_EQ(r.panel->getTrackHeaderAt(1)->getY(), 0) << "the neighbours have glided up to open the gap";
    EXPECT_EQ(r.panel->getTrackHeaderAt(2)->getY(), rh);

    drag.up(drag.pressY + 2 * rh + 20);
    const std::vector<synth::TrackId> moved{original[1], original[2], original[0]};
    EXPECT_EQ(r.trackOrder(), moved);
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(r.panel->getTrackHeaderAt(i)->getY(), i * rh) << "the rebuilt rows sit in static slots";

    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.trackOrder(), original) << "one undo step puts the tracks back";
    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.trackOrder().size(), 2u) << "the next undo step is the third track's creation, not a second move";
}

TEST(TimelinePanelTrackReorderTests, TheLiftedRowStaysUnderTheExactGrabPoint) {
    TrackDragRig r(3);
    const int rh = r.rowHeight();
    auto drag = dragOf(r, 1, 17); // grabbed 17 px below the row's top
    auto* dragged = r.panel->getTrackHeaderAt(1);
    const int pressY = drag.pressY;
    ASSERT_EQ(pressY, rh + 17);

    drag.down();
    for (int y : {pressY + 9, pressY + 21, pressY - 5}) {
        drag.dragTo(y);
        EXPECT_EQ(dragged->getY(), y - 17) << "top == pointer.y - grab offset at pointer y " << y;
    }
    drag.dragTo(pressY - 5000);
    EXPECT_EQ(dragged->getY(), 0) << "held inside the list";
    drag.up(pressY - 5000);
}

TEST(TimelinePanelTrackReorderTests, DraggingUpMovesTheTrackToTheTopSlot) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    auto drag = dragOf(r, 2);
    drag.down();
    drag.dragTo(drag.pressY - 2 * r.rowHeight() - 30);
    drag.up(drag.pressY - 2 * r.rowHeight() - 30);
    const std::vector<synth::TrackId> moved{original[2], original[0], original[1]};
    EXPECT_EQ(r.trackOrder(), moved);
}

TEST(TimelinePanelTrackReorderTests, ARowNeedsTheCentrePastTheNeighboursMidpointToTakeItsSlot) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    const int rh = r.rowHeight();
    auto drag = dragOf(r, 0, 0); // grabbed at the row's top edge: the pointer is the row's top
    drag.down();
    drag.dragTo(rh - 4); // centre just short of the neighbour's midpoint (1.5 rows)
    drag.up(rh - 4);
    EXPECT_EQ(r.trackOrder(), original) << "short of the midpoint: same order";

    auto again = dragOf(r, 0, 0);
    again.down();
    again.dragTo(rh + 6);
    again.up(rh + 6);
    EXPECT_EQ(r.trackOrder()[0], original[1]) << "past it: the neighbour moved up";
}

TEST(TimelinePanelTrackReorderTests, EscapeMidDragCancelsWithoutMovingTracksOrAddingAnUndoStep) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    const int rh = r.rowHeight();
    auto drag = dragOf(r, 0);
    auto* row = r.panel->getTrackHeaderAt(0);
    drag.down();
    drag.dragTo(drag.pressY + rh + 30);
    ASSERT_TRUE(r.panel->isTrackReorderActiveForTest());

    ASSERT_TRUE(r.panel->sendEscapeToTrackDragForTest());
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
    EXPECT_EQ(row->getY(), 0) << "the row is back in its origin slot";
    EXPECT_EQ(r.panel->getTrackHeaderAt(1)->getY(), rh) << "and its neighbours are back in theirs";
    EXPECT_FALSE(r.panel->sendEscapeToTrackDragForTest()) << "the key listener is gone once cancelled";

    drag.up(drag.pressY + rh + 30);
    EXPECT_EQ(r.trackOrder(), original);
    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.trackOrder().size(), 2u) << "the last undo step is still the third track's creation";
}

TEST(TimelinePanelTrackReorderTests, APressBelowTheThresholdIsAPlainClick) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    auto drag = dragOf(r, 1);
    drag.down();
    EXPECT_EQ(r.panel->getFocusedTrackIndexForTest(), 1) << "the press selects the row, as before";
    drag.dragTo(drag.pressY + 2);
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
    EXPECT_EQ(r.panel->getTrackHeaderAt(1)->getY(), r.rowHeight());
    drag.up(drag.pressY + 2);
    EXPECT_EQ(r.trackOrder(), original);
    EXPECT_EQ(r.panel->getFocusedTrackIndexForTest(), 1);
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
}

TEST(TimelinePanelTrackReorderTests, AnUnrelatedRebuildMidDragDiscardsTheGestureSafely) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    auto drag = dragOf(r, 0);
    drag.down();
    drag.dragTo(drag.pressY + r.rowHeight() + 30);
    ASSERT_TRUE(r.panel->isTrackReorderActiveForTest());

    r.mc.simulateAddAudioTrackClick(); // a track added mid-drag rebuilds the header column
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
    ASSERT_EQ(r.panel->getTrackHeaderCount(), 4);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(r.panel->getTrackHeaderAt(i)->getY(), i * r.rowHeight());
    EXPECT_FALSE(r.panel->sendEscapeToTrackDragForTest());
    EXPECT_EQ(r.trackOrder()[0], original[0]);
}

TEST(TimelinePanelTrackReorderTests, TheListAutoscrollsWhenTheDragIsHeldNearItsBottomEdge) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    panel.setTimelineDoc(&doc);
    for (int i = 0; i < 12; ++i)
        doc.addTrack(synth::TrackKind::Midi, "Track " + juce::String(i + 1));
    auto& viewport = panel.getTrackHeaderViewport();
    ASSERT_EQ(viewport.getViewPositionY(), 0);

    auto& list = panel.getTrackHeaderListForTest();
    auto* row = panel.getTrackHeaderAt(0);
    RowDrag drag{list, *row, 10};
    drag.down();
    const int nearBottom = viewport.getMaximumVisibleHeight() - 4;
    drag.dragTo(nearBottom); // first event crosses the threshold
    drag.dragTo(nearBottom); // the next one, now dragging, scrolls
    EXPECT_GT(viewport.getViewPositionY(), 0);
    drag.up(nearBottom);
}

// The real hit-test target for a press on the track name is the label (and for the colour edge the
// swatch), not the row: a test that sends events to the row itself skips that. These find the
// target with getComponentAt, like a live click.
TEST(TimelinePanelTrackReorderTests, DraggingFromTheNameLabelReordersLikeAnyOtherPartOfTheRow) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    const int rh = r.rowHeight();
    auto* row = r.panel->getTrackHeaderAt(0);
    auto& list = r.panel->getTrackHeaderListForTest();
    const auto centre = row->getNameLabel().getBounds().getCentre();
    auto* target = row->getComponentAt(centre);
    ASSERT_EQ(target, &row->getNameLabel()) << "the label is what a press on the name really hits";

    auto at = [&](int listY) {
        return target->getLocalPoint(&list, juce::Point<float>((float)(row->getX() + centre.x), (float)listY));
    };
    const int pressY = row->getY() + centre.y;
    target->mouseDown(makeClickEvent(*target, at(pressY)));
    target->mouseDrag(makeDragEvent(*target, at(pressY + 2 * rh), at(pressY)));
    EXPECT_TRUE(r.panel->isTrackReorderActiveForTest());
    target->mouseUp(makeClickEvent(*target, at(pressY + 2 * rh)));

    const std::vector<synth::TrackId> moved{original[1], original[2], original[0]};
    EXPECT_EQ(r.trackOrder(), moved);
}

TEST(TimelinePanelTrackReorderTests, DraggingFromTheColourSwatchReordersToo) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    const int rh = r.rowHeight();
    auto* row = r.panel->getTrackHeaderAt(0);
    auto& list = r.panel->getTrackHeaderListForTest();
    auto* swatch = row->findChildWithID("trackColourSwatch");
    ASSERT_NE(swatch, nullptr);
    const auto centre = swatch->getBounds().getCentre();
    ASSERT_EQ(row->getComponentAt(centre), swatch);

    auto at = [&](int listY) {
        return swatch->getLocalPoint(&list, juce::Point<float>((float)(row->getX() + centre.x), (float)listY));
    };
    const int pressY = row->getY() + centre.y;
    swatch->mouseDown(makeClickEvent(*swatch, at(pressY)));
    swatch->mouseDrag(makeDragEvent(*swatch, at(pressY + 2 * rh), at(pressY)));
    swatch->mouseUp(makeClickEvent(*swatch, at(pressY + 2 * rh)));

    const std::vector<synth::TrackId> moved{original[1], original[2], original[0]};
    EXPECT_EQ(r.trackOrder(), moved);
}

TEST(TimelinePanelTrackReorderTests, ADoubleClickOnTheNameLabelStillStartsARename) {
    TrackDragRig r(3);
    const auto original = r.trackOrder();
    auto* row = r.panel->getTrackHeaderAt(1);
    auto& label = row->getNameLabel();
    juce::Component& target = label; // Component's mouse entry points are public; Label's are protected
    ASSERT_FALSE(label.isBeingEdited());
    const juce::Point<float> p(4.0f, 4.0f);
    target.mouseDown(makeClickEvent(label, p));
    target.mouseUp(makeClickEvent(label, p));
    target.mouseDown(makeClickEvent(label, p));
    target.mouseDoubleClick(makeClickEvent(label, p));
    target.mouseUp(makeClickEvent(label, p));
    EXPECT_TRUE(label.isBeingEdited());
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
    EXPECT_EQ(r.trackOrder(), original);
    EXPECT_EQ(r.panel->getFocusedTrackIndexForTest(), 1) << "the click still selects the row";
}
