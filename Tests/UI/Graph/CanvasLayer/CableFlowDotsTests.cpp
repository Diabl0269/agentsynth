// CableFlowDotsTests.cpp
//
// The flow dots are placed from a track measured once per cable, where getPointAlongPath used to put them, and a
// frame's dots are filled one colour at a time
// (docs/layout/rendering.md#an-idle-canvas-with-moving-flow-dots-draws-one-picture).

#include "UI/Graph/CanvasLayer/CableFlowDots.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

std::vector<juce::Point<float>> pathPoints(const juce::Path& path) {
    std::vector<juce::Point<float>> points;
    for (juce::Path::Iterator it(path); it.next();)
        points.emplace_back(it.x1, it.y1);
    return points;
}

// The dots as they were drawn before the track: getPointAlongPath on the cable's own curve.
juce::Path referenceDots(juce::Point<float> p1, juce::Point<float> p2, float phase) {
    const auto wire = GraphEditor::buildCablePath(p1, p2);
    const float len = wire.getLength();
    juce::Path dots;
    for (int d = 0; d < 3; ++d) {
        const float t = std::fmod(phase + (float)d / 3.0f, 1.0f);
        const auto pt = wire.getPointAlongPath(t * len);
        dots.addEllipse(pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
    }
    return dots;
}

} // namespace

TEST(CableFlowDots, ADotLandsWhereGetPointAlongPathPutIt) {
    synth::ui::CableFlowDots dots(&GraphEditor::buildCablePath);
    const std::pair<juce::Point<float>, juce::Point<float>> cables[] = {
        {{100.0f, 100.0f}, {700.0f, 120.0f}}, // long and nearly straight
        {{400.0f, 300.0f}, {120.0f, 900.0f}}, // backwards and down: the curve loops round
        {{50.0f, 50.0f}, {60.0f, 58.0f}},     // a few pixels long
        {{900.0f, 200.0f}, {300.0f, 200.0f}}, // straight back
    };
    for (const auto& [p1, p2] : cables)
        for (float phase = 0.0f; phase < 1.0f; phase += 0.0137f) {
            juce::Path placed;
            dots.addDots(placed, p1, p2, phase, {});
            const auto got = pathPoints(placed), want = pathPoints(referenceDots(p1, p2, phase));
            ASSERT_EQ(got.size(), want.size());
            for (size_t i = 0; i < got.size(); ++i)
                ASSERT_LT(got[i].getDistanceFrom(want[i]), 0.5f) << "phase " << phase << " from " << p1.toString();
        }
}

TEST(CableFlowDots, ACableIsMeasuredOnceHoweverManyFramesItIsDrawn) {
    synth::ui::CableFlowDots dots(&GraphEditor::buildCablePath);
    for (int frame = 0; frame < 300; ++frame) {
        juce::Path placed;
        dots.addDots(placed, {10.0f, 10.0f}, {600.0f, 400.0f}, (float)frame * 0.02f - std::floor((float)frame * 0.02f),
                     {});
    }
    EXPECT_EQ(dots.tracksMeasured(), 1);
    juce::Path moved;
    dots.addDots(moved, {10.0f, 10.0f}, {610.0f, 400.0f}, 0.0f, {}); // an end moved: a new curve
    EXPECT_EQ(dots.tracksMeasured(), 2);
}

TEST(CableFlowDots, AFrameFillsItsDotsOnceForEachColour) {
    synth::ui::CableFlowDots dots(&GraphEditor::buildCablePath);
    const auto audio = juce::Colours::orange, mod = juce::Colours::cyan;
    for (int i = 0; i < 12; ++i)
        dots.batch(i % 2 == 0 ? audio : mod, {10.0f, 30.0f * (float)i}, {500.0f, 30.0f * (float)i + 80.0f}, 0.25f, {});
    juce::Image image(juce::Image::ARGB, 600, 500, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    dots.fill(g);
    EXPECT_EQ(dots.lastFillCount(), 2);
    dots.fill(g); // the batch was used up
    EXPECT_EQ(dots.lastFillCount(), 0);
}

TEST(CableFlowDots, ADotInsideTheKeepOutAreaIsLeftOut) {
    synth::ui::CableFlowDots dots(&GraphEditor::buildCablePath);
    juce::Path all, kept;
    dots.addDots(all, {0.0f, 0.0f}, {900.0f, 0.0f}, 0.0f, {});
    dots.addDots(kept, {0.0f, 0.0f}, {900.0f, 0.0f}, 0.0f, juce::Rectangle<float>(-20.0f, -20.0f, 40.0f, 40.0f));
    EXPECT_EQ(pathPoints(kept).size() * 3, pathPoints(all).size() * 2); // the dot at the start is gone
}
