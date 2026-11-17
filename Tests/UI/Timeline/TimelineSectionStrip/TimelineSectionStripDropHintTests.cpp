// TimelineSectionStripDropHintTests.cpp -- the words beside the drop line while a block is dragged: they say what a
// release does (the setting, flipped while Shift is held, the same mode the release commits), are empty when no drag
// shows them, and cross-fade when Shift flips the mode.
#include "TimelineSectionStripFixture.h"
#include "TimelineSectionStripThreeBlocksFixture.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/TooltipAnchor.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDropWords.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
const juce::String kPush = "Release to insert here";
const juce::String kCover = "Release to replace what is here";
const juce::String kPlaceCopy = "Release to place a copy here";
const juce::String kPushCopy = "Release to insert a copy here";
const juce::String kCoverCopy = "Release to replace it with a copy";

// Intro over beats 0..8, Verse over 8..16, with the cover setting held in a temp settings file.
struct HintFixture : StripFixture {
    juce::File dir = juce::File::createTempFile("sectionstrip");
    std::unique_ptr<juce::PropertiesFile> props;
    PlacementId intro, verse;

    explicit HintFixture(bool cover) {
        dir.createDirectory();
        juce::PropertiesFile::Options options;
        options.filenameSuffix = ".settings";
        options.folderName = dir.getFullPathName();
        props = std::make_unique<juce::PropertiesFile>(dir.getChildFile("p.settings"), options);
        props->setValue("timelineSectionsCoverOnDrop", cover);
        strip.setPropertiesFile(props.get());
        doc.addClip(track, 0.0, 4.0, "Intro clip");
        doc.addClip(track, 8.0, 4.0, "Verse clip");
        doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, 0.5);
        doc.makeSection(8.0, 16.0, "Verse", 0xff4FC1FF, 0.5);
        strip.refresh();
        intro = doc.getPlacements()[0].id;
        verse = doc.getPlacements()[1].id;
    }
    ~HintFixture() {
        strip.setPropertiesFile(nullptr);
        dir.deleteRecursively();
    }
    juce::Point<float> grab() const { return {x(2.0), 12.0f}; }
    void press() { strip.mouseDown(event(grab(), grab(), false)); }
    void dragTo(double leftBeat, int flags = 0, float y = 12.0f) {
        strip.mouseDrag(event({x(leftBeat + 2.0), y}, grab(), true, flags));
    }
    void release(double leftBeat, int flags = 0) {
        strip.mouseUp(event({x(leftBeat + 2.0), 12.0f}, grab(), true, flags));
    }
};

struct MotionScope {
    MotionScope() {
        FadeVisibility::setAnimateOffScreenForTest(true);
        setAnimationMode(AnimationMode::followSystem);
        setReducedMotionForTest(false);
    }
    ~MotionScope() {
        FadeVisibility::setAnimateOffScreenForTest(false);
        setReducedMotionForTest(std::nullopt);
        setAnimationMode(AnimationMode::followSystem);
    }
};

} // namespace

TEST(TimelineSectionStripDropHintTest, TheHintNamesTheModeTheReleaseThenCommits) {
    for (const bool cover : {false, true}) {
        for (const bool shift : {false, true}) {
            HintFixture f(cover);
            EXPECT_TRUE(f.strip.getDropHintTextForTest().isEmpty()) << "nothing before a drag";
            f.press();
            f.dragTo(8.0, shift ? kShift : 0);
            const bool covers = cover != shift;
            EXPECT_EQ(f.strip.getDropHintTextForTest(), covers ? kCover : kPush)
                << "setting " << (int)cover << " shift " << (int)shift;
            f.release(8.0, shift ? kShift : 0);
            EXPECT_EQ(f.doc.getPlacement(f.verse) == nullptr, covers) << "the release did what the hint said";
            EXPECT_TRUE(f.strip.getDropHintTextForTest().isEmpty()) << "gone after the release";
        }
    }
}

// Each setting x Shift x copy, dragging earlier (the Verse over the Intro) and later (the Intro over the Verse): the
// words are true in both directions.
TEST(TimelineSectionStripDropHintTest, TheWordsAreTrueForEveryModeShiftCopyAndDirection) {
    for (const bool cover : {false, true})
        for (const bool shift : {false, true})
            for (const bool copy : {false, true})
                for (const bool earlier : {false, true}) {
                    HintFixture f(cover);
                    const int flags = (shift ? kShift : 0) | (copy ? kAlt : 0);
                    if (earlier) {
                        const juce::Point<float> down{f.x(10.0), 12.0f}; // grab the Verse
                        f.strip.mouseDown(f.event(down, down, false));
                        f.strip.mouseDrag(f.event({f.x(2.0), 12.0f}, down, true, flags));
                    } else {
                        f.press();
                        f.dragTo(10.0, flags);
                    }
                    EXPECT_EQ(f.strip.isBlockDragCopyForTest(), copy);
                    const bool covers = cover != shift;
                    // A copy dropped after the Verse lands in free bars: nothing moves, so it is placed, not inserted.
                    const bool nothingMoves = copy && !earlier;
                    const auto expected = covers ? (copy ? kCoverCopy : kCover)
                                                 : (nothingMoves ? kPlaceCopy : (copy ? kPushCopy : kPush));
                    EXPECT_EQ(f.strip.getDropHintTextForTest(), expected)
                        << "cover " << cover << " shift " << shift << " copy " << copy << " earlier " << earlier;
                    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
                }
}

TEST(TimelineSectionStripDropHintTest, ACopyDragUsesTheCopyWords) {
    HintFixture f(false);
    f.press();
    f.dragTo(24.0, kAlt);
    ASSERT_TRUE(f.strip.isBlockDragCopyForTest());
    EXPECT_EQ(f.strip.getDropHintTextForTest(), kPlaceCopy) << "bars 25-28 are free: nothing moves";
    f.dragTo(8.0, kAlt);
    EXPECT_EQ(f.strip.getDropHintTextForTest(), kPushCopy) << "over the Verse the music after it moves";
    f.dragTo(24.0, kAlt);
    f.dragTo(24.0, kAlt | kShift);
    EXPECT_EQ(f.strip.getDropHintTextForTest(), kCoverCopy);
    f.release(24.0, kAlt | kShift);
}

// The hint's "something moves" answer is the plan's: a MOVE over a neighbour pushes it (insert), and the release then
// really moves it; a COPY into free bars pushes nothing (place), and the release leaves the Verse where it was.
TEST(TimelineSectionStripDropHintTest, TheHintAgreesWithWhatTheReleaseDoes) {
    {
        HintFixture f(false);
        f.press();
        f.dragTo(10.0); // the Intro's centre passes the Verse's: it lands after, the Verse moves up to bar 1
        EXPECT_EQ(f.strip.getDropHintTextForTest(), kPush);
        f.release(10.0);
        EXPECT_NEAR(f.doc.getPlacement(f.verse)->startBeat, 0.0, 1e-6);
    }
    {
        HintFixture f(false);
        f.press();
        f.dragTo(24.0, kAlt);
        EXPECT_EQ(f.strip.getDropHintTextForTest(), kPlaceCopy);
        f.release(24.0, kAlt);
        EXPECT_NEAR(f.doc.getPlacement(f.verse)->startBeat, 8.0, 1e-6);
        EXPECT_EQ(f.doc.getPlacements().size(), 3u);
    }
}

// The pill is sized by the longest phrase of the drag, so the Shift cross-fade never resizes it.
TEST(TimelineSectionStripDropHintTest, ThePillIsSizedByTheLongestPhraseOfTheDrag) {
    for (const bool copy : {false, true}) {
        HintFixture f(false);
        f.press();
        f.dragTo(8.0, copy ? kAlt : 0);
        const float before = f.strip.getDropHintBoundsForTest().getWidth();
        f.strip.modifierKeysChanged(juce::ModifierKeys(kShift | (copy ? kAlt : 0)));
        EXPECT_FLOAT_EQ(f.strip.getDropHintBoundsForTest().getWidth(), before) << "copy " << copy;
        f.release(8.0, kShift | (copy ? kAlt : 0));
    }
}

TEST(SectionDropWordsTest, OneVocabularyForTheHintTheMenuAndThePreferences) {
    using synth::ui::SectionDropWords;
    EXPECT_EQ(SectionDropWords::hint(true, false, false), "Release to place here");
    EXPECT_EQ(SectionDropWords::hint(true, true, false), "Release to place a copy here");
    EXPECT_EQ(SectionDropWords::placeAgainMenu(true), "Place again after this (insert)");
    EXPECT_EQ(SectionDropWords::placeAgainMenu(false), "Place again after this (replace what is there)");
    EXPECT_EQ(SectionDropWords::insertChoice(), "Insert (the music after moves later)");
    EXPECT_EQ(SectionDropWords::replaceChoice(), "Replace what is there");
    EXPECT_TRUE(SectionDropWords::shiftLine().contains("insert or replace"));
}

TEST(TimelineSectionStripDropHintTest, TheHintIsHiddenOutsideTheStripAndAfterEsc) {
    HintFixture f(false);
    f.press();
    f.dragTo(8.0);
    ASSERT_FALSE(f.strip.getDropHintTextForTest().isEmpty());
    f.dragTo(8.0, 0, -10.0f);
    EXPECT_TRUE(f.strip.getDropHintTextForTest().isEmpty()) << "a release above the strip cancels";
    f.dragTo(8.0);
    EXPECT_FALSE(f.strip.getDropHintTextForTest().isEmpty()) << "back inside it returns";
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.strip.getDropHintTextForTest().isEmpty());
    f.release(8.0);
}

TEST(TimelineSectionStripDropHintTest, ShiftMidDragSwapsTheWordsWithACrossFadeAndRepaints) {
    MotionScope scope;
    HintFixture f(false);
    f.press();
    f.dragTo(8.0);
    auto& motion = f.strip.getMotionForTest();
    EXPECT_FALSE(motion.isHintSwappingForTest()) << "the first words appear with the lift, not as a swap";
    f.strip.modifierKeysChanged(juce::ModifierKeys(kShift)); // Shift pressed: the plan, and so the words, change
    EXPECT_EQ(f.strip.getDropHintTextForTest(), kCover);
    ASSERT_TRUE(motion.isHintSwappingForTest());
    EXPECT_FLOAT_EQ(motion.hintSwap(), 0.0f);
    motion.stepForTest(0.0);
    motion.stepForTest(60.0);
    EXPECT_GT(motion.hintSwap(), 0.0f);
    EXPECT_LT(motion.hintSwap(), 1.0f);
    motion.stepForTest(400.0);
    EXPECT_FLOAT_EQ(motion.hintSwap(), 1.0f);
    EXPECT_FALSE(motion.isHintSwappingForTest()) << "it stops itself";
    f.strip.modifierKeysChanged(juce::ModifierKeys(0)); // released: back
    EXPECT_EQ(f.strip.getDropHintTextForTest(), kPush);
    EXPECT_TRUE(motion.isHintSwappingForTest());
    f.release(8.0);
}

TEST(TimelineSectionStripDropHintTest, TheSwapLandsAtOnceWithAnimationsOff) {
    MotionScope scope;
    setAnimationMode(AnimationMode::off);
    HintFixture f(false);
    f.press();
    f.dragTo(8.0);
    f.strip.modifierKeysChanged(juce::ModifierKeys(kShift));
    auto& motion = f.strip.getMotionForTest();
    EXPECT_FALSE(motion.isHintSwappingForTest());
    EXPECT_FLOAT_EQ(motion.hintSwap(), 1.0f);
    EXPECT_EQ(f.strip.getDropHintTextForTest(), kCover);
    f.release(8.0, kShift);
}

TEST(TimelineSectionStripDropHintTest, TheBlockMenuNamesWhatPlaceAgainDoesWithTheSetting) {
    {
        HintFixture f(false);
        EXPECT_EQ(f.strip.getPlaceAgainAfterMenuTextForTest(), "Place again after this (insert)");
    }
    {
        HintFixture f(true);
        EXPECT_EQ(f.strip.getPlaceAgainAfterMenuTextForTest(), "Place again after this (replace what is there)");
    }
}

TEST(TimelineSectionStripDropHintTest, PlaceAgainAfterThisReallyUsesTheNamedMode) {
    for (const bool cover : {false, true}) {
        HintFixture f(cover);
        ASSERT_TRUE(f.strip.applySectionMenuChoice(
            f.intro,
            RecordingStrip::SectionMenuChoice::PlaceAgainAfter)); // right after the Intro, where the Verse stands
        EXPECT_EQ(f.doc.getPlacement(f.verse) == nullptr, cover) << "cover removes it, make room pushes it";
    }
}

// The words never sit on the drop line or on the block being dragged, wherever the drag is and whichever side has room.
TEST(TimelineSectionStripDropHintTest, TheWordsNeverCoverTheDropLineOrTheDraggedBlock) {
    for (const bool shift : {false, true}) {
        for (const double leftBeat : {-2.0, 0.0, 1.0, 4.0, 8.0, 12.0, 16.0, 24.0}) {
            HintFixture f(false);
            f.press();
            f.dragTo(leftBeat, shift ? kShift : 0);
            if (f.strip.getDropHintTextForTest().isEmpty())
                continue;
            const auto pill = f.strip.getDropHintBoundsForTest();
            const auto ghost = f.strip.getDragGhostBoundsForTest();
            const float lineX = f.x(f.strip.getBlockDragTargetBeatForTest());
            EXPECT_FALSE(pill.getHorizontalRange().intersects({ghost.getX(), ghost.getRight()}))
                << "beat " << leftBeat << " shift " << (int)shift;
            EXPECT_FALSE(pill.getHorizontalRange().contains(lineX)) << "beat " << leftBeat << " shift " << (int)shift;
            EXPECT_GE(pill.getX(), 0.0f);
            EXPECT_LE(pill.getRight(), (float)f.strip.getWidth());
        }
    }
}

// The words never cover a block the drop pushes aside (or covers), in either direction and with Shift held.
TEST(TimelineSectionStripDropHintTest, TheWordsNeverCoverABlockThePlanMoves) {
    for (const bool pressVerse : {false, true}) {
        for (const bool shift : {false, true}) {
            for (const double leftBeat : {-2.0, 0.0, 1.0, 2.0, 4.0, 6.0, 8.0, 10.0, 12.0}) {
                HintFixture f(false);
                const juce::Point<float> grab{f.x(pressVerse ? 10.0 : 2.0), 12.0f};
                const double grabBeat = pressVerse ? 10.0 : 2.0;
                const double fromLeft = pressVerse ? 8.0 : 0.0;
                f.strip.mouseDown(f.event(grab, grab, false));
                const juce::Point<float> to{f.x(leftBeat + (grabBeat - fromLeft)), 12.0f};
                f.strip.mouseDrag(f.event(to, grab, true, shift ? kShift : 0));
                if (f.strip.getDropHintTextForTest().isEmpty())
                    continue;
                const auto pill = f.strip.getDropHintBoundsForTest();
                for (const auto& block : f.strip.getPlannedBlockRectsForTest())
                    EXPECT_FALSE(pill.getHorizontalRange().intersects(block.getHorizontalRange()))
                        << "verse " << (int)pressVerse << " beat " << leftBeat << " shift " << (int)shift;
                EXPECT_GE(pill.getX(), 0.0f);
                EXPECT_LE(pill.getRight(), (float)f.strip.getWidth());
            }
        }
    }
}

// A pill that has to hop to the other side of the line fades out where it was and in where it goes.
TEST(TimelineSectionStripDropHintTest, ThePillCrossFadesWhenItChangesSides) {
    MotionScope motion;
    int changes = 0;
    // Narrower strips squeeze one side of the line, so some width forces a hop whatever the platform's font measures.
    for (const int width : {1200, 1000, 800, 700, 600, 500, 400}) {
        HintFixture f(false);
        f.strip.setSize(width, 24);
        const juce::Point<float> grab{f.x(10.0), 12.0f}; // the Verse, grabbed 2 beats in
        f.strip.mouseDown(f.event(grab, grab, false));
        std::optional<bool> rightOfLine;
        for (double beat = 12.0; beat >= -2.0; beat -= 0.5) {
            f.strip.mouseDrag(f.event({f.x(beat + 2.0), 12.0f}, grab, true));
            if (f.strip.getDropHintTextForTest().isEmpty())
                continue;
            const bool right =
                f.strip.getDropHintBoundsForTest().getCentreX() >= f.x(f.strip.getBlockDragTargetBeatForTest());
            if (rightOfLine && *rightOfLine != right) {
                ++changes;
                EXPECT_TRUE(f.strip.isDropHintSideSwapForTest())
                    << "a side change cross-fades, width " << width << " beat " << beat;
            }
            rightOfLine = right;
        }
        f.strip.mouseUp(f.event({f.x(0.0), 12.0f}, grab, true));
    }
    EXPECT_GT(changes, 0) << "some drag crosses a place where the pill must change sides";
}

// ---- The strip's tooltip sits below its row ----

namespace {
struct PointerReset {
    ~PointerReset() { synth::ui::AppTooltipWindow::setSyntheticPointer(std::nullopt); }
};
} // namespace

TEST(TimelineSectionStripTooltipTest, TheStripAsksForItsWholeRowToBeKeptClear) {
    ThreeBlocks f;
    EXPECT_TRUE(f.strip.getProperties()[synth::ui::tooltip_anchor::kKeepClearProperty]);
}

TEST(TimelineSectionStripTooltipTest, TheTipIsClearOfEveryBlockWhereverThePointerRests) {
    PointerReset reset;
    ThreeBlocks f;
    juce::Component parent;
    parent.setSize(1300, 200);
    parent.addAndMakeVisible(f.strip);
    f.strip.setBounds(0, 40, 1200, 24);
    synth::ui::AppTooltipWindow window(&parent);
    window.setTipComponentForTest(&f.strip);
    // Over the middle and over the right end (where the chip sits) of each block, and over the empty strip.
    for (const double beat : {1.0, 4.0, 7.5, 8.5, 10.0, 11.8, 12.5, 14.0, 15.8, 20.0}) {
        const juce::Point<int> pointer(f.strip.getX() + (int)f.x(beat), f.strip.getY() + 12);
        const auto tip = f.strip.tooltipAt({f.x(beat), 12.0f});
        ASSERT_FALSE(tip.isEmpty());
        window.setLastTipForTest(tip);
        synth::ui::AppTooltipWindow::setSyntheticPointer(parent.localPointToGlobal(pointer));
        window.displayTip(parent.localPointToGlobal(pointer), tip);
        EXPECT_FALSE(window.getBounds().intersects(f.strip.getBounds())) << "beat " << beat;
        window.hideTip();
    }
    parent.removeChildComponent(&f.strip);
}

// Test-mode input never moves the OS cursor, so getTooltip must read the synthetic pointer the window places the tip
// at: hovering a block names that block, not the empty strip.
TEST(TimelineSectionStripTooltipTest, GetTooltipReadsTheSyntheticPointerSoAHoveredBlockIsNamed) {
    PointerReset reset;
    ThreeBlocks f;
    juce::Component parent;
    parent.setSize(1300, 200);
    parent.addAndMakeVisible(f.strip);
    f.strip.setBounds(0, 40, 1200, 24);
    const auto hover = [&](double beat) {
        synth::ui::AppTooltipWindow::setSyntheticPointer(
            parent.localPointToGlobal(juce::Point<int>(f.strip.getX() + (int)f.x(beat), f.strip.getY() + 12)));
        return f.strip.getTooltip();
    };
    const auto empty = hover(20.0);
    EXPECT_TRUE(empty.startsWith("Drag across bars"));
    for (const double beat : {1.0, 10.0, 12.5}) {
        const auto tip = hover(beat);
        EXPECT_EQ(tip, f.strip.tooltipAt({f.x(beat), 12.0f})) << "beat " << beat;
        EXPECT_FALSE(tip.startsWith("Drag across bars")) << "beat " << beat;
    }
    parent.removeChildComponent(&f.strip);
}
