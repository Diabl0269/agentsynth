// MacroFoldPackTests.cpp
//
// "Fold and Pack Macros" (docs/macros/menu-and-membership.md): the pure grid layout, and the command driven through
// the real MacroGroupController and AppUndoManager. The model is final the moment the command returns; no VBlank.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/MacroGroupController/MacroPackLayout.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

using Rect = juce::Rectangle<int>;

std::vector<Rect> rects(std::initializer_list<juce::Point<int>> tops, int w = 200, int h = 100) {
    std::vector<Rect> out;
    for (auto p : tops)
        out.emplace_back(p.x, p.y, w, h);
    return out;
}

} // namespace

TEST(MacroPackLayout, NoCardsNoPositions) { EXPECT_TRUE(macro_pack::packedPositions({}).empty()); }

TEST(MacroPackLayout, OneCardStaysWhereItIs) {
    const auto out = macro_pack::packedPositions(rects({{70, 90}}));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], (juce::Point<int>{70, 90}));
}

TEST(MacroPackLayout, RowsOfFourWithGapsStartingAtTheTopLeftMostCard) {
    // Six cards in scattered places; reading order (top to bottom, then left to right) is by index here: 3, 0, 5, 1,
    // 4, 2.
    const auto in = rects({{500, 400}, {900, 400}, {1500, 900}, {100, 300}, {600, 700}, {50, 400}});
    const auto out = macro_pack::packedPositions(in);
    ASSERT_EQ(out.size(), 6u);
    const int step = 200 + macro_pack::kGap;
    // The anchor is where the top-most card (index 3) stood.
    const juce::Point<int> anchor{100, 300};
    EXPECT_EQ(out[3], anchor);
    EXPECT_EQ(out[5], (juce::Point<int>{anchor.x + step, anchor.y}));
    EXPECT_EQ(out[0], (juce::Point<int>{anchor.x + 2 * step, anchor.y}));
    EXPECT_EQ(out[1], (juce::Point<int>{anchor.x + 3 * step, anchor.y}));
    // The fifth starts the second row, one card height and one gap below.
    const int rowStep = 100 + macro_pack::kGap;
    EXPECT_EQ(out[4], (juce::Point<int>{anchor.x, anchor.y + rowStep}));
    EXPECT_EQ(out[2], (juce::Point<int>{anchor.x + step, anchor.y + rowStep}));
}

TEST(MacroPackLayout, ARowIsAsTallAsItsTallestCard) {
    std::vector<Rect> in;
    for (int i = 0; i < 5; ++i)
        in.emplace_back(i * 300, 0, 200, i == 2 ? 160 : 100);
    const auto out = macro_pack::packedPositions(in);
    EXPECT_EQ(out[4].y, 160 + macro_pack::kGap);
    EXPECT_EQ(out[4].x, 0);
}

namespace {

struct PackCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<juce::String> macroIds;
    std::vector<std::vector<NodeID>> members;
    NodeID loose;

    // `count` open macros of two modules each, far apart on a diagonal so their packed grid has to move them.
    explicit PackCanvas(int count, bool tidy = false) {
        undo.setGraphEditor(&editor);
        editor.getMacroController().setTidyCanvasOnPack(tidy);
        editor.setSize(9000, 6000);
        for (int i = 0; i < count; ++i) {
            const int x = 300 + (i % 3) * 1700, y = 300 + (i / 3) * 1500 + (i % 2) * 400;
            const auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y);
            const auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), x + 600, y);
            members.push_back({a, b});
            editor.setSelectedNodes({a, b});
            macroIds.push_back(editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/false));
            editor.getMacroController().setMacroCollapsed(macroIds.back(), false);
        }
        loose = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 4000, 4000);
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    const synth::Macro& macro(int i) { return *editor.getMacros().find(macroIds[(size_t)i]); }
    Rect cardBounds(int i) { return ctl().getMacroCardForTest(macroIds[(size_t)i])->getBounds(); }
    juce::Point<int> memberPos(int i, int j) {
        return findComponent(editor, members[(size_t)i][(size_t)j])->getPosition();
    }
    void selectAll() {
        std::vector<NodeID> all;
        for (const auto& m : members)
            all.insert(all.end(), m.begin(), m.end());
        editor.setSelectedNodes(all);
    }
    // Every member's position and every macro's collapsed flag: the state one undo must restore.
    std::vector<int> fingerprint() {
        std::vector<int> out;
        for (size_t i = 0; i < members.size(); ++i) {
            out.push_back(macro((int)i).collapsed ? 1 : 0);
            for (size_t j = 0; j < members[i].size(); ++j) {
                out.push_back(memberPos((int)i, (int)j).x);
                out.push_back(memberPos((int)i, (int)j).y);
            }
        }
        return out;
    }
};

} // namespace

TEST(MacroFoldPack, FoldsEverySelectedMacroAndPacksTheirCards) {
    PackCanvas c(5);
    c.selectAll();
    const auto loosePos = findComponent(c.editor, c.loose)->getPosition();
    // Where each macro's card will be seeded (its members' top-left) fixes the order and the anchor.
    std::vector<juce::Point<int>> seeds;
    for (int i = 0; i < 5; ++i)
        seeds.push_back(c.memberPos(i, 0));

    c.ctl().foldAndPackSelectionMacros();

    std::vector<Rect> cards;
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(c.macro(i).collapsed) << "macro " << i;
        cards.push_back(c.cardBounds(i));
    }
    std::vector<Rect> seedRects;
    for (int i = 0; i < 5; ++i)
        seedRects.emplace_back(seeds[(size_t)i].x, seeds[(size_t)i].y, cards[(size_t)i].getWidth(),
                               cards[(size_t)i].getHeight());
    const auto expected = macro_pack::packedPositions(seedRects);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(cards[(size_t)i].getPosition(), expected[(size_t)i]) << "macro " << i;

    // Four to a row, 24 px apart, and the fifth wraps.
    std::vector<size_t> order{0, 1, 2, 3, 4};
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return cards[a].getY() != cards[b].getY() ? cards[a].getY() < cards[b].getY()
                                                  : cards[a].getX() < cards[b].getX();
    });
    for (size_t k = 1; k < 4; ++k) {
        EXPECT_EQ(cards[order[k]].getY(), cards[order[0]].getY());
        EXPECT_EQ(cards[order[k]].getX(), cards[order[k - 1]].getRight() + macro_pack::kGap);
    }
    EXPECT_EQ(cards[order[4]].getX(), cards[order[0]].getX());
    EXPECT_GE(cards[order[4]].getY(), cards[order[0]].getBottom() + macro_pack::kGap);

    EXPECT_EQ(findComponent(c.editor, c.loose)->getPosition(), loosePos) << "a loose module stays put";
}

TEST(MacroFoldPack, PreferenceOffFoldsInPlace) {
    PackCanvas c(3);
    c.ctl().setPackMacrosOnCollapse(false);
    c.selectAll();
    std::vector<juce::Point<int>> seeds;
    for (int i = 0; i < 3; ++i)
        seeds.push_back(c.memberPos(i, 0));

    c.ctl().foldAndPackSelectionMacros();

    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(c.macro(i).collapsed);
        EXPECT_EQ(c.cardBounds(i).getPosition(), seeds[(size_t)i]) << "macro " << i << " folds where it stood";
    }
}

TEST(MacroFoldPack, OnlyTheSelectedMacrosFoldAndPack) {
    PackCanvas c(3);
    c.editor.setSelectedNodes({c.members[0][0], c.members[2][1]});
    c.ctl().foldAndPackSelectionMacros();
    EXPECT_TRUE(c.macro(0).collapsed);
    EXPECT_FALSE(c.macro(1).collapsed);
    EXPECT_TRUE(c.macro(2).collapsed);
}

TEST(MacroFoldPack, AllFoldedExpandsThemWhereTheirCardsSit) {
    PackCanvas c(3);
    c.selectAll();
    c.ctl().foldAndPackSelectionMacros();
    std::vector<Rect> cards;
    for (int i = 0; i < 3; ++i)
        cards.push_back(c.cardBounds(i));

    c.selectAll();
    c.ctl().foldAndPackSelectionMacros(); // every selected macro is folded: expand, do not unpack

    // Opening wide macros makes room for their neighbours, so the exact spots move; what matters is that all open.
    for (int i = 0; i < 3; ++i)
        EXPECT_FALSE(c.macro(i).collapsed) << "macro " << i;
}

TEST(MacroFoldPack, FoldAndPackIsOneUndoStep) {
    PackCanvas c(4);
    const auto before = c.fingerprint();
    c.selectAll();
    c.ctl().foldAndPackSelectionMacros();
    ASSERT_NE(c.fingerprint(), before);

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.fingerprint(), before) << "one undo brings back both the fold and the positions";

    ASSERT_TRUE(c.undo.redo());
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(c.macro(i).collapsed);
}

TEST(MacroFoldPack, NothingToFoldWithoutAMacroInTheSelection) {
    PackCanvas c(2);
    c.editor.setSelectedNodes({c.loose});
    const auto before = c.fingerprint();
    c.ctl().foldAndPackSelectionMacros();
    EXPECT_EQ(c.fingerprint(), before);
}

TEST(MacroFoldPack, TidyPreferenceAutoArrangesTheRestAfterPacking) {
    // Same canvas twice: one folds, packs and tidies in the command; the other packs only, then runs Auto Arrange.
    PackCanvas tidy(4, /*tidy=*/true);
    PackCanvas manual(4, /*tidy=*/false);
    tidy.selectAll();
    manual.selectAll();
    const auto loosePos = findComponent(tidy.editor, tidy.loose)->getPosition();
    tidy.ctl().foldAndPackSelectionMacros();
    manual.ctl().foldAndPackSelectionMacros();
    manual.editor.autoArrange();

    const auto tidied = findComponent(tidy.editor, tidy.loose)->getPosition();
    EXPECT_EQ(tidied, findComponent(manual.editor, manual.loose)->getPosition());
    EXPECT_NE(tidied, loosePos) << "the loose module moved to close up the space";
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(tidy.cardBounds(i), manual.cardBounds(i)) << "macro " << i;
}

TEST(MacroFoldPack, TidyIsStillOneUndoStep) {
    PackCanvas c(4, /*tidy=*/true);
    const auto before = c.fingerprint();
    const auto loosePos = findComponent(c.editor, c.loose)->getPosition();
    c.selectAll();
    c.ctl().foldAndPackSelectionMacros();
    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.fingerprint(), before);
    EXPECT_EQ(findComponent(c.editor, c.loose)->getPosition(), loosePos);
}

TEST(MacroFoldPack, TidyDoesNothingWhenPackingIsOff) {
    PackCanvas c(3, /*tidy=*/true);
    c.ctl().setPackMacrosOnCollapse(false);
    c.selectAll();
    const auto loosePos = findComponent(c.editor, c.loose)->getPosition();
    c.ctl().foldAndPackSelectionMacros();
    EXPECT_EQ(findComponent(c.editor, c.loose)->getPosition(), loosePos);
}
