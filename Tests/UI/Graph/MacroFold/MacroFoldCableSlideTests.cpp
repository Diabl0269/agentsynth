// MacroFoldCableSlideTests.cpp
//
// A cable crossing a macro's border (outside module -> macro port) ends on the closed card's jack when the macro is
// folded and on the open border's port when it is open. Folding or unfolding, that end slides between the two with the
// border instead of jumping, and the canvas does not draw the cable itself while it slides
// (docs/layout/macro-fold-motion.md "A cable crossing the border slides with it"). Driven through the real
// setMacroCollapsed, stepped with MacroFoldAnimator::applyAtMs.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

using VisibleCable = graph_editor_types::VisibleCable;

struct CrossingCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a, b, outside;
    juce::String macroId;

    struct SystemMotionPin {
        SystemMotionPin() { synth::ui::setReducedMotionForTest(false); }
        ~SystemMotionPin() { synth::ui::setReducedMotionForTest(std::nullopt); }
    } systemMotionPin;

    // A and B in a macro with auto ports; a cable from a module outside it into B runs through a macro port.
    explicit CrossingCanvas(bool startCollapsed) {
        undo.setGraphEditor(&editor);
        editor.setSize(6000, 4000);
        a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1200, 300);
        b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1700, 300);
        outside = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 200, 1500);
        editor.connectPorts(a, 0, b, 1, /*isMidi=*/false);
        editor.connectPorts(outside, 0, b, 0, /*isMidi=*/false);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
        toggle(startCollapsed);
        land();
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    MacroFoldAnimator& fold() { return glide().fold(); }
    void toggle(bool collapsed) { editor.getMacroController().setMacroCollapsed(macroId, collapsed); }
    void land() {
        editor.finishCardGlideForTest();
        editor.finishHullGlideForTest();
    }

    // The canvas cable leaving `outside` (it ends on the macro's port, never on A or B).
    std::optional<VisibleCable> crossing() {
        for (const auto& c : editor.buildVisibleCables())
            if (c.id.srcUid == outside.uid)
                return c;
        return std::nullopt;
    }
    std::optional<VisibleCable> drawnByFold(const graph_editor_types::CableId& id) {
        for (const auto& d : fold().drawnCables())
            if (d.cable.id == id)
                return d.cable;
        return std::nullopt;
    }
};

float distance(juce::Point<float> p, juce::Point<float> q) { return p.getDistanceFrom(q); }

void expectSlides(CrossingCanvas& c, bool collapsing) {
    const auto before = c.crossing();
    ASSERT_TRUE(before.has_value());
    c.glide().setForceAnimateForTest(true);
    c.toggle(collapsing);
    ASSERT_TRUE(c.fold().isLive());

    // Where it ends once settled, measured on a copy that lands at once.
    CrossingCanvas settledCanvas(!collapsing);
    settledCanvas.toggle(collapsing);
    settledCanvas.land();
    const auto after = settledCanvas.crossing();
    ASSERT_TRUE(after.has_value());
    ASSERT_GT(distance(before->p2, after->p2), 20.0f) << "sanity: the end on the macro moves between card and border";

    EXPECT_FALSE(c.crossing().has_value()) << "the canvas leaves the sliding cable to the fold";
    auto first = c.drawnByFold(before->id);
    ASSERT_TRUE(first.has_value()) << "the fold draws the crossing cable";
    EXPECT_LT(distance(first->p2, before->p2), 1.0f) << "first frame: the end is where it was";
    EXPECT_LT(distance(first->p1, before->p1), 1.0f) << "the outside end stays put";

    c.fold().applyAtMs(c.fold().totalMs() * 0.25);
    auto mid = c.drawnByFold(before->id);
    ASSERT_TRUE(mid.has_value());
    EXPECT_GT(distance(mid->p2, before->p2), 1.0f) << "mid: it has left the old end";
    EXPECT_GT(distance(mid->p2, after->p2), 1.0f) << "mid: it has not reached the new end yet";

    c.land();
    EXPECT_FALSE(c.fold().isLive());
    const auto landed = c.crossing();
    ASSERT_TRUE(landed.has_value()) << "the canvas draws it again once the fold lands";
    EXPECT_LT(distance(landed->p2, after->p2), 1.0f);
}

} // namespace

TEST(MacroFoldCableSlide, ExpandingSlidesTheCrossingCableFromTheCardJackToTheBorderPort) {
    CrossingCanvas c(/*startCollapsed=*/true);
    expectSlides(c, /*collapsing=*/false);
}

TEST(MacroFoldCableSlide, CollapsingSlidesTheCrossingCableFromTheBorderPortToTheCardJack) {
    CrossingCanvas c(/*startCollapsed=*/false);
    expectSlides(c, /*collapsing=*/true);
}

TEST(MacroFoldCableSlide, CollapsingFadesTheInnerCableToThePortWithItsModule) {
    CrossingCanvas c(/*startCollapsed=*/false);
    std::optional<graph_editor_types::CableId> inner;
    for (const auto& cable : c.editor.buildVisibleCables())
        if (cable.id.dstUid == c.b.uid && cable.id.srcUid != c.a.uid)
            inner = cable.id; // macro port -> B
    ASSERT_TRUE(inner.has_value());
    c.glide().setForceAnimateForTest(true);
    c.toggle(true);
    EXPECT_TRUE(c.drawnByFold(*inner).has_value()) << "it rides B into the card instead of vanishing at once";
}

TEST(MacroFoldCableSlide, ReduceMotionAndOffDrawTheCableAtItsNewEndAtOnce) {
    for (const bool off : {false, true}) {
        CrossingCanvas c(/*startCollapsed=*/true);
        if (off)
            synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
        else
            synth::ui::setReducedMotionForTest(true);
        c.glide().setForceAnimateForTest(true);
        c.toggle(false);
        EXPECT_TRUE(c.fold().drawnCables().empty()) << (off ? "Off" : "Reduce Motion");
        EXPECT_TRUE(c.crossing().has_value()) << "the canvas draws it where it ends";
        c.land();
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
    }
}
