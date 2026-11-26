// CanvasFrameTests.cpp
//
// The growing canvas frame: pure sizing (CanvasFrame::targetFor), the update modes, and the GraphEditor glue (frame
// follows the outermost card, content is target + slack, no 10000 px wall, drops at x=20000 stay put), and that every
// way of moving a macro toward the left/top edge leaves units and frame inside it, once the frame glide has landed.
// (docs/layout/layout.md#canvas-frame)

#include "../../../Macros/MacroContainer/MacroDragTestHelpers.h"
#include "../../../Macros/MacroContainer/MacroUnitDragHelpers.h"

#include "AppUndoManager.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/CanvasFrame/CanvasFrame.h"
#include <gtest/gtest.h>

namespace {

using Mode = CanvasFrame::Mode;

struct Rig {
    juce::Component host;
    juce::VBlankAnimatorUpdater updater{&host};
    CanvasFrame frame{updater};
    int changes = 0;
    Rig() {
        frame.onChanged = [this] { ++changes; };
    }
};

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    CanvasFrame& frame() { return editor.getCanvasFrameForTest(); }
    void land() {
        editor.updateComponents();
        frame().finishAnimationForTest();
    }
};

// A collapsed macro, an open macro (two members) and a loose module, all well inside the canvas.
struct Patch {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::String foldedId, openId;
    NodeID foldedA, foldedB, memberA, memberB, loose;

    Patch() {
        undo.setGraphEditor(&editor);
        editor.setSize(4400, 3600);
        foldedA = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 160, 100);
        foldedB = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 560, 100);
        foldedId = group({foldedA, foldedB});
        memberA = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 250, 300);
        memberB = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 600, 300);
        openId = group({memberA, memberB});
        loose = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
        editor.getMacroController().setMacroCollapsed(foldedId, true);
        editor.getMacroController().setMacroCollapsed(openId, false);
        editor.clearSelection();
        settle();
        undo.clearUndoHistory();
    }

    juce::String group(const std::vector<NodeID>& nodes) {
        editor.setSelectedNodes(nodes);
        return editor.getMacroController().groupSelectionIntoMacro();
    }

    // What the app reaches on its own: the pending rebuild and the frame's glide landing.
    void settle() {
        editor.updateComponents();
        editor.finishCardGlideForTest();
        // Only a glide that is running lands; a frame left off the origin with no glide must show as it is.
        if (editor.getCanvasFrameForTest().isAnimating())
            editor.getCanvasFrameForTest().finishAnimationForTest();
    }

    // The invariant: no unit and no frame edge left of or above the origin, and the frame holds every unit.
    void expectInsideFrame(const juce::String& step) {
        settle();
        const auto frame = editor.getCanvasFrameForTest().current();
        EXPECT_GE(frame.getX(), 0.0f) << step << ": frame left edge";
        EXPECT_GE(frame.getY(), 0.0f) << step << ": frame top edge";
        for (const auto& unit : editor.getMacroController().buildLayoutUnits({})) {
            EXPECT_GE(unit.rect.getX(), 0) << step << ": " << unit.key << " left edge";
            EXPECT_GE(unit.rect.getY(), 0) << step << ": " << unit.key << " top edge";
            EXPECT_TRUE(frame.contains(unit.rect.toFloat())) << step << ": " << unit.key << " sits outside the frame";
        }
    }

    // The gesture, then its undo and redo, each checked.
    void expectHoldsAfter(const std::function<void()>& gesture, const juce::String& what) {
        expectInsideFrame(what + " (before)");
        gesture();
        expectInsideFrame(what + " (after the drop)");
        ASSERT_TRUE(undo.undo()) << what;
        expectInsideFrame(what + " (undone)");
        ASSERT_TRUE(undo.redo()) << what;
        expectInsideFrame(what + " (redone)");
    }
};

} // namespace

TEST(CanvasFrameTarget, EmptyUnionIsTheStartSize) {
    EXPECT_EQ(CanvasFrame::targetFor({}), juce::Rectangle<int>(0, 0, 1800, 1100));
}

TEST(CanvasFrameTarget, ContentBelowTheFloorStaysAtTheStartSize) {
    EXPECT_EQ(CanvasFrame::targetFor({100, 100, 300, 300}), juce::Rectangle<int>(0, 0, 1800, 1100));
}

TEST(CanvasFrameTarget, GrowsInFourHundredPixelStepsPastTheOutermostCard) {
    // Right 2050 + 400 = 2450 -> 2800; bottom 1250 + 400 = 1650 -> 2000.
    EXPECT_EQ(CanvasFrame::targetFor({1000, 900, 1050, 350}), juce::Rectangle<int>(0, 0, 2800, 2000));
    // Exactly on a step: 2000 + 400 = 2400 stays 2400.
    EXPECT_EQ(CanvasFrame::targetFor({1800, 100, 200, 100}).getWidth(), 2400);
}

TEST(CanvasFrameUpdate, GrowOnlyIgnoresShrinkAndAnimateShrinks) {
    Rig r;
    r.frame.update({0, 0, 5000, 100}, Mode::Snap);
    ASSERT_EQ(r.frame.target().getWidth(), 5600);
    const int before = r.changes;
    r.frame.update({0, 0, 100, 100}, Mode::GrowOnly);
    EXPECT_EQ(r.frame.target().getWidth(), 5600);
    EXPECT_EQ(r.changes, before) << "an unchanged target fires nothing";
    r.frame.update({0, 0, 100, 100}, Mode::Animate);
    EXPECT_EQ(r.frame.target(), juce::Rectangle<int>(0, 0, 1800, 1100));
    r.frame.finishAnimationForTest();
    EXPECT_EQ(r.frame.current(), juce::Rectangle<float>(0.0f, 0.0f, 1800.0f, 1100.0f));
}

TEST(CanvasFrameUpdate, SnapIsInstantAndAnimateIsNot) {
    Rig r;
    r.frame.update({0, 0, 3000, 100}, Mode::Snap);
    EXPECT_FALSE(r.frame.isAnimating());
    EXPECT_EQ(r.frame.current().getWidth(), 3600.0f);

    r.frame.update({0, 0, 5000, 100}, Mode::Animate);
    EXPECT_TRUE(r.frame.isAnimating());
    EXPECT_EQ(r.frame.current().getWidth(), 3600.0f) << "the tween has not run a frame yet";
    EXPECT_EQ(r.frame.target().getWidth(), 5600);
}

TEST(CanvasFrameUpdate, SameTargetFiresNoCallback) {
    Rig r;
    r.frame.update({0, 0, 3000, 100}, Mode::Snap);
    const int before = r.changes;
    for (auto mode : {Mode::Animate, Mode::GrowOnly, Mode::Snap})
        r.frame.update({0, 0, 3000, 100}, mode);
    EXPECT_EQ(r.changes, before);
}

TEST(CanvasFrameUpdate, SnapRequestedAppliesToTheNextAnimateUpdateOnly) {
    Rig r;
    r.frame.requestSnapOnNextUpdate();
    r.frame.update({0, 0, 3000, 100}, Mode::GrowOnly); // the 30 Hz tick must not eat the request
    r.frame.update({0, 0, 4000, 100}, Mode::Animate);
    EXPECT_FALSE(r.frame.isAnimating());
    EXPECT_EQ(r.frame.current().getWidth(), 4400.0f);
    r.frame.update({0, 0, 6000, 100}, Mode::Animate);
    EXPECT_TRUE(r.frame.isAnimating());
}

TEST(CanvasFrameUpdate, ShiftMovesTheAnimatedRect) {
    Rig r;
    r.frame.shiftCurrentBy({-50.0f, 20.0f});
    EXPECT_EQ(r.frame.current().getPosition(), juce::Point<float>(-50.0f, 20.0f));
}

TEST(CanvasFrameEditor, FrameFollowsTheOutermostCardAndShrinksBack) {
    Canvas c;
    EXPECT_EQ(c.editor.getCanvasFrameRect(), juce::Rectangle<float>(0.0f, 0.0f, 1800.0f, 1100.0f));

    const auto id = c.osc(5000, 300);
    c.land();
    const auto* comp = findComponent(c.editor, id);
    ASSERT_NE(comp, nullptr);
    const float minW = static_cast<float>(5000 + comp->getWidth() + CanvasFrame::kPad);
    EXPECT_GE(c.editor.getCanvasFrameRect().getWidth(), minW);

    // Content covers the frame TARGET plus slack, so a fast drag never clips a card.
    auto* content = c.editor.getModuleComponents()[0]->getParentComponent();
    ASSERT_NE(content, nullptr);
    EXPECT_GE(content->getWidth(), static_cast<int>(c.editor.getCanvasFrameRect().getWidth()));
    EXPECT_EQ(content->getWidth(), c.frame().target().getWidth() + CanvasFrame::kContentSlack);

    c.engine.getGraph().removeNode(id);
    c.land();
    EXPECT_EQ(c.editor.getCanvasFrameRect().getWidth(), 1800.0f);
    EXPECT_EQ(c.editor.getCanvasFrameRect().getHeight(), 1100.0f);
}

TEST(CanvasFrameEditor, ADropFarRightIsNotClampedToTenThousand) {
    Canvas c;
    const auto id = c.osc(300, 300);
    auto* comp = findComponent(c.editor, id);
    ASSERT_NE(comp, nullptr);
    comp->setTopLeftPosition(20000, 300);
    c.editor.finalizeModuleDrag(comp);
    EXPECT_EQ(comp->getX(), 20000);
    EXPECT_GE(c.frame().target().getWidth(), 20000 + comp->getWidth() + CanvasFrame::kPad);
}

TEST(CanvasFrameHoldsPatch, AFoldedMacroCardDraggedPastTheLeftEdge) {
    Patch p;
    p.expectHoldsAfter([&] { dragCardCentreTo(p.editor, p.foldedId, {-100, 200}); }, "folded card drag left");
}

TEST(CanvasFrameHoldsPatch, AFoldedMacroCardDraggedPastTheTopEdge) {
    Patch p;
    p.expectHoldsAfter([&] { dragCardCentreTo(p.editor, p.foldedId, {400, -300}); }, "folded card drag up");
}

TEST(CanvasFrameHoldsPatch, AnOpenMacroDraggedByItsChipPastTheLeftEdge) {
    Patch p;
    p.expectHoldsAfter([&] { dragChipBy(p.editor, p.openId, {-450, 0}); }, "open macro chip drag left");
}

TEST(CanvasFrameHoldsPatch, AnOpenMacroDraggedByItsChipPastTheTopEdge) {
    Patch p;
    p.expectHoldsAfter([&] { dragChipBy(p.editor, p.openId, {0, -700}); }, "open macro chip drag up");
}

TEST(CanvasFrameHoldsPatch, AMemberCardDraggedPastTheLeftEdgeOfItsOpenMacro) {
    Patch p;
    p.expectHoldsAfter([&] { dragBodyBy(*findComponent(p.editor, p.memberA), {-450, 0}, kPlainClick); },
                       "member card drag");
}

TEST(CanvasFrameHoldsPatch, AMixedSelectionOfALooseCardAndAMemberDraggedPastTheEdge) {
    Patch p;
    p.expectHoldsAfter(
        [&] {
            p.editor.setSelectedNodes({p.loose, p.memberB});
            dragBodyBy(*findComponent(p.editor, p.loose), {-1000, 0}, kPlainClick);
        },
        "mixed selection drag");
}

TEST(CanvasFrameHoldsPatch, ALooseCardNudgedPastTheEdgeWithTheArrowKeys) {
    Patch p;
    p.expectHoldsAfter(
        [&] {
            p.editor.setSelectedNodes({p.loose});
            for (int i = 0; i < 130; ++i)
                p.editor.getCardKeyboard().moveSelectionBy({-1, 0});
        },
        "arrow-key nudge");
}

TEST(CanvasFrameHoldsPatch, AMemberCardNudgedPastTheEdgeWithTheArrowKeys) {
    Patch p;
    p.expectHoldsAfter(
        [&] {
            p.editor.setSelectedNodes({p.memberA});
            for (int i = 0; i < 45; ++i)
                p.editor.getCardKeyboard().moveSelectionBy({-1, 0});
        },
        "member nudge");
}

// The frame glide itself: a slide shifts the animated rect so the screen does not jump, and the rect must then glide
// back to the origin even though the target did not change.
TEST(CanvasFrameHoldsPatch, AFrameShiftedBySlideGlidesBackToTheOriginWhenTheTargetIsUnchanged) {
    juce::Component host;
    juce::VBlankAnimatorUpdater updater{&host};
    CanvasFrame frame{updater};
    frame.shiftCurrentBy({264.0f, 40.0f});
    frame.update({100, 100, 200, 200}, CanvasFrame::Mode::Animate);
    EXPECT_TRUE(frame.isAnimating());
    frame.finishAnimationForTest();
    EXPECT_EQ(frame.current().getPosition(), juce::Point<float>(0.0f, 0.0f));
}
