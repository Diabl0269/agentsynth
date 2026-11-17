// SoundPackRowGlideTests.cpp -- Settings > Sound packs: a pack's row grows and shrinks over the credit's fade instead
// of stepping, and the rows below slide with it (docs/layout/animation.md, "Fading things in and out"). Headless: the
// animated path is forced with FadeAnimateGuard and stepped by hand.
#include "../../SoundPacks/SoundPackTestKit.h"
#include "../Layout/FadeVisibilityTestGuard.h"
#include "UI/Settings/SoundPacks/SoundPackRow.h"
#include "UI/Settings/SoundPacks/SoundPacksSettingsTab.h"
#include <gtest/gtest.h>

using namespace sound_pack_test;
using synth::ui::FadeVisibility;
using synth::ui::SoundPackRow;

namespace {

constexpr int kWidth = 466;

// A press and release on the middle of `component`, delivered to its own mouse callbacks as the live mouse would.
void clickThroughMouse(juce::Component& component) {
    const auto centre = component.getLocalBounds().toFloat().getCentre();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    const auto now = juce::Time::getCurrentTime();
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto event = [&](juce::ModifierKeys m) {
        return juce::MouseEvent(source, centre, m, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, now, centre,
                                now, 1, false);
    };
    component.mouseEnter(event(juce::ModifierKeys()));
    component.mouseDown(event(mods));
    component.mouseUp(event(juce::ModifierKeys()));
}

synth::soundpacks::SoundPackInstaller::Row richPack() {
    synth::soundpacks::SoundPackInstaller::Row row;
    row.id = "big";
    row.name = "Big pack";
    row.credit = juce::String::repeatedString("Recorded in a hall by someone patient. ", 6);
    row.licence = "CC BY 4.0";
    row.licenceUrl = "https://example.test/licence";
    for (int i = 1; i <= 9; ++i) {
        synth::soundpacks::IndexInstrument instrument;
        instrument.id = "i" + juce::String(i);
        instrument.name = "Instrument" + juce::String(i);
        row.instruments.push_back(instrument);
    }
    return row;
}

int fullHeight() {
    SoundPackRow row("big", {});
    row.update(richPack());
    row.setExpanded(true, false);
    return row.heightForWidth(kWidth);
}

// Several packs on one fake server, listed in the tab.
struct ManyPacks : Rig {
    SoundPacksSettingsTab tab{installer};
    explicit ManyPacks(int count, int tabHeight = 300) {
        tab.setSize(500, tabHeight);
        tab.resized();
        juce::StringArray entries;
        for (int i = 0; i < count; ++i) {
            const juce::String id = "p" + juce::String(i);
            const auto zip = goodPackZip(id, "1.0.0");
            entries.add(packEntryJson(id, "1.0.0", zip));
            server.bodies["https://example.test/" + id + "-1.0.0.zip"] = zip;
        }
        const auto json = indexJsonOf(entries);
        server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
        installer.refreshIndex();
    }
};

} // namespace

TEST(SoundPackRowGlide, TheHeightGrowsFrameByFrameFromClosedToFullAndBackExactly) {
    const int full = fullHeight();
    ASSERT_GT(full, SoundPackRow::kHeight + 40);
    SoundPackRow row("big", {});
    row.update(richPack());
    row.setSize(kWidth, SoundPackRow::kHeight);
    FadeAnimateGuard guard;

    EXPECT_EQ(row.heightForWidth(kWidth), SoundPackRow::kHeight);
    row.setExpanded(true);
    EXPECT_EQ(row.heightForWidth(kWidth), SoundPackRow::kHeight) << "frame 0 has not moved";
    int last = row.heightForWidth(kWidth);
    bool strictlyBetween = false;
    for (float t = 0.1f; t < 0.99f; t += 0.1f) {
        FadeVisibility::stepAllForTest(t);
        const int h = row.heightForWidth(kWidth);
        EXPECT_GE(h, last) << "growing never steps back (t=" << t << ")";
        strictlyBetween = strictlyBetween || (h > SoundPackRow::kHeight && h < full);
        last = h;
    }
    EXPECT_TRUE(strictlyBetween);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(row.heightForWidth(kWidth), full);
    last = full;

    row.setExpanded(false);
    EXPECT_EQ(row.heightForWidth(kWidth), full) << "frame 0 of the close has not moved";
    for (float t = 0.1f; t < 0.99f; t += 0.1f) {
        FadeVisibility::stepAllForTest(t);
        const int h = row.heightForWidth(kWidth);
        EXPECT_LE(h, last) << "shrinking never steps back (t=" << t << ")";
        last = h;
    }
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(row.heightForWidth(kWidth), SoundPackRow::kHeight);
}

TEST(SoundPackRowGlide, ReduceMotionIsAShortFadeAndAnimationsOffLandsAtOnce) {
    const int full = fullHeight();
    SoundPackRow row("big", {});
    row.update(richPack());
    {
        FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
        row.setExpanded(true);
        FadeVisibility::stepAllForTest(0.5f);
        const int mid = row.heightForWidth(kWidth);
        EXPECT_GT(mid, SoundPackRow::kHeight);
        EXPECT_LT(mid, full);
        FadeVisibility::stepAllForTest(1.0f);
        EXPECT_EQ(row.heightForWidth(kWidth), full);
    }
    FadeAnimateGuard off(synth::ui::AnimationMode::off);
    row.setExpanded(false);
    EXPECT_EQ(row.heightForWidth(kWidth), SoundPackRow::kHeight) << "Off: no frames in between";
    row.setExpanded(true);
    EXPECT_EQ(row.heightForWidth(kWidth), full);
}

TEST(SoundPackRowGlide, ARowOpenedWithoutAnimationIsFullHeightFromTheStart) {
    SoundPackRow row("big", {});
    row.update(richPack());
    row.setExpanded(true, false);
    EXPECT_EQ(row.heightForWidth(kWidth), fullHeight());
    EXPECT_TRUE(row.isCreditShown());
}

TEST(SoundPackRowGlide, TheCreditNeverPaintsOutsideTheRowWhileItGlides) {
    SoundPackRow row("big", {});
    row.update(richPack());
    juce::Component parent;
    parent.addAndMakeVisible(row);
    row.setSize(kWidth, SoundPackRow::kHeight);
    FadeAnimateGuard guard;
    row.setExpanded(true);
    row.onHeightChanged = [&] { row.setSize(kWidth, row.heightForWidth(kWidth)); };
    const int full = fullHeight();
    for (float t : {0.3f, 0.6f, 0.9f}) {
        FadeVisibility::stepAllForTest(t);
        const int h = row.getHeight();
        ASSERT_LT(h, full);
        ASSERT_GT(row.getCredit().getBottom(), h) << "the credit reaches past the row mid-glide, so the clip matters";
        parent.setSize(kWidth, full + 20);
        juce::Image image(juce::Image::ARGB, kWidth, parent.getHeight(), true);
        juce::Graphics g(image);
        parent.paintEntireComponent(g, false);
        for (int y = h; y < image.getHeight(); ++y)
            for (int x = 0; x < kWidth; ++x)
                ASSERT_EQ(image.getPixelAt(x, y).getAlpha(), 0) << "painted below the row at " << x << "," << y;
    }
}

TEST(SoundPackRowGlide, AClickOnTheArrowGlidesTheRowAndTheRowsBelowInsteadOfJumping) {
    ManyPacks rig(3);
    auto& first = rig.tab.getRowForTest(0);
    auto& second = rig.tab.getRowForTest(1);
    const int closedY = second.getY();
    const int closedContent = rig.tab.getContentHeightForTest();
    FadeAnimateGuard guard;

    clickThroughMouse(first.getDisclosure());
    ASSERT_TRUE(first.isExpanded());
    EXPECT_EQ(second.getY(), closedY) << "frame 0";
    FadeVisibility::stepAllForTest(0.5f);
    const int midY = second.getY();
    EXPECT_GT(midY, closedY);
    EXPECT_EQ(first.getHeight(), first.heightForWidth(first.getWidth()));
    EXPECT_GT(rig.tab.getContentHeightForTest(), closedContent);
    FadeVisibility::stepAllForTest(1.0f);
    const int openY = second.getY();
    EXPECT_GT(openY, midY);
    EXPECT_EQ(first.getHeight(), first.heightForWidth(first.getWidth()));

    clickThroughMouse(first.getDisclosure());
    EXPECT_FALSE(first.isExpanded());
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_LT(second.getY(), openY);
    EXPECT_GT(second.getY(), closedY);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(second.getY(), closedY);
    EXPECT_EQ(rig.tab.getContentHeightForTest(), closedContent);
}

TEST(SoundPackRowGlide, AnArrowClickUnderAnimationsOffGoesStraightToTheEnd) {
    ManyPacks rig(2);
    const int closedY = rig.tab.getRowForTest(1).getY();
    FadeAnimateGuard off(synth::ui::AnimationMode::off);
    clickThroughMouse(rig.tab.getRowForTest(0).getDisclosure());
    EXPECT_GT(rig.tab.getRowForTest(1).getY(), closedY);
    EXPECT_EQ(rig.tab.getRowForTest(0).getHeight(), rig.tab.getRowForTest(0).heightForWidth(kWidth));
}

TEST(SoundPackRowGlide, TheViewStaysPutWhenARowAboveTheViewportGlides) {
    ManyPacks rig(8, 200);
    auto& viewport = rig.tab.getViewportForTest();
    auto& watched = rig.tab.getRowForTest(5);
    viewport.setViewPosition(0, watched.getY());
    ASSERT_GT(viewport.getViewPositionY(), rig.tab.getRowForTest(0).getBottom());
    const int offset = watched.getY() - viewport.getViewPositionY();
    FadeAnimateGuard guard;

    clickThroughMouse(rig.tab.getRowForTest(0).getDisclosure());
    for (float t : {0.25f, 0.5f, 0.75f, 1.0f}) {
        FadeVisibility::stepAllForTest(t);
        EXPECT_EQ(watched.getY() - viewport.getViewPositionY(), offset) << "t=" << t;
    }
}

// ---- An open row shows no cut '...' line, and the title toggles like the chevron ----

namespace {

synth::soundpacks::SoundPackInstaller::Row sizedRichPack() {
    auto row = richPack();
    row.downloadBytes = 40 * 1024 * 1024;
    return row;
}

// What a left click (or a right click, or the end of a drag) on `part` hands the row, which listens to its name
// and line: the event the live mouse delivers to a mouse listener, with `part` as the event component.
juce::MouseEvent upOn(juce::Component& part, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier,
                      bool dragged = false) {
    const auto centre = part.getLocalBounds().toFloat().getCentre();
    const auto now = juce::Time::getCurrentTime();
    const auto down = dragged ? centre - juce::Point<float>(40.0f, 0.0f) : centre;
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &part, &part, now, down, now, 1, dragged);
}

} // namespace

TEST(SoundPackRowOpenLine, AnOpenRowShowsOnlyTheSizeWhereTheCutListWasAndTheFullListBelow) {
    SoundPackRow row("big", {});
    row.update(sizedRichPack());
    row.setSize(300, SoundPackRow::kHeight); // narrow: the closed line is cut with ...
    ASSERT_TRUE(row.getDetailTooltip().isNotEmpty()) << "the closed line is elided at this width";
    EXPECT_TRUE(row.isDetailShown());
    EXPECT_FALSE(row.isOpenDetailShown());

    row.setExpanded(true, false);
    row.setSize(300, row.heightForWidth(300));
    EXPECT_FALSE(row.getDetailLabel().isVisible()) << "no cut line while open";
    ASSERT_TRUE(row.getOpenDetailLabel().isVisible());
    EXPECT_EQ(row.getOpenDetailLabel().getText(), synth::soundpacks::formatPackSize(40 * 1024 * 1024));
    EXPECT_EQ(row.getOpenDetailLabel().getBounds(), row.getDetailBounds()) << "the size stays where the line was";
    const auto& open = row.getOpenDetailLabel();
    EXPECT_LE(juce::GlyphArrangement::getStringWidthInt(open.getFont(), open.getText()),
              open.getBorderSize().subtractedFrom(open.getLocalBounds()).getWidth())
        << "the open line fits, so it is never cut";
    EXPECT_TRUE(row.getCredit().getInstrumentsText().contains("Instrument9")) << "the full list is in the details";

    row.setExpanded(false, false);
    EXPECT_TRUE(row.getDetailLabel().isVisible());
    EXPECT_FALSE(row.getOpenDetailLabel().isVisible());
}

TEST(SoundPackRowOpenLine, TheLineCrossFadesWithTheRowGlideInEveryMotionMode) {
    for (const auto mode :
         {synth::ui::AnimationMode::full, synth::ui::AnimationMode::reduced, synth::ui::AnimationMode::off}) {
        SoundPackRow row("big", {});
        row.update(sizedRichPack());
        row.setSize(kWidth, SoundPackRow::kHeight);
        FadeAnimateGuard guard(mode);
        row.setExpanded(true);
        if (mode != synth::ui::AnimationMode::off) {
            FadeVisibility::stepAllForTest(0.5f);
            EXPECT_TRUE(row.getDetailLabel().isVisible()) << "the list fades out, it does not vanish";
            EXPECT_LT(row.getDetailLabel().getAlpha(), 1.0f);
            EXPECT_GT(row.getOpenDetailLabel().getAlpha(), 0.0f);
            EXPECT_LT(row.getOpenDetailLabel().getAlpha(), 1.0f);
            FadeVisibility::stepAllForTest(1.0f);
        }
        EXPECT_FALSE(row.getDetailLabel().isVisible()) << (int)mode;
        EXPECT_TRUE(row.getOpenDetailLabel().isVisible()) << (int)mode;
        row.setExpanded(false);
        FadeVisibility::stepAllForTest(1.0f);
        EXPECT_TRUE(row.getDetailLabel().isVisible()) << (int)mode;
        EXPECT_FALSE(row.getOpenDetailLabel().isVisible()) << (int)mode;
    }
}

TEST(SoundPackRowTitleClick, AClickOnTheNameOrTheLineOpensAndClosesTheRowLikeTheChevron) {
    SoundPackRow row("big", {});
    row.update(sizedRichPack());
    row.setSize(kWidth, SoundPackRow::kHeight);
    int changes = 0;
    row.onExpandedChanged = [&] { ++changes; };

    row.mouseUp(upOn(row.getNameLabel()));
    EXPECT_TRUE(row.isExpanded());
    EXPECT_EQ(row.getDisclosure().getChevronOpenness(), 1.0f) << "the chevron turns with it";
    row.mouseUp(upOn(row.getNameLabel()));
    EXPECT_FALSE(row.isExpanded());
    row.mouseUp(upOn(row.getDetailLabel()));
    EXPECT_TRUE(row.isExpanded());
    row.mouseUp(upOn(row.getOpenDetailLabel()));
    EXPECT_FALSE(row.isExpanded());
    EXPECT_EQ(changes, 4);
}

TEST(SoundPackRowTitleClick, ARightClickADragOrAClickOnTheStatusLineLeavesTheRowAlone) {
    SoundPackRow row("big", {});
    row.update(sizedRichPack());
    row.setSize(kWidth, SoundPackRow::kHeight);
    row.mouseUp(upOn(row.getNameLabel(), juce::ModifierKeys::rightButtonModifier));
    row.mouseUp(upOn(row.getNameLabel(), juce::ModifierKeys::leftButtonModifier, true));
    row.mouseUp(upOn(row));
    EXPECT_FALSE(row.isExpanded());
}

TEST(SoundPackRowTitleClick, TheTitleShowsAHandAndLightsTheChevronButAddsNoTabStop) {
    SoundPackRow row("big", {});
    row.update(sizedRichPack());
    for (auto* part : {&row.getNameLabel(), &row.getDetailLabel(), &row.getOpenDetailLabel()}) {
        EXPECT_TRUE(interceptsClicks(*part));
        EXPECT_EQ(part->getMouseCursor(), juce::MouseCursor(juce::MouseCursor::PointingHandCursor));
        EXPECT_FALSE(part->getWantsKeyboardFocus()) << "the chevron stays the row's one Tab stop for opening";
    }
    EXPECT_FALSE(row.getDisclosure().isLit());
    row.mouseEnter(upOn(row.getNameLabel(), {}));
    EXPECT_TRUE(row.getDisclosure().isLit()) << "hovering the title lights the arrow it stands for";
    row.mouseExit(upOn(row.getNameLabel(), {}));
    EXPECT_FALSE(row.getDisclosure().isLit());
}
