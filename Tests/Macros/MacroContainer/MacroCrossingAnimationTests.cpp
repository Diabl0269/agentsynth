// The animation layered on top of the Cmd+drag hull-crossing gesture --
// GraphEditor::finalizeMacroMembershipDrag arms MacroCrossingAnimator whenever the drag actually
// crossed a hull (docs/macros/menu-and-membership.md#cable-crawl-and-module-flash-fro41), so the
// module flashes and any cable re-routed through an auto-created/removed macro port slides to its
// new anchor instead of jumping there.
//
// Every test below drives the REAL Cmd+drag gesture (MacroDragTestHelpers.h's dragBodyBy), same
// idiom as MacroDragMembershipTests.cpp, then drives the animation itself with the manual test
// seams (advanceMacroCrossingAnimForTest/finishMacroCrossingAnimForTest) rather than a real VBlank
// frame -- there is no Component peer in a headless gtest run, exactly why MacroCrossingAnimator
// is pure state with no juce_animation dependency of its own (see its header).

#include "AudioEngine/AudioEngine.h"
#include "MacroDragTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <optional>

namespace {

std::optional<GraphEditor::VisibleCable> cableLandingOn(GraphEditor& editor, NodeID dst) {
    for (const auto& c : editor.buildVisibleCables())
        if (c.destNodeId == dst.uid)
            return c;
    return std::nullopt;
}

// Asserts `mid` lies on or between `a`/`b` (inclusive, with a little float slack), and strictly
// differs from both -- the shape of "somewhere mid-tween", not a hard-coded fraction. Skipped on
// an axis where `a`/`b` are too close to tell mid-tween movement apart from float noise.
void expectStrictlyBetween(float mid, float a, float b) {
    if (std::abs(b - a) < 0.5f)
        return;
    const float lo = std::min(a, b) - 0.5f;
    const float hi = std::max(a, b) + 0.5f;
    EXPECT_GE(mid, lo);
    EXPECT_LE(mid, hi);
    EXPECT_NE(mid, a);
    EXPECT_NE(mid, b);
}

// An LFO at x = `lfoX` (outside the macro's hull when large) dragged with real jack/knob drag calls onto the
// Position knob of a Wavetable member of an expanded macro. Returns the release point (canvas coordinates).
struct KnobDropRig {
    AudioEngine engine;
    GraphEditor editor{engine};
    NodeID lfo, member;
    juce::Point<float> dropPoint;

    explicit KnobDropRig(bool lfoInsideMacro) {
        editor.setSize(1600, 1200);
        member = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 100, 100);
        auto filler = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 500);
        std::vector<NodeID> members{member, filler};
        if (lfoInsideMacro) {
            lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 900);
            members.push_back(lfo);
        } else {
            lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 900, 100);
        }
        editor.setSelectedNodes(members);
        const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(macroId, false);
    }

    void dropLfoOnTheKnob() {
        auto* memberComp = findComponent(editor, member);
        juce::Slider* position = nullptr;
        for (auto* child : memberComp->getChildren())
            if (auto* sl = dynamic_cast<juce::Slider*>(child))
                if (sl->getComponentID() == "Position")
                    position = sl;
        ASSERT_NE(position, nullptr);
        const auto at = memberComp->getBounds().getPosition() + position->getBounds().getCentre();
        dropPoint = editor.getChildComponent(0)->getLocalPoint(nullptr, at).toFloat();
        editor.beginConnectionDrag(findComponent(editor, lfo), 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
        editor.dragConnection(at);
        editor.endConnectionDrag(at);
    }

    // The visible cables that end on a macro port, as (cable, point at the port end).
    std::vector<std::pair<GraphEditor::VisibleCable, juce::Point<float>>> portEnds() {
        std::vector<std::pair<GraphEditor::VisibleCable, juce::Point<float>>> ends;
        for (const auto& c : editor.buildVisibleCables()) {
            if (editor.getMacroController().nodeIsMacroPort(NodeID{c.id.srcUid}))
                ends.push_back({c, c.p1});
            if (editor.getMacroController().nodeIsMacroPort(NodeID{c.id.dstUid}))
                ends.push_back({c, c.p2});
        }
        return ends;
    }
};

} // namespace

// ---------------------------------------------------------------------------------------------
// 1/2/3. A join that re-routes a cable through a spliced port: armed on finalize, mid-tween the
//    endpoint sits strictly between the old and new anchor, and finishing lands it exactly on the
//    new one with the interpolation state cleared.
// ---------------------------------------------------------------------------------------------

TEST(MacroCrossingAnimation, CmdDragJoinSlidesTheRePortedCableFromOldAnchorToNew) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    auto f = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto g = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1300, 100);

    // Same crossing setup as MacroDragMembershipTests.cpp's
    // CmdDragJoinSplicesOutInteriorPortAndCreatesNewCrossingPort: A->F crosses the macro's
    // boundary (auto-porting it), F->G stays outside for now.
    editor.connectPorts(a, 0, f, 0, /*isMidi=*/false);
    editor.connectPorts(f, 0, g, 0, /*isMidi=*/false);

    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(/*autoCreatePorts=*/true);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    // Before the drag: F is fed by the auto-created port, not directly by A -- this is the OLD
    // anchor the animation should slide away from.
    const auto beforeDrag = cableLandingOn(editor, f);
    ASSERT_TRUE(beforeDrag.has_value()) << "sanity: F starts fed by exactly one visible cable (the port)";
    const auto oldAnchor = beforeDrag->p1;

    auto* compF = findComponent(editor, f);
    ASSERT_NE(compF, nullptr);
    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());
    dragBodyBy(*compF, hull.getCentre() - compF->getBounds().getCentre(), kCmdClick, [&] {
        EXPECT_TRUE(editor.hasMacroDragCandidate()) << "sanity: dragging F into the hull must arm the JOIN candidate";
    });
    ASSERT_NE(editor.getMacroController().macroForNode(f), nullptr) << "sanity: F joined the macro";

    // (1) Armed on finalize.
    ASSERT_TRUE(editor.isMacroCrossingAnimLiveForTest())
        << "a join that re-routes a cable through a spliced port must arm the animation";

    // Immediately after finalize (progress still 0), the cable landing on F must still show the
    // OLD anchor -- proving the tween starts from where the wire really was, not already snapped.
    const auto atStart = cableLandingOn(editor, f);
    ASSERT_TRUE(atStart.has_value());
    EXPECT_NEAR(atStart->p1.x, oldAnchor.x, 0.5f);
    EXPECT_NEAR(atStart->p1.y, oldAnchor.y, 0.5f);

    // (2) Mid-tween: strictly between the old anchor and wherever it eventually lands.
    editor.advanceMacroCrossingAnimForTest(0.5f);
    const auto atHalf = cableLandingOn(editor, f);
    ASSERT_TRUE(atHalf.has_value());

    // (3) Finish: exact final anchor (the live graph's own answer, since applyTo() is a no-op once
    // the tween is done), and the interpolation state cleared.
    editor.finishMacroCrossingAnimForTest();
    EXPECT_FALSE(editor.isMacroCrossingAnimLiveForTest()) << "finish() must clear the live tween state";
    const auto atEnd = cableLandingOn(editor, f);
    ASSERT_TRUE(atEnd.has_value());

    expectStrictlyBetween(atHalf->p1.x, oldAnchor.x, atEnd->p1.x);
    expectStrictlyBetween(atHalf->p1.y, oldAnchor.y, atEnd->p1.y);

    // The final anchor must actually have moved from the old port's position (A->F is direct
    // again, landing straight on F's jack) and must be stable -- re-reading it gives the same
    // answer, confirming nothing is still interpolating.
    EXPECT_TRUE(std::abs(atEnd->p1.x - oldAnchor.x) > 0.5f || std::abs(atEnd->p1.y - oldAnchor.y) > 0.5f)
        << "the settled anchor must differ from the old port's position -- the splice really moved it";
    const auto atEndAgain = cableLandingOn(editor, f);
    ASSERT_TRUE(atEndAgain.has_value());
    EXPECT_FLOAT_EQ(atEnd->p1.x, atEndAgain->p1.x);
    EXPECT_FLOAT_EQ(atEnd->p1.y, atEndAgain->p1.y);
}

// ---------------------------------------------------------------------------------------------
// A leave whose members were never connected has no cable to slide, but the module itself still
// crossed the hull -- the flash-only case.
// ---------------------------------------------------------------------------------------------

TEST(MacroCrossingAnimation, LeaveWithNoCrossingCablesStillArmsTheModuleFlash) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    const auto macroId = makeExpandedTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    auto* compA = findComponent(editor, a);
    ASSERT_NE(compA, nullptr);

    dragBodyBy(*compA, {2400, 0}, kCmdClick,
               [&] { EXPECT_TRUE(editor.hasMacroDragCandidate()) << "sanity: this must arm the LEAVE candidate"; });
    ASSERT_EQ(editor.getMacroController().macroForNode(a), nullptr) << "sanity: A left the macro";

    EXPECT_TRUE(editor.isMacroCrossingAnimLiveForTest())
        << "even with no cables to slide (A and B are unconnected), leaving a hull must still "
           "flash the module";

    editor.finishMacroCrossingAnimForTest();
    EXPECT_FALSE(editor.isMacroCrossingAnimLiveForTest());
}

// ---------------------------------------------------------------------------------------------
// 4. A plain drag that never crosses any hull arms nothing at all.
// ---------------------------------------------------------------------------------------------

TEST(MacroCrossingAnimation, PlainDragOutsideAnyHullArmsNoAnimation) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    NodeID a, b;
    ASSERT_FALSE(makeExpandedTwoMemberMacro(editor, engine, a, b).isEmpty());

    auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);

    dragBodyBy(*compC, {30, 30}, kCmdClick,
               [&] { EXPECT_FALSE(editor.hasMacroDragCandidate()) << "sanity: nowhere near any hull"; });

    EXPECT_FALSE(editor.isMacroCrossingAnimLiveForTest())
        << "a plain move that never crosses a hull must arm no animation at all";
}

// ---------------------------------------------------------------------------------------------
// 5. A cable dropped from outside a macro onto a knob inside it has no earlier self to slide from:
//    each new cable's port end emerges from the drop point and settles on the port.
// ---------------------------------------------------------------------------------------------

TEST(MacroCrossingAnimation, ALfoDroppedOnAKnobInsideAMacroSlidesItsPortEndInFromTheDropPoint) {
    KnobDropRig r(/*lfoInsideMacro=*/false);
    r.dropLfoOnTheKnob();

    ASSERT_TRUE(r.editor.isMacroCrossingAnimLiveForTest()) << "a drop that mints a port must arm the slide";
    const auto atStart = r.portEnds();
    ASSERT_FALSE(atStart.empty());
    for (const auto& [cable, end] : atStart) {
        EXPECT_NEAR(end.x, r.dropPoint.x, 0.5f) << "at t = 0 the port end sits where the cable was released";
        EXPECT_NEAR(end.y, r.dropPoint.y, 0.5f);
    }

    r.editor.advanceMacroCrossingAnimForTest(0.5f);
    r.editor.finishMacroCrossingAnimForTest();
    EXPECT_FALSE(r.editor.isMacroCrossingAnimLiveForTest());
    const auto settled = r.portEnds();
    ASSERT_EQ(settled.size(), atStart.size());
    for (const auto& [cable, end] : settled)
        EXPECT_TRUE(std::abs(end.x - r.dropPoint.x) > 0.5f || std::abs(end.y - r.dropPoint.y) > 0.5f)
            << "after finish the end rests on the port's own anchor";
}

TEST(MacroCrossingAnimation, ASameLevelKnobDropArmsNothing) {
    KnobDropRig r(/*lfoInsideMacro=*/true);
    r.dropLfoOnTheKnob();

    EXPECT_FALSE(r.editor.isMacroCrossingAnimLiveForTest()) << "no port was minted, so nothing slides";
}
