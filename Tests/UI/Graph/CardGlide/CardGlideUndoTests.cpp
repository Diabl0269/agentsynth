// CardGlideUndoTests.cpp
//
// Undo and redo of a move that glided forward glide too (docs/layout/animation.md "Undo and redo glide"): driven
// through the real AppUndoManager::undo()/redo(), with no VBlank, through GraphEditor's advance/finish seams. The
// geometry is final the moment undo returns; the cards are hidden and their snapshots slide from where they were.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include <gtest/gtest.h>
#include <map>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<NodeID> ids;

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
        ids = {addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1500, 900),
               addModuleAt(editor, engine, std::make_unique<FilterModule>(), 300, 1400),
               addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 200),
               addModuleAt(editor, engine, std::make_unique<FilterModule>(), 2000, 1800)};
        engine.getGraph().addConnection({{ids[0], 0}, {ids[1], 0}});
        engine.getGraph().addConnection({{ids[2], 0}, {ids[3], 0}});
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    ModuleComponent* comp(NodeID id) { return findComponent(editor, id); }
    std::map<juce::uint32, juce::Rectangle<int>> bounds() {
        std::map<juce::uint32, juce::Rectangle<int>> out;
        for (auto id : ids)
            out[id.uid] = comp(id)->getBounds();
        return out;
    }
    void land() { editor.finishCardGlideForTest(); }
};

// Every card that moved between `from` and `to` is hidden, sits at `to`, and is drawn at `from`.
void expectGlidingFromTo(Canvas& c, const std::map<juce::uint32, juce::Rectangle<int>>& from,
                         const std::map<juce::uint32, juce::Rectangle<int>>& to) {
    int moved = 0;
    for (auto id : c.ids) {
        auto* comp = c.comp(id);
        ASSERT_NE(comp, nullptr);
        EXPECT_EQ(comp->getBounds(), to.at(id.uid)) << "geometry is final at once";
        if (from.at(id.uid) == to.at(id.uid))
            continue;
        ++moved;
        EXPECT_EQ(comp->getAlpha(), 0.0f) << "the real card is hidden while its snapshot glides";
        EXPECT_EQ(c.glide().currentRectFor(comp), from.at(id.uid)) << "drawn where it was, not at the end";
    }
    EXPECT_GT(moved, 0);
}

} // namespace

TEST(CardGlideUndo, UndoOfAnArrangeGlidesBackAndRedoGlidesForward) {
    Canvas c;
    const auto scrambled = c.bounds();
    c.editor.autoArrange();
    c.land();
    const auto arranged = c.bounds();
    ASSERT_NE(scrambled, arranged);
    const int arms = c.glide().armCount();

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.glide().armCount(), arms + 1);
    EXPECT_TRUE(c.glide().isLive());
    expectGlidingFromTo(c, arranged, scrambled);

    c.editor.advanceCardGlideForTest(0.5f);
    for (auto id : c.ids)
        if (arranged.at(id.uid) != scrambled.at(id.uid)) {
            const auto mid = c.glide().currentRectFor(c.comp(id));
            EXPECT_NE(mid, arranged.at(id.uid));
            EXPECT_NE(mid, scrambled.at(id.uid));
        }

    c.land();
    EXPECT_FALSE(c.glide().isLive());
    EXPECT_EQ(c.bounds(), scrambled);
    for (auto id : c.ids)
        EXPECT_EQ(c.comp(id)->getAlpha(), 1.0f);

    ASSERT_TRUE(c.undo.redo());
    EXPECT_EQ(c.glide().armCount(), arms + 2);
    expectGlidingFromTo(c, scrambled, arranged);
    c.land();
    EXPECT_EQ(c.bounds(), arranged);
    for (auto id : c.ids)
        EXPECT_EQ(c.comp(id)->getAlpha(), 1.0f);
}
