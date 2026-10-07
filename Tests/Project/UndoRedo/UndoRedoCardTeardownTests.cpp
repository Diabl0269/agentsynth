// Concern: which module cards a restore tears down. A restore that frees nodes (an undo of an add or a duplicate, a
// redo of a delete) detaches and drops only those nodes' cards; every other card stays the same object, bound to the
// processor that survived, and the graph still lands exactly on the snapshot.
#include "../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <gtest/gtest.h>
#include <map>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<NodeID> ids;
    int unbinds = 0; // the owner's unbind seam (mixer columns, MIDI Learn) still runs before any node is freed

    explicit Canvas(int modules) {
        undo.setGraphEditor(&editor);
        editor.setSize(2400, 1600);
        editor.onBeforeDetachAllModuleComponents = [this] { ++unbinds; };
        for (int i = 0; i < modules; ++i)
            ids.push_back(addModuleAt(editor, engine,
                                      i % 2 == 0 ? std::unique_ptr<juce::AudioProcessor>(new OscillatorModule())
                                                 : std::unique_ptr<juce::AudioProcessor>(new FilterModule()),
                                      100 + 260 * (i % 8), 100 + 300 * (i / 8)));
        for (int i = 0; i + 1 < modules; i += 2)
            engine.getGraph().addConnection({{ids[(size_t)i], 0}, {ids[(size_t)i + 1], 0}});
    }

    std::map<juce::uint32, ModuleComponent*> cards() {
        std::map<juce::uint32, ModuleComponent*> out;
        for (auto* comp : editor.getModuleComponents())
            out[comp->getNodeId().uid] = comp;
        return out;
    }
    // cards(), each tagged: a new card built at a freed card's address carries no tag.
    std::map<juce::uint32, ModuleComponent*> taggedCards() {
        for (auto* comp : editor.getModuleComponents())
            comp->getProperties().set("seen", true);
        return cards();
    }
    juce::String json() { return juce::JSON::toString(synth::AIStateMapper::graphToJSON(engine.getGraph())); }

    // Adds a VCA wired after the first oscillator as one structural undo step; returns its node.
    NodeID addVcaStep() {
        NodeID added;
        undo.recordStructuralChange(engine.getGraph(), [this, &added] {
            added = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 1800, 1400);
            engine.getGraph().addConnection({{ids[0], 0}, {added, 0}});
        });
        return added;
    }
};

// Every card in `before` except `gone` is still the same object, and `gone` has no card any more.
void expectOnlyTornDown(Canvas& c, const std::map<juce::uint32, ModuleComponent*>& before, NodeID gone) {
    const auto after = c.cards();
    EXPECT_EQ(after.count(gone.uid), 0u) << "the freed node's card is gone";
    for (const auto& [uid, card] : before)
        if (uid != gone.uid) {
            ASSERT_EQ(after.count(uid), 1u) << uid;
            ASSERT_EQ(after.at(uid), card) << "a surviving node keeps its card object";
            EXPECT_TRUE(card->getProperties().contains("seen")) << "the same card, not a new one at its address";
            EXPECT_EQ(card->getModule(), c.engine.getGraph().getNodeForId(NodeID(uid))->getProcessor());
        }
}

} // namespace

TEST(UndoRedoCardTeardown, UndoOfAnAddDropsOnlyTheAddedCard) {
    for (int modules : {4, 40}) { // small and grown: the result must not depend on the project's size
        Canvas c(modules);
        const auto original = c.json();
        const auto added = c.addVcaStep();
        const auto withAdd = c.taggedCards();
        ASSERT_EQ(withAdd.count(added.uid), 1u);

        ASSERT_TRUE(c.undo.undo());
        EXPECT_EQ(c.json(), original) << "undo lands exactly on the pre-edit graph (" << modules << " modules)";
        expectOnlyTornDown(c, withAdd, added);
        EXPECT_EQ(c.unbinds, 1) << "the owner's UI is unbound once, before the node is freed";
        EXPECT_EQ((int)c.editor.getModuleComponents().size(), modules);
    }
}

TEST(UndoRedoCardTeardown, RedoOfADeleteDropsOnlyTheDeletedCard) {
    Canvas c(12);
    const auto victim = c.ids[3];
    const auto original = c.json();
    c.editor.setSelectedNodes({victim});
    c.editor.deleteSelection();
    const auto deleted = c.json();
    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.json(), original);
    const auto restored = c.taggedCards();
    ASSERT_EQ(restored.count(victim.uid), 1u) << "the undo rebuilt the deleted node's card";

    const int unbindsBefore = c.unbinds;
    ASSERT_TRUE(c.undo.redo());
    EXPECT_EQ(c.json(), deleted) << "redo lands exactly on the post-delete graph";
    expectOnlyTornDown(c, restored, victim);
    EXPECT_EQ(c.unbinds, unbindsBefore + 1);
}

TEST(UndoRedoCardTeardown, UndoThatFreesNothingKeepsEveryCardAndUnbindsNothing) {
    Canvas c(6);
    const auto before = c.taggedCards();
    auto* fine = c.engine.getGraph().getNodeForId(c.ids[0])->getProcessor()->getParameters()[0];
    c.undo.captureBeforeState(c.engine.getGraph());
    fine->setValueNotifyingHost(fine->getValue() > 0.5f ? 0.1f : 0.9f);
    c.undo.pushSnapshotFromCapture(c.engine.getGraph());
    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.cards(), before);
    for (auto* comp : c.editor.getModuleComponents())
        EXPECT_TRUE(comp->getProperties().contains("seen"));
    EXPECT_EQ(c.unbinds, 0);
}
