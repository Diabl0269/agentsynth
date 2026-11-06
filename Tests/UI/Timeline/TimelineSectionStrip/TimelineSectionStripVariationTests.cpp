// TimelineSectionStripVariationTests.cpp -- the variation chip on each Sections strip block, its menu and the headless
// seam behind it, the V key, the words a screen reader and a tooltip use, the probe's `variations` key, and the clip
// link tooltip for a track a variation owns.
#include "ShortcutManager/ShortcutManager.h"
#include "TestMode/TestModeStateSource.h"
#include "Timeline/TimelineDoc/SectionVariationLetter.h"
#include "TimelineSectionStripFixture.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/ClipAccessibilityText.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;
using synth::ui::sectionstrip::chipBounds;
using VC = TimelineSectionStrip::VariationChoice;

namespace {

// Lets an off-screen strip animate (no VBlank reaches it) and puts every global back afterwards.
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

// A Verse over bars 1-2 (beats 0..8) with a Lead and a Drums clip, played again at beat 16.
struct VariationFixture : StripFixture {
    TrackId drums;
    ClipId leadClip, drumClip;
    PlacementId first, second;
    VariationFixture() {
        drums = doc.addTrack(TrackKind::Midi, "Drums");
        leadClip = doc.addClip(track, 0.0, 4.0, "Lead 1");
        drumClip = doc.addClip(drums, 0.0, 4.0, "Beat");
        const auto id = doc.makeSection(0.0, 8.0, "Verse", 0xff56D4C0, 0.5);
        doc.placeSection(id, 16.0, SectionDropMode::MakeRoom, 0.5);
        first = doc.getPlacements()[0].id;
        second = doc.getPlacements()[1].id;
        strip.refresh();
    }
    int letterOf(PlacementId id) const { return doc.getPlacement(id)->variation; }
    // B owns the lead only, played by the second Verse.
    int makeB() {
        const ClipId inSecond = clipAt(track, 16.0);
        const int letter = doc.addVariationForTrack(inSecond);
        strip.refresh();
        return letter;
    }
    ClipId clipAt(TrackId t, double beat) const {
        for (const auto& c : doc.getTrack(t)->clips)
            if (std::abs(c.startBeat - beat) < 1e-6)
                return c.id;
        return {};
    }
    juce::String probe(const juce::String& key) const {
        juce::String out;
        for (int i = 0; i < strip.getNumChildComponents(); ++i)
            if (auto* source = dynamic_cast<synth::testmode::StateSource*>(strip.getChildComponent(i))) {
                auto* object = new juce::DynamicObject();
                juce::var holder(object);
                source->addTestModeState(*object);
                out = object->getProperty(key).toString();
            }
        return out;
    }
    juce::Point<float> chipCentre(PlacementId id) const {
        for (const auto& block : strip.buildBlocks())
            if (block.id == id)
                return chipBounds(block.bounds).getCentre();
        return {};
    }
};

} // namespace

//==============================================================================
// ---- Geometry ----

TEST(SectionStripChipTest, ChipSitsAtTheRightEndAndHidesWhenTheNameWouldHaveNoRoom) {
    const juce::Rectangle<float> wide(100.0f, 3.0f, 320.0f, 18.0f);
    const auto chip = chipBounds(wide);
    ASSERT_FALSE(chip.isEmpty());
    EXPECT_FLOAT_EQ(chip.getWidth(), 16.0f);
    EXPECT_FLOAT_EQ(chip.getHeight(), wide.getHeight() - 6.0f);
    EXPECT_FLOAT_EQ(wide.getRight() - chip.getRight(), 4.0f);
    EXPECT_TRUE(wide.contains(chip));

    // Name room is what is left after the name inset and the chip with its inset: 24 px keeps the chip.
    const float enough = synth::ui::sectionstrip::kBlockPadX + 16.0f + 4.0f + 24.0f;
    EXPECT_FALSE(chipBounds({0.0f, 3.0f, enough, 18.0f}).isEmpty());
    EXPECT_TRUE(chipBounds({0.0f, 3.0f, enough - 1.0f, 18.0f}).isEmpty());
    EXPECT_TRUE(chipBounds({0.0f, 3.0f, 1.0f, 18.0f}).isEmpty());
}

//==============================================================================
// ---- The chip ----

TEST(TimelineSectionStripVariationTest, ClickingTheChipSelectsTheBlockAndOpensTheVariationMenu) {
    VariationFixture f;
    const auto at = f.chipCentre(f.second);
    ASSERT_GT(at.x, 0.0f);
    f.strip.mouseDown(f.event(at, at, false));
    f.strip.mouseUp(f.event(at, at, false));
    EXPECT_EQ(f.strip.variationMenus, 1);
    EXPECT_EQ(f.strip.lastVariationMenu, f.second);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.second);
    EXPECT_EQ(f.strip.blockMenus, 0);
}

TEST(TimelineSectionStripVariationTest, ClickingTheBlockElsewhereDoesNotOpenIt) {
    VariationFixture f;
    const juce::Point<float> body{f.x(1.0), 12.0f};
    f.strip.mouseDown(f.event(body, body, false));
    f.strip.mouseUp(f.event(body, body, false));
    EXPECT_EQ(f.strip.variationMenus, 0);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.first);
}

TEST(TimelineSectionStripVariationTest, ADragThatStartsOnTheChipStillMovesTheBlock) {
    VariationFixture f;
    const auto at = f.chipCentre(f.second);
    const juce::Point<float> to{at.x + 200.0f, at.y};
    f.strip.mouseDown(f.event(at, at, false));
    f.strip.mouseDrag(f.event(to, at, true));
    EXPECT_TRUE(f.strip.isBlockDraggingForTest());
    f.strip.mouseUp(f.event(to, at, true));
    EXPECT_EQ(f.strip.variationMenus, 0);
}

TEST(TimelineSectionStripVariationTest, HoveringTheChipBrightensItAndTheTooltipNamesTheVariation) {
    VariationFixture f;
    ASSERT_GT(f.makeB(), 0);
    const auto at = f.chipCentre(f.second);
    f.strip.mouseMove(f.event(at, at, false));
    EXPECT_EQ(f.strip.tooltipAt(at), "Variation B of Verse. Click to switch, add or play it everywhere.");
    const auto onBlock = f.strip.tooltipAt({f.x(1.0), 12.0f});
    EXPECT_TRUE(onBlock.startsWith("Verse - variation A")) << onBlock;
    const auto secondBody = f.strip.tooltipAt({f.x(17.0), 12.0f});
    EXPECT_TRUE(secondBody.startsWith("Verse - variation B")) << secondBody;
}

TEST(TimelineSectionStripVariationTest, ASectionWithOneVariationLeavesTheBlockTooltipAsItWas) {
    VariationFixture f;
    EXPECT_FALSE(f.strip.tooltipAt({f.x(1.0), 12.0f}).contains("variation"));
}

//==============================================================================
// ---- The menu and its seam ----

TEST(TimelineSectionStripVariationTest, EveryChoiceAppliesAsOneUndoStep) {
    VariationFixture f;
    struct Case {
        VC choice;
        int letter;
    };
    const auto lettersOf = [&] { return f.probe("variations"); };

    // New variation: the placement now plays B.
    ASSERT_TRUE(f.strip.applyVariationMenuChoice(f.second, VC::NewVariation));
    EXPECT_EQ(f.letterOf(f.second), 1);
    EXPECT_EQ(lettersOf(), "A*;B*");
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getSection(f.doc.getPlacement(f.second)->section)->variations.size(), 1u) << "one step";
    EXPECT_EQ(f.letterOf(f.second), 0);

    // Switch.
    ASSERT_TRUE(f.strip.applyVariationMenuChoice(f.second, VC::NewVariation));
    ASSERT_TRUE(f.strip.applyVariationMenuChoice(f.second, VC::Switch, 0));
    EXPECT_EQ(f.letterOf(f.second), 0);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.letterOf(f.second), 1) << "the switch alone is undone";

    // Play everywhere: B on the first Verse too.
    ASSERT_TRUE(f.strip.applyVariationMenuChoice(f.second, VC::PlayEverywhere));
    EXPECT_EQ(f.letterOf(f.first), 1);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.letterOf(f.first), 0);
    EXPECT_EQ(f.letterOf(f.second), 1);

    // Remove: B goes, the placement is back on A.
    ASSERT_TRUE(f.strip.applyVariationMenuChoice(f.second, VC::RemoveVariation));
    EXPECT_EQ(f.letterOf(f.second), 0);
    EXPECT_EQ(f.doc.getSection(f.doc.getPlacement(f.second)->section)->variations.size(), 1u);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getSection(f.doc.getPlacement(f.second)->section)->variations.size(), 2u);
    EXPECT_EQ(f.letterOf(f.second), 1);
}

TEST(TimelineSectionStripVariationTest, ARefusedChoiceChangesNothingAndLeavesNoUndoStep) {
    VariationFixture f;
    EXPECT_FALSE(f.strip.applyVariationMenuChoice(f.first, VC::RemoveVariation)) << "A cannot be removed";
    EXPECT_FALSE(f.strip.applyVariationMenuChoice(f.first, VC::PlayEverywhere)) << "everyone plays A already";
    EXPECT_FALSE(f.strip.applyVariationMenuChoice(f.first, VC::Switch, 0)) << "already playing it";
    EXPECT_FALSE(f.strip.applyVariationMenuChoice(f.first, VC::Switch, 5)) << "no such letter";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineSectionStripVariationTest, PlayEverywhereIsOfferedOnlyWhenSomeCopyPlaysAnotherLetter) {
    VariationFixture f;
    EXPECT_FALSE(f.strip.isVariationChoiceEnabled(f.first, VC::PlayEverywhere));
    ASSERT_GT(f.makeB(), 0);
    EXPECT_TRUE(f.strip.isVariationChoiceEnabled(f.second, VC::PlayEverywhere));
    EXPECT_TRUE(f.strip.isVariationChoiceEnabled(f.first, VC::PlayEverywhere));
    EXPECT_EQ(f.strip.variationChoiceLabel(f.second, VC::PlayEverywhere), "Play B in every Verse");
    EXPECT_FALSE(f.strip.isVariationChoiceEnabled(f.first, VC::RemoveVariation));
    EXPECT_TRUE(f.strip.isVariationChoiceEnabled(f.second, VC::RemoveVariation));
    EXPECT_EQ(f.strip.variationChoiceLabel(f.second, VC::RemoveVariation), "Remove variation B");
    EXPECT_EQ(f.strip.variationChoiceLabel(f.second, VC::NewVariation), "New variation (copy of B)");

    // A section that plays once has nobody else to play it.
    StripFixture g;
    g.doc.addClip(g.track, 0.0, 4.0, "x");
    g.doc.makeSection(0.0, 8.0, "Solo", 0xff56D4C0, 0.5);
    g.strip.refresh();
    const auto only = g.doc.getPlacements().front().id;
    ASSERT_TRUE(g.strip.applyVariationMenuChoice(only, VC::NewVariation));
    EXPECT_FALSE(g.strip.isVariationChoiceEnabled(only, VC::PlayEverywhere));
}

TEST(TimelineSectionStripVariationTest, NewVariationIsDisabledAtTwentySixLetters) {
    StripFixture f;
    f.doc.addClip(f.track, 0.0, 4.0, "x");
    f.doc.makeSection(0.0, 8.0, "Solo", 0xff56D4C0, 0.5);
    f.strip.refresh();
    const auto id = f.doc.getPlacements().front().id;
    for (int i = 1; i < TimelineDoc::kMaxVariations; ++i)
        ASSERT_TRUE(f.strip.applyVariationMenuChoice(id, VC::NewVariation));
    EXPECT_FALSE(f.strip.isVariationChoiceEnabled(id, VC::NewVariation));
    EXPECT_FALSE(f.strip.applyVariationMenuChoice(id, VC::NewVariation));
}

TEST(TimelineSectionStripVariationTest, RadioRowsNameTheTracksAVariationOwnsAndTickTheCurrentOne) {
    VariationFixture f;
    EXPECT_EQ(f.strip.variationRowLabel(f.second, 0), "A");
    ASSERT_EQ(f.makeB(), 1);
    EXPECT_EQ(f.strip.variationRowLabel(f.second, 1), "B (Lead)") << "owns the lead only";
    // A full copy owns every track: just its letter.
    ASSERT_TRUE(f.strip.applyVariationMenuChoice(f.first, VC::NewVariation));
    EXPECT_EQ(f.strip.variationRowLabel(f.first, 2), "C");

    // The menu itself: one row per letter, the placement's own ticked, then New / Play everywhere / Remove.
    juce::PopupMenu menu;
    f.strip.addVariationItems(menu, f.second);
    std::vector<juce::String> texts;
    juce::String ticked;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        if (it.getItem().isSeparator)
            continue;
        texts.push_back(it.getItem().text);
        if (it.getItem().isTicked)
            ticked = it.getItem().text;
    }
    ASSERT_EQ(texts.size(), 6u);
    EXPECT_EQ(texts[0], "A");
    EXPECT_EQ(texts[1], "B (Lead)");
    EXPECT_EQ(texts[2], "C");
    EXPECT_EQ(texts[3], "New variation (copy of B)");
    EXPECT_EQ(texts[4], "Play B in every Verse");
    EXPECT_EQ(texts[5], "Remove variation B");
    EXPECT_EQ(ticked, "B (Lead)");
}

TEST(TimelineSectionStripVariationTest, TheRowNamesAtMostThreeOwnTracksThenACount) {
    StripFixture f;
    const auto a = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto b = f.doc.addTrack(TrackKind::Midi, "Pad");
    const auto c = f.doc.addTrack(TrackKind::Midi, "Keys");
    const auto d = f.doc.addTrack(TrackKind::Midi, "Arp");
    for (const auto t : {f.track, a, b, c, d})
        f.doc.addClip(t, 0.0, 4.0, "x");
    const auto sec = f.doc.makeSection(0.0, 8.0, "Verse", 0xff56D4C0, 0.5);
    f.doc.placeSection(sec, 16.0, SectionDropMode::MakeRoom, 0.5);
    f.strip.refresh();
    const auto second = f.doc.getPlacements()[1].id;
    // Own the lead, then each further track by adding variations on the same placement's current set.
    ASSERT_GE(f.doc.addVariationForTrack(f.doc.getTrack(f.track)->clips.back().id), 1);
    for (const auto t : {a, b, c})
        ASSERT_GE(f.doc.addVariationForTrack(f.doc.getTrack(t)->clips.back().id), 1);
    f.strip.refresh();
    const auto label = f.strip.variationRowLabel(second, f.doc.getPlacement(second)->variation);
    EXPECT_TRUE(label.endsWith(", +1)")) << label;
    EXPECT_TRUE(label.startsWith("E (")) << label;
}

//==============================================================================
// ---- Keys, words and the probe ----

TEST(TimelineSectionStripVariationTest, VOpensTheSelectedBlocksVariationMenu) {
    VariationFixture f;
    const juce::KeyPress v('v', juce::ModifierKeys::noModifiers, 0);
    EXPECT_FALSE(f.strip.keyPressed(v)) << "nothing selected, nothing to open";
    EXPECT_EQ(f.strip.variationMenus, 0);
    f.strip.selectPlacement(f.second);
    EXPECT_TRUE(f.strip.keyPressed(v));
    EXPECT_EQ(f.strip.variationMenus, 1);
    EXPECT_EQ(f.strip.lastVariationMenu, f.second);
    EXPECT_FALSE(f.strip.keyPressed(juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0)))
        << "Cmd+V is not this key";
}

TEST(TimelineSectionStripVariationTest, TheKeyDoesNotClashWithAnyDefaultShortcut) {
    ShortcutManager manager;
    const juce::KeyPress v('v', juce::ModifierKeys::noModifiers, 0);
    EXPECT_TRUE(manager.getActionsForKeyPress(v).isEmpty()) << manager.getActionsForKeyPress(v).joinIntoString(",");
}

TEST(TimelineSectionStripVariationTest, TheBlockTitleNamesTheVariationOnlyWhenThereIsMoreThanOne) {
    VariationFixture f;
    EXPECT_EQ(f.strip.getProxyTitleForTest(1), "Verse, bars 5 to 6, 2 of 2");
    ASSERT_GT(f.makeB(), 0);
    EXPECT_EQ(f.strip.getProxyTitleForTest(0), "Verse, bars 1 to 2, 1 of 2, variation A");
    EXPECT_TRUE(f.strip.getProxyTitleForTest(1).endsWith(", 2 of 2, variation B")) << f.strip.getProxyTitleForTest(1);
    EXPECT_TRUE(f.strip.getProxyDescriptionForTest(1).contains("V: variations"));
}

TEST(TimelineSectionStripVariationTest, TheProbeListsEachPlacementsLetterAndTheSelectedSectionsCount) {
    VariationFixture f;
    EXPECT_EQ(f.probe("variations"), "A;A");
    EXPECT_EQ(f.probe("sectionVariationCount"), "0");
    ASSERT_GT(f.makeB(), 0);
    f.strip.selectPlacement(f.first);
    EXPECT_EQ(f.probe("variations"), "A*;B*");
    EXPECT_EQ(f.probe("sectionVariationCount"), "2");
    EXPECT_TRUE(f.probe("sections").contains("Verse:0.000:8.000:1/2")) << "the sections string is unchanged";
}

//==============================================================================
// ---- Motion ----

TEST(TimelineSectionStripVariationTest, SwitchingOutlinesTheRewrittenPlacementsForFourHundredMs) {
    MotionScope scope;
    VariationFixture f;
    ASSERT_GT(f.makeB(), 0);
    auto& motion = f.strip.getMotionForTest();
    motion.stepForTest(1000.0);
    ASSERT_TRUE(f.strip.applyVariationMenuChoice(f.second, VC::PlayEverywhere));
    EXPECT_TRUE(motion.isOutlined(f.first)) << "the copy that switched";
    EXPECT_FALSE(motion.isOutlined(f.second)) << "the copy that already played it";
    EXPECT_DOUBLE_EQ(motion.getOutlineDurationMsForTest(), SectionStripMotion::kOutlineMs);
    motion.stepForTest(0.0);
    motion.stepForTest(500.0);
    EXPECT_FALSE(motion.isOutlined(f.first));
}

//==============================================================================
// ---- The clip link tooltip ----

namespace {
struct LaneVariationFixture {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    TimelineClipLaneArea lane{state, selection};
    TrackId lead, drums;
    PlacementId first, second;
    LaneVariationFixture() {
        state.pixelsPerBeat = 40.0;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1600, 400);
        lead = doc.addTrack(TrackKind::Midi, "Lead");
        drums = doc.addTrack(TrackKind::Midi, "Drums");
        doc.addClip(lead, 0.0, 4.0, "L");
        doc.addClip(drums, 0.0, 4.0, "D");
        const auto s = doc.makeSection(0.0, 8.0, "Verse", 0xff56D4C0, 0.5);
        doc.placeSection(s, 16.0, SectionDropMode::MakeRoom, 0.5);
        first = doc.getPlacements()[0].id;
        second = doc.getPlacements()[1].id;
    }
    ClipId clipAt(TrackId t, double beat) const {
        for (const auto& c : doc.getTrack(t)->clips)
            if (std::abs(c.startBeat - beat) < 1e-6)
                return c.id;
        return {};
    }
    juce::String tip(TrackId t, double beat) const {
        const auto mark = TimelineClipLaneArea::linkMarkBounds(lane.getClipRect(clipAt(t, beat)));
        return lane.linkTooltipAt(mark.getCentre());
    }
};
} // namespace

TEST(TimelineSectionStripVariationTest, AnOwnedTrackSaysItBelongsToTheVariationAndCountsItsCopies) {
    LaneVariationFixture f;
    ASSERT_EQ(f.doc.addVariationForTrack(f.clipAt(f.lead, 16.0)), 1);
    // Only the second Verse plays B: the lead there belongs to B alone.
    const auto follow = f.lane.sectionFollowOf(*f.doc.getClip(f.clipAt(f.lead, 16.0)));
    EXPECT_TRUE(follow.own);
    EXPECT_EQ(follow.plays, 1);
    EXPECT_EQ(follow.letter, 1);
    EXPECT_EQ(TimelineClipLaneArea::sectionFollowTooltip(follow), "Belongs to Verse B, the only one.");
    EXPECT_EQ(f.tip(f.lead, 16.0), "Belongs to Verse B, the only one.");

    // A third copy of B: the tooltip counts them and says editing any changes all.
    f.doc.placeSection(f.doc.getPlacement(f.second)->section, 32.0, SectionDropMode::MakeRoom, 0.5, 1);
    EXPECT_EQ(f.tip(f.lead, 16.0), "Belongs to Verse B (plays 2 times). Edit any Verse B to change them all.");
}

TEST(TimelineSectionStripVariationTest, ASharedTrackCountsEveryPlacementWithTheSameKey) {
    LaneVariationFixture f;
    ASSERT_EQ(f.doc.addVariationForTrack(f.clipAt(f.lead, 16.0)), 1);
    // The drums are shared by both Verses (B owns only the lead).
    EXPECT_EQ(f.tip(f.drums, 0.0), "Follows Verse (plays 2 times). Edit any Verse to change them all.");
    EXPECT_EQ(f.tip(f.drums, 16.0), "Follows Verse (plays 2 times). Edit any Verse to change them all.");
    // The lead in the first Verse (A) is now alone with that key: it follows nothing.
    EXPECT_EQ(f.lane.sectionFollowOf(*f.doc.getClip(f.clipAt(f.lead, 0.0))).plays, 0);
    EXPECT_TRUE(f.tip(f.lead, 0.0).isEmpty());
}

TEST(TimelineSectionStripVariationTest, TheScreenReaderSaysBelongsToForAnOwnedTrack) {
    synth::Track track;
    track.name = "Lead";
    synth::Clip clip;
    clip.name = "L";
    clip.lengthBeats = 4.0;
    EXPECT_TRUE(
        synth::ui::describeClipForAccessibility(clip, track, 4.0, 0, {}, "Verse B").endsWith(", belongs to Verse B"));
    EXPECT_TRUE(synth::ui::describeClipForAccessibility(clip, track, 4.0, 0, "Verse").endsWith(", follows Verse"));

    LaneVariationFixture f;
    ASSERT_EQ(f.doc.addVariationForTrack(f.clipAt(f.lead, 16.0)), 1);
    f.lane.setKeyboardClip(f.clipAt(f.lead, 16.0));
    EXPECT_TRUE(f.lane.getKeyboardClipAccessibilityText().endsWith(", belongs to Verse B"))
        << f.lane.getKeyboardClipAccessibilityText();
}
