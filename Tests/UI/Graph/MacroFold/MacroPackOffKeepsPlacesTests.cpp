// MacroPackOffKeepsPlacesTests.cpp
//
// With "Collapsing also packs macros together" off, macros keep their places: folding and opening them again, one at a
// time or all at once, puts every macro back where it was. They only slide aside when an open macro grows into them
// (docs/macros/menu-and-membership.md#fold-and-pack-macros-cmdctrlg).

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

namespace {

struct StackCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<juce::String> macroIds;
    std::vector<std::vector<NodeID>> members;

    // `count` open macros of two modules each, stacked in one column (or one row when `sideBySide`), clear of each
    // other by `gap` px. Creating a macro opens a border wider than its members, so the gap leaves room for that and
    // the canvas starts with nothing pushed.
    StackCanvas(int count, bool sideBySide, int gap = 200) {
        undo.setGraphEditor(&editor);
        editor.getMacroController().setPackMacrosOnCollapse(false);
        editor.setSize(9000, 6000);
        int next = 200;
        for (int i = 0; i < count; ++i) {
            const int x = sideBySide ? next : 200, y = sideBySide ? 200 : next;
            const auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y);
            const auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), x + 320, y);
            members.push_back({a, b});
            editor.setSelectedNodes({a, b});
            macroIds.push_back(editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/false));
            editor.getMacroController().setMacroCollapsed(macroIds.back(), false);
            const auto hull = editor.getMacroController().macroHullBounds(macroIds.back());
            next = (sideBySide ? hull.getRight() : hull.getBottom()) + gap + 24;
        }
        for (const auto& id : macroIds)
            EXPECT_TRUE(editor.getMacros().find(id)->displaced.empty()) << "the fixture pushed a macro";
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    const synth::Macro& macro(int i) { return *editor.getMacros().find(macroIds[(size_t)i]); }
    void selectAll() {
        std::vector<NodeID> all;
        for (const auto& m : members)
            all.insert(all.end(), m.begin(), m.end());
        editor.setSelectedNodes(all);
    }
    std::vector<juce::Point<int>> positions() {
        std::vector<juce::Point<int>> out;
        for (const auto& m : members)
            for (auto id : m)
                out.push_back(findComponent(editor, id)->getPosition());
        return out;
    }
};

std::string describe(const std::vector<juce::Point<int>>& ps) {
    std::string s;
    for (auto p : ps)
        s += "(" + std::to_string(p.x) + "," + std::to_string(p.y) + ") ";
    return s;
}

} // namespace

TEST(MacroPackOff, FoldAllThenOpenAllPutsStackedMacrosBack) {
    for (const bool sideBySide : {false, true}) {
        StackCanvas c(3, sideBySide);
        const auto before = c.positions();
        for (int round = 0; round < 3; ++round) {
            c.selectAll();
            c.ctl().foldAndPackSelectionMacros(); // fold in place
            for (int i = 0; i < 3; ++i)
                ASSERT_TRUE(c.macro(i).collapsed);
            c.selectAll();
            c.ctl().foldAndPackSelectionMacros(); // open them all again
            for (int i = 0; i < 3; ++i)
                ASSERT_FALSE(c.macro(i).collapsed);
            EXPECT_EQ(c.positions(), before) << "round " << round << (sideBySide ? " side by side" : " stacked") << ": "
                                             << describe(c.positions()) << " vs " << describe(before);
        }
    }
}

TEST(MacroPackOff, FoldingAndOpeningOneMacroAtATimePutsTheOthersBack) {
    for (const bool sideBySide : {false, true}) {
        StackCanvas c(3, sideBySide);
        const auto before = c.positions();
        for (int i = 0; i < 3; ++i) {
            c.ctl().setMacroCollapsed(c.macroIds[(size_t)i], true);
            c.ctl().setMacroCollapsed(c.macroIds[(size_t)i], false);
            EXPECT_EQ(c.positions(), before) << "macro " << i << (sideBySide ? " side by side" : " stacked") << ": "
                                             << describe(c.positions()) << " vs " << describe(before);
        }
    }
}

TEST(MacroPackOff, CmdGToggleTwicePutsEveryMacroBack) {
    StackCanvas c(3, false);
    const auto before = c.positions();
    c.selectAll();
    c.ctl().groupOrToggleSelectionMacros();
    c.selectAll();
    c.ctl().groupOrToggleSelectionMacros();
    EXPECT_EQ(c.positions(), before) << describe(c.positions()) << " vs " << describe(before);
}

namespace {

// Folds every macro in place, then sets the folded cards 40 px apart: each one's open border reaches over the next.
std::vector<juce::Rectangle<int>> foldIntoATightStack(StackCanvas& c, bool sideBySide) {
    c.selectAll();
    c.ctl().foldAndPackSelectionMacros();
    for (size_t i = 1; i < c.macroIds.size(); ++i) {
        const auto prev = c.editor.getMacros().find(c.macroIds[i - 1])->bounds;
        const auto cur = c.editor.getMacros().find(c.macroIds[i])->bounds;
        const auto target = sideBySide ? juce::Point<int>(prev.getRight() + 40, prev.getY())
                                       : juce::Point<int>(prev.getX(), prev.getBottom() + 40);
        c.ctl().moveUnitBy("m:" + c.macroIds[i], target - cur.getPosition());
    }
    c.ctl().refreshAfterUnitMoves();
    std::vector<juce::Rectangle<int>> cards;
    for (const auto& id : c.macroIds)
        cards.push_back(c.editor.getMacros().find(id)->bounds);
    return cards;
}

} // namespace

// Regression test for FRO801: with packing off, opening a tight stack of folded macros pushed each one aside (right),
// but folding them all again left them pushed: a macro that had opened since its push no longer matched its push
// record, so it read as moved by the user and never came home.
TEST(MacroPackOff, OpeningAFoldedStackSlidesThemAsideAndFoldingBringsThemHome) {
    for (const bool sideBySide : {false, true}) {
        StackCanvas c(3, sideBySide);
        const auto cardsBefore = foldIntoATightStack(c, sideBySide);
        for (int round = 0; round < 3; ++round) {
            c.selectAll();
            c.ctl().foldAndPackSelectionMacros(); // open all
            for (int i = 0; i < 3; ++i)
                for (int j = i + 1; j < 3; ++j)
                    EXPECT_FALSE(c.ctl()
                                     .macroHullBounds(c.macroIds[(size_t)i])
                                     .intersects(c.ctl().macroHullBounds(c.macroIds[(size_t)j])))
                        << "open macros slide aside instead of overlapping: " << i << " and " << j;
            c.selectAll();
            c.ctl().foldAndPackSelectionMacros(); // fold all
            for (int i = 0; i < 3; ++i)
                EXPECT_EQ(c.editor.getMacros().find(c.macroIds[(size_t)i])->bounds, cardsBefore[(size_t)i])
                    << "round " << round << " macro " << i << (sideBySide ? " in a row" : " in a column");
        }
    }
}

TEST(MacroPackOff, OpeningAndFoldingOneOfAFoldedStackBringsTheOthersHome) {
    StackCanvas c(3, false);
    const auto cardsBefore = foldIntoATightStack(c, false);
    for (int i = 0; i < 3; ++i) {
        c.ctl().setMacroCollapsed(c.macroIds[(size_t)i], false);
        c.ctl().setMacroCollapsed(c.macroIds[(size_t)i], true);
        for (int k = 0; k < 3; ++k)
            EXPECT_EQ(c.editor.getMacros().find(c.macroIds[(size_t)k])->bounds, cardsBefore[(size_t)k])
                << "after macro " << i << " opened and folded, macro " << k;
    }
}
