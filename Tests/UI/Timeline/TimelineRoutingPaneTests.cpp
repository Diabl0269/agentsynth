// TimelineRoutingPaneTests.cpp: the Timeline tab's side pane (docs/timeline/tracks.md#routing-from-the-side-pane) --
// what it shows for the selected track, that it follows selection and document changes, and that its rows reach the
// same host calls as the track header. Bare-panel tests cover the document-only states; the rest use a real
// MainComponent as the TrackHeaderHost.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanel/TimelinePanelTestFixture.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineRoutingPane/TimelineRoutingPane.h"
#include <gtest/gtest.h>

namespace {

using synth::TrackKind;
using synth::ui::TimelinePanelComponent;
using synth::ui::TimelineRoutingPane;
using synth::ui::TimelineTrackHeaderComponent;

// A click on the row's own background: selects the track (the panel's click-to-select) without touching a child.
void clickRow(TimelinePanelComponent& panel, int index) {
    auto* row = panel.getTrackHeaderAt(index);
    ASSERT_NE(row, nullptr);
    row->mouseDown(makeClickEvent(*row, {110.0f, 3.0f}));
}

// The id of the menu item with this text, or 0.
int menuItemId(juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == text)
            return it.getItem().itemID;
    return 0;
}

// A bare panel over a document, no host: enough for everything the pane derives from the document alone.
struct BarePanel {
    BottomDockActiveTabResetGuardMDT resetGuard;
    synth::TimelineDoc doc; // declared before the panel: the panel removes its listener in its destructor
    TimelinePanelComponent panel;

    BarePanel() {
        panel.setSize(1200, 400);
        panel.setTimelineDoc(&doc);
        panel.getSidePane().setOpen(true);
    }
    TimelineRoutingPane& pane() { return panel.getRoutingPane(); }
};

// A real app: MainComponent as the host, one Poly MIDI on the canvas so a new MIDI track auto-wires unambiguously.
class RoutingPaneAppTest : public TimelineAppWiringTest {
protected:
    void SetUp() override {
        TimelineAppWiringTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProviderTL>());
        mc->setSize(1600, 900);
        quiesceEngine(*mc);
        prepareCanvas(*mc, 1);
        mc->getTimelinePanel().getSidePane().setOpen(true);
    }
    void TearDown() override {
        mc.reset();
        TimelineAppWiringTest::TearDown();
    }
    TimelinePanelComponent& panel() { return mc->getTimelinePanel(); }
    TimelineRoutingPane& pane() { return panel().getRoutingPane(); }

    BottomDockActiveTabResetGuardMDT resetGuard;
    std::unique_ptr<MainComponent> mc;
};

} // namespace

// ---- Document-only states ------------------------------------------------------------------

TEST(TimelineRoutingPaneTest, TheTimelinePaneStartsClosedAndTheDockToggleReachesIt) {
    BottomDockActiveTabResetGuardMDT guard;
    TimelinePanelComponent panel;
    EXPECT_FALSE(panel.getSidePane().isOpen()) << "opt-in, unlike the Mixer's pane";
    EXPECT_EQ(panel.getSidePane().getOccupiedWidth(), 0);
    EXPECT_TRUE(panel.getSidePane().hasContent());

    EXPECT_TRUE(panel.toggleSidePane());
    EXPECT_TRUE(panel.getSidePane().isOpen());
    EXPECT_EQ(panel.getSidePane().getOccupiedWidth(), synth::ui::SidePane::kDefaultWidth);
    EXPECT_TRUE(panel.toggleSidePane());
    EXPECT_FALSE(panel.getSidePane().isOpen());
}

TEST(TimelineRoutingPaneTest, NothingSelectedShowsTheSelectATrackLine) {
    BarePanel f;
    f.doc.addTrack(TrackKind::Midi, "Lead");
    EXPECT_EQ(f.pane().getEmptyLineForTest(), "Select a MIDI or audio track to see its routing.");
    EXPECT_FALSE(f.pane().getPlaysIntoButtonForTest().isVisible());
    EXPECT_FALSE(f.pane().isNotesGoToShownForTest());
}

TEST(TimelineRoutingPaneTest, AnAutomationTrackShowsTheNothingToRouteLine) {
    BarePanel f;
    f.doc.addTrack(TrackKind::Automation, "Filter");
    clickRow(f.panel, 0);
    EXPECT_EQ(f.pane().getEmptyLineForTest(), "Nothing to route on an automation track.");
    EXPECT_FALSE(f.pane().getPlaysIntoButtonForTest().isVisible());
    EXPECT_FALSE(f.pane().getShowInMixerLinkForTest().isVisible());
}

TEST(TimelineRoutingPaneTest, AnUnboundTrackSaysNotConnectedInTheWarningState) {
    BarePanel f;
    f.doc.addTrack(TrackKind::Midi, "Lead");
    clickRow(f.panel, 0);
    EXPECT_EQ(f.pane().getHeaderNameForTest(), "Lead");
    EXPECT_EQ(f.pane().getKindBadgeForTest(), "MIDI");
    EXPECT_EQ(f.pane().getPlaysIntoTextForTest(), "Not connected");
    EXPECT_TRUE(f.pane().isPlaysIntoWarningForTest());
    EXPECT_TRUE(f.pane().getMissingNoteForTest().isEmpty());
    EXPECT_FALSE(f.pane().getShowOnCanvasLinkForTest().isVisible()) << "nothing to show";
    EXPECT_EQ(f.pane().getNotesGoToTextForTest(), "None");
    EXPECT_EQ(f.pane().getChannelTextForTest(), "No mixer channel");
}

// A short bottom panel must not clip the lower sections: the pane scrolls inside the side pane instead.
TEST(TimelineRoutingPaneTest, AShortPaneScrollsToTheLowerSectionsInsteadOfClippingThem) {
    BarePanel f;
    f.doc.addTrack(TrackKind::Midi, "Lead");
    clickRow(f.panel, 0);
    f.panel.setSize(1200, 140);

    auto& shown = f.pane().getPaneComponent();
    ASSERT_NE(&shown, static_cast<juce::Component*>(&f.pane())) << "the side pane hosts a scroller";
    auto* viewport = f.pane().findParentComponentOfClass<juce::Viewport>();
    ASSERT_NE(viewport, nullptr);
    ASSERT_GT(f.pane().getContentHeight(), viewport->getHeight()) << "the sections need more than the panel gives";
    EXPECT_EQ(f.pane().getHeight(), f.pane().getContentHeight()) << "the pane is as tall as its sections";
    EXPECT_TRUE(viewport->canScrollVertically());
    viewport->setViewPosition(0, f.pane().getHeight());
    EXPECT_GT(viewport->getViewPositionY(), 0) << "Sound goes to is reachable";
}

TEST(TimelineRoutingPaneTest, AMissingBindingIsNamedAsMissingAndKeepsItsNotes) {
    BarePanel f;
    const auto id = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.doc.setTrackBinding(id, "uuid-gone");
    f.doc.reconcileBindings([](const juce::String&) { return false; });
    clickRow(f.panel, 0);
    EXPECT_EQ(f.pane().getPlaysIntoTextForTest(), "Track In (missing)");
    EXPECT_TRUE(f.pane().isPlaysIntoWarningForTest());
    EXPECT_EQ(f.pane().getMissingNoteForTest(), "Its notes are kept. Pick a new Track In to hear them again.");
    EXPECT_FALSE(f.pane().getShowOnCanvasLinkForTest().isVisible()) << "the node is gone";
}

TEST(TimelineRoutingPaneTest, AnAudioTrackHidesNotesGoTo) {
    BarePanel f;
    f.doc.addTrack(TrackKind::Audio, "Vox");
    clickRow(f.panel, 0);
    EXPECT_EQ(f.pane().getKindBadgeForTest(), "Audio");
    EXPECT_TRUE(f.pane().getPlaysIntoButtonForTest().isVisible());
    EXPECT_FALSE(f.pane().isNotesGoToShownForTest());
}

TEST(TimelineRoutingPaneTest, ThePaneFollowsTheSelectionAndTheDocument) {
    BarePanel f;
    const auto first = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.doc.addTrack(TrackKind::Audio, "Vox");
    clickRow(f.panel, 0);
    EXPECT_EQ(f.pane().getHeaderNameForTest(), "Lead");
    clickRow(f.panel, 1);
    EXPECT_EQ(f.pane().getHeaderNameForTest(), "Vox");
    EXPECT_EQ(f.pane().getKindBadgeForTest(), "Audio");

    clickRow(f.panel, 0);
    f.doc.setTrackName(first, "Bass");
    EXPECT_EQ(f.pane().getHeaderNameForTest(), "Bass") << "a rename shows at once, with no timer";

    f.doc.removeTrack(first);
    EXPECT_EQ(f.pane().getEmptyLineForTest(), "Select a MIDI or audio track to see its routing.")
        << "the selected track is gone";
}

TEST(TimelineRoutingPaneTest, AClosedPaneCatchesUpWhenItOpens) {
    BarePanel f;
    f.doc.addTrack(TrackKind::Midi, "Lead");
    f.panel.getSidePane().setOpen(false);
    clickRow(f.panel, 0);
    f.panel.getSidePane().setOpen(true);
    EXPECT_EQ(f.pane().getHeaderNameForTest(), "Lead");
}

// ---- With a real host ------------------------------------------------------------------------

TEST_F(RoutingPaneAppTest, AMidiTrackShowsItsTrackInItsDestinationsAndItsChannel) {
    mc->simulateAddInstrumentTrackClick(TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    ASSERT_EQ(panel().getTrackHeaderCount(), 1);
    clickRow(panel(), 0);

    auto& p = pane();
    EXPECT_EQ(p.getKindBadgeForTest(), "MIDI");
    EXPECT_EQ(p.getPlaysIntoTextForTest(), panel().getTrackHeaderAt(0)->getBindingChipText());
    EXPECT_FALSE(p.isPlaysIntoWarningForTest());
    EXPECT_TRUE(p.getPlaysIntoTextForTest().startsWith("Track In"));
    EXPECT_TRUE(p.getShowOnCanvasLinkForTest().isVisible());

    ASSERT_TRUE(p.isNotesGoToShownForTest());
    EXPECT_TRUE(p.getNotesGoToTextForTest().contains("Oscillator")) << p.getNotesGoToTextForTest();

    EXPECT_TRUE(p.getChannelTextForTest().startsWith("Channel")) << p.getChannelTextForTest();
    EXPECT_TRUE(pane().getChannelChipForTest().isVisible());
    EXPECT_TRUE(pane().getShowInMixerLinkForTest().isVisible());
}

TEST_F(RoutingPaneAppTest, AnAudioTrackHasNoNotesGoToRow) {
    mc->simulateAddAudioTrackClick();
    ASSERT_EQ(panel().getTrackHeaderCount(), 1);
    clickRow(panel(), 0);
    EXPECT_EQ(pane().getKindBadgeForTest(), "Audio");
    EXPECT_FALSE(pane().isNotesGoToShownForTest());
    EXPECT_FALSE(pane().getPlaysIntoTextForTest().isEmpty());
    EXPECT_FALSE(pane().isPlaysIntoWarningForTest());
}

TEST_F(RoutingPaneAppTest, TheNotesGoToButtonOpensThePickerAndTheRowFollowsTheGraph) {
    mc->simulateAddInstrumentTrackClick(TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    clickRow(panel(), 0);
    ASSERT_TRUE(pane().getNotesGoToTextForTest().contains("Oscillator"));

    int opened = 0;
    pane().setOpenMidiDestinationsHookForTest([&opened] { ++opened; });
    pane().getNotesGoToButtonForTest().onClick();
    EXPECT_EQ(opened, 1);

    // The picker is the header's own: switching the Oscillator off in it changes the graph, and the row follows (the
    // ADSR stays).
    auto picker = pane().createMidiDestinationPickerForTest();
    ASSERT_NE(picker, nullptr);
    const auto rows = picker->getVisibleRowNamesForTest();
    ASSERT_FALSE(rows.empty());
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<size_t>(i)].contains("Oscillator"))
            picker->toggleRowForTest(i);
    EXPECT_FALSE(pane().getNotesGoToTextForTest().contains("Oscillator")) << pane().getNotesGoToTextForTest();
}

TEST_F(RoutingPaneAppTest, RebindingFromThePlaysIntoMenuChangesTheBindingInOneUndoStep) {
    mc->simulateAddMidiTrackClick();
    clickRow(panel(), 0);
    auto& doc = mc->getTimelineDoc();
    const auto original = doc.getTracks()[0].bindingUuid;
    ASSERT_TRUE(original.isNotEmpty());
    mc->getUndoManager().clearUndoHistory();

    // The real button path: a click builds the menu; the hook stands in for showing it.
    juce::String seenItems;
    int newNodeId = 0;
    pane().setShowBindingMenuHookForTest([&](juce::PopupMenu& menu) {
        newNodeId = menuItemId(menu, "New Track In node");
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            seenItems += it.getItem().text + "|";
    });
    pane().getPlaysIntoButtonForTest().onClick();
    ASSERT_NE(newNodeId, 0) << seenItems;
    EXPECT_FALSE(seenItems.contains("MIDI destinations")) << "the Notes go to row owns that";

    pane().applyBindingMenuChoice(newNodeId);
    const auto rebound = doc.getTracks()[0].bindingUuid;
    EXPECT_NE(rebound, original);
    EXPECT_FALSE(doc.getTracks()[0].orphaned);
    EXPECT_FALSE(pane().isPlaysIntoWarningForTest());

    ASSERT_TRUE(mc->getUndoManager().canUndo());
    mc->getUndoManager().undo();
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, original);
    EXPECT_FALSE(mc->getUndoManager().canUndo()) << "the rebind was exactly one undo step";
}

TEST_F(RoutingPaneAppTest, ThePlaysIntoMenuTicksTheCurrentNodeAndOffersOnlyThisKindOfNode) {
    mc->simulateAddMidiTrackClick();
    clickRow(panel(), 0);
    const auto expected = panel().getTrackHeaderAt(0)->collectBindingOptions();
    ASSERT_EQ(expected.size(), 1u);

    int tickedItems = 0;
    int nodeItems = 0;
    pane().setShowBindingMenuHookForTest([&](juce::PopupMenu& menu) {
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next()) {
            if (it.getItem().itemID >= 1 && it.getItem().itemID <= static_cast<int>(expected.size())) {
                ++nodeItems;
                tickedItems += it.getItem().isTicked ? 1 : 0;
            }
        }
    });
    pane().getPlaysIntoButtonForTest().onClick();
    EXPECT_EQ(nodeItems, 1);
    EXPECT_EQ(tickedItems, 1);
}

TEST_F(RoutingPaneAppTest, ShowOnCanvasSelectsTheBoundNode) {
    mc->simulateAddMidiTrackClick();
    clickRow(panel(), 0);
    const int before = mc->getGraphEditor().getSelectionCount();
    ASSERT_TRUE(pane().getShowOnCanvasLinkForTest().isVisible());
    pane().getShowOnCanvasLinkForTest().onClick();
    EXPECT_EQ(mc->getGraphEditor().getSelectionCount(), 1) << "was " << before;
}

TEST_F(RoutingPaneAppTest, ShowInMixerRevealsTheChannelInTheMixerTab) {
    mc->simulateAddInstrumentTrackClick(TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    mc->getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    clickRow(panel(), 0);
    ASSERT_TRUE(pane().getShowInMixerLinkForTest().isVisible());

    pane().getShowInMixerLinkForTest().onClick();
    EXPECT_EQ(mc->getBottomDock().getActiveTab(), synth::ui::BottomDockComponent::Tab::Mixer);
}

TEST_F(RoutingPaneAppTest, TheDockShortcutTogglesTheTimelinePaneOnTheTimelineTab) {
    auto& dock = mc->getBottomDock();
    dock.setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    EXPECT_TRUE(dock.hasActiveSidePane());
    ASSERT_TRUE(panel().getSidePane().isOpen());
    EXPECT_TRUE(dock.toggleActiveSidePane());
    EXPECT_FALSE(panel().getSidePane().isOpen());
    EXPECT_TRUE(dock.toggleActiveSidePane(/*forceOpen=*/true));
    EXPECT_TRUE(panel().getSidePane().isOpen());
}

TEST_F(RoutingPaneAppTest, DeletingTheBoundNodeShowsMissingWithoutAnyPaneSpecificWiring) {
    mc->simulateAddMidiTrackClick();
    clickRow(panel(), 0);
    auto& doc = mc->getTimelineDoc();
    const auto uuid = doc.getTracks()[0].bindingUuid;
    for (auto* node : mc->getAudioEngine().getGraph().getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            mc->getGraphEditor().requestDeleteModule(node->nodeID);

    EXPECT_EQ(pane().getPlaysIntoTextForTest(), "Track In (missing)");
    EXPECT_TRUE(pane().isPlaysIntoWarningForTest());
}
