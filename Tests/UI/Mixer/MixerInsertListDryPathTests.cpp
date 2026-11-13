// MixerInsertListDryPathTests.cpp (docs/mixer/mixer.md#parallel-paths): a path wired straight to the merge shows as
// a dimmed "Dry (no effects)" row of its own, so "2 paths" always has two path rows.
#include "MixerInsertListSplitFixture.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Mixer/MixerRowsMotion.h"
#include <set>

namespace {
using namespace mixer_split_test;
using Kind = synth::MixerInsertEntry::Kind;

/** Source -> {Reverb | straight across} -> VCA -> strip. */
struct DryPatch : SplitListRig {
    NodeID reverb, vca;
    DryPatch() {
        reverb = add("Reverb", 600, 300);
        vca = add("VCA", 900, 400);
        wire(source, reverb);
        wire(source, vca);
        wire(reverb, vca);
        wire(vca, strip);
        refresh();
    }
};
} // namespace

TEST(MixerInsertListDryPathTests, TheModelEmitsADryRowForTheStraightPath) {
    DryPatch rig;
    const auto column = rig.column();
    ASSERT_EQ(column.inserts.size(), 4u);
    EXPECT_EQ(column.inserts[0].kind, Kind::Split);
    EXPECT_EQ(column.inserts[1].kind, Kind::Module);
    EXPECT_EQ(column.inserts[1].path, 0);
    EXPECT_EQ(column.inserts[2].kind, Kind::DryPath);
    EXPECT_EQ(column.inserts[2].depth, 1);
    EXPECT_EQ(column.inserts[2].path, 1);
    EXPECT_EQ(column.inserts[2].nodeId, NodeID{});
    EXPECT_EQ(column.inserts[3].kind, Kind::Module);
    EXPECT_EQ(synth::countInsertModules(column.inserts), 2);
}

TEST(MixerInsertListDryPathTests, TwoPathsShowTwoPathRowsOnceTheSplitIsOpen) {
    DryPatch rig;
    rig.list.setSplitOpen(0, true);
    // Split, Reverb (path 1), Dry (path 2), VCA, then the "+ Insert" row.
    ASSERT_EQ(rig.list.getRowCount(), 5);
    EXPECT_EQ(rig.list.describeRow(1), "Insert 2, Reverb, path 1");
    EXPECT_EQ(rig.list.describeRow(2), "Insert 3, path 2: dry, no effects");
    EXPECT_TRUE(rig.list.isDryRow(2));
    EXPECT_EQ(rig.list.getRowAccessibilityComponentForTest(2)->getTitle(), "Path 2: dry, no effects");
}

TEST(MixerInsertListDryPathTests, TheDryRowHasNoBypassAndCannotBeMovedOrRemoved) {
    DryPatch rig;
    rig.list.setSplitOpen(0, true);
    EXPECT_EQ(rig.list.getBypassButtonForTest(2), nullptr);
    EXPECT_NE(rig.list.getBypassButtonForTest(1), nullptr);
    const auto before = rig.rows();
    rig.list.moveRow(2, -1);
    rig.list.moveRow(2, 1);
    rig.list.removeRow(2);
    rig.list.toggleBypassForRow(2);
    EXPECT_EQ(rig.rows(), before);
    EXPECT_EQ(rig.mutations, 0);
}

TEST(MixerInsertListDryPathTests, TheDryRowKeyIsStableAndDistinctPerPath) {
    DryPatch rig;
    const auto keys = rig.list.entryKeys();
    ASSERT_EQ(keys.size(), 4u);
    EXPECT_TRUE(keys[2].startsWith("dry:"));
    EXPECT_EQ(std::set<juce::String>(keys.begin(), keys.end()).size(), keys.size());
}

TEST(MixerInsertListDryPathTests, ActivatingTheDryRowOffersAnAddIntoThatPath) {
    DryPatch rig;
    rig.list.setSplitOpen(0, true);
    int picked = 0;
    synth::ui::MixerInsertList::pickerHookForTest() = [&](std::unique_ptr<synth::ui::ModMatrixPicker>) { ++picked; };
    rig.list.activateDryRow(2);
    synth::ui::MixerInsertList::pickerHookForTest() = nullptr;
    EXPECT_EQ(picked, 1);
}
