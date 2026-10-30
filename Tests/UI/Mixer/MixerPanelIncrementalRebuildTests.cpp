// MixerPanelIncrementalRebuildTests.cpp -- a mixer rebuild keeps every strip column whose column did not change and
// rebuilds only the rest, and a restore that frees some nodes unbinds only the columns bound to them
// (MixerPanelColumnReuse.cpp). Counted with MixerPanelComponent::getStripColumnsBuiltForTest(); the kept columns are
// checked for being bound, showing the restored state, and matching what a fresh snapshot shows.
#include "../../TestWait.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <set>

namespace {

class MockProviderMIRT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMIRT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct MixerRig {
    MainComponent mc{std::make_unique<MockProviderMIRT>()};

    MixerRig() {
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mixer().setVisible(true); // a hidden mixer defers its rebuilds; these tests look at the shown one
        mc.newPatchForTest();
        for (int i = 0; i < 3; ++i)
            mc.simulateAddAudioTrackClick();
    }
    synth::ui::MixerPanelComponent& mixer() { return mc.getBottomDock().getMixerPanel(); }
    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }
    AppUndoManager& undo() { return mc.getUndoManager(); }
    std::vector<synth::ui::MixerColumnComponent*> columns() {
        std::vector<synth::ui::MixerColumnComponent*> out;
        for (int i = 0; auto* column = mixer().getStripColumnForTest(i); ++i)
            out.push_back(column);
        return out;
    }
    ChannelStripModule* strip(int index) {
        auto* node = graph().getNodeForId(columns().at((size_t)index)->getNodeId());
        return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    }
    // What a panel built from nothing would show, in column order: each strip column's uuid and name.
    std::vector<std::pair<juce::String, juce::String>> freshColumns() {
        std::vector<std::pair<juce::String, juce::String>> out;
        for (const auto& column :
             synth::buildMixerSnapshot(graph(), mc.getTimelineDoc(), mc.getGraphEditor().getMacros()).columns)
            if (column.kind == synth::MixerColumn::Kind::Strip || column.kind == synth::MixerColumn::Kind::Bus)
                out.emplace_back(column.uuid, column.name);
        return out;
    }
    std::vector<std::pair<juce::String, juce::String>> shownColumns() {
        std::vector<std::pair<juce::String, juce::String>> out;
        for (auto* column : columns())
            out.emplace_back(column->getUuid(), column->getTitle());
        return out;
    }
};

juce::AudioProcessorParameter* gainOf(ChannelStripModule& strip) {
    for (auto* param : strip.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged && ranged->getParameterID() == "gain")
            return param;
    return nullptr;
}

} // namespace

TEST(MixerPanelIncrementalRebuild, AKnobUndoKeepsEveryColumnBound) {
    MixerRig rig;
    const auto before = rig.columns();
    ASSERT_EQ(before.size(), 3u);
    auto* strip = rig.strip(1);
    ASSERT_NE(strip, nullptr);
    auto* gain = gainOf(*strip);
    ASSERT_NE(gain, nullptr);
    const float original = gain->getValue();
    const int built = rig.mixer().getStripColumnsBuiltForTest();

    rig.undo().captureBeforeState(rig.graph());
    gain->setValueNotifyingHost(original > 0.5f ? 0.1f : 0.9f);
    rig.undo().pushSnapshotFromCapture(rig.graph());
    ASSERT_TRUE(rig.undo().undo());

    EXPECT_FLOAT_EQ(gain->getValue(), original) << "the knob is back";
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), built) << "a knob undo re-creates no mixer column";
    EXPECT_EQ(rig.columns(), before) << "the same columns are shown";
    for (auto* column : rig.columns())
        EXPECT_TRUE(column->isFaderBoundForTest()) << "every kept column is still bound";
    EXPECT_EQ(rig.shownColumns(), rig.freshColumns());
}

TEST(MixerPanelIncrementalRebuild, UndoingAnAddedTrackRebuildsNoOtherColumn) {
    MixerRig rig;
    const auto before = rig.columns();
    rig.mc.simulateAddAudioTrackClick();
    ASSERT_EQ(rig.columns().size(), 4u);
    const int built = rig.mixer().getStripColumnsBuiltForTest();

    ASSERT_TRUE(rig.undo().undo());
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), built) << "only the freed strip's column goes";
    EXPECT_EQ(rig.columns(), before);
    for (auto* column : rig.columns())
        EXPECT_TRUE(column->isFaderBoundForTest());
    EXPECT_EQ(rig.shownColumns(), rig.freshColumns());

    ASSERT_TRUE(rig.undo().redo());
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), built + 1) << "the redo builds the restored strip's column";
    const auto after = rig.columns();
    ASSERT_EQ(after.size(), 4u);
    EXPECT_TRUE(std::equal(before.begin(), before.end(), after.begin())) << "and keeps the others";
    for (auto* column : after)
        EXPECT_TRUE(column->isFaderBoundForTest());
    EXPECT_EQ(rig.shownColumns(), rig.freshColumns());
}

TEST(MixerPanelIncrementalRebuild, AKeptColumnShowsTheRestoredMute) {
    MixerRig rig;
    auto* column = rig.columns().at(0);
    column->toggleMuted();
    ASSERT_TRUE(rig.strip(0)->isMuted());
    ASSERT_TRUE(column->getMuteButtonForTest().getToggleState());

    ASSERT_TRUE(rig.undo().undo());
    EXPECT_EQ(rig.columns().at(0), column) << "kept";
    EXPECT_FALSE(rig.strip(0)->isMuted());
    EXPECT_FALSE(column->getMuteButtonForTest().getToggleState()) << "a kept column re-reads mute";
}

TEST(MixerPanelIncrementalRebuild, ADuplicateAndItsUndoBuildOnlyTheCopysColumn) {
    MixerRig rig;
    const auto before = rig.columns();
    const int built = rig.mixer().getStripColumnsBuiltForTest();
    const auto first = rig.mc.getTimelineDoc().getTracks().front().id;
    // TrackHeaderHost is a private base of MainComponent; the C-style cast is the header's own call path.
    ((synth::ui::TrackHeaderHost&)rig.mc).duplicateTrack(first);
    ASSERT_EQ(rig.columns().size(), 4u) << "the copy has its own channel";
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), built + 1) << "only the copy's column is built";
    EXPECT_EQ(rig.shownColumns(), rig.freshColumns());

    ASSERT_TRUE(rig.undo().undo());
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), built + 1) << "its undo builds none";
    EXPECT_EQ(rig.columns(), before);
    for (auto* column : rig.columns())
        EXPECT_TRUE(column->isFaderBoundForTest());
    EXPECT_EQ(rig.shownColumns(), rig.freshColumns());
}

TEST(MixerPanelIncrementalRebuild, UnbindingEverythingRebuildsEveryColumn) {
    MixerRig rig;
    const int built = rig.mixer().getStripColumnsBuiltForTest();
    rig.mixer().unbindAllColumns();
    rig.mixer().rebuildIfStale();
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), built + 3) << "an unbound column is never kept";
    for (auto* column : rig.columns())
        EXPECT_TRUE(column->isFaderBoundForTest());
}

// A module patched into a track's chain on the canvas shows in that track's inserts at once. The patch is
// a recorded graph edit made the way the canvas makes one (a cable edit rebuilds no components), so nothing but
// the edit's own undo-manager broadcast reaches the mixer; before the fix the insert showed only after an
// unrelated mixer action. Only the patched track's column is rebuilt, and an undo takes the insert away again.
namespace {
// The node directly feeding `column`'s strip: its last insert, or the chain's source when it has none.
juce::AudioProcessorGraph::NodeID nodeFeedingStrip(const synth::MixerColumn& column) {
    return column.inserts.empty() ? column.sourceNodeId : column.inserts.back().nodeId;
}

const synth::MixerColumn* snapshotColumn(MixerRig& rig, const synth::MixerSnapshot& snapshot, int index) {
    const auto uuid = rig.columns().at((size_t)index)->getUuid();
    for (const auto& column : snapshot.columns)
        if (column.uuid == uuid)
            return &column;
    return nullptr;
}

std::vector<juce::String> insertUuids(synth::ui::MixerColumnComponent& column) {
    std::vector<juce::String> out;
    auto& list = column.getInsertListForTest();
    for (int row = 0; row < list.getRowCount(); ++row)
        if (row != list.getAddRowIndex())
            out.push_back(list.getRowUuid(row));
    return out;
}

// The graph's change broadcast is queued, and the work it triggers may queue more: run the queue a few times.
void deliverBroadcasts() {
    for (int i = 0; i < 3; ++i)
        synth::test::flushMessageQueue();
}
} // namespace

TEST(MixerPanelIncrementalRebuild, AModulePatchedIntoATrackOnTheCanvasShowsInItsInsertsAtOnce) {
    MixerRig rig;
    deliverBroadcasts();
    const auto snapshot =
        synth::buildMixerSnapshot(rig.graph(), rig.mc.getTimelineDoc(), rig.mc.getGraphEditor().getMacros());
    const auto* column = snapshotColumn(rig, snapshot, 1);
    ASSERT_NE(column, nullptr);
    const auto feeder = nodeFeedingStrip(*column);
    ASSERT_NE(feeder, juce::AudioProcessorGraph::NodeID{}) << "the track's chain has a node feeding its strip";
    const auto stripId = column->nodeId;
    const auto before = rig.columns();
    const auto insertsBefore = insertUuids(*before[1]);

    // Adding the card alone (not yet cabled) changes no chain: nothing is rebuilt.
    std::set<juce::uint32> existing;
    for (auto* node : rig.graph().getNodes())
        existing.insert(node->nodeID.uid);
    const int builtBeforeAdd = rig.mixer().getStripColumnsBuiltForTest();
    rig.mc.getGraphEditor().addModuleAtCanvasPosition("Delay", {3000, 3000}, nullptr);
    deliverBroadcasts();
    juce::AudioProcessorGraph::NodeID added;
    for (auto* node : rig.graph().getNodes())
        if (existing.count(node->nodeID.uid) == 0)
            added = node->nodeID;
    ASSERT_NE(added, juce::AudioProcessorGraph::NodeID{});
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), builtBeforeAdd) << "an unconnected card rebuilds nothing";
    EXPECT_EQ(rig.columns(), before);

    // Cable it in between the chain and the strip: one recorded edit, no mixer call.
    bool spliced = false;
    rig.undo().recordStructuralChange(rig.graph(),
                                      [&] { spliced = synth::spliceInInsert(rig.graph(), feeder, stripId, added); });
    ASSERT_TRUE(spliced);
    deliverBroadcasts();
    synth::test::waitUntil([&] { return rig.mixer().getStripColumnsBuiltForTest() > builtBeforeAdd; });

    const auto after = rig.columns();
    ASSERT_EQ(after.size(), before.size());
    auto expected = insertsBefore;
    expected.push_back(rig.graph().getNodeForId(added)->properties["uuid"].toString());
    EXPECT_EQ(insertUuids(*after[1]), expected) << "the patched module shows in the track's inserts";
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), builtBeforeAdd + 1) << "only that track's column is rebuilt";
    EXPECT_EQ(after[0], before[0]);
    EXPECT_EQ(after[2], before[2]);

    ASSERT_TRUE(rig.undo().undo());
    deliverBroadcasts();
    EXPECT_EQ(insertUuids(*rig.columns().at(1)), insertsBefore) << "undoing the cable takes the insert away";
    for (auto* shown : rig.columns())
        EXPECT_TRUE(shown->isFaderBoundForTest());
}

// A hidden mixer does no work (docs/layout/rendering.md, "A closed panel does no work"): rebuildIfStale() only notes
// that a check is due, and the panel catches up once, when it is shown again. A shown panel rebuilds at once.
TEST(MixerPanelIncrementalRebuild, AHiddenMixerSkipsTheStaleCheckAndCatchesUpWhenShown) {
    MixerRig rig;
    const auto before = rig.columns().size();
    const auto first = rig.mc.getTimelineDoc().getTracks().front().id;

    // Shown: the change is laid out as soon as the check runs.
    ((synth::ui::TrackHeaderHost&)rig.mc).duplicateTrack(first);
    rig.mixer().rebuildIfStale();
    ASSERT_EQ(rig.columns().size(), before + 1);
    EXPECT_EQ(rig.shownColumns(), rig.freshColumns());

    rig.mixer().setVisible(false);
    const int rebuilds = rig.mixer().getRebuildCountForTest();
    const int built = rig.mixer().getStripColumnsBuiltForTest();
    ((synth::ui::TrackHeaderHost&)rig.mc).duplicateTrack(first);
    deliverBroadcasts();
    rig.mixer().rebuildIfStale();
    EXPECT_EQ(rig.mixer().getRebuildCountForTest(), rebuilds) << "hidden: nothing is built";
    EXPECT_EQ(rig.mixer().getStripColumnsBuiltForTest(), built);
    EXPECT_EQ(rig.columns().size(), before + 1) << "still showing the last layout";

    rig.mixer().setVisible(true);
    EXPECT_EQ(rig.mixer().getRebuildCountForTest(), rebuilds + 1) << "shown again: one catch-up rebuild";
    EXPECT_EQ(rig.columns().size(), before + 2);
    EXPECT_EQ(rig.shownColumns(), rig.freshColumns());

    // Nothing was missed, so showing it again later does no work.
    rig.mixer().setVisible(false);
    rig.mixer().setVisible(true);
    EXPECT_EQ(rig.mixer().getRebuildCountForTest(), rebuilds + 1);
}
