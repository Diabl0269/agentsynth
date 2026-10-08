// MixerFadeTests.cpp (docs/layout/animation.md#fading-things-in-and-out, docs/mixer/panel.md): what the mixer
// fades and slides instead of popping -- a section closing or opening (the height slides in every column, Master
// included), the Direct column, a pinned zone, the empty hint, the header's colour dot and sources badge, the
// "+ Send" row and the side pane's Show all. The animated path is forced off screen and stepped by hand.
#include "../Layout/FadeVisibilityTestGuard.h"
#include "MixerHeaderTestRig.h"
#include "MixerZonesTestRig.h"
#include <gtest/gtest.h>

namespace {

using mixer_header_test::HeaderRig;
using synth::ui::FadeVisibility;
using synth::ui::MixerSection;

constexpr int kSendsHeight = 60; // three rows of 20

bool inOpenInterval(int value, int a, int b) { return value > std::min(a, b) && value < std::max(a, b); }

} // namespace

TEST(MixerFadeTest, ClosingASectionSlidesEveryColumnsFaderTogetherAndCrossFadesTheStrip) {
    HeaderRig rig;
    auto& first = rig.firstColumn();
    auto* master = rig.panel().getMasterColumnForTest();
    ASSERT_NE(master, nullptr);
    const int before = first.getFaderForTest().getY();
    FadeAnimateGuard guard;

    rig.panel().getSectionLayout().setHidden(MixerSection::Sends, true);
    EXPECT_TRUE(rig.panel().getSectionLayout().isHidden(MixerSection::Sends)) << "the logical state is immediate";
    EXPECT_EQ(first.getFaderForTest().getY(), before) << "frame 0 is where it was";
    EXPECT_TRUE(first.getSectionViewportForTest(MixerSection::Sends).isVisible()) << "the rows stay while they fade";
    EXPECT_FALSE(interceptsClicks(first.getSectionViewportForTest(MixerSection::Sends)));

    FadeVisibility::stepAllForTest(0.5f);
    const int mid = first.getFaderForTest().getY();
    EXPECT_TRUE(inOpenInterval(mid, before, before - (kSendsHeight - 14))) << "halfway up, not jumped: " << mid;
    EXPECT_EQ(master->getFaderForTest().getY(), mid) << "Master slides with the strips";
    EXPECT_EQ(rig.busColumn()->getFaderForTest().getY(), mid);
    const auto& viewport = first.getSectionViewportForTest(MixerSection::Sends);
    EXPECT_LT(viewport.getAlpha(), 1.0f);
    auto& strip = first.getCollapsedSectionForTest(MixerSection::Sends);
    EXPECT_TRUE(strip.isVisible());
    EXPECT_LT(strip.getAlpha(), 1.0f);

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(first.getFaderForTest().getY(), before - (kSendsHeight - 14));
    EXPECT_EQ(master->getFaderForTest().getY(), first.getFaderForTest().getY());
    EXPECT_FALSE(viewport.isVisible());
    EXPECT_FLOAT_EQ(viewport.getAlpha(), 1.0f);
    EXPECT_TRUE(strip.isVisible());
    EXPECT_FLOAT_EQ(strip.getAlpha(), 1.0f);
    EXPECT_EQ(strip.getHeight(), synth::ui::MixerSectionLayout::kCollapsedHeight);

    rig.panel().getSectionLayout().setHidden(MixerSection::Sends, false);
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_TRUE(inOpenInterval(first.getFaderForTest().getY(), before, before - (kSendsHeight - 14)));
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(first.getFaderForTest().getY(), before) << "open again, on the same line";
    EXPECT_TRUE(viewport.isVisible());
    EXPECT_FALSE(strip.isVisible());
}

TEST(MixerFadeTest, AReversalMidFadeStartsFromTheCurrentHeight) {
    HeaderRig rig;
    auto& first = rig.firstColumn();
    const int before = first.getFaderForTest().getY();
    FadeAnimateGuard guard;

    rig.panel().getSectionLayout().setHidden(MixerSection::Sends, true);
    FadeVisibility::stepAllForTest(0.5f);
    const int mid = first.getFaderForTest().getY();
    rig.panel().getSectionLayout().setHidden(MixerSection::Sends, false);
    EXPECT_EQ(first.getFaderForTest().getY(), mid) << "no jump to either end";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(first.getFaderForTest().getY(), before);
}

TEST(MixerFadeTest, ClosingMastersInsertsSlidesItAndTheEqCurveDimsWithItsHeight) {
    HeaderRig rig;
    auto* master = rig.panel().getMasterColumnForTest();
    ASSERT_NE(master, nullptr);
    auto& first = rig.firstColumn();
    FadeAnimateGuard guard;

    rig.panel().getSectionLayout().setHidden(MixerSection::Inserts, true);
    rig.panel().getSectionLayout().setHidden(MixerSection::Eq, true);
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_TRUE(master->getInsertViewportForTest().isVisible());
    EXPECT_LT(master->getInsertViewportForTest().getAlpha(), 1.0f);
    EXPECT_GT(master->getInsertViewportForTest().getHeight(), synth::ui::MixerSectionLayout::kCollapsedHeight);
    EXPECT_TRUE(first.getEqThumbnailForTest().getAlpha() > 0.0f && first.getEqThumbnailForTest().getAlpha() < 1.0f);

    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(master->getInsertViewportForTest().isVisible());
    EXPECT_EQ(master->getInsertViewportForTest().getHeight(), synth::ui::MixerSectionLayout::kCollapsedHeight);
    EXPECT_EQ(first.getEqThumbnailForTest().getHeight(), 0);
}

TEST(MixerFadeTest, HidingTheDirectColumnFadesItOutAndShowingItFadesItIn) {
    MixerZonesRig r(1);
    auto* direct = r.panel->getDirectColumnForTest();
    ASSERT_NE(direct, nullptr);
    ASSERT_TRUE(direct->isVisible());
    FadeAnimateGuard guard;

    r.panel->setChannelHidden(synth::MixerViewDoc::kDirectId, true);
    EXPECT_TRUE(direct->isVisible()) << "on screen until the fade has ended";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_LT(direct->getAlpha(), 1.0f);
    EXPECT_GT(direct->getAlpha(), 0.0f);
    EXPECT_FALSE(interceptsClicks(*direct));
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(direct->isVisible());
    EXPECT_FLOAT_EQ(direct->getAlpha(), 1.0f);

    r.panel->setChannelHidden(synth::MixerViewDoc::kDirectId, false);
    EXPECT_TRUE(direct->isVisible());
    EXPECT_FLOAT_EQ(direct->getAlpha(), 0.0f) << "frame 0 is the first fade frame";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(direct->getAlpha(), 0.5f, 0.01f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(direct->getAlpha(), 1.0f);
    EXPECT_TRUE(interceptsClicks(*direct));
}

TEST(MixerFadeTest, PinningAColumnFadesItsZoneInAndEmptyingItFadesItOut) {
    MixerZonesRig r(2);
    auto& left = r.panel->getLeftZoneViewportForTest();
    ASSERT_FALSE(left.isVisible());
    const auto id = r.stripId(0);
    FadeAnimateGuard guard;

    r.panel->pinChannel(id, synth::MixerZone::Left);
    EXPECT_TRUE(left.isVisible());
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(left.getAlpha(), 0.5f, 0.01f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(left.getAlpha(), 1.0f);

    r.panel->pinChannel(id, synth::MixerZone::Scrolling);
    EXPECT_TRUE(left.isVisible()) << "the empty zone fades out rather than vanishing";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_LT(left.getAlpha(), 1.0f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(left.isVisible());
}

TEST(MixerFadeTest, TheEmptyHintFadesOutWhenTheFirstChannelArrives) {
    MixerZonesRig r(0);
    const auto& hint = r.panel->getEmptyHintForTest();
    ASSERT_TRUE(hint.isVisible());
    FadeAnimateGuard guard;

    r.mc.simulateAddAudioTrackClick();
    r.panel->rebuild();
    EXPECT_TRUE(hint.isVisible());
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(hint.getAlpha(), 0.5f, 0.01f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(hint.isVisible());
}

TEST(MixerFadeTest, TheHeadersColourDotAndSourcesBadgeFadeAndKeepNoClicksWhileLeaving) {
    HeaderRig rig;
    auto& header = rig.panel().getMasterColumnForTest()->getHeaderForTest();
    auto& dot = header.getColourDotForTest();
    auto& badge = header.getSourcesButtonForTest();
    ASSERT_FALSE(dot.isVisible());
    ASSERT_FALSE(badge.isVisible());
    FadeAnimateGuard guard;

    header.setColour(juce::Colours::red);
    header.setSources("Kick");
    EXPECT_TRUE(dot.isVisible());
    EXPECT_TRUE(badge.isVisible());
    EXPECT_FLOAT_EQ(badge.getAlpha(), 0.0f);
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(dot.getAlpha(), 0.5f, 0.01f);
    EXPECT_NEAR(badge.getAlpha(), 0.5f, 0.01f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(badge.getAlpha(), 1.0f);

    header.setSources({});
    header.setColour(juce::Colour());
    EXPECT_TRUE(badge.isVisible());
    EXPECT_FALSE(interceptsClicks(badge));
    EXPECT_EQ(badge.getTitle(), "") << "its words are cleared with the source";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(badge.isVisible());
    EXPECT_FALSE(dot.isVisible());
}

TEST(MixerFadeTest, PlusSendFadesOutWhenASendCannotBeAddedAnyMore) {
    HeaderRig rig;
    auto& list = rig.firstColumn().getSendList();
    auto& proxy = list.getAddSendAccessibilityComponentForTest();
    ASSERT_TRUE(proxy.isVisible());
    ASSERT_TRUE(list.canAddSend());
    FadeAnimateGuard guard;

    list.unbindFromGraph(); // nothing can be added to an unbound list
    list.resized();
    EXPECT_TRUE(proxy.isVisible()) << "the painted row is still there while it fades";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(proxy.getAlpha(), 0.5f, 0.01f);
    EXPECT_FALSE(interceptsClicks(proxy));
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(proxy.isVisible());
}

TEST(MixerFadeTest, ShowAllFadesInWithTheFirstHiddenChannelAndOutWithTheLast) {
    MixerZonesRig r(2);
    auto& showAll = r.panel->getZonesPaneForTest().getShowAllForTest();
    ASSERT_FALSE(showAll.isVisible());
    FadeAnimateGuard guard;

    r.panel->setChannelHidden(r.stripId(0), true);
    EXPECT_TRUE(showAll.isVisible());
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(showAll.getAlpha(), 0.5f, 0.01f);
    FadeVisibility::stepAllForTest(1.0f);

    r.panel->showAllChannels();
    EXPECT_TRUE(showAll.isVisible());
    EXPECT_FALSE(interceptsClicks(showAll));
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(showAll.isVisible());
}

TEST(MixerFadeTest, UnderAnimationsOffEverythingLandsAtOnce) {
    HeaderRig rig;
    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    rig.panel().getSectionLayout().setHidden(MixerSection::Sends, true);
    auto& first = rig.firstColumn();
    EXPECT_FALSE(first.getSectionViewportForTest(MixerSection::Sends).isVisible());
    EXPECT_TRUE(first.getCollapsedSectionForTest(MixerSection::Sends).isVisible());
    EXPECT_EQ(first.getCollapsedSectionForTest(MixerSection::Sends).getHeight(), 14);
    EXPECT_FLOAT_EQ(rig.panel().getSectionLayout().getShownAmount(MixerSection::Sends), 0.0f);
}
