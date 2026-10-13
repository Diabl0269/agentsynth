// MixerInsertListAddTests.cpp (docs/mixer/mixer.md#adding-an-insert): adding an effect from the mixer -- the
// "+ Insert" row, the "+" on the line between two rows, "Add after" and a split's "Add to path", the effect picker with
// recent picks first, and the module landing cabled in, on the canvas, as one undo step.
#include "MixerInsertListSplitFixture.h"

#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

using namespace mixer_split_test;

namespace {

// Captures the picker showAddPicker() would open, for the life of a test.
struct PickerCapture {
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    PickerCapture() {
        synth::ui::MixerInsertList::pickerHookForTest() = [this](auto p) { picker = std::move(p); };
    }
    ~PickerCapture() { synth::ui::MixerInsertList::pickerHookForTest() = nullptr; }

    void pick(const juce::String& text) {
        ASSERT_NE(picker, nullptr);
        const auto items = picker->getVisibleItemTextsForTest();
        const auto it = std::find(items.begin(), items.end(), text);
        ASSERT_NE(it, items.end()) << text;
        picker->chooseVisibleItemForTest((int)(it - items.begin()));
    }
};

NodeID newNode(SplitListRig& r, const std::set<juce::uint32>& before) {
    for (auto* n : r.graph.getNodes())
        if (before.count(n->nodeID.uid) == 0)
            return n->nodeID;
    return {};
}

std::set<juce::uint32> nodeIds(SplitListRig& r) {
    std::set<juce::uint32> ids;
    for (auto* n : r.graph.getNodes())
        ids.insert(n->nodeID.uid);
    return ids;
}

} // namespace

TEST(MixerInsertListAddTests, PlusInsertOpensTheEffectPickerAndThePickLandsBeforeTheStripAsOneUndoStep) {
    SplitListRig r;
    const auto eq = r.add("Parametric EQ", 300, 400);
    r.wire(r.source, eq);
    r.wire(eq, r.strip);
    r.refresh();
    ASSERT_EQ(r.list.getAddRowIndex(), 1);
    EXPECT_EQ(r.list.describeRow(1), "Add an insert");

    PickerCapture capture;
    r.list.activateAddRow();
    ASSERT_NE(capture.picker, nullptr);
    const auto items = capture.picker->getVisibleItemTextsForTest();
    EXPECT_NE(std::find(items.begin(), items.end(), "Reverb"), items.end());
    EXPECT_EQ(std::find(items.begin(), items.end(), "Oscillator"), items.end()) << "effects only";

    const auto before = nodeIds(r);
    capture.pick("Reverb");
    const auto reverb = newNode(r, before);
    ASSERT_NE(reverb.uid, 0u);
    EXPECT_EQ(r.mutations, 1);
    EXPECT_TRUE(r.wired(eq, reverb));
    EXPECT_TRUE(r.wired(reverb, r.strip));
    EXPECT_FALSE(r.wired(eq, r.strip));
    EXPECT_EQ(r.rows()[1], "Insert 2, Reverb");
    auto* card = r.editor.getModuleComponents().isEmpty() ? nullptr : [&]() -> ModuleComponent* {
        for (auto* c : r.editor.getModuleComponents())
            if (c->getNodeId() == reverb)
                return c;
        return nullptr;
    }();
    ASSERT_NE(card, nullptr) << "the module is on the canvas too";

    ASSERT_TRUE(r.undo.undo());
    EXPECT_EQ(r.graph.getNodeForId(reverb), nullptr) << "one undo removes it";
    EXPECT_TRUE(r.wired(eq, r.strip)) << "and puts the cable back";
    EXPECT_FALSE(r.undo.canUndo());

    // The pick is remembered on top of the next picker.
    r.refresh();
    r.list.activateAddRow();
    ASSERT_NE(capture.picker, nullptr);
    EXPECT_EQ(capture.picker->getVisibleRowNamesForTest().front(), "Recent");
    EXPECT_EQ(capture.picker->getVisibleItemTextsForTest().front(), "Reverb");
}

TEST(MixerInsertListAddTests, ThePlusOnTheLineBetweenTwoRowsAddsRightThere) {
    SplitListRig r;
    const auto eq = r.add("Parametric EQ", 300, 400);
    const auto comp = r.add("Compressor", 600, 400);
    r.wire(r.source, eq);
    r.wire(eq, comp);
    r.wire(comp, r.strip);
    r.refresh();

    const int boundary = r.list.getRowBounds(1).getY();
    const auto target = r.list.gapTargetAt({40, boundary});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->predecessor, eq);
    EXPECT_EQ(target->successor, comp);
    EXPECT_EQ(r.list.tooltipAt({40, boundary}), "Add an effect here");
    EXPECT_FALSE(r.list.gapTargetAt({40, boundary + 9}).has_value()) << "mid-row is the row, not the line";

    const auto top = r.list.gapTargetAt({40, 0});
    ASSERT_TRUE(top.has_value()) << "above the first row: between the source and it";
    EXPECT_EQ(top->predecessor, r.source);

    const auto before = nodeIds(r);
    r.list.addModuleAt("Chorus", *target);
    const auto chorus = newNode(r, before);
    ASSERT_NE(chorus.uid, 0u);
    EXPECT_TRUE(r.wired(eq, chorus));
    EXPECT_TRUE(r.wired(chorus, comp));
    EXPECT_EQ(r.rows()[1], "Insert 2, Chorus");
}

TEST(MixerInsertListAddTests, AddToPathGoesAtTheEndOfThatPathAndADryPathGetsItsFirstModule) {
    TicketPatch p;
    auto pathTarget = p.list.pathAddTarget(1, 1);
    ASSERT_TRUE(pathTarget.has_value());
    EXPECT_EQ(pathTarget->predecessor, p.reverb);

    auto before = nodeIds(p);
    p.list.addModuleAt("Chorus", *pathTarget);
    const auto chorus = newNode(p, before);
    ASSERT_NE(chorus.uid, 0u);
    EXPECT_TRUE(p.wired(p.reverb, chorus));
    EXPECT_TRUE(p.wired(chorus, p.vca));
    p.list.setSplitOpen(1, true);
    EXPECT_EQ(p.rows()[4], "Insert 5, Chorus, path 2");

    // Make path 1 dry, then add to it: the fork's own cable to the merge takes the module.
    p.list.removeRow(2); // Delay
    ASSERT_FALSE(p.wired(p.filter, p.delay));
    const int split = 1;
    ASSERT_TRUE(p.list.isSplitRow(split));
    std::optional<synth::ui::MixerInsertList::AddTarget> dry;
    for (int path = 0; path < 2 && !dry; ++path)
        if (auto t = p.list.pathAddTarget(split, path); t && t->predecessor == p.filter)
            dry = t;
    ASSERT_TRUE(dry.has_value());
    EXPECT_EQ(dry->successor, p.vca);
    before = nodeIds(p);
    p.list.addModuleAt("Phaser", *dry);
    const auto phaser = newNode(p, before);
    ASSERT_NE(phaser.uid, 0u);
    EXPECT_TRUE(p.wired(p.filter, phaser));
    EXPECT_TRUE(p.wired(phaser, p.vca));
    EXPECT_FALSE(p.wired(p.filter, p.vca));
}

TEST(MixerInsertListAddTests, AChainThatEndsInASplitHasNoPlusInsertButEachPathCanTakeOne) {
    SplitListRig r;
    const auto delay = r.add("Delay", 300, 250);
    const auto reverb = r.add("Reverb", 300, 550);
    r.wire(r.source, delay);
    r.wire(r.source, reverb);
    r.wire(delay, r.strip);
    r.wire(reverb, r.strip);
    r.refresh();
    EXPECT_FALSE(r.list.hasAddRow()) << "the paths meet at the strip itself: no single cable to splice into";
    EXPECT_TRUE(r.list.pathAddTarget(0, 0).has_value());
    EXPECT_TRUE(r.list.pathAddTarget(0, 1).has_value());
}

TEST(MixerInsertListAddTests, EveryEffectThePickerOffersResolvesThroughTheFactory) {
    const auto& types = synth::ui::MixerInsertList::getAddableModuleTypes();
    for (const auto* expected : {"Parametric EQ", "Compressor", "Limiter", "Gate", "Delay", "Reverb", "Chorus"})
        EXPECT_TRUE(types.contains(expected)) << expected;
    for (const auto& name : types)
        EXPECT_NE(synth::AIStateMapper::createModule(name), nullptr) << name;
}
