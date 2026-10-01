// AutomationLanesAddRowTests.cpp -- adding a lane from the timeline: the "+ Add automation..." row that
// closes an open track's lanes (and the Unassigned section's), and the track header's "Add automation..."
// menu entry. Real events on the real panel's children against a stub host; the MainComponent side
// (what a track offers, where the lane lands) is in AutomationLanesAddMainTests.cpp.

#include "AutomationLanesTestFixture.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationRow.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"

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

TEST(AutomationLanesAddRowTest, TheRowExistsOnlyUnderAnOpenTrackAndSitsAtTheLayoutsY) {
    AddFixture f;
    f.addLane(f.lead, "other");
    ASSERT_NE(f.row(), nullptr);
    EXPECT_EQ(f.panel.addAutomationRowForTest(f.lead), nullptr) << "Lead is folded";

    const auto layout = f.panel.getClipLaneArea().getRowLayout();
    EXPECT_EQ(layout.trackExtraHeight(0), 40 + AddAutomationRow::kBaseHeight) << "one lane row plus the add row";
    const int firstLaneTop = layout.trackTop(0) + layout.trackRowHeight(0);
    EXPECT_EQ(f.row()->getY(), firstLaneTop + 40) << "right under the last lane, in the header column";
    EXPECT_EQ(f.row()->getHeight(), AddAutomationRow::kBaseHeight);
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getY(), layout.trackTop(1)) << "Lead starts after the add row";
    EXPECT_EQ(layout.trackTop(1), layout.trackTop(0) + layout.trackRowHeight(0) + 40 + AddAutomationRow::kBaseHeight);

    f.panel.setTrackAutomationExpanded(f.lead, true);
    ASSERT_NE(f.panel.addAutomationRowForTest(f.lead), nullptr);
    f.panel.setTrackAutomationExpanded(f.bass, false);
    EXPECT_EQ(f.row(), nullptr) << "folding removes it";
}

TEST(AutomationLanesAddRowTest, ATrackWithNoLanesShowsNoRowEvenWhenOpened) {
    AddFixture f;
    f.panel.setTrackAutomationExpanded(f.lead, true);
    EXPECT_EQ(f.panel.addAutomationRowForTest(f.lead), nullptr) << "Lead has no lane: its menu is the way in";
}

TEST(AutomationLanesAddRowTest, TheRowScalesWithRowZoom) {
    AddFixture f;
    auto& clips = f.panel.getClipLaneArea();
    const juce::Point<float> anchor((float)clips.getX() + 100.0f, (float)clips.getY() + 20.0f);
    f.panel.mouseMagnify(
        makeTimelineMouseEvent(f.panel, anchor, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), false, anchor),
        1.5f);
    const double scale = f.panel.getViewState().rowHeightScale;
    ASSERT_GT(scale, 1.0);
    EXPECT_EQ(f.row()->getHeight(), (int)std::llround(AddAutomationRow::kBaseHeight * scale));
    EXPECT_EQ(f.panel.getClipLaneArea().getRowLayout().trackExtraHeight(0),
              f.panel.laneRowBoundsForTest(f.bassLane).getHeight() + f.row()->getHeight());
}

TEST(AutomationLanesAddRowTest, AClickAtTheRowsYOpensThePickerOfThatTracksParameters) {
    AddFixture f;
    const auto bounds = f.panel.addAutomationRowBoundsForTest(f.bass);
    ASSERT_EQ(f.componentAt({bounds.getX() + 60, bounds.getCentreY()}), f.row()) << "a real click lands on the row";

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
    EXPECT_EQ(f.row()->getY(), f.panel.laneHeaderForTest(lane.id)->getBottom()) << "the row moved below the new lane";

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
    auto* header = f.panel.getTrackHeaderAt(1); // Lead: no lanes, so no fold arrow
    ASSERT_NE(header, nullptr);
    EXPECT_FALSE(header->getFoldArrow().isVisible());

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
