// SoundPackSourcesGlideTests.cpp -- Settings > Sound packs: "Show sources" lists the recordings inside the open row,
// fading in while the row grows and the instruments slide down, and "Hide sources" reverses it. Headless: the animated
// path is forced with FadeAnimateGuard and stepped by hand.
#include "../../SoundPacks/SoundPackTestKit.h"
#include "../Layout/FadeVisibilityTestGuard.h"
#include "UI/Settings/SoundPacks/SoundPackRow.h"
#include <gtest/gtest.h>

using synth::ui::FadeVisibility;
using synth::ui::SoundPackRow;

namespace {

constexpr int kWidth = 466;

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

struct SourcesRow {
    juce::TemporaryFile file{".md"};
    SoundPackRow row{"big", {}};
    juce::Component parent;
    explicit SourcesRow(bool withFile = true) {
        file.getFile().replaceWithText(
            "## Grand piano (`piano`)\n- Source pack: Test Library\n- URL: https://a.test/x\n"
            "- Files used (3): a.wav\n\n## Organ (`organ`)\n- Source pack: Test Library\n"
            "- Files used (1): b.wav\n\n## Harp (`harp`)\n- Source pack: Other Library\n"
            "- URL: https://b.test/y\n- Files used (5): c.wav\n");
        synth::soundpacks::SoundPackInstaller::Row r;
        r.id = "big";
        r.name = "Big pack";
        r.credit = "Recorded in a hall.";
        if (withFile)
            r.sources = file.getFile();
        for (int i = 1; i <= 3; ++i) {
            synth::soundpacks::IndexInstrument instrument;
            instrument.id = "i" + juce::String(i);
            instrument.name = "Instrument" + juce::String(i);
            r.instruments.push_back(instrument);
        }
        row.update(r);
        parent.addAndMakeVisible(row);
        row.setExpanded(true, false);
        row.setSize(kWidth, row.heightForWidth(kWidth));
        row.onHeightChanged = [this] { row.setSize(kWidth, row.heightForWidth(kWidth)); };
    }
    int height() { return row.heightForWidth(kWidth); }
    int instrumentsY() { return row.getCredit().getInstrumentsY(); }
};

} // namespace

TEST(SoundPackSourcesGlide, OpeningGrowsTheRowAndSlidesTheInstrumentsFrameByFrameAndClosingReturnsExactly) {
    SourcesRow rig;
    auto& credit = rig.row.getCredit();
    const int closedH = rig.height();
    const int closedY = rig.instrumentsY();
    FadeAnimateGuard guard;

    clickThroughMouse(credit.getSourcesLink());
    ASSERT_TRUE(credit.isSourcesOpen());
    EXPECT_EQ(credit.getSourcesLink().getButtonText(), "Hide sources");
    EXPECT_EQ(rig.height(), closedH) << "frame 0 has not moved";
    int lastH = closedH, lastY = closedY;
    bool between = false;
    for (float t = 0.1f; t < 0.99f; t += 0.1f) {
        FadeVisibility::stepAllForTest(t);
        EXPECT_GE(rig.height(), lastH) << t;
        EXPECT_GE(rig.instrumentsY(), lastY) << t;
        EXPECT_EQ(rig.row.getHeight(), rig.height()) << "the row follows each frame";
        between = between || rig.height() > closedH;
        lastH = rig.height();
        lastY = rig.instrumentsY();
    }
    EXPECT_TRUE(between);
    FadeVisibility::stepAllForTest(1.0f);
    const int openH = rig.height();
    const int openY = rig.instrumentsY();
    EXPECT_GT(openH, closedH);
    EXPECT_GT(openY, closedY);
    EXPECT_TRUE(credit.isSourcesListShown());
    EXPECT_TRUE(credit.getSourcesListText().contains("Grand piano: 3 recordings from Test Library, a.test/x"));
    EXPECT_TRUE(credit.getSourcesListText().contains("Organ: 1 recording from Test Library"));

    clickThroughMouse(credit.getSourcesLink());
    EXPECT_EQ(credit.getSourcesLink().getButtonText(), "Show sources");
    lastH = openH;
    for (float t = 0.1f; t < 0.99f; t += 0.1f) {
        FadeVisibility::stepAllForTest(t);
        EXPECT_LE(rig.height(), lastH) << t;
        lastH = rig.height();
    }
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(rig.height(), closedH);
    EXPECT_EQ(rig.instrumentsY(), closedY);
    EXPECT_FALSE(credit.isSourcesListShown());
}

TEST(SoundPackSourcesGlide, ReduceMotionIsAShortFadeAndOffLandsAtOnceWithTheSameEndStates) {
    int fullOpen = 0;
    int closed = 0;
    {
        SourcesRow rig;
        closed = rig.height();
        rig.row.getCredit().setSourcesOpen(true, false);
        fullOpen = rig.height();
        ASSERT_GT(fullOpen, closed);
    }
    {
        SourcesRow rig;
        FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
        rig.row.getCredit().setSourcesOpen(true);
        FadeVisibility::stepAllForTest(0.5f);
        EXPECT_GT(rig.height(), closed);
        EXPECT_LT(rig.height(), fullOpen);
        FadeVisibility::stepAllForTest(1.0f);
        EXPECT_EQ(rig.height(), fullOpen);
        rig.row.getCredit().setSourcesOpen(false);
        FadeVisibility::stepAllForTest(1.0f);
        EXPECT_EQ(rig.height(), closed);
    }
    SourcesRow rig;
    FadeAnimateGuard off(synth::ui::AnimationMode::off);
    rig.row.getCredit().setSourcesOpen(true);
    EXPECT_EQ(rig.height(), fullOpen) << "Off: no frames in between";
    EXPECT_EQ(rig.row.getHeight(), fullOpen) << "and the row was told";
    rig.row.getCredit().setSourcesOpen(false);
    EXPECT_EQ(rig.height(), closed);
}

TEST(SoundPackSourcesGlide, APackWithoutASourcesFileHasNoLinkAndAnOpenListSnapsClosed) {
    SourcesRow none(false);
    EXPECT_FALSE(none.row.getCredit().isSourcesLinkShown());
    none.row.getCredit().setSourcesOpen(true, false);
    EXPECT_FALSE(none.row.getCredit().isSourcesOpen());

    SourcesRow rig;
    auto& credit = rig.row.getCredit();
    credit.setSourcesOpen(true, false);
    ASSERT_TRUE(credit.isSourcesListShown());
    const int openH = rig.height();
    synth::soundpacks::SoundPackInstaller::Row bare;
    bare.id = "big";
    bare.name = "Big pack";
    rig.row.update(bare);
    EXPECT_FALSE(credit.isSourcesOpen());
    EXPECT_FALSE(credit.isSourcesListShown());
    EXPECT_LT(rig.height(), openH);
}
