// CableChainMotionTests.cpp
//
// A module inserted on a cable splits it in two running to the new card, deleting the middle module of a chain runs the
// two cables together into one, and the outlines around a card follow the card's own corners
// (docs/layout/insert-motion.md). The arming rules on plain cables, then the
// real drop, Delete key and undo paths on a real canvas with the cable motion forced on (the canvas is not showing).

#include "InsertGap/InsertGapTestFixture.h"

#include "UI/Graph/CableRetractAnimator/CableRetractAnimator.h"
#include "UI/Graph/CardOutline.h"
#include "UI/Graph/ModDot/ModDotCanvasPicker.h"
#include "UI/Theme/Theme.h"

using namespace insert_gap_test;
using VisibleCable = graph_editor_types::VisibleCable;

namespace {
VisibleCable cable(uint32_t src, uint32_t dst, juce::Point<float> p1, juce::Point<float> p2, int srcPort = 0,
                   int dstPort = 0) {
    VisibleCable c;
    c.id.srcUid = src;
    c.id.srcPort = srcPort;
    c.id.dstUid = dst;
    c.id.dstPort = dstPort;
    c.p1 = p1;
    c.p2 = p2;
    return c;
}
CableRetractAnimator::Options chains(bool reduced = false, bool only = false) { return {false, reduced, true, only}; }

const juce::Point<float> a1{100, 50}, c2{900, 50}, b2{400, 50}, b1{600, 50};
} // namespace

TEST(CableChainMotion, ASplitRunsBothHalvesFromTheOldCableToTheNewCard) {
    CableRetractAnimator r;
    const auto old = cable(1, 3, a1, c2);
    const auto first = cable(1, 2, a1, b2), second = cable(2, 3, b1, c2);
    const auto other = cable(7, 8, {0, 0}, {10, 10});
    ASSERT_TRUE(r.arm({old, other}, {first, second, other}, chains()));
    EXPECT_EQ(r.numSplitHalves(), 2u);
    EXPECT_TRUE(r.ghosts().empty()) << "the old cable does not retract: it is the halves";
    EXPECT_FALSE(r.isGrowing(first.id));
    EXPECT_TRUE(r.isMorphing(first.id));
    EXPECT_TRUE(r.isMorphing(second.id));
    EXPECT_FALSE(r.isMorphing(other.id));

    r.applyTweenAt(0.0f);
    EXPECT_EQ(r.morphed(first).p1, old.p1);
    EXPECT_EQ(r.morphed(first).p2, old.p2) << "both halves start as the old cable, nothing pops";
    EXPECT_EQ(r.morphed(second).p1, old.p1);
    EXPECT_EQ(r.morphed(second).p2, old.p2);
    r.applyTweenAt(0.5f);
    const auto mid = r.morphed(first);
    EXPECT_GT(mid.p2.x, b2.x);
    EXPECT_LT(mid.p2.x, c2.x) << "its end runs from the old destination to the new card";
    const float e = synth::ui::easeOutCubic(0.5f);
    EXPECT_FLOAT_EQ(mid.p2.x, c2.x + (b2.x - c2.x) * e);
    r.applyTweenAt(1.0f);
    EXPECT_EQ(r.morphed(first).p2, b2);
    EXPECT_EQ(r.morphed(second).p1, b1);
    r.finish();
    EXPECT_FALSE(r.isLive());
}

TEST(CableChainMotion, AJoinRunsTheOldHalvesOntoTheNewCableAndHidesItUntilTheyArrive) {
    CableRetractAnimator r;
    const auto first = cable(1, 2, a1, b2), second = cable(2, 3, b1, c2);
    const auto joined = cable(1, 3, a1, c2);
    ASSERT_TRUE(r.arm({first, second}, {joined}, chains()));
    EXPECT_EQ(r.numJoins(), 1u);
    EXPECT_TRUE(r.isHidden(joined.id));
    EXPECT_TRUE(r.ghosts().empty());

    r.applyTweenAt(0.0f);
    auto halves = r.joinHalves();
    ASSERT_EQ(halves.size(), 2u);
    EXPECT_EQ(halves[0].p2, b2) << "starts as the old cables, nothing pops";
    EXPECT_EQ(halves[1].p1, b1);
    r.applyTweenAt(1.0f);
    halves = r.joinHalves();
    for (const auto& half : halves) {
        EXPECT_EQ(half.p1, joined.p1);
        EXPECT_EQ(half.p2, joined.p2) << "both halves end exactly on the new cable";
    }
    r.finish();
    EXPECT_FALSE(r.isHidden(joined.id));
    EXPECT_FALSE(r.isLive());
}

TEST(CableChainMotion, CablesThatAreNotAChainKeepTheirOrdinaryMotion) {
    CableRetractAnimator r;
    const auto gone = cable(1, 3, a1, c2);
    const auto unrelated = cable(5, 6, {0, 0}, {50, 50});
    ASSERT_TRUE(r.arm({gone}, {unrelated}, chains()));
    EXPECT_EQ(r.numSplitHalves(), 0u);
    EXPECT_EQ(r.numJoins(), 0u);
    EXPECT_EQ(r.ghosts().size(), 1u) << "a plain removal still retracts";
    CableRetractAnimator only;
    EXPECT_FALSE(only.arm({gone}, {unrelated}, chains(false, true))) << "chains-only arms nothing else";
}

TEST(CableChainMotion, ReduceMotionIsAPlainFadeNotAMorph) {
    CableRetractAnimator split;
    const auto old = cable(1, 3, a1, c2);
    const auto first = cable(1, 2, a1, b2), second = cable(2, 3, b1, c2);
    ASSERT_TRUE(split.arm({old}, {first, second}, chains(true, true)));
    EXPECT_EQ(split.numSplitHalves(), 0u);
    ASSERT_EQ(split.ghosts().size(), 1u) << "the old cable fades out";
    EXPECT_EQ(split.ghosts()[0].p2, old.p2) << "with no geometry change";
    EXPECT_TRUE(split.isGrowing(first.id)) << "and the two new ones fade in";
    EXPECT_TRUE(split.isGrowing(second.id));

    CableRetractAnimator join;
    const auto j1 = cable(1, 2, a1, b2), j2 = cable(2, 3, b1, c2), joined = cable(1, 3, a1, c2);
    ASSERT_TRUE(join.arm({j1, j2}, {joined}, chains(true, true)));
    EXPECT_EQ(join.ghosts().size(), 2u);
    EXPECT_TRUE(join.isGrowing(joined.id));
}

TEST(CableChainMotion, AStereoPairSplitsLegByLeg) {
    CableRetractAnimator r;
    const auto oldL = cable(1, 3, a1, c2, 0, 0), oldR = cable(1, 3, a1 + juce::Point<float>(0, 20), c2, 1, 1);
    const auto aL = cable(1, 2, a1, b2, 0, 0), aR = cable(1, 2, a1, b2, 1, 1);
    const auto bL = cable(2, 3, b1, c2, 0, 0), bR = cable(2, 3, b1, c2, 1, 1);
    ASSERT_TRUE(r.arm({oldL, oldR}, {aL, aR, bL, bR}, chains()));
    EXPECT_EQ(r.numSplitHalves(), 4u);
    r.applyTweenAt(0.0f);
    EXPECT_EQ(r.morphed(aR).p1, oldR.p1) << "each leg starts as its own old cable";
}

// ---- The real paths ----

namespace {
struct Wired {
    Canvas c;
    std::vector<NodeID> row;
    explicit Wired(int n = 2, AnimationMode mode = AnimationMode::full, bool singleJack = false)
        : c(mode) {
        row = c.filterRow(n, 400, 400, 120);
        for (int i = 0; i + 1 < n; ++i) {
            const int jacks =
                singleJack
                    ? 1
                    : dynamic_cast<ModuleBase*>(c.card(row[(size_t)i])->getModule())->getVisibleOutputPortCount();
            for (int j = 0; j < jacks; ++j)
                c.editor.connectPorts(row[(size_t)i], j, row[(size_t)i + 1], j, false, false);
        }
        c.editor.finishCardGlideForTest();
        c.editor.getCableRetractForTest().setForceMotionForTest(true);
        c.editor.getCardGlideForTest().setForceAnimateForTest(true); // not on screen: a glide only arms when forced
    }
    const CableRetractAnimator& retract() { return c.editor.getCableRetractForTest(); }
    std::vector<VisibleCable> cablesBetween(NodeID a, NodeID b) {
        std::vector<VisibleCable> out;
        for (const auto& cb : c.editor.buildVisibleCables())
            if (cb.id.srcUid == a.uid && cb.id.dstUid == b.uid)
                out.push_back(cb);
        return out;
    }
};
} // namespace

TEST(CableChainMotion, DroppingAModuleOnACableSplitsItIntoTwoHalvesRunningToTheNewCard) {
    Wired w;
    const auto old = w.cablesBetween(w.row[0], w.row[1]);
    ASSERT_FALSE(old.empty());
    const auto added = w.c.dropBetween("Delay", between(w.c.rect(w.row[0]), w.c.rect(w.row[1])));
    ASSERT_NE(added.uid, 0u);
    ASSERT_TRUE(w.retract().isLive());
    EXPECT_EQ(w.retract().numSplitHalves(), old.size() * 2) << "two halves for every cable that was there";

    const auto fromUp = w.cablesBetween(w.row[0], added);
    ASSERT_FALSE(fromUp.empty());
    w.c.editor.advanceCableRetractForTest(0.0f);
    EXPECT_EQ(w.retract().morphed(fromUp[0]).p2.x, old[0].p2.x) << "starts on the old cable's far end";
    w.c.editor.advanceCableRetractForTest(0.5f);
    const auto mid = w.retract().morphed(fromUp[0]).p2;
    EXPECT_LT(mid.x, old[0].p2.x);
    EXPECT_GT(mid.x, fromUp[0].p2.x) << "part of the way to the new card";
    w.c.editor.advanceCableRetractForTest(1.0f);
    EXPECT_EQ(w.retract().morphed(fromUp[0]).p2, fromUp[0].p2);
    w.c.editor.finishCableRetractForTest();
    EXPECT_FALSE(w.retract().isLive());
    EXPECT_TRUE(w.cablesBetween(w.row[0], w.row[1]).empty()) << "the direct cable is gone";
}

TEST(CableChainMotion, DeletingTheMiddleModuleRunsTheTwoCablesTogetherIntoOne) {
    Wired w(3, AnimationMode::full, /*singleJack=*/true); // the delete heals a chain of single cables
    const auto first = w.cablesBetween(w.row[0], w.row[1]);
    const auto second = w.cablesBetween(w.row[1], w.row[2]);
    ASSERT_FALSE(first.empty());
    w.c.editor.setSelectedNodes({w.row[1]});
    ASSERT_TRUE(w.c.editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    const auto joined = w.cablesBetween(w.row[0], w.row[2]);
    ASSERT_FALSE(joined.empty()) << "deleting the middle module reconnects its neighbours";
    ASSERT_TRUE(w.retract().isLive());
    EXPECT_EQ(w.retract().numJoins(), joined.size());
    EXPECT_TRUE(w.retract().isHidden(joined[0].id));

    w.c.editor.advanceCableRetractForTest(0.0f);
    const auto halves = w.retract().joinHalves();
    ASSERT_EQ(halves.size(), joined.size() * 2);
    EXPECT_EQ(halves[0].p2, first[0].p2) << "the halves start as the cables that were drawn";
    w.c.editor.advanceCableRetractForTest(1.0f);
    for (const auto& half : w.retract().joinHalves())
        EXPECT_TRUE(half.p2 == joined[0].p2 || half.p2 == joined.back().p2) << "and end on the joined cable";
    w.c.editor.finishCableRetractForTest();
    EXPECT_FALSE(w.retract().isHidden(joined[0].id));
    EXPECT_FALSE(second.empty());
}

TEST(CableChainMotion, TheCanvasNotShowingLandsTheCablesAtOnce) {
    Wired w;
    w.c.editor.getCableRetractForTest().setForceMotionForTest(false);
    const auto added = w.c.dropBetween("Delay", between(w.c.rect(w.row[0]), w.c.rect(w.row[1])));
    ASSERT_NE(added.uid, 0u);
    EXPECT_FALSE(w.retract().isLive());
}

TEST(CableChainMotion, AnimationsOffLandsTheCablesAtOnce) {
    Wired w(2, AnimationMode::off);
    const auto added = w.c.dropBetween("Delay", between(w.c.rect(w.row[0]), w.c.rect(w.row[1])));
    ASSERT_NE(added.uid, 0u);
    EXPECT_FALSE(w.retract().isLive());
}

TEST(CableChainMotion, ReduceMotionFadesTheCablesInPlace) {
    Wired w(2, AnimationMode::reduced);
    const auto added = w.c.dropBetween("Delay", between(w.c.rect(w.row[0]), w.c.rect(w.row[1])));
    ASSERT_NE(added.uid, 0u);
    ASSERT_TRUE(w.retract().isLive());
    EXPECT_EQ(w.retract().numSplitHalves(), 0u) << "no running, a fade";
    EXPECT_FALSE(w.retract().ghosts().empty());
    EXPECT_TRUE(w.retract().isGrowing(w.cablesBetween(w.row[0], added)[0].id));
}

// ---- Outlines follow the card's corners ----

namespace {
float cornerRadiusOf(juce::Component& c) { return synth::theme::themeOf(c).metrics.cornerRadius; }

/** The point of the card body's top-left corner arc at 45 degrees, the pixel an outline on the card's corner crosses.
 */
juce::Point<int> cornerArcPixel(juce::Rectangle<int> card, float radius) {
    const auto body = synth::ui::cardBodyRect(card.toFloat());
    const float d = radius * (1.0f - 0.70710678f);
    return {static_cast<int>(body.getX() + d), static_cast<int>(body.getY() + d)};
}
} // namespace

TEST(CardOutline, TheRingAroundACardAnUndoBroughtBackHugsTheCardsCorners) {
    Canvas c;
    const auto id = c.filter(400, 400);
    c.editor.finishCardGlideForTest();
    c.glide().setForceAnimateForTest(true);
    c.editor.setSelectedNodes({id});
    ASSERT_TRUE(c.editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.undo.undo());
    const auto card = c.rect(id);
    c.glide().applyTimelineAtMs(synth::ui::ExitEnterTimeline::kGrowMs + 20.0); // the outline phase, nearly opaque

    juce::Image img(juce::Image::ARGB, c.editor.getWidth(), c.editor.getHeight(), true);
    {
        juce::Graphics g(img);
        c.glide().paint(g);
    }
    const auto p = cornerArcPixel(card, cornerRadiusOf(c.editor));
    float strongest = 0.0f;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            strongest = juce::jmax(strongest, img.getPixelAt(p.x + dx, p.y + dy).getFloatAlpha());
    EXPECT_GT(strongest, 0.2f) << "the ring crosses the card body's corner arc";
    c.editor.finishCardGlideForTest();
}

TEST(CardOutline, TheDragLandingGhostSitsOnTheCardBodyWithTheCardsCorners) {
    Canvas c;
    c.editor.setSize(1600, 1000);
    const juce::Point<int> cursor{700, 500};
    const auto area = juce::Rectangle<int>(300, 100, 800, 800);
    const auto quiet = c.editor.createComponentSnapshot(area, false);
    c.libraryEnter("Delay", cursor);
    const auto ghost = c.editor.getDragDropController().getDragPreviewGhost();
    ASSERT_FALSE(ghost.isEmpty());
    const auto drag = c.editor.createComponentSnapshot(area, false);
    const auto p = cornerArcPixel(ghost, cornerRadiusOf(c.editor)) - area.getPosition();
    float change = 0.0f;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy) {
            const auto a = quiet.getPixelAt(p.x + dx, p.y + dy), b = drag.getPixelAt(p.x + dx, p.y + dy);
            change = juce::jmax(change, std::abs(a.getFloatRed() - b.getFloatRed()) +
                                            std::abs(a.getFloatGreen() - b.getFloatGreen()) +
                                            std::abs(a.getFloatBlue() - b.getFloatBlue()));
        }
    EXPECT_GT(change, 0.15f) << "the ghost's outline crosses the card body's corner arc";
    const auto outside = [&](int x, int y) {
        const auto a = quiet.getPixelAt(x, y), b = drag.getPixelAt(x, y);
        return std::abs(a.getFloatRed() - b.getFloatRed()) + std::abs(a.getFloatGreen() - b.getFloatGreen()) +
               std::abs(a.getFloatBlue() - b.getFloatBlue());
    };
    EXPECT_LT(outside(p.x - 2, p.y - 2), 0.02f) << "and nothing is drawn outside the corner, in the card's margin";
    c.libraryExit("Delay", cursor);
}

TEST(CardOutline, ThePickOnCanvasOutlineOfACardHugsItsCorners) {
    Canvas c(AnimationMode::off); // the outline lands at once
    const auto id = c.filter(400, 400);
    synth::ui::ModDotCanvasPicker picker(c.editor, [](NodeID) { return true; });
    picker.begin();
    const auto card = picker.getLocalArea(nullptr, c.card(id)->getScreenBounds());
    const auto render = [&picker, &c] {
        juce::Image img(juce::Image::ARGB, c.editor.getWidth(), c.editor.getHeight(), true);
        juce::Graphics g(img);
        picker.paintEntireComponent(g, false);
        return img;
    };
    const auto bare = render();
    const auto at = card.getCentre();
    picker.mouseMove(mouseAt(picker, at, at, false));
    ASSERT_GT(picker.getOutlineAlphaForTest(), 0.9f);
    const auto outlined = render();

    const auto p = cornerArcPixel(card, cornerRadiusOf(c.editor));
    const auto difference = [&](int x, int y) {
        return outlined.getPixelAt(x, y).getFloatAlpha() - bare.getPixelAt(x, y).getFloatAlpha();
    };
    float onArc = 0.0f;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            onArc = juce::jmax(onArc, difference(p.x + dx, p.y + dy));
    EXPECT_GT(onArc, 0.5f) << "the outline crosses the card body's corner arc";
    EXPECT_LT(difference(p.x - 2, p.y - 2), 0.02f) << "and nothing is drawn outside the corner";
    picker.end();
}
