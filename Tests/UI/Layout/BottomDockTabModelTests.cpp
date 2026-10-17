// BottomDockTabModelTests.cpp -- the bottom dock's view model on its own: parsing and migrating the
// saved tab order, the main view's round trip and load-time clamp, and the main-view swap.
#include "UI/Layout/BottomDockTabModel.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockTabModel;
using synth::ui::DockView;
using Order = std::vector<DockView>;

} // namespace

TEST(BottomDockTabModelTests, DefaultIsTheThreeTabsThenTheCanvasInMain) {
    const BottomDockTabModel model;
    EXPECT_EQ(model.getOrder(), (Order{DockView::Timeline, DockView::Mixer, DockView::MidiRemote, DockView::Canvas}));
    EXPECT_EQ(model.getMainView(), DockView::Canvas);
    EXPECT_EQ(model.getStoredMainView(), DockView::Canvas);
}

TEST(BottomDockTabModelTests, AThreeEntryOrderMigratesByAppendingTheCanvas) {
    EXPECT_EQ(BottomDockTabModel::parseOrder("mixer,timeline,midiRemote"),
              (Order{DockView::Mixer, DockView::Timeline, DockView::MidiRemote, DockView::Canvas}));
    EXPECT_EQ(BottomDockTabModel::parseOrder("midiRemote,mixer,timeline"),
              (Order{DockView::MidiRemote, DockView::Mixer, DockView::Timeline, DockView::Canvas}));
}

TEST(BottomDockTabModelTests, AValidFourEntryOrderIsUsedAsIs) {
    EXPECT_EQ(BottomDockTabModel::parseOrder("canvas,mixer,timeline,midiRemote"),
              (Order{DockView::Canvas, DockView::Mixer, DockView::Timeline, DockView::MidiRemote}));
}

// Anything that is not three non-canvas names or all four names is rejected whole. Builds before the
// canvas tab accepted exactly three names, so the four-entry order this build writes makes them fall
// back to their own default rather than half-applying it.
TEST(BottomDockTabModelTests, AMalformedOrderIsRejected) {
    for (const char* bad : {"", "timeline", "timeline,mixer", "timeline,mixer,midiRemote,canvas,timeline",
                            "timeline,timeline,mixer", "timeline,mixer,mixer,canvas", "timeline,mixer,piano",
                            "timeline,mixer,canvas", "timeline,mixer,midiRemote,piano"})
        EXPECT_FALSE(BottomDockTabModel::parseOrder(bad).has_value()) << "'" << bad << "'";
}

TEST(BottomDockTabModelTests, LoadFallsBackToTheDefaultOrderAndTheCanvas) {
    BottomDockTabModel model;
    model.load("mixer,mixer,timeline", "piano");
    EXPECT_EQ(model.getOrder(), BottomDockTabModel::defaultOrder());
    EXPECT_EQ(model.getMainView(), DockView::Canvas);
    EXPECT_EQ(model.getStoredMainView(), DockView::Canvas);
}

TEST(BottomDockTabModelTests, TheOrderAndEveryViewKeyRoundTrip) {
    const Order order{DockView::MidiRemote, DockView::Canvas, DockView::Timeline, DockView::Mixer};
    const auto saved = BottomDockTabModel::serializeOrder(order);
    EXPECT_EQ(saved, "midiRemote,canvas,timeline,mixer");
    EXPECT_EQ(BottomDockTabModel::parseOrder(saved), order);
    for (DockView v : BottomDockTabModel::defaultOrder())
        EXPECT_EQ(BottomDockTabModel::viewForKey(BottomDockTabModel::keyFor(v)), v);
}

// A main view written by a build that can swap views is kept and written back, but this build still
// shows the canvas in the main area: it cannot lay out another view there.
TEST(BottomDockTabModelTests, AStoredNonCanvasMainViewRoundTripsButIsClampedToTheCanvas) {
    static_assert(!BottomDockTabModel::kMainViewSwapEnabled, "update this test when the swap is enabled");
    BottomDockTabModel model;
    model.load("canvas,mixer,midiRemote,timeline", "timeline");
    EXPECT_EQ(model.getStoredMainView(), DockView::Timeline);
    EXPECT_EQ(model.getMainView(), DockView::Canvas);
    EXPECT_TRUE(model.isInMain(DockView::Canvas));
    EXPECT_FALSE(model.isInMain(DockView::Timeline));
    EXPECT_EQ(model.getOrder(), (Order{DockView::Canvas, DockView::Mixer, DockView::MidiRemote, DockView::Timeline}));
}

// The view coming down takes the slot of the view going up, and the swap is its own inverse.
TEST(BottomDockTabModelTests, SetMainViewSwapsTheTwoViewsSlots) {
    BottomDockTabModel model;
    ASSERT_TRUE(model.setMainView(DockView::Mixer));
    EXPECT_EQ(model.getMainView(), DockView::Mixer);
    EXPECT_EQ(model.getStoredMainView(), DockView::Mixer);
    EXPECT_EQ(model.getOrder(), (Order{DockView::Timeline, DockView::Canvas, DockView::MidiRemote, DockView::Mixer}));

    ASSERT_TRUE(model.setMainView(DockView::Timeline));
    EXPECT_EQ(model.getOrder(), (Order{DockView::Mixer, DockView::Canvas, DockView::MidiRemote, DockView::Timeline}))
        << "the mixer comes down into the timeline's slot";

    EXPECT_FALSE(model.setMainView(DockView::Timeline)) << "already in main: nothing changes";
    ASSERT_TRUE(model.setMainView(DockView::Mixer));
    ASSERT_TRUE(model.setMainView(DockView::Canvas));
    EXPECT_EQ(model.getOrder(), BottomDockTabModel::defaultOrder()) << "swapping back restores the order";
}

TEST(BottomDockTabModelTests, SwapSlotsAndSetSlotKeepEveryOtherSlot) {
    BottomDockTabModel model;
    model.swapSlots(DockView::Timeline, DockView::MidiRemote);
    EXPECT_EQ(model.getOrder(), (Order{DockView::MidiRemote, DockView::Mixer, DockView::Timeline, DockView::Canvas}));
    model.setSlot(0, DockView::Mixer);
    model.setSlot(1, DockView::MidiRemote);
    model.setSlot(9, DockView::Timeline); // out of range: ignored
    EXPECT_EQ(model.getOrder(), (Order{DockView::Mixer, DockView::MidiRemote, DockView::Timeline, DockView::Canvas}));
}
