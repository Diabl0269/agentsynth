// AutomationLanesChangeSourceTests.cpp -- a modulator row's "Change source...": the item is there only when
// the host says it can re-point the routing, the picker leaves out the current source, and against a real
// MainComponent the lane follows the new LFO, keeps its drawn amount curve (re-keyed to the new routing's
// Attenuverter) and the whole change is ONE graph + timeline undo step.

#include "AutomationLanesAmountFixture.h"
#include "AutomationLanesMenuFixture.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

using namespace amount_test;
using synth::ui::ModulatorInfo;
using synth::ui::ModulatorRow;

namespace {

// A host that offers two LFOs and records the re-point it is asked for.
struct SourceHost : synth::ui::TrackHeaderHost {
    bool canChange = true;
    bool oneLfoOnly = false;
    int changes = 0;
    juce::String changedTo;

    bool canChangeModulatorSource(const ModulatorInfo&) override { return canChange; }
    bool changeModulatorSource(const ModulatorInfo&, const juce::String& lfoUuid) override {
        ++changes;
        changedTo = lfoUuid;
        return true;
    }
    std::vector<LfoChoice> getLfoChoices(const juce::String&, const juce::String&) override {
        LfoChoice current;
        current.uuid = "lfo-1";
        current.name = "LFO 1";
        LfoChoice other;
        other.uuid = "lfo-2";
        other.name = "LFO 2";
        return oneLfoOnly ? std::vector<LfoChoice>{current} : std::vector<LfoChoice>{current, other};
    }
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return "Filter"; }
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

ModulatorInfo lfoOne() {
    ModulatorInfo info;
    info.sourceUuid = "lfo-1";
    info.sourceTitle = "LFO 1";
    info.isLfo = true;
    info.attenuverterUuid = "atten-1";
    info.targetUuid = "filter";
    info.paramId = "cutoff";
    return info;
}

juce::StringArray menuTexts(const juce::PopupMenu& menu) {
    juce::StringArray texts;
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        texts.add(it.getItem().text);
    return texts;
}

bool findItem(const juce::PopupMenu& menu, int id, juce::PopupMenu::Item& found) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().itemID == id) {
            found = it.getItem();
            return true;
        }
    return false;
}

// Captures the picker the row would have opened; always cleared on exit.
struct SourcePickerCapture {
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    SourcePickerCapture() {
        synth::ui::test_hooks::changeSourcePickerHookForTest() = [this](auto p) { picker = std::move(p); };
    }
    ~SourcePickerCapture() { synth::ui::test_hooks::changeSourcePickerHookForTest() = nullptr; }
};
} // namespace

TEST(AutomationLanesChangeSourceTest, TheItemIsHiddenWhenTheHostSaysNo) {
    SourceHost host;
    host.canChange = false;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    const auto texts = menuTexts(row.buildMenu());
    EXPECT_FALSE(texts.contains("Change source..."));
    EXPECT_TRUE(texts.contains("Show on canvas"));
    EXPECT_TRUE(texts.contains("Remove modulator"));

    // A stale menu id from before the host changed its mind opens nothing.
    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    EXPECT_EQ(capture.picker, nullptr);
}

TEST(AutomationLanesChangeSourceTest, TheItemIsShownWhenTheHostSaysYes) {
    SourceHost host;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    juce::PopupMenu::Item item;
    ASSERT_TRUE(findItem(row.buildMenu(), ModulatorRow::kChangeSourceMenuId, item));
    EXPECT_EQ(item.text, "Change source...");
    EXPECT_TRUE(item.isEnabled);
}

TEST(AutomationLanesChangeSourceTest, TheItemIsDisabledAndSaysWhyWhenNoOtherLfoExists) {
    SourceHost host;
    host.oneLfoOnly = true;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    juce::PopupMenu::Item item;
    ASSERT_TRUE(findItem(row.buildMenu(), ModulatorRow::kChangeSourceMenuId, item));
    EXPECT_EQ(item.text, "Change source... (no other LFO)");
    EXPECT_FALSE(item.isEnabled);

    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    EXPECT_EQ(capture.picker, nullptr) << "nothing to pick from";
}

TEST(AutomationLanesChangeSourceTest, ThePickerOffersEveryLfoButTheCurrentOneAndAPickReachesTheHost) {
    SourceHost host;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);

    const auto names = capture.picker->getVisibleItemTextsForTest();
    ASSERT_EQ(names.size(), 1u) << "no New LFO and no current source";
    EXPECT_EQ(names.front(), "LFO 2");

    capture.picker->chooseVisibleItemForTest(0);
    EXPECT_EQ(host.changes, 1);
    EXPECT_EQ(host.changedTo, "lfo-2");
}

TEST(AutomationLanesChangeSourceTest, ThePickerKeepsAnLfoThatAlreadyMovesTheParameterGreyedOut) {
    auto lfos = SourceHost().getLfoChoices({}, {});
    lfos[1].movesThisParameter = true;
    const auto choices = synth::ui::collectChangeSourceChoices(lfos, "lfo-1", "Cutoff");
    ASSERT_EQ(choices.items.size(), 1u);
    EXPECT_FALSE(choices.items.front().enabled);
}

TEST_F(TimelinePanelIntegrationTest, ChangeSourceFollowsTheOtherLfoKeepsTheAmountCurveAndIsOneUndoStep) {
    AmountScene s;
    const int raw = s.channelFor("cutoff");
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    const std::vector<synth::AutomationLane::Breakpoint> drawn{{0.0, 0.25, 0.0f, kHold}, {4.0, -0.5, 0.0f, kHold}};
    s.undo().recordTimelineChange(s.doc(), [&] { synth::ui::writeAmountLane(s.doc(), s.track, s.attenUuid, drawn); });
    const auto* before = s.amountLane();
    ASSERT_NE(before, nullptr);
    const auto amountLaneId = before->id;
    const auto oldLfo = s.lfoUuid;
    const auto oldAtten = s.attenUuid;

    auto other = addNodeWithUuid(s.mc, "LFO");
    const auto otherUuid = other->properties["uuid"].toString();
    s.mc.getGraphEditor().updateComponents();
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 2u);

    // The real menu path: the row's menu offers it, then "Change source..." opens the picker.
    {
        lane_menu_test::MenuCapture menu;
        s.row()->showMenuAt(juce::PopupMenu::Options());
        ASSERT_EQ(menu.count, 1);
        EXPECT_TRUE(menu.itemTexts().contains("Change source..."));
    }
    SourcePickerCapture capture;
    s.row()->applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);
    const auto names = capture.picker->getVisibleItemTextsForTest();
    ASSERT_EQ(names.size(), 1u) << "the current source is left out";
    const auto depthBefore = s.depthOf(s.chainsInto(oldLfo, raw).front());
    capture.picker->chooseVisibleItemForTest(0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    // The lane follows the other LFO; the old routing is gone, the old LFO card stays.
    EXPECT_TRUE(s.chainsInto(oldLfo, raw).empty());
    const auto chains = s.chainsInto(otherUuid, raw);
    ASSERT_EQ(chains.size(), 1u);
    EXPECT_FLOAT_EQ(s.depthOf(chains.front()), depthBefore) << "the new routing keeps the old depth";
    EXPECT_NE(s.byUuid(oldLfo), nullptr);
    ASSERT_NE(s.row(), nullptr);
    EXPECT_EQ(s.row()->getInfo().sourceUuid, otherUuid);
    const auto newAtten = s.row()->getInfo().attenuverterUuid;
    EXPECT_NE(newAtten, oldAtten);

    // The amount lane is the same lane, re-keyed, with its points untouched.
    EXPECT_EQ(synth::ui::amountLaneFor(s.doc(), oldAtten), nullptr);
    const auto* after = synth::ui::amountLaneFor(s.doc(), newAtten);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->id, amountLaneId);
    ASSERT_EQ(after->points.size(), drawn.size());
    for (size_t i = 0; i < drawn.size(); ++i) {
        EXPECT_DOUBLE_EQ(after->points[i].beat, drawn[i].beat);
        EXPECT_DOUBLE_EQ(after->points[i].value, drawn[i].value);
    }

    // ONE undo restores the old source and the lane's old key; one redo re-applies both.
    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.chainsInto(oldLfo, raw).size(), 1u);
    EXPECT_TRUE(s.chainsInto(otherUuid, raw).empty());
    ASSERT_NE(s.row(), nullptr);
    EXPECT_EQ(s.row()->getInfo().sourceUuid, oldLfo);
    const auto* restored = synth::ui::amountLaneFor(s.doc(), s.row()->getInfo().attenuverterUuid);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->points.size(), drawn.size());
    EXPECT_EQ(synth::ui::amountLaneFor(s.doc(), newAtten), nullptr);

    ASSERT_TRUE(s.undo().redo());
    EXPECT_EQ(s.chainsInto(otherUuid, raw).size(), 1u);
    EXPECT_TRUE(s.chainsInto(oldLfo, raw).empty());
    ASSERT_NE(s.row(), nullptr);
    EXPECT_EQ(s.row()->getInfo().sourceUuid, otherUuid);
    const auto* redone = synth::ui::amountLaneFor(s.doc(), s.row()->getInfo().attenuverterUuid);
    ASSERT_NE(redone, nullptr);
    EXPECT_EQ(redone->points.size(), drawn.size());
}

TEST_F(TimelinePanelIntegrationTest, ChangeSourceWithNoAmountLaneStillMovesTheRoutingInOneUndoStep) {
    AmountScene s;
    const int raw = s.channelFor("cutoff");
    const auto oldLfo = s.lfoUuid;
    auto other = addNodeWithUuid(s.mc, "LFO");
    const auto otherUuid = other->properties["uuid"].toString();
    s.mc.getGraphEditor().updateComponents();
    ASSERT_EQ(s.amountLane(), nullptr);

    ASSERT_TRUE(s.mc.changeModulatorSourceForTest(s.row()->getInfo(), otherUuid));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_EQ(s.chainsInto(otherUuid, raw).size(), 1u);
    EXPECT_TRUE(s.chainsInto(oldLfo, raw).empty());

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.chainsInto(oldLfo, raw).size(), 1u);
    EXPECT_TRUE(s.chainsInto(otherUuid, raw).empty());
}

TEST_F(TimelinePanelIntegrationTest, ChangeSourceRefusesTheCurrentSourceAndALfoThatAlreadyMovesTheParameter) {
    AmountScene s;
    const auto info = s.row()->getInfo();
    EXPECT_FALSE(s.mc.changeModulatorSourceForTest(info, s.lfoUuid)) << "already the source";

    // A second LFO cabled by hand: re-pointing the first routing at it would double up on one jack.
    auto other = addNodeWithUuid(s.mc, "LFO");
    s.mc.getGraphEditor().updateComponents();
    const int raw = s.channelFor("cutoff");
    ASSERT_NE(
        s.mc.getGraphEditor().connectModulationSource(other->nodeID, 0, s.byUuid(s.targetUuid)->nodeID, raw, 0.5f).uid,
        0u);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    const auto otherUuid = other->properties["uuid"].toString();
    EXPECT_FALSE(s.mc.changeModulatorSourceForTest(info, otherUuid));
    EXPECT_EQ(s.chainsInto(s.lfoUuid, raw).size(), 1u) << "nothing changed";
    EXPECT_EQ(s.chainsInto(otherUuid, raw).size(), 1u);
}
