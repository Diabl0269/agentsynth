// MacroMultiSelectTests.cpp
//
// Shift-click and shift-drag select several macros at once, so Fold and Pack Macros and Expand act on all of them.
// Driven through the real mouse handlers: a folded card's own mouseDown, the canvas's mouseDown/Drag/Up for an open
// macro's name chip and for a marquee.

#include "../MacroContainer/MacroContainerTestHelpers.h"

#include "Modules/OscillatorModule.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

juce::MouseEvent shiftMouse(juce::Component& comp, juce::Point<int> pos, bool dragged, bool shift = true) {
    auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    if (shift)
        mods = mods.withFlags(juce::ModifierKeys::shiftModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), mods, 0.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos.toFloat(),
                            juce::Time::getCurrentTime(), 1, dragged);
}

struct Patch {
    AudioEngine engine;
    GraphEditor editor{engine, nullptr};

    Patch() { editor.setSize(1600, 1200); }

    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    juce::String macroAt(int x, int y, bool collapsed) {
        editor.setSelectedNodes({osc(x, y), osc(x + 300, y)});
        const auto id = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(id, collapsed);
        editor.updateComponents();
        editor.clearSelection();
        return id;
    }
    bool collapsed(const juce::String& id) { return editor.getMacros().find(id)->collapsed; }
    MacroCardComponent& card(const juce::String& id) { return *editor.getMacroController().getMacroCardForTest(id); }
    void shiftClickCard(const juce::String& id) {
        auto& c = card(id);
        const juce::Point<int> p(c.getWidth() / 2, c.getHeight() / 2);
        c.mouseDown(shiftMouse(c, p, false));
        c.mouseUp(shiftMouse(c, p, false));
    }
    void shiftClickChip(const juce::String& id) {
        const auto p = editor.getMacroController().macroChipBounds(id).getCentre();
        editor.mouseDown(shiftMouse(editor, p, false));
        editor.mouseUp(shiftMouse(editor, p, false));
    }
};

} // namespace

// Regression test for FRO802: Shift-click on the canvas did not select several macros.
TEST(MacroMultiSelect, ShiftClickingTwoFoldedCardsSelectsBothAndAgainTakesOneOut) {
    Patch p;
    const auto a = p.macroAt(100, 100, true);
    const auto b = p.macroAt(100, 500, true);

    p.shiftClickCard(a);
    p.shiftClickCard(b);
    EXPECT_TRUE(p.editor.getMacroController().isMacroInSelection(a));
    EXPECT_TRUE(p.editor.getMacroController().isMacroInSelection(b)) << "each card reads as selected";

    p.shiftClickCard(a);
    EXPECT_FALSE(p.editor.getMacroController().isMacroInSelection(a)) << "a second Shift-click takes it out";
    EXPECT_TRUE(p.editor.getMacroController().isMacroInSelection(b));
}

TEST(MacroMultiSelect, ShiftClickingTheNameChipsOfOpenMacrosSelectsThemAll) {
    Patch p;
    const auto a = p.macroAt(100, 100, false);
    const auto b = p.macroAt(100, 500, false);

    p.shiftClickChip(a);
    p.shiftClickChip(b);
    EXPECT_TRUE(p.editor.getMacroController().isMacroInSelection(a));
    EXPECT_TRUE(p.editor.getMacroController().isMacroInSelection(b));

    p.shiftClickChip(a);
    EXPECT_FALSE(p.editor.getMacroController().isMacroInSelection(a));
    EXPECT_TRUE(p.editor.getMacroController().isMacroInSelection(b));
}

TEST(MacroMultiSelect, FoldAndPackFoldsEveryMacroShiftClickSelected) {
    Patch p;
    const auto a = p.macroAt(100, 100, false);
    const auto b = p.macroAt(100, 500, false);
    const auto untouched = p.macroAt(100, 900, false);

    p.shiftClickChip(a);
    p.shiftClickChip(b);
    p.editor.getMacroController().foldAndPackSelectionMacros();

    EXPECT_TRUE(p.collapsed(a));
    EXPECT_TRUE(p.collapsed(b));
    EXPECT_FALSE(p.collapsed(untouched)) << "a macro that was not selected stays open";
}

TEST(MacroMultiSelect, AShiftMarqueeAcrossFoldedCardsSelectsThem) {
    Patch p;
    const auto a = p.macroAt(100, 100, true);
    const auto b = p.macroAt(100, 500, true);
    const auto outside = p.macroAt(900, 100, true);

    const auto covered = p.card(a).getBounds().getUnion(p.card(b).getBounds()).expanded(20);
    p.editor.mouseDown(shiftMouse(p.editor, covered.getTopLeft(), false));
    p.editor.mouseDrag(shiftMouse(p.editor, covered.getBottomRight(), true));
    p.editor.mouseUp(shiftMouse(p.editor, covered.getBottomRight(), true));

    EXPECT_TRUE(p.editor.getMacroController().isMacroInSelection(a));
    EXPECT_TRUE(p.editor.getMacroController().isMacroInSelection(b));
    EXPECT_FALSE(p.editor.getMacroController().isMacroInSelection(outside));
}
