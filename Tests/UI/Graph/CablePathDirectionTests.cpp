// The cable curve's direction rule: p1 is the output end and leaves heading right, p2 is the input
// end and is entered heading right (from the left) -- backward and near-vertical cables loop with a bounded
// handle instead of leaving over their own card or arriving from below.

#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/CableCurve.h"
#include <gtest/gtest.h>

namespace {

struct Cubic {
    juce::Point<float> start, c1, c2, end;
};

Cubic readCubic(const juce::Path& path) {
    Cubic out;
    int cubics = 0;
    for (juce::Path::Iterator it(path); it.next();) {
        if (it.elementType == juce::Path::Iterator::startNewSubPath)
            out.start = {it.x1, it.y1};
        else if (it.elementType == juce::Path::Iterator::cubicTo) {
            out.c1 = {it.x1, it.y1};
            out.c2 = {it.x2, it.y2};
            out.end = {it.x3, it.y3};
            ++cubics;
        }
    }
    EXPECT_EQ(cubics, 1) << "a cable is exactly one cubic";
    return out;
}

} // namespace

TEST(CablePathDirection, BackwardCableLeavesRightAndArrivesFromTheLeft) {
    const auto c = readCubic(GraphEditor::buildCablePath({500.0f, 100.0f}, {100.0f, 300.0f}));
    EXPECT_GT(c.c1.x, c.start.x) << "leaves the output heading right, not left over its own card";
    EXPECT_LT(c.c2.x, c.end.x) << "arrives at the input from its left";
    EXPECT_LE(c.c1.x - c.start.x, synth::ui::kCableMaxBackHandle) << "the loop stays bounded";
    EXPECT_LE(c.end.x - c.c2.x, synth::ui::kCableMaxBackHandle);
}

TEST(CablePathDirection, VeryFarBackwardCableIsBounded) {
    const auto c = readCubic(GraphEditor::buildCablePath({5000.0f, 0.0f}, {0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(c.c1.x, 5000.0f + synth::ui::kCableMaxBackHandle);
    EXPECT_FLOAT_EQ(c.c2.x, -synth::ui::kCableMaxBackHandle);
}

TEST(CablePathDirection, NearVerticalCableStillLeavesAndEntersHorizontally) {
    for (const float dx : {0.0f, 1.0f, -1.0f, 30.0f}) {
        const auto c = readCubic(GraphEditor::buildCablePath({200.0f, 50.0f}, {200.0f + dx, 400.0f}));
        EXPECT_GE(c.c1.x, c.start.x + synth::ui::kCableMinHandle) << "dx=" << dx;
        EXPECT_LE(c.c2.x, c.end.x - synth::ui::kCableMinHandle) << "dx=" << dx;
        EXPECT_FLOAT_EQ(c.c1.y, c.start.y);
        EXPECT_FLOAT_EQ(c.c2.y, c.end.y);
    }
}

TEST(CablePathDirection, FarForwardCableKeepsTheOriginalCurve) {
    const auto c = readCubic(GraphEditor::buildCablePath({100.0f, 20.0f}, {500.0f, 300.0f}));
    EXPECT_FLOAT_EQ(c.c1.x, 300.0f) << "p1.x + dx/2";
    EXPECT_FLOAT_EQ(c.c2.x, 300.0f) << "p2.x - dx/2";
    EXPECT_FLOAT_EQ(c.c1.y, 20.0f);
    EXPECT_FLOAT_EQ(c.c2.y, 300.0f);

    // The threshold: exactly 2 * kMin is still the original formula.
    const float dx = 2.0f * synth::ui::kCableMinHandle;
    const auto edge = readCubic(GraphEditor::buildCablePath({0.0f, 0.0f}, {dx, 10.0f}));
    EXPECT_FLOAT_EQ(edge.c1.x, dx * 0.5f);
    EXPECT_FLOAT_EQ(edge.c2.x, dx * 0.5f);
}

TEST(CablePathDirection, GraphEditorAndTheSharedCurveAgree) {
    const juce::Point<float> a(12.0f, 34.0f), b(-80.0f, 90.0f);
    const auto viaEditor = readCubic(GraphEditor::buildCablePath(a, b));
    const auto viaShared = readCubic(synth::ui::makeCablePath(a, b));
    EXPECT_EQ(viaEditor.c1, viaShared.c1);
    EXPECT_EQ(viaEditor.c2, viaShared.c2);
}
