// TimelinePanelTrackReorderUndoGlideTests.cpp: undo and redo of a track reorder glide the header rows between
// their two orders instead of jumping (docs/layout/animation.md "Undo and redo glide"). The panel is off-screen, so
// the glide is forced on through a test seam; the real AppUndoManager::undo()/redo() drives it, and the frames the
// VBlank pump would run are advanced by hand after the tween's real duration has passed.
#include "../../Layout/BottomDockActiveTabResetGuard.h"
#include "AppUndoManager.h"
#include "MainComponent/MainComponent.h"
#include "TimelinePanelTestFixture.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

namespace {

struct UndoGlideRig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderTL>()};
    synth::ui::TimelinePanelComponent* panel = nullptr;
    std::vector<synth::TrackId> original;
    int rowHeight = 0;

    UndoGlideRig() {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        for (int i = 0; i < 3; ++i)
            mc.simulateAddAudioTrackClick();
        panel = &mc.getTimelinePanel();
        panel->setSize(1400, 700);
        original = order();
        rowHeight = panel->getTrackHeaderAt(0)->getHeight();
        // The reorder a drop commits: one timeline undo step, first track to the bottom.
        auto& doc = mc.getTimelineDoc();
        mc.getUndoManager().recordTimelineChange(doc, [&] { doc.moveTrack(original[0], 2); });
        panel->forceTrackGlideForTest(true);
    }

    std::vector<synth::TrackId> order() {
        std::vector<synth::TrackId> ids;
        for (const auto& track : mc.getTimelineDoc().getTracks())
            ids.push_back(track.id);
        return ids;
    }
    int yOf(synth::TrackId id) {
        for (int i = 0; i < panel->getTrackHeaderCount(); ++i)
            if (panel->getTrackHeaderAt(i)->getTrackId() == id)
                return panel->getTrackHeaderAt(i)->getY();
        return -1;
    }
    void finishGlide() {
        juce::Thread::sleep(250); // past kSettleMs: the tween is evaluated from the clock
        panel->advanceTrackGlideForTest();
    }
};

} // namespace

TEST(TimelinePanelTrackReorderUndoGlide, UndoAndRedoGlideTheRowsInsteadOfJumping) {
    UndoGlideRig r;
    const int rh = r.rowHeight;
    ASSERT_NE(r.order(), r.original);

    ASSERT_TRUE(r.mc.getUndoManager().undo());
    ASSERT_EQ(r.order(), r.original);
    EXPECT_TRUE(r.panel->isTrackReorderActiveForTest()) << "a glide is running";
    // The tracks changed places in the doc at once; the rows are still drawn where they were.
    EXPECT_NE(r.yOf(r.original[0]), 0) << "the track that returns to the top has not jumped there";
    EXPECT_GT(r.yOf(r.original[0]), 0);
    EXPECT_LT(r.yOf(r.original[1]), rh) << "its neighbours start from their moved-up places";
    r.finishGlide();
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(r.yOf(r.original[(size_t)i]), i * rh) << "settled in the restored order";

    ASSERT_TRUE(r.mc.getUndoManager().redo());
    EXPECT_TRUE(r.panel->isTrackReorderActiveForTest());
    EXPECT_NE(r.yOf(r.original[0]), 2 * rh) << "redo glides the track down instead of jumping";
    EXPECT_LT(r.yOf(r.original[0]), 2 * rh);
    r.finishGlide();
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
    EXPECT_EQ(r.yOf(r.original[0]), 2 * rh);
}

TEST(TimelinePanelTrackReorderUndoGlide, ARebuildThatIsNotAnUndoLandsAtOnce) {
    UndoGlideRig r;
    r.mc.getTimelineDoc().moveTrack(r.original[0], 0); // a plain doc change (menu / load style), no undo in flight
    EXPECT_FALSE(r.panel->isTrackReorderActiveForTest());
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(r.panel->getTrackHeaderAt(i)->getY(), i * r.rowHeight);
}
