// AutomationLanesAddRowTests.cpp -- adding a lane from the timeline: the "+ Add automation..." row that
// closes an open track's lanes (and the Unassigned section's), and the track header's "Add automation..."
// menu entry. Real events on the real panel's children against a stub host; the MainComponent side
// (what a track offers, where the lane lands) is in AutomationLanesAddMainTests.cpp.

#include "AutomationLanesTestFixture.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationRow.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AddAutomationRow;
using synth::ui::ModMatrixPicker;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

// Offers a fixed parameter list per track and creates lanes the way MainComponent does (one undo step, on the
// track it is given), recording what it was asked.
struct AddHost : TrackHeaderHost {
    synth::TimelineDoc& doc;
    AppUndoManager& undo;
    std::vector<AutomatableParameter> offered;
    std::vector<synth::TrackId> asked;

    AddHost(synth::TimelineDoc& d, AppUndoManager& u)
        : doc(d)
        , undo(u) {}

    static AutomatableParameter param(const juce::String& module, const juce::String& name, const juce::String& id) {
        AutomatableParameter p;
        p.nodeUuid = "node-" + module;
        p.paramId = id;
        p.moduleTitle = module;
        p.parameterName = name;
        return p;
    }

    std::vector<AutomatableParameter> getAutomatableParameters(synth::TrackId track) override {
        asked.push_back(track);
        return offered;
    }
    synth::LaneId addAutomationLane(synth::TrackId track, const AutomatableParameter& p) override {
        synth::LaneId lane;
        synth::AutomationLane::RangeSnapshot range;
        range.maxValue = 100.0f;
        undo.recordTimelineChange(doc, [&] { lane = doc.addLane(track, p.nodeUuid, p.paramId, range); });
        return lane;
    }

    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

// Bass (one open lane) over Lead (no lanes), with a host that offers two modules' parameters and a picker
// hook that keeps whatever picker the panel would have launched.
struct AddFixture : LanesPanel {
    AddHost host{doc, undo};
    synth::TrackId bass, lead;
    synth::LaneId bassLane;
    std::unique_ptr<ModMatrixPicker> picker;

    AddFixture() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lead = doc.addTrack(TrackKind::Midi, "Lead");
        bassLane = addLane(bass, "cutoff");
        host.offered = {AddHost::param("Filter 1", "Cutoff", "cutoff"), AddHost::param("Filter 1", "Resonance", "res"),
                        AddHost::param("Oscillator 1", "Detune", "detune")};
        host.offered[0].nodeUuid = "node-cutoff"; // the uuid LanesPanel::addLane() bound Bass's lane to
        panel.setTrackHeaderHost(&host);
        panel.setAddAutomationPickerHookForTest([this](std::unique_ptr<ModMatrixPicker> p) { picker = std::move(p); });
        panel.setTrackAutomationExpanded(bass, true);
    }
    ~AddFixture() { panel.setTrackHeaderHost(nullptr); } // the host dies first

    AddAutomationRow* row() { return panel.addAutomationRowForTest(bass); }
};

} // namespace

TEST(AutomationLanesAddRowTest, TheButtonExistsOnlyUnderAnOpenTrackAndSitsInTheLastLanesGutter) {
    AddFixture f;
    f.addLane(f.lead, "other");
    ASSERT_NE(f.row(), nullptr);
    EXPECT_EQ(f.panel.addAutomationRowForTest(f.lead), nullptr) << "Lead is folded";

    const auto layout = f.panel.getClipLaneArea().getRowLayout();
    EXPECT_EQ(layout.trackExtraHeight(0), 40) << "a track with a lane adds no row for the button";
    const int firstLaneTop = layout.trackTop(0) + layout.trackRowHeight(0);
    ASSERT_TRUE(f.row()->isCompact());
    const auto* header = f.panel.laneHeaderForTest(f.bassLane);
    ASSERT_NE(header, nullptr);
    EXPECT_GE(f.row()->getY(), firstLaneTop) << "inside the last lane's own row";
    EXPECT_LE(f.row()->getBottom(), firstLaneTop + 40);
    EXPECT_LE(f.row()->getRight(), synth::ui::AutomationLaneHeaderComponent::kIndent)
        << "in the empty gutter left of the lane's colour stripe";
    EXPECT_EQ(f.row()->getHeight(), AddAutomationRow::kCompactSize);
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getY(), layout.trackTop(1)) << "Lead starts right after the lane";
    EXPECT_EQ(layout.trackTop(1), layout.trackTop(0) + layout.trackRowHeight(0) + 40);
    EXPECT_EQ(f.componentAt(f.panel.addAutomationRowBoundsForTest(f.bass).getCentre()), f.row())
        << "a real click on the button lands on it, not on the lane header under it";

    f.panel.setTrackAutomationExpanded(f.lead, true);
    ASSERT_NE(f.panel.addAutomationRowForTest(f.lead), nullptr);
    f.panel.setTrackAutomationExpanded(f.bass, false);
    EXPECT_EQ(f.row(), nullptr) << "folding removes it";
}

TEST(AutomationLanesAddRowTest, ATrackWithNoLanesOpensToJustTheAddRow) {
    AddFixture f;
    auto* header = f.panel.getTrackHeaderAt(1); // Lead: no lanes
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->getFoldArrow().isVisible()) << "every track has the arrow";
    EXPECT_EQ(f.panel.addAutomationRowForTest(f.lead), nullptr) << "folded: nothing under it";

    clickButton(header->getFoldArrow());

    ASSERT_TRUE(f.panel.isTrackAutomationExpandedForTest(f.lead));
    auto* row = f.panel.addAutomationRowForTest(f.lead);
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->isCompact()) << "with no lane yet the button is a whole row";
    const auto layout = f.panel.getClipLaneArea().getRowLayout();
    EXPECT_EQ(layout.trackExtraHeight(1), AddAutomationRow::kBaseHeight) << "the add row is the whole fold-out";
    EXPECT_EQ(row->getY(), layout.trackTop(1) + layout.trackRowHeight(1));
    const auto bounds = f.panel.addAutomationRowBoundsForTest(f.lead);
    EXPECT_EQ(f.componentAt({bounds.getX() + 60, bounds.getCentreY()}), row) << "a real click lands on the row";

    clickButton(*row);
    ASSERT_NE(f.picker, nullptr);
    EXPECT_EQ(f.host.asked.back(), f.lead) << "the picker lists Lead's parameters";
    f.picker->chooseVisibleItemForTest(0);
    const auto* lead = f.doc.getTrack(f.lead);
    ASSERT_EQ(lead->lanes.size(), 1u) << "its first lane, made from the timeline";
    EXPECT_NE(f.panel.laneEditorForTest(lead->lanes.front().id), nullptr) << "and shown under the track";
}

TEST(AutomationLanesAddRowTest, ReturnAndSpaceOnAnEmptyTracksArrowFoldIt) {
    AddFixture f;
    auto& arrow = f.panel.getTrackHeaderAt(1)->getFoldArrow();
    EXPECT_TRUE(arrow.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(f.panel.isTrackAutomationExpandedForTest(f.lead));
    EXPECT_EQ(arrow.getTitle(), "Hide Lead automation");
    EXPECT_TRUE(arrow.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_FALSE(f.panel.isTrackAutomationExpandedForTest(f.lead));
    EXPECT_EQ(f.panel.addAutomationRowForTest(f.lead), nullptr);
}

TEST(AutomationLanesAddRowTest, AnEmptyTracksRowScalesWithRowZoom) {
    AddFixture f;
    f.panel.setTrackAutomationExpanded(f.lead, true);
    auto& clips = f.panel.getClipLaneArea();
    const juce::Point<float> anchor((float)clips.getX() + 100.0f, (float)clips.getY() + 20.0f);
    f.panel.mouseMagnify(
        makeTimelineMouseEvent(f.panel, anchor, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), false, anchor),
        1.5f);
    const double scale = f.panel.getViewState().rowHeightScale;
    ASSERT_GT(scale, 1.0);
    auto* leadRow = f.panel.addAutomationRowForTest(f.lead);
    ASSERT_NE(leadRow, nullptr);
    EXPECT_EQ(leadRow->getHeight(), (int)std::llround(AddAutomationRow::kBaseHeight * scale));
    EXPECT_EQ(f.panel.getClipLaneArea().getRowLayout().trackExtraHeight(1), leadRow->getHeight());
    EXPECT_EQ(f.panel.getClipLaneArea().getRowLayout().trackExtraHeight(0),
              f.panel.laneRowBoundsForTest(f.bassLane).getHeight())
        << "a track with a lane adds only its lane rows";
}

TEST(AutomationLanesAddRowTest, AClickOnTheButtonOpensThePickerOfThatTracksParameters) {
    AddFixture f;
    const auto bounds = f.panel.addAutomationRowBoundsForTest(f.bass);
    ASSERT_EQ(f.componentAt(bounds.getCentre()), f.row()) << "a real click lands on the button";

    clickButton(*f.row());

    ASSERT_NE(f.picker, nullptr);
    EXPECT_EQ(f.host.asked.back(), f.bass) << "the picker lists Bass's parameters";
    // "cutoff" already has a lane, so it is not offered; rows are grouped by module.
    EXPECT_EQ(f.picker->getVisibleRowNamesForTest(),
              (std::vector<juce::String>{"Filter 1", "Resonance", "Oscillator 1", "Detune"}));
    f.picker->setSearchTextForTest("osc det");
    EXPECT_EQ(f.picker->getVisibleItemTextsForTest(), (std::vector<juce::String>{"Detune"}))
        << "the module title is searchable, word by word";
}

TEST(AutomationLanesAddRowTest, ReturnAndSpaceOpenThePickerAndTheRowIsANamedTabStop) {
    AddFixture f;
    auto& row = *f.row();
    EXPECT_TRUE(row.getWantsKeyboardFocus()) << "a Tab stop";
    EXPECT_EQ(row.getTitle(), "Add automation to Bass");
    EXPECT_EQ(row.getTooltip(), "Add automation to Bass");

    EXPECT_TRUE(row.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_NE(f.picker, nullptr);
    f.picker.reset();
    EXPECT_TRUE(row.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_NE(f.picker, nullptr);

    f.doc.setTrackName(f.bass, "Sub");
    EXPECT_EQ(row.getTitle(), "Add automation to Sub") << "a rename reaches the name";
}

TEST(AutomationLanesAddRowTest, PickingCreatesTheLaneOnThatTrackInOneUndoStepAndShowsIt) {
    AddFixture f;
    clickButton(*f.row());
    ASSERT_NE(f.picker, nullptr);
    f.picker->chooseVisibleItemForTest(0); // Filter 1 / Resonance: Cutoff already has Bass's lane

    const auto* bass = f.doc.getTrack(f.bass);
    ASSERT_EQ(bass->lanes.size(), 2u);
    const auto lane = bass->lanes.back();
    EXPECT_EQ(lane.paramId, "res");
    EXPECT_EQ(f.panel.getSelectedAutomationLane(), lane.id) << "shown and selected";
    EXPECT_TRUE(f.panel.isTrackAutomationExpandedForTest(f.bass));
    EXPECT_NE(f.panel.laneEditorForTest(lane.id), nullptr);
    const auto* newHeader = f.panel.laneHeaderForTest(lane.id);
    EXPECT_GE(f.row()->getY(), newHeader->getY()) << "the button moved to the new last lane's gutter";
    EXPECT_LE(f.row()->getBottom(), newHeader->getBottom());

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getTrack(f.bass)->lanes.size(), 1u) << "ONE undo step removes it";
}

TEST(AutomationLanesAddRowTest, TheUnassignedSectionHasItsRowAndOffersTheAutomationTracksParameters) {
    AddFixture f;
    const auto unassigned = f.doc.addTrack(TrackKind::Automation, "Automation");
    f.addLane(unassigned, "loose");
    ASSERT_TRUE(f.panel.isTrackAutomationExpandedForTest(unassigned)) << "the section starts open";
    auto* row = f.panel.addAutomationRowForTest(unassigned);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->getTitle(), "Add automation to Automation");

    clickButton(*row);
    EXPECT_EQ(f.host.asked.back(), unassigned);
    ASSERT_NE(f.picker, nullptr);
    f.picker->chooseVisibleItemForTest(0);
    EXPECT_EQ(f.doc.getTrack(unassigned)->lanes.size(), 2u) << "the lane lands on the Automation track";
}

TEST(AutomationLanesAddRowTest, TheHeaderMenuOffersAddAutomationAndOpensThePickerForAnEmptyTrack) {
    AddFixture f;
    auto* header = f.panel.getTrackHeaderAt(1); // Lead: no lanes, folded
    ASSERT_NE(header, nullptr);

    const auto menu = header->buildContextMenu();
    const auto* item = findMenuItem(menu, "Add automation...");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);
    EXPECT_EQ(item->itemID, TimelineTrackHeaderComponent::kAddAutomationMenuId);

    header->applyContextMenuChoice(TimelineTrackHeaderComponent::kAddAutomationMenuId);
    ASSERT_NE(f.picker, nullptr);
    EXPECT_EQ(f.host.asked.back(), f.lead);
    f.picker->chooseVisibleItemForTest(0);

    const auto* lead = f.doc.getTrack(f.lead);
    ASSERT_EQ(lead->lanes.size(), 1u) << "its first lane";
    EXPECT_EQ(f.panel.getSelectedAutomationLane(), lead->lanes.front().id);
    EXPECT_TRUE(f.panel.isTrackAutomationExpandedForTest(f.lead));
    EXPECT_NE(f.panel.addAutomationRowForTest(f.lead), nullptr) << "now it has lanes, it has the row too";
}

TEST(AutomationLanesAddRowTest, TheHeaderMenuEntryIsDisabledWhenTheTrackOffersNothing) {
    AddFixture f;
    f.host.offered.clear();
    const auto menu = f.panel.getTrackHeaderAt(1)->buildContextMenu();
    const auto* item = findMenuItem(menu, "Add automation...");
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->isEnabled);
}

TEST(AutomationLanesAddRowTest, EveryHeaderRowSitsAtTheSameYAndHeightAsItsLaneRow) {
    AddFixture f;
    f.panel.setSize(1200, 600);
    const auto inPanel = [&](juce::Component* c) {
        return f.panel.getLocalArea(c->getParentComponent(), c->getBounds());
    };
    const auto* header = f.panel.laneHeaderForTest(f.bassLane);
    const auto* editor = f.panel.laneEditorForTest(f.bassLane);
    ASSERT_NE(header, nullptr);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(inPanel(const_cast<juce::Component*>(static_cast<const juce::Component*>(header))).getY(),
              inPanel(const_cast<juce::Component*>(static_cast<const juce::Component*>(editor))).getY())
        << "the lane header and its curve start at one y";
    EXPECT_EQ(header->getHeight(), editor->getHeight());

    auto* trackHeader = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(trackHeader, nullptr);
    EXPECT_EQ(inPanel(trackHeader).getY(), f.panel.getClipLaneArea().getY())
        << "the first track row starts at the clips";
    auto* second = f.panel.getTrackHeaderAt(1);
    EXPECT_EQ(inPanel(second).getY(),
              f.panel.getClipLaneArea().getY() + f.panel.getClipLaneArea().getRowLayout().trackTop(1));
}
