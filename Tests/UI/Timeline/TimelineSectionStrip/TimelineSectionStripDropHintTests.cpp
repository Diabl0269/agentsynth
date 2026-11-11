// TimelineSectionStripDropHintTests.cpp -- the words beside the drop line while a block is dragged: they say what a
// release does (the setting, flipped while Shift is held, the same mode the release commits), are empty when no drag
// shows them, and cross-fade when Shift flips the mode.
#include "TimelineSectionStripFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
const juce::String kPush = "Release to push the music later";
const juce::String kCover = "Release to cover it";

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

TEST(TimelineSectionStripDropHintTest, ACopyDragUsesTheSameWords) {
    HintFixture f(false);
    f.press();
    f.dragTo(24.0, kAlt);
    ASSERT_TRUE(f.strip.isBlockDragCopyForTest());
    EXPECT_EQ(f.strip.getDropHintTextForTest(), kPush);
    f.dragTo(24.0, kAlt | kShift);
    EXPECT_EQ(f.strip.getDropHintTextForTest(), kCover);
    f.release(24.0, kAlt | kShift);
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
        EXPECT_EQ(f.strip.getPlaceAgainAfterMenuTextForTest(), "Place again after this (push the music later)");
    }
    {
        HintFixture f(true);
        EXPECT_EQ(f.strip.getPlaceAgainAfterMenuTextForTest(), "Place again after this (cover what is there)");
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
