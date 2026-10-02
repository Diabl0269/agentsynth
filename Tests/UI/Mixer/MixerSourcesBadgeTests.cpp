// MixerSourcesBadgeTests.cpp (docs/mixer/panel.md#the-sources-badge): what plays into a channel is a small badge in the
// column header, not a row under it. There is no blank row on any column; the badge shows only on a channel with a
// source that differs from its name; hovering it, its screen-reader title and its description list every source; it
// is a Tab stop that acts on Return. A real off-screen MainComponent and its mixer panel.
#include "MixerHeaderTestRig.h"
#include <gtest/gtest.h>

namespace {

using mixer_header_test::HeaderRig;
using synth::ui::MixerColumnComponent;
using synth::ui::MixerSection;

// Tracks 0 and 2 now feed one strip, so that strip's source differs from its own name.
void shareStripBetweenFirstAndLastTrack(HeaderRig& rig) {
    rig.mc.simulateAddAudioTrackClick();
    auto& doc = rig.doc();
    const auto& tracks = doc.getTracks();
    ASSERT_GE(tracks.size(), 3u);
    doc.setTrackBinding(tracks[2].id, tracks[0].bindingUuid);
    rig.panel().rebuild();
}

} // namespace

TEST(MixerSourcesBadgeTest, NoColumnReservesARowUnderItsHeaderWhateverItsSources) {
    HeaderRig rig;
    shareStripBetweenFirstAndLastTrack(rig);

    int checked = 0;
    for (int i = 0; rig.panel().getStripColumnForTest(i) != nullptr; ++i) {
        auto& column = *rig.panel().getStripColumnForTest(i);
        EXPECT_EQ(column.getHeaderForTest().getBottom(), column.getSectionViewportForTest(MixerSection::Inserts).getY())
            << "the first section starts right under the header, column " << i;
        EXPECT_EQ(column.getFaderForTest().getY(), rig.firstColumn().getFaderForTest().getY())
            << "faders stay level without any reserved row, column " << i;
        ++checked;
    }
    EXPECT_GE(checked, 3);
    ASSERT_NE(rig.panel().getMasterColumnForTest(), nullptr);
    EXPECT_EQ(rig.panel().getMasterColumnForTest()->getFaderForTest().getY(),
              rig.firstColumn().getFaderForTest().getY());
}

TEST(MixerSourcesBadgeTest, ASharedSourceShowsABadgeListingEveryTrackInItsTooltipTitleAndDescription) {
    HeaderRig rig;
    shareStripBetweenFirstAndLastTrack(rig);
    const auto first = rig.doc().getTracks()[0].name;
    const auto last = rig.doc().getTracks()[2].name;

    auto& column = rig.firstColumn();
    ASSERT_TRUE(column.hasSources());
    auto& badge = column.getHeaderForTest().getSourcesButtonForTest();
    ASSERT_TRUE(badge.isVisible());
    const auto words = "Plays into this channel: " + first + ", " + last;
    EXPECT_EQ(badge.getTooltip(), words);
    EXPECT_EQ(badge.getTitle(), words);
    EXPECT_EQ(badge.getDescription(), words);
    EXPECT_EQ(column.getHeaderForTest().getSources(), first + ", " + last);

    EXPECT_FALSE(badge.getBounds().isEmpty());
    EXPECT_TRUE(column.getHeaderForTest().getLocalBounds().contains(badge.getBounds()));
}

TEST(MixerSourcesBadgeTest, ABusListsItsFeedingStripsAndALinkedChannelNamedAfterItsTrackHasNoBadge) {
    HeaderRig rig;
    auto* bus = rig.busColumn();
    ASSERT_NE(bus, nullptr);
    ASSERT_TRUE(bus->hasSources());
    const auto sender = rig.firstColumn().getHeaderForTest().getDisplayName();
    EXPECT_EQ(bus->getHeaderForTest().getSourcesButtonForTest().getTooltip(), "Plays into this channel: " + sender);

    EXPECT_FALSE(rig.firstColumn().hasSources()) << "a linked channel named after its one track says nothing more";
    EXPECT_FALSE(rig.firstColumn().getHeaderForTest().getSourcesButtonForTest().isVisible());
    EXPECT_TRUE(rig.firstColumn().getHeaderForTest().getSourcesButtonForTest().getBounds().isEmpty());
}

TEST(MixerSourcesBadgeTest, TheBadgeIsAKeyboardReachableButtonThatSelectsTheChannel) {
    HeaderRig rig;
    auto& badge = rig.busColumn()->getHeaderForTest().getSourcesButtonForTest();
    EXPECT_TRUE(badge.getWantsKeyboardFocus());
    EXPECT_FALSE(badge.getTooltip().isEmpty());
    EXPECT_FALSE(badge.getTitle().isEmpty());

    int selected = 0;
    rig.busColumn()->onColumnClicked = [&selected] { ++selected; };
    EXPECT_TRUE(static_cast<juce::Component&>(badge).keyPressed(juce::KeyPress(juce::KeyPress::returnKey)))
        << "Return acts on it";
    HeaderRig::pumpMessages();
    EXPECT_EQ(selected, 1) << "activating it selects the channel, like a click on the header";

    mixer_header_test::clickThroughMouse(badge);
    EXPECT_EQ(selected, 2);
}

TEST(MixerSourcesBadgeTest, TheHeaderDescriptionAlsoListsTheSources) {
    HeaderRig rig;
    auto* bus = rig.busColumn();
    ASSERT_NE(bus, nullptr);
    EXPECT_TRUE(bus->getHeaderForTest().getDescription().contains("Plays into this channel: "))
        << bus->getHeaderForTest().getDescription();
}

TEST(MixerSourcesBadgeTest, TheNameKeepsRoomAndTheBadgeNeverOverlapsIt) {
    HeaderRig rig;
    auto& header = rig.busColumn()->getHeaderForTest();
    const auto badge = header.getSourcesButtonForTest().getBounds();
    const auto name = header.getNameLabelForTest().getBounds();
    ASSERT_FALSE(badge.isEmpty());
    EXPECT_FALSE(badge.intersects(name));
    EXPECT_GE(name.getWidth(), 40) << "even on a bus (BUS tag and sources badge) the name still has room";
}
