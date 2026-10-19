// AutomationLanesChangeSourceTests.cpp -- a modulator row's "Change source...": the item is there only when
// the host says it can re-point the routing (an LFO row and an envelope row alike), the picker lists LFOs and
// envelopes under group headers with the current source ticked and disabled, then "New LFO" / "New envelope" last,
// and against a real MainComponent the lane follows the new source
// (reading the channel the source catalogue gives it), keeps its drawn amount curve (re-keyed to the new routing's
// Attenuverter) and the whole change is ONE graph + timeline undo step.

#include "AutomationLanesAmountFixture.h"
#include "AutomationLanesMenuFixture.h"
#include "UI/Graph/CardBody/CardBodySwapMotion.h"
#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

using namespace amount_test;
using synth::ui::ModulatorInfo;
using synth::ui::ModulatorRow;

namespace {

// A host that offers two LFOs (and optionally an envelope on output 3) and records the re-point it is asked for.
struct SourceHost : synth::ui::TrackHeaderHost {
    bool canChange = true;
    bool oneLfoOnly = false;
    int changes = 0;
    int newChanges = 0;
    juce::String newType;
    int newChannel = -1;
    juce::String changedTo;
    int changedChannel = -1;
    bool withEnvelope = false;

    bool canChangeModulatorSource(const ModulatorInfo&) override { return canChange; }
    bool changeModulatorSource(const ModulatorInfo&, const juce::String& sourceUuid, int sourceChannel) override {
        ++changes;
        changedTo = sourceUuid;
        changedChannel = sourceChannel;
        return true;
    }
    bool changeModulatorSourceToNew(const ModulatorInfo&, const juce::String& typeName, int sourceChannel) override {
        ++newChanges;
        newType = typeName;
        newChannel = sourceChannel;
        return true;
    }
    std::vector<LfoChoice> getChangeSourceChoices(const juce::String&, const juce::String&) override {
        LfoChoice current;
        current.uuid = "lfo-1";
        current.name = "LFO 1";
        LfoChoice other;
        other.uuid = "lfo-2";
        other.name = "LFO 2";
        LfoChoice env;
        env.uuid = "env-1";
        env.name = "Envelope 1";
        env.sourceChannel = 3;
        env.isEnvelope = true;
        if (oneLfoOnly)
            return {current};
        return withEnvelope ? std::vector<LfoChoice>{current, other, env} : std::vector<LfoChoice>{current, other};
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

ModulatorInfo envelopeOne() {
    ModulatorInfo info;
    info.sourceUuid = "env-1";
    info.sourceTitle = "Envelope 1";
    info.sourceChannel = 3;
    info.isLfo = false;
    info.attenuverterUuid = "atten-2";
    info.targetUuid = "filter";
    info.paramId = "cutoff";
    return info;
}

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

TEST(AutomationLanesChangeSourceTest, TheItemStaysEnabledWithNoOtherSourceBecauseANewOneCanBeMade) {
    SourceHost host;
    host.oneLfoOnly = true;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    juce::PopupMenu::Item item;
    ASSERT_TRUE(findItem(row.buildMenu(), ModulatorRow::kChangeSourceMenuId, item));
    EXPECT_EQ(item.text, "Change source...");
    EXPECT_TRUE(item.isEnabled);

    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);
    EXPECT_EQ(capture.picker->getVisibleItemTextsForTest(),
              (std::vector<juce::String>{"LFO 1", "New LFO", "New envelope"}));
}

TEST(AutomationLanesChangeSourceTest, ThePickerGroupsSourcesTicksTheCurrentOneAndAPickReachesTheHost) {
    SourceHost host;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);

    EXPECT_EQ(capture.picker->getVisibleRowNamesForTest(),
              (std::vector<juce::String>{"LFOs", "LFO 1", "LFO 2", "New", "New LFO", "New envelope"}));
    EXPECT_EQ(capture.picker->getSearchPlaceholderForTest(), "Search or create a source");
    EXPECT_TRUE(capture.picker->isVisibleItemTickedForTest(0)) << "the current source is ticked...";
    EXPECT_FALSE(capture.picker->isVisibleItemPickableForTest(0)) << "...and cannot be picked";
    EXPECT_FALSE(capture.picker->isVisibleItemTickedForTest(1));
    EXPECT_TRUE(capture.picker->isVisibleItemNewForTest(2));
    EXPECT_TRUE(capture.picker->isVisibleItemNewForTest(3));

    capture.picker->chooseVisibleItemForTest(0);
    EXPECT_EQ(host.changes, 0) << "the current source is not a pick";
    capture.picker->chooseVisibleItemForTest(1);
    EXPECT_EQ(host.changes, 1);
    EXPECT_EQ(host.changedTo, "lfo-2");
}

TEST(AutomationLanesChangeSourceTest, TheNewRowsStayLastWhileFilteringAndMakeAFreshModuleOfTheirType) {
    SourceHost host;
    host.withEnvelope = true;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);

    capture.picker->setSearchTextForTest("LFO");
    auto names = capture.picker->getVisibleItemTextsForTest();
    ASSERT_GE(names.size(), 3u);
    EXPECT_EQ(names[names.size() - 2], "New LFO");
    EXPECT_EQ(names.back(), "New envelope") << "a create row stays whatever the query finds";
    EXPECT_EQ(names.front(), "LFO 1");
    EXPECT_EQ(capture.picker->getHighlightedItemIndexForTest(), 1) << "Return picks the existing LFO 2, not a new one";

    capture.picker->setSearchTextForTest("nothing matches this");
    names = capture.picker->getVisibleItemTextsForTest();
    EXPECT_EQ(names, (std::vector<juce::String>{"New LFO", "New envelope"}));

    capture.picker->setSearchTextForTest({});
    capture.picker->chooseVisibleItemForTest(3);
    EXPECT_EQ(host.newChanges, 1);
    EXPECT_EQ(host.newType, "LFO");
    EXPECT_EQ(host.changes, 0);
}

TEST(AutomationLanesChangeSourceTest, NewEnvelopeCreatesTheCataloguesEnvelopeType) {
    SourceHost host;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);
    const auto names = capture.picker->getVisibleItemTextsForTest();
    ASSERT_EQ(names.back(), "New envelope");
    capture.picker->chooseVisibleItemForTest((int)names.size() - 1);
    EXPECT_EQ(host.newChanges, 1);
    EXPECT_EQ(host.newType, "ADSR");
    EXPECT_GE(host.newChannel, 0);
}

TEST(AutomationLanesChangeSourceTest, ThePickerKeepsAnLfoThatAlreadyMovesTheParameterGreyedOut) {
    auto lfos = SourceHost().getChangeSourceChoices({}, {});
    lfos[1].movesThisParameter = true;
    const auto choices = synth::ui::collectChangeSourceChoices(lfos, "lfo-1", 0, "Cutoff");
    const auto moving =
        std::find_if(choices.items.begin(), choices.items.end(), [](const auto& item) { return item.text == "LFO 2"; });
    ASSERT_NE(moving, choices.items.end());
    EXPECT_FALSE(moving->enabled);
}

// An envelope row through the real right-click path: the menu offers an enabled item, the picker lists the LFOs but
// not the current output, and a pick carries the chosen source's channel.
TEST(AutomationLanesChangeSourceTest, AnEnvelopeRowOffersTheItemAndItsPickerListsLfosAndEnvelopes) {
    SourceHost host;
    host.withEnvelope = true;
    ModulatorRow row(envelopeOne(), &host, "Cutoff");
    row.setBounds(0, 0, 200, 80);
    {
        lane_menu_test::MenuCapture menu;
        row.mouseDown(makeClickEvent(row, {5.0f, 5.0f}, lane_menu_test::rightButton()));
        ASSERT_EQ(menu.count, 1);
        juce::PopupMenu::Item item;
        ASSERT_TRUE(findItem(menu.menu, ModulatorRow::kChangeSourceMenuId, item));
        EXPECT_EQ(item.text, "Change source...");
        EXPECT_TRUE(item.isEnabled);
    }
    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);
    const auto names = capture.picker->getVisibleItemTextsForTest();
    ASSERT_EQ(names.size(), 5u) << "both LFOs, the envelope, and the two New rows";
    EXPECT_EQ(names[0], "LFO 1");
    EXPECT_EQ(names[1], "LFO 2");
    EXPECT_EQ(names[2], "Envelope 1");
    EXPECT_TRUE(capture.picker->isVisibleItemTickedForTest(2)) << "the envelope's own output is the current source";

    capture.picker->chooseVisibleItemForTest(1);
    EXPECT_EQ(host.changedTo, "lfo-2");
    EXPECT_EQ(host.changedChannel, 0);
}

TEST(AutomationLanesChangeSourceTest, AnLfoRowPickingAnEnvelopePassesItsChannel) {
    SourceHost host;
    host.withEnvelope = true;
    ModulatorRow row(lfoOne(), &host, "Cutoff");
    SourcePickerCapture capture;
    row.applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);
    const auto names = capture.picker->getVisibleItemTextsForTest();
    ASSERT_EQ(names.size(), 5u);
    EXPECT_EQ(names[2], "Envelope 1");
    capture.picker->chooseVisibleItemForTest(2);
    EXPECT_EQ(host.changedTo, "env-1");
    EXPECT_EQ(host.changedChannel, 3);
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
    // Both LFOs (titled "LFO"; the current one ticked and disabled), the two default envelopes, then the two New rows.
    ASSERT_EQ(names.size(), 6u);
    int otherIndex = -1;
    for (int i = 0; i < (int)names.size(); ++i)
        if (names[(size_t)i] == "LFO" && !capture.picker->isVisibleItemTickedForTest(i))
            otherIndex = i;
    ASSERT_GE(otherIndex, 0);
    const auto depthBefore = s.depthOf(s.chainsInto(oldLfo, raw).front());
    capture.picker->chooseVisibleItemForTest(otherIndex);
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

namespace {
// The first output the source catalogue offers for the node `uuid` (what the mod dot cables).
int catalogueChannelOf(AmountScene& s, const juce::String& uuid) {
    auto* node = s.byUuid(uuid);
    for (const auto& item : synth::ui::enumerateModSources(s.mc.getAudioEngine().getGraph()))
        if (node != nullptr && item.node == node->nodeID)
            return item.channel;
    return -1;
}

// The row whose source is `uuid`, or null.
ModulatorRow* rowFor(AmountScene& s, const juce::String& uuid) {
    for (int i = 0; i < 4; ++i)
        if (auto* r = s.row(i); r != nullptr && r->getInfo().sourceUuid == uuid)
            return r;
    return nullptr;
}

// An ADSR cabled into the scene's cutoff the way the mod dot does it (through an Attenuverter chain).
juce::String addEnvelopeRouting(AmountScene& s, int& channel) {
    auto adsr = addNodeWithUuid(s.mc, "ADSR");
    const auto uuid = adsr->properties["uuid"].toString();
    s.mc.getGraphEditor().updateComponents();
    channel = catalogueChannelOf(s, uuid);
    EXPECT_GE(channel, 0);
    EXPECT_NE(s.mc.getGraphEditor()
                  .connectModulationSource(adsr->nodeID, channel, s.byUuid(s.targetUuid)->nodeID,
                                           s.channelFor("cutoff"), 0.5f)
                  .uid,
              0u);
    s.mc.getGraphEditor().updateComponents();
    s.flush();
    return uuid;
}
} // namespace

TEST_F(TimelinePanelIntegrationTest, AnEnvelopeRoutingChangesToAnLfoKeepsItsAmountLaneAndIsOneUndoStep) {
    AmountScene s;
    const int raw = s.channelFor("cutoff");
    int adsrChannel = -1;
    const auto adsrUuid = addEnvelopeRouting(s, adsrChannel);
    auto spare = addNodeWithUuid(s.mc, "LFO");
    const auto spareUuid = spare->properties["uuid"].toString();
    s.mc.getGraphEditor().updateComponents();

    auto* adsrRow = rowFor(s, adsrUuid);
    ASSERT_NE(adsrRow, nullptr);
    EXPECT_FALSE(adsrRow->getInfo().isLfo);
    ASSERT_TRUE(adsrRow->getInfo().attenuverterUuid.isNotEmpty());
    EXPECT_TRUE(s.mc.canChangeModulatorSourceForTest(adsrRow->getInfo()));
    const auto oldAtten = adsrRow->getInfo().attenuverterUuid;

    const std::vector<synth::AutomationLane::Breakpoint> drawn{{0.0, 0.25, 0.0f, kHold}, {4.0, -0.5, 0.0f, kHold}};
    s.undo().recordTimelineChange(s.doc(), [&] { synth::ui::writeAmountLane(s.doc(), s.track, oldAtten, drawn); });
    const auto* before = synth::ui::amountLaneFor(s.doc(), oldAtten);
    ASSERT_NE(before, nullptr);
    const auto laneId = before->id;

    ASSERT_TRUE(s.mc.changeModulatorSourceForTest(adsrRow->getInfo(), spareUuid, 0));
    s.flush();
    EXPECT_TRUE(s.chainsInto(adsrUuid, raw).empty());
    ASSERT_EQ(s.chainsInto(spareUuid, raw).size(), 1u);
    auto* moved = rowFor(s, spareUuid);
    ASSERT_NE(moved, nullptr);
    const auto newAtten = moved->getInfo().attenuverterUuid;
    EXPECT_NE(newAtten, oldAtten);
    EXPECT_EQ(synth::ui::amountLaneFor(s.doc(), oldAtten), nullptr);
    const auto* after = synth::ui::amountLaneFor(s.doc(), newAtten);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->id, laneId);
    ASSERT_EQ(after->points.size(), drawn.size());
    EXPECT_DOUBLE_EQ(after->points[1].value, drawn[1].value);

    ASSERT_TRUE(s.undo().undo());
    ASSERT_EQ(s.chainsInto(adsrUuid, raw).size(), 1u);
    EXPECT_EQ(s.chainsInto(adsrUuid, raw).front().sourceChannelIndex, adsrChannel);
    EXPECT_TRUE(s.chainsInto(spareUuid, raw).empty());
    auto* restoredRow = rowFor(s, adsrUuid);
    ASSERT_NE(restoredRow, nullptr);
    const auto* restored = synth::ui::amountLaneFor(s.doc(), restoredRow->getInfo().attenuverterUuid);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->points.size(), drawn.size());
}

TEST_F(TimelinePanelIntegrationTest, AnLfoRoutingChangesToAnEnvelopeReadingTheCataloguesChannel) {
    AmountScene s;
    const int raw = s.channelFor("cutoff");
    auto adsr = addNodeWithUuid(s.mc, "ADSR");
    const auto adsrUuid = adsr->properties["uuid"].toString();
    s.mc.getGraphEditor().updateComponents();
    const int adsrChannel = catalogueChannelOf(s, adsrUuid);
    ASSERT_GE(adsrChannel, 0);
    ASSERT_TRUE(s.mc.canChangeModulatorSourceForTest(s.row()->getInfo()));

    ASSERT_TRUE(s.mc.changeModulatorSourceForTest(s.row()->getInfo(), adsrUuid, adsrChannel));
    s.flush();
    EXPECT_TRUE(s.chainsInto(s.lfoUuid, raw).empty());
    const auto chains = s.chainsInto(adsrUuid, raw);
    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains.front().sourceChannelIndex, adsrChannel);

    // The same output again is a no-op; a channel the catalogue does not offer is refused.
    auto* envRow = rowFor(s, adsrUuid);
    ASSERT_NE(envRow, nullptr);
    EXPECT_EQ(envRow->getInfo().sourceChannel, adsrChannel);
    EXPECT_FALSE(s.mc.changeModulatorSourceForTest(envRow->getInfo(), adsrUuid, adsrChannel));
    EXPECT_FALSE(s.mc.changeModulatorSourceForTest(envRow->getInfo(), s.lfoUuid, 999));

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.chainsInto(s.lfoUuid, raw).size(), 1u);
    EXPECT_TRUE(s.chainsInto(adsrUuid, raw).empty());
}

TEST_F(TimelinePanelIntegrationTest, AnEnvelopeRowsRealMenuOffersChangeSourceListingLfosAndEnvelopes) {
    AmountScene s;
    int adsrChannel = -1;
    const auto adsrUuid = addEnvelopeRouting(s, adsrChannel);
    auto spare = addNodeWithUuid(s.mc, "ADSR");
    s.mc.getGraphEditor().updateComponents();
    auto* envRow = rowFor(s, adsrUuid);
    ASSERT_NE(envRow, nullptr);
    {
        lane_menu_test::MenuCapture menu;
        envRow->showMenuAt(juce::PopupMenu::Options());
        ASSERT_EQ(menu.count, 1);
        juce::PopupMenu::Item item;
        ASSERT_TRUE(findItem(menu.menu, ModulatorRow::kChangeSourceMenuId, item));
        EXPECT_EQ(item.text, "Change source...");
        EXPECT_TRUE(item.isEnabled);
    }
    SourcePickerCapture capture;
    envRow->applyMenuChoice(ModulatorRow::kChangeSourceMenuId);
    ASSERT_NE(capture.picker, nullptr);
    const auto names = capture.picker->getVisibleItemTextsForTest();
    // The LFO (greyed: it already moves cutoff), the two default envelopes, the spare ADSR and the routed one (ticked),
    // then the two New rows.
    ASSERT_EQ(names.size(), 7u);
    EXPECT_NE(std::find(names.begin(), names.end(), juce::String("LFO")), names.end());
    EXPECT_EQ(std::count(names.begin(), names.end(), juce::String("ADSR")), 2);
    int ticked = 0;
    for (int i = 0; i < (int)names.size(); ++i)
        ticked += capture.picker->isVisibleItemTickedForTest(i) ? 1 : 0;
    EXPECT_EQ(ticked, 1);
}

// Regression test for FRO881: Change source on the first of two rows moved it to the bottom (its slot in the lane's
// order was stored under the old Attenuverter) and rebuilt it. Its undo brings the same row back to its old place with
// the same swap.
namespace {
// Full animations whatever the machine's Reduce Motion says; restored on exit.
struct AnimationsOn {
    AnimationsOn() { synth::ui::setAnimationMode(synth::ui::AnimationMode::full); }
    ~AnimationsOn() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
};
} // namespace

TEST_F(TimelinePanelIntegrationTest, ChangeSourceOnTheFirstOfTwoRowsKeepsItsRowItsPlaceAndItsFoldAndUndoSwapsBack) {
    AnimationsOn animate;
    AmountScene s;
    int adsrChannel = -1;
    const auto adsrUuid = addEnvelopeRouting(s, adsrChannel);
    auto spare = addNodeWithUuid(s.mc, "LFO");
    const auto spareUuid = spare->properties["uuid"].toString();
    s.mc.getGraphEditor().updateComponents();
    s.flush();
    s.undo().clearUndoHistory();

    auto& lanes = s.panel().getAutomationLanes();
    ASSERT_EQ(lanes.modulatorCount(s.lane), 2);
    auto* first = s.row(0);
    auto* second = s.row(1);
    ASSERT_EQ(first->getInfo().sourceUuid, s.lfoUuid) << "the premise: the LFO is the first row";
    ASSERT_EQ(second->getInfo().sourceUuid, adsrUuid);
    ASSERT_TRUE(s.doc().getLane(s.lane)->modulatorOrder.empty()) << "the premise: never reordered";
    clickButton(first->getFoldArrow());
    lanes.stepModulatorFoldForTest(1.0f);
    ASSERT_TRUE(lanes.isModulatorCollapsed(s.lane, 0));
    const int foldedHeight = s.panel().modulatorRowBoundsForTest(s.lane, 0).getHeight();
    const int openHeight = s.panel().modulatorRowBoundsForTest(s.lane, 1).getHeight();
    ASSERT_LT(foldedHeight, openHeight);
    first->setForceSwapMotionForTest(true);

    ASSERT_TRUE(s.mc.changeModulatorSourceForTest(first->getInfo(), spareUuid, 0));
    s.flush();

    ASSERT_EQ(lanes.modulatorCount(s.lane), 2);
    EXPECT_EQ(s.row(0), first) << "the same row, re-bound in place";
    EXPECT_EQ(s.row(1), second);
    EXPECT_EQ(first->getInfo().sourceUuid, spareUuid);
    EXPECT_EQ(second->getInfo().sourceUuid, adsrUuid);
    EXPECT_EQ(s.doc().getLane(s.lane)->modulatorOrder,
              (std::vector<juce::String>{first->getInfo().attenuverterUuid, second->getInfo().attenuverterUuid}))
        << "the new routing holds the old one's slot";
    EXPECT_TRUE(first->isSwapMotionRunning()) << "the name swaps in place";
    EXPECT_TRUE(lanes.isModulatorCollapsed(s.lane, 0)) << "and the fold stays";
    EXPECT_EQ(s.panel().modulatorRowBoundsForTest(s.lane, 0).getHeight(), foldedHeight);
    EXPECT_EQ(s.panel().modulatorRowBoundsForTest(s.lane, 1).getHeight(), openHeight);
    first->stepSwapMotionForTest(synth::SwapMotion::totalMs());
    ASSERT_FALSE(first->isSwapMotionRunning());

    ASSERT_TRUE(s.undo().undo());
    s.flush();
    ASSERT_EQ(lanes.modulatorCount(s.lane), 2);
    EXPECT_EQ(s.row(0), first) << "the undo re-binds the same row back";
    EXPECT_EQ(s.row(1), second);
    EXPECT_EQ(first->getInfo().sourceUuid, s.lfoUuid);
    EXPECT_TRUE(first->isSwapMotionRunning()) << "and swaps the name back the same way";
    EXPECT_TRUE(s.doc().getLane(s.lane)->modulatorOrder.empty()) << "the order is part of the undo step";
    EXPECT_TRUE(lanes.isModulatorCollapsed(s.lane, 0));
    EXPECT_EQ(s.panel().modulatorRowBoundsForTest(s.lane, 0).getHeight(), foldedHeight);

    ASSERT_TRUE(s.undo().redo());
    s.flush();
    EXPECT_EQ(s.row(0), first);
    EXPECT_EQ(first->getInfo().sourceUuid, spareUuid);
}
