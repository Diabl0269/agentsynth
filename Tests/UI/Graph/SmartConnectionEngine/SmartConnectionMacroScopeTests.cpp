// SmartConnectionMacroScopeTests.cpp
//
// A module placed inside a track's macro connects only to that macro's own cards, and one on the open canvas only to
// cards that are not in a macro (docs/layout/smart-connections.md#one-container-at-a-time). Two tracks, each its own
// macro of a Filter and a VCA in series; a Delay dropped between the second track's Filter and VCA must offer, wire
// and reroute nothing of the first track, and every cable outside the insert is the same before and after.
// Real library drag calls on a real canvas (InsertGapTestFixture.h).

#include "../InsertGap/InsertGapTestFixture.h"

#include "Modules/VCAModule.h"

#include <tuple>

using namespace insert_gap_test;

namespace {
using Edge = std::tuple<juce::uint32, int, juce::uint32, int>; // src node, raw channel, dst node, raw channel

std::set<Edge> edgesOf(AudioEngine& engine) {
    std::set<Edge> out;
    for (const auto& c : engine.getGraph().getConnections())
        out.insert({c.source.nodeID.uid, c.source.channelIndex, c.destination.nodeID.uid, c.destination.channelIndex});
    return out;
}

/** Every edge with neither end on `touched`: the cables an insert between two of those cards must leave alone. */
std::set<Edge> foreignEdges(AudioEngine& engine, const std::set<juce::uint32>& touched) {
    std::set<Edge> out;
    for (const auto& e : edgesOf(engine))
        if (touched.count(std::get<0>(e)) == 0 && touched.count(std::get<2>(e)) == 0)
            out.insert(e);
    return out;
}

/** The stereo audio pair of `a` into the same jacks of `b`, the way a user patches a track's chain. */
void wire(Canvas& c, NodeID a, NodeID b) {
    for (int j = 0; j < 2; ++j)
        c.editor.connectPorts(a, j, b, j, false, false);
}

/** Audio cables from `a` to `b` (raw channels below 4: the stereo legs, not CV). */
int linksBetween(Canvas& c, NodeID a, NodeID b) {
    int n = 0;
    for (const auto& e : edgesOf(c.engine))
        if (std::get<0>(e) == a.uid && std::get<2>(e) == b.uid && std::get<1>(e) < 4)
            ++n;
    return n;
}

/** Every live suggestion's neighbour (and an insert's upstream) is one of `allowed`; the failure names the stray. */
::testing::AssertionResult suggestionsStayWithin(GraphEditor& editor, const std::set<juce::uint32>& allowed) {
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        if (allowed.count(s.neighborId.uid) == 0)
            return ::testing::AssertionFailure() << "suggested a cable to node " << s.neighborId.uid;
        if (s.isInsert && allowed.count(s.upstreamId.uid) == 0)
            return ::testing::AssertionFailure() << "an insert reroutes the cable from node " << s.upstreamId.uid;
    }
    return ::testing::AssertionSuccess();
}

/** Two tracks, each a macro holding a Filter feeding a VCA. Track 2's border sits 40 px under track 1's. */
struct TwoTracks {
    Canvas c;
    NodeID fltA, vcaA, fltB, vcaB;
    juce::String trackA, trackB;

    explicit TwoTracks(bool foldFirst = false) {
        fltA = c.filter(400, 3000);
        vcaA = c.add(std::make_unique<VCAModule>(), 400, 3600);
        fltB = c.filter(400, 4000);
        vcaB = c.add(std::make_unique<VCAModule>(), 400, 4400);
        row(fltA, vcaA, 400);
        wire(c, fltA, vcaA);
        trackA = c.openMacro({fltA, vcaA});
        row(fltB, vcaB, 1200);
        wire(c, fltB, vcaB);
        trackB = c.openMacro({fltB, vcaB});
        const int dy = c.hull(trackA).getBottom() + 40 - c.hull(trackB).getY();
        row(fltB, vcaB, c.rect(fltB).getY() + dy);
        c.editor.finishCardGlideForTest();
        if (foldFirst) {
            c.macros().setMacroCollapsed(trackA, true);
            // The project this came from: the folded track's hidden cards sit right over the open track's row, as
            // they do when the first track was laid out above the second and then folded.
            c.place(fltA, c.rect(fltB).getX(), c.rect(fltB).getY() - 128);
            c.place(vcaA, c.rect(vcaB).getX(), c.rect(vcaB).getY() - 128);
            c.editor.finishCardGlideForTest();
        }
    }

    void row(NodeID first, NodeID second, int y) { // a gap a Delay fits in, as in a real track
        c.place(first, 400, y);
        c.place(second, 400 + c.rect(first).getWidth() + 320, y);
    }

    std::set<juce::uint32> trackBCards(NodeID added = {}) const { return {fltB.uid, vcaB.uid, added.uid}; }
};
} // namespace

TEST(SmartConnectionMacroScope, TheTwoTracksAreSetUpFortyPixelsApart) {
    TwoTracks t;
    EXPECT_EQ(t.c.hull(t.trackB).getY() - t.c.hull(t.trackA).getBottom(), 40);
    EXPECT_GT(linksBetween(t.c, t.fltA, t.vcaA), 0);
    EXPECT_GT(linksBetween(t.c, t.fltB, t.vcaB), 0);
}

// Regression test for FRO1044: a Delay dragged into track 2's macro was offered, and on drop wired, a cable to a card
// of track 1 sitting just above it. Swept over every aim from track 2's top border down to its cards, with and
// without Ctrl: no aim may name a card outside track 2.
TEST(SmartConnectionMacroScope, ALibraryDragInsideOneTracksMacroNeverSuggestsTheOtherTrack) {
    for (const bool ctrl : {false, true}) {
        TwoTracks t;
        t.c.editor.getSmartConnections().setInsertModifierOverrideForTests(ctrl);
        const auto b = t.c.hull(t.trackB);
        const int x = between(t.c.rect(t.fltB), t.c.rect(t.vcaB)).x;
        t.c.libraryEnter("Delay", {x, b.getY() + 4});
        for (int y = b.getY() + 4; y <= t.c.rect(t.fltB).getCentreY(); y += 8) {
            t.c.libraryMove("Delay", {x, y});
            ASSERT_EQ(t.c.editor.getMacroDragJoinId(), t.trackB) << "y " << y;
            EXPECT_TRUE(suggestionsStayWithin(t.c.editor, t.trackBCards())) << "ctrl " << ctrl << ", y " << y;
        }
        t.c.libraryExit("Delay", {x, b.getY() + 4});
    }
}

// Regression test for FRO1044: the drop itself, aimed at track 2's top edge (closest to track 1) and between its two
// cards: track 2 becomes Filter -> Delay -> VCA, every other cable is the same one as before, and one undo restores
// both tracks exactly.
TEST(SmartConnectionMacroScope, ADropInsideOneTracksMacroLeavesEveryOtherCableAsItWas) {
    for (const bool atTopEdge : {true, false}) {
        TwoTracks t;
        t.c.editor.getSmartConnections().setInsertModifierOverrideForTests(true);
        const auto before = edgesOf(t.c.engine);
        const auto foreignBefore = foreignEdges(t.c.engine, t.trackBCards());
        const auto gapCentre = between(t.c.rect(t.fltB), t.c.rect(t.vcaB));
        const juce::Point<int> p{gapCentre.x, atTopEdge ? t.c.hull(t.trackB).getY() + 4 : gapCentre.y};

        t.c.libraryEnter("Delay", p);
        EXPECT_TRUE(suggestionsStayWithin(t.c.editor, t.trackBCards()));
        const auto added = t.c.libraryDrop("Delay", p);
        t.c.editor.finishCardGlideForTest();
        ASSERT_NE(added.uid, 0u);
        EXPECT_TRUE(t.c.isMember(t.trackB, added)) << "the Delay joins track 2's macro";
        EXPECT_EQ(foreignEdges(t.c.engine, t.trackBCards(added)), foreignBefore)
            << "no cable outside track 2's Filter -> VCA was added, removed or rerouted (top edge " << atTopEdge << ")";
        for (auto other : {t.fltA, t.vcaA})
            EXPECT_EQ(linksBetween(t.c, added, other) + linksBetween(t.c, other, added), 0) << "nothing to track 1";
        if (!atTopEdge) {
            EXPECT_GT(linksBetween(t.c, t.fltB, added), 0) << "track 2's Filter feeds the Delay";
            EXPECT_GT(linksBetween(t.c, added, t.vcaB), 0) << "the Delay feeds track 2's VCA";
            EXPECT_EQ(linksBetween(t.c, t.fltB, t.vcaB), 0) << "the direct cable is replaced";
        }

        ASSERT_TRUE(t.c.undo.undo());
        t.c.editor.finishCardGlideForTest();
        EXPECT_EQ(edgesOf(t.c.engine), before) << "one undo restores both tracks exactly";
    }
}

// Regression test for FRO1044: the cards of a FOLDED macro are hidden but still on the canvas, at their old places;
// laid out over another track's row they were picked as neighbours ("ghost modules"), and the drop rerouted a cable
// of the folded track. A hidden card is never a neighbour, and the insert lands in the open track.
TEST(SmartConnectionMacroScope, HiddenCardsOfAFoldedTrackAreNeverNeighbours) {
    TwoTracks t(/*foldFirst=*/true);
    ASSERT_FALSE(t.c.card(t.fltA)->isVisible());
    ASSERT_TRUE(t.c.card(t.fltA)->getBounds().intersects(t.c.hull(t.trackB)))
        << "the folded track's hidden cards overlap the open track";
    const auto before = edgesOf(t.c.engine);
    const auto foreignBefore = foreignEdges(t.c.engine, t.trackBCards());

    const auto p = between(t.c.rect(t.fltB), t.c.rect(t.vcaB));
    const int top = t.c.hull(t.trackB).getY() + 4;
    t.c.libraryEnter("Delay", {p.x, top});
    for (int y = top; y <= p.y; y += 8) { // from the border, where the hidden cards are in reach, down to the gap
        t.c.libraryMove("Delay", {p.x, y});
        EXPECT_TRUE(suggestionsStayWithin(t.c.editor, t.trackBCards())) << "y " << y;
    }
    t.c.libraryMove("Delay", p);
    EXPECT_TRUE(suggestionsStayWithin(t.c.editor, t.trackBCards()));
    EXPECT_GT(t.c.editor.getSmartConnections().getSmartSuggestionCount(), 0) << "the open track's insert is offered";
    const auto added = t.c.libraryDrop("Delay", p);
    t.c.editor.finishCardGlideForTest();
    ASSERT_NE(added.uid, 0u);
    EXPECT_EQ(foreignEdges(t.c.engine, t.trackBCards(added)), foreignBefore);
    EXPECT_GT(linksBetween(t.c, t.fltB, added), 0);
    EXPECT_GT(linksBetween(t.c, added, t.vcaB), 0);
    EXPECT_EQ(linksBetween(t.c, t.fltB, t.vcaB), 0);

    ASSERT_TRUE(t.c.undo.undo());
    t.c.editor.finishCardGlideForTest();
    EXPECT_EQ(edgesOf(t.c.engine), before);
}

// The other half of the rule: a module dropped on the open canvas never wires into a macro's cards, even right
// beside its border.
TEST(SmartConnectionMacroScope, ADropOnTheOpenCanvasNeverWiresIntoAMacro) {
    TwoTracks t;
    t.c.editor.getSmartConnections().setInsertModifierOverrideForTests(true);
    const auto b = t.c.hull(t.trackB);
    const auto before = edgesOf(t.c.engine);
    const juce::Point<int> p{b.getRight() + 60, t.c.rect(t.vcaB).getCentreY()};
    t.c.libraryEnter("Delay", p);
    ASSERT_TRUE(t.c.editor.getMacroDragJoinId().isEmpty()) << "outside every border";
    EXPECT_EQ(t.c.editor.getSmartConnections().getSmartSuggestionCount(), 0);
    const auto added = t.c.libraryDrop("Delay", p);
    t.c.editor.finishCardGlideForTest();
    ASSERT_NE(added.uid, 0u);
    EXPECT_EQ(edgesOf(t.c.engine), before);
}
