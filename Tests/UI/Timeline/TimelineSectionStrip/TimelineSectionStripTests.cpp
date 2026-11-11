// TimelineSectionStripTests.cpp — the Sections strip: make by drag, rename, the block and strip menus' edits, the
// accessibility proxies, the probe, its place above the ruler in the panel, the settings, and the clip lane's
// follows-mark and "Make section" verb.
#include "AppUndoManager.h"
#include "ShortcutManager/ShortcutManager.h"
#include "TestMode/TestModeStateSource.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelineSectionStripFixture.h"
#include "UI/Timeline/ClipAccessibilityText.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineSectionStrip/SectionStripSettings.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include "UI/Timeline/TimelineViewState.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace synth;
using namespace synth::ui;

using namespace sectionstrip_test;

TEST(TimelineSectionStripTest, DraggingAcrossBarsMakesOneBarSnappedSectionAsOneUndoStep) {
    StripFixture f;
    f.doc.addClip(f.track, 0.0, 16.0, "Long");
    f.dragAcross(5.0, 13.5);
    ASSERT_EQ(f.doc.getSections().size(), 1u);
    ASSERT_EQ(f.doc.getPlacements().size(), 1u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[0].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(f.doc.getSections()[0].lengthBeats, 12.0);
    EXPECT_EQ(f.doc.getSections()[0].name, "Section 1");
    EXPECT_NE(f.strip.getRenameEditorForTest(), nullptr) << "the new block asks for its name at once";
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.doc.getPlacements()[0].id);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.doc.getSections().empty()) << "one undo removes the section and its splits";
    EXPECT_EQ(f.doc.getTrack(f.track)->clips.size(), 1u);
}

TEST(TimelineSectionStripTest, AClickOnEmptyStripMakesNothing) {
    StripFixture f;
    const juce::Point<float> p{f.x(5.0), 12.0f};
    f.strip.mouseDown(f.event(p, p, false));
    f.strip.mouseUp(f.event(p, p, false));
    EXPECT_TRUE(f.doc.getSections().empty());
}

TEST(TimelineSectionStripTest, RenameCommitsOnReturnAndIsOneUndoStep) {
    StripFixture f;
    const auto id = f.makeRepeatedSection();
    f.strip.beginRenameSection(id);
    auto* editor = f.strip.getRenameEditorForTest();
    ASSERT_NE(editor, nullptr);
    editor->setText("Hook", false);
    editor->onReturnKey();
    EXPECT_EQ(f.strip.getRenameEditorForTest(), nullptr);
    EXPECT_EQ(f.doc.getSections()[0].name, "Hook");
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getSections()[0].name, "Chorus");
}

TEST(TimelineSectionStripTest, RenameEscapeAndEmptyNameChangeNothing) {
    StripFixture f;
    const auto id = f.makeRepeatedSection();
    f.strip.beginRenameSection(id);
    f.strip.getRenameEditorForTest()->setText("Nope", false);
    f.strip.getRenameEditorForTest()->onEscapeKey();
    EXPECT_EQ(f.doc.getSections()[0].name, "Chorus");
    f.strip.beginRenameSection(id);
    f.strip.getRenameEditorForTest()->setText("", false);
    f.strip.getRenameEditorForTest()->onReturnKey();
    EXPECT_EQ(f.doc.getSections()[0].name, "Chorus");
    EXPECT_FALSE(f.undo.canUndo()) << "no rename was recorded";
}

TEST(TimelineSectionStripTest, PlaceAgainAfterAndAtEndEachAddOnePlacementInOneStep) {
    StripFixture f;
    const auto id = f.makeRepeatedSection();
    ASSERT_EQ(f.doc.getPlacements().size(), 2u);
    EXPECT_TRUE(f.strip.applySectionMenuChoice(id, TimelineSectionStrip::SectionMenuChoice::PlaceAgainAtEnd));
    ASSERT_EQ(f.doc.getPlacements().size(), 3u);
    double lastEnd = 0.0;
    for (const auto& p : f.doc.getPlacements())
        if (p.startBeat != 0.0 && p.startBeat != 16.0)
            lastEnd = p.startBeat;
    EXPECT_GE(lastEnd, 24.0) << "at the end: after everything already there";
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getPlacements().size(), 2u);
    EXPECT_TRUE(f.strip.applySectionMenuChoice(id, TimelineSectionStrip::SectionMenuChoice::PlaceAgainAfter));
    EXPECT_EQ(f.doc.getPlacements().size(), 3u);
}

TEST(TimelineSectionStripTest, MakeUniqueIsDisabledForALoneBlockAndSplitsOtherwise) {
    StripFixture f;
    f.doc.addClip(f.track, 0.0, 4.0, "A");
    f.doc.makeSection(0.0, 8.0, "Verse", 0xff56D4C0, 0.5);
    f.strip.refresh();
    const auto lone = f.doc.getPlacements().front().id;
    EXPECT_FALSE(f.strip.isSectionMenuChoiceEnabled(lone, TimelineSectionStrip::SectionMenuChoice::MakeUnique));

    StripFixture g;
    const auto id = g.makeRepeatedSection();
    EXPECT_TRUE(g.strip.isSectionMenuChoiceEnabled(id, TimelineSectionStrip::SectionMenuChoice::MakeUnique));
    EXPECT_TRUE(g.strip.applySectionMenuChoice(id, TimelineSectionStrip::SectionMenuChoice::MakeUnique));
    EXPECT_EQ(g.doc.getSections().size(), 2u);
    EXPECT_TRUE(g.undo.undo());
    EXPECT_EQ(g.doc.getSections().size(), 1u);
}

TEST(TimelineSectionStripTest, RemoveKeepMusicAndDeleteWithBarsAreOneStepEach) {
    StripFixture f;
    const auto id = f.makeRepeatedSection();
    EXPECT_TRUE(f.strip.applySectionMenuChoice(id, TimelineSectionStrip::SectionMenuChoice::RemoveKeepMusic));
    EXPECT_EQ(f.doc.getPlacements().size(), 1u);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getPlacements().size(), 2u);
    EXPECT_TRUE(f.strip.applySectionMenuChoice(id, TimelineSectionStrip::SectionMenuChoice::DeleteWithBars));
    EXPECT_EQ(f.doc.getPlacements().size(), 1u);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getPlacements().size(), 2u);
}

TEST(TimelineSectionStripTest, SelectItsClipsHandsTheClipsStartingInsideToTheCallback) {
    StripFixture f;
    const auto id = f.makeRepeatedSection();
    EXPECT_TRUE(f.strip.applySectionMenuChoice(id, TimelineSectionStrip::SectionMenuChoice::SelectClips));
    EXPECT_FALSE(f.selectedClips.empty());
}

TEST(TimelineSectionStripTest, StripMenuMakesSectionsFromMarkers) {
    StripFixture f;
    f.doc.addClip(f.track, 0.0, 32.0, "Long");
    f.doc.addMarker(0.0, "Intro", 0xffffffff);
    f.doc.addMarker(16.0, "Verse", 0xffffffff);
    f.strip.refresh();
    EXPECT_TRUE(f.strip.applyStripMenuChoice(TimelineSectionStrip::StripMenuChoice::MakeFromMarkers));
    EXPECT_EQ(f.doc.getPlacements().size(), 2u);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.doc.getPlacements().empty());
}

TEST(TimelineSectionStripTest, RightClickOpensTheBlockMenuOnABlockAndTheStripMenuElsewhere) {
    StripFixture f;
    f.makeRepeatedSection();
    const juce::Point<float> onBlock{f.x(2.0), 12.0f}, off{f.x(10.0), 12.0f};
    auto rc = [&](juce::Point<float> p) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                                juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, &f.strip, &f.strip, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(),
                                1, false);
    };
    f.strip.mouseDown(rc(onBlock));
    f.strip.mouseDown(rc(off));
    EXPECT_EQ(f.strip.blockMenus, 1);
    EXPECT_EQ(f.strip.stripMenus, 1);
}

TEST(TimelineSectionStripTest, CoverSettingFlipsTheDropMode) {
    StripFixture f;
    EXPECT_EQ(f.strip.dropMode(), SectionDropMode::MakeRoom);
    EXPECT_EQ(f.strip.dropMode(true), SectionDropMode::Cover);

    juce::PropertiesFile::Options options;
    options.applicationName = "SectionStripCoverTest";
    options.filenameSuffix = ".settings";
    options.folderName =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sectionstrip").getFullPathName();
    juce::PropertiesFile props(options);
    props.setValue(kTimelineSectionsCoverOnDropKey, true);
    f.strip.setPropertiesFile(&props);
    EXPECT_EQ(f.strip.dropMode(), SectionDropMode::Cover);
    EXPECT_EQ(f.strip.dropMode(true), SectionDropMode::MakeRoom);
    f.strip.setPropertiesFile(nullptr);
}

TEST(TimelineSectionStripTest, ProxiesTitleEachBlockWithBarsAndPosition) {
    StripFixture f;
    f.makeRepeatedSection();
    ASSERT_EQ(f.strip.getProxyCountForTest(), 2);
    EXPECT_EQ(f.strip.getProxyTitleForTest(0), "Chorus, bars 1 to 2, 1 of 2");
    EXPECT_EQ(f.strip.getProxyTitleForTest(1), "Chorus, bars 5 to 6, 2 of 2");
    EXPECT_TRUE(f.strip.getTitle() == "Sections");
    EXPECT_EQ(f.strip.getDescription(), "Sections, 1 section");
}

TEST(TimelineSectionStripTest, TooltipNamesTheBlockAndTheEmptyStripNamesTheGesture) {
    StripFixture f;
    f.makeRepeatedSection();
    const auto onBlock = f.strip.tooltipAt({f.x(2.0), 12.0f});
    EXPECT_TRUE(onBlock.contains("Chorus")) << onBlock;
    EXPECT_TRUE(onBlock.contains("plays 2 times")) << onBlock;
    const auto empty = f.strip.tooltipAt({f.x(10.0), 12.0f});
    EXPECT_TRUE(empty.contains("Drag across bars to make a section")) << empty;
    EXPECT_FALSE(empty.contains("press")) << "no key is promised when it would make nothing";
}

TEST(TimelineSectionStripTest, TheEmptyStripNamesTheMakeSectionKeyAndItsSourceWhenItWouldWork) {
    StripFixture f;
    f.makeRepeatedSection();
    f.strip.makeSectionKeySource = [] { return juce::String("the loop"); };
    const auto empty = f.strip.tooltipAt({f.x(10.0), 12.0f});
    EXPECT_TRUE(empty.startsWith("Drag across bars to make a section")) << empty;
    EXPECT_TRUE(empty.contains("to make one from the loop")) << empty;
    EXPECT_TRUE(empty.contains("J")) << "names the key: " << empty;
}

TEST(TimelineSectionStripTest, ProbePublishesTheSectionsAndTheSelection) {
    StripFixture f;
    const auto id = f.makeRepeatedSection();
    f.strip.selectPlacement(id);
    juce::String all;
    for (int i = 0; i < f.strip.getNumChildComponents(); ++i)
        if (auto* probe = dynamic_cast<synth::testmode::StateSource*>(f.strip.getChildComponent(i))) {
            auto* object = new juce::DynamicObject();
            juce::var holder(object);
            probe->addTestModeState(*object);
            all = juce::JSON::toString(holder);
        }
    EXPECT_TRUE(all.contains("Chorus:0.000:8.000:1/2")) << all;
}

//==============================================================================
// ---- The panel: the strip sits above the ruler ----

TEST(TimelineSectionStripPanelTest, StripSitsAboveTheRulerAtTheSameXAndWidth) {
    TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    auto& strip = panel.getSectionStrip();
    ASSERT_TRUE(strip.isVisible());
    EXPECT_EQ(strip.getHeight(), 24);
    EXPECT_EQ(strip.getWidth(), panel.getRuler().getWidth());
    EXPECT_EQ(strip.getX(), panel.getRuler().getX());
    EXPECT_EQ(strip.getBottom(), panel.getRuler().getY());
}

TEST(TimelineSectionStripPanelTest, TurningTheSettingOffRemovesTheStripAndGivesTheRowsBack) {
    TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    const int rulerY = panel.getRuler().getY();
    juce::ApplicationProperties appProps;
    juce::PropertiesFile::Options options;
    options.applicationName = "SectionStripPanelTest";
    options.filenameSuffix = ".settings";
    options.folderName =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sectionstrip").getFullPathName();
    appProps.setStorageParameters(options);
    panel.setApplicationProperties(&appProps);
    appProps.getUserSettings()->setValue(kTimelineShowSectionsStripKey, false);
    panel.refreshSectionsStripSetting();
    EXPECT_FALSE(panel.getSectionStrip().isVisible());
    EXPECT_EQ(panel.getRuler().getY(), rulerY - 24);
    appProps.getUserSettings()->setValue(kTimelineShowSectionsStripKey, true);
    panel.refreshSectionsStripSetting();
    EXPECT_TRUE(panel.getSectionStrip().isVisible());
    EXPECT_EQ(panel.getRuler().getY(), rulerY);
    panel.setApplicationProperties(nullptr);
}

//==============================================================================
// ---- The clip lane: follows-mark, tooltip, accessibility, range verb ----

namespace {
struct LaneFixture {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    TimelineClipLaneArea lane{state, selection};
    TrackId track;
    ClipId clip;
    double startedAt = -1.0;

    LaneFixture() {
        state.pixelsPerBeat = 40.0;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1200, 400);
        track = doc.addTrack(TrackKind::Midi, "Lead");
        clip = doc.addClip(track, 0.0, 4.0, "A");
        lane.onSectionMade = [this](double b) { startedAt = b; };
    }
};
} // namespace

TEST(TimelineSectionStripClipLaneTest, AClipInARepeatedSectionFollowsItAndSaysSo) {
    LaneFixture f;
    EXPECT_EQ(f.lane.sectionFollowOf(*f.doc.getClip(f.clip)).plays, 0);
    const auto id = f.doc.makeSection(0.0, 8.0, "Chorus", 0xff56D4C0, 0.5);
    EXPECT_EQ(f.lane.sectionFollowOf(*f.doc.getClip(f.clip)).plays, 0) << "a lone placement follows nothing";
    f.doc.placeSection(id, 16.0, SectionDropMode::MakeRoom, 0.5);
    const auto follow = f.lane.sectionFollowOf(*f.doc.getClip(f.clip));
    EXPECT_EQ(follow.plays, 2);
    EXPECT_EQ(follow.name, "Chorus");
    const auto text = describeClipForAccessibility(*f.doc.getClip(f.clip), *f.doc.getTrack(f.track), 4.0, 0, "Chorus");
    EXPECT_TRUE(text.endsWith(", follows Chorus")) << text;
}

TEST(TimelineSectionStripClipLaneTest, RangeMenuOffersMakeSectionAfterDeleteAndCloseGap) {
    LaneFixture f;
    f.lane.setActiveTool(EditTool::Range);
    const auto from = juce::Point<float>((float)f.state.beatToX(0.0), 20.0f);
    const auto to = juce::Point<float>((float)f.state.beatToX(8.0), 20.0f);
    auto ev = [&](juce::Point<float> p, bool dragged) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, &f.lane, &f.lane, juce::Time::getCurrentTime(), from,
                                juce::Time::getCurrentTime(), 1, dragged);
    };
    f.lane.mouseDown(ev(from, false));
    f.lane.mouseDrag(ev(to, true));
    f.lane.mouseUp(ev(to, true));
    auto menu = f.lane.buildRangeContextMenu();
    juce::StringArray names;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        names.add(it.getItem().text);
    const int gap = names.indexOf("Delete range and close gap");
    ASSERT_GE(gap, 0);
    EXPECT_EQ(names[gap + 1], "Make section");

    EXPECT_TRUE(f.lane.applyRangeChoice(TimelineClipLaneArea::RangeChoice::MakeSection));
    ASSERT_EQ(f.doc.getSections().size(), 1u);
    EXPECT_DOUBLE_EQ(f.startedAt, 0.0);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_TRUE(f.doc.getSections().empty());
}

TEST(TimelineSectionStripClipLaneTest, MakeSectionIsARebindableActionDefaultingToShiftCmdJ) {
    ShortcutManager manager;
    EXPECT_EQ(ShortcutManager::getCategory("timelineMakeSection"), ShortcutCategory::Timeline);
    EXPECT_TRUE(ShortcutManager::getActionDescription("timelineMakeSection").isNotEmpty());
    const juce::KeyPress optCmdS('j', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    EXPECT_EQ(manager.getBinding("timelineMakeSection"), optCmdS);
    EXPECT_TRUE(manager.getConflictingAction("timelineMakeSection", optCmdS).isEmpty());
}
