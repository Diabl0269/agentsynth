// LfoCustomShapesTrustTests.cpp
//
// Model-authored patches may set an LFO's Custom shape list through LFOModule::setUntrustedExtraState, under the
// same caps as the Filter's: at most 16 shapes, 64 points a curve, three map coordinates and a short whitelisted
// name; everything else in the JSON is ignored. See docs/ai/patch-format.md#per-node-state and
// docs/ai/patch-safety.md.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/LFOModule.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;
using synth::LfoCustomShapes;
using synth::LfoCustomWave;

namespace {

juce::var point(double x, double y, double bend = 0.0) {
    auto* p = new juce::DynamicObject();
    p->setProperty("x", x);
    p->setProperty("y", y);
    p->setProperty("bend", bend);
    return juce::var(p);
}

juce::var curve(const juce::Array<juce::var>& points) {
    auto* c = new juce::DynamicObject();
    c->setProperty("version", 1);
    c->setProperty("points", juce::var(points));
    return juce::var(c);
}

juce::var shape(const juce::var& name, const juce::Array<juce::var>& position, const juce::var& curveVar) {
    auto* s = new juce::DynamicObject();
    s->setProperty("name", name);
    s->setProperty("position", juce::var(position));
    s->setProperty("curve", curveVar);
    return juce::var(s);
}

juce::var shapeList(const juce::Array<juce::var>& shapes) {
    auto* o = new juce::DynamicObject();
    o->setProperty("version", 1);
    o->setProperty("shapes", juce::var(shapes));
    return juce::var(o);
}

juce::var swoop() { return curve({point(0.0, 1.0), point(0.3, 0.2, -0.5), point(1.0, 0.0)}); }

juce::var twoShapes() { return shapeList({shape("fall", {0.1, 0.2}, swoop()), shape("rise", {0.9, 0.7}, swoop())}); }

juce::var patchWith(const juce::var& state, int id = 1) {
    auto* node = new juce::DynamicObject();
    node->setProperty("id", id);
    node->setProperty("type", "LFO");
    if (!state.isVoid())
        node->setProperty("state", state);
    auto* root = new juce::DynamicObject();
    root->setProperty("nodes", juce::Array<juce::var>{juce::var(node)});
    root->setProperty("connections", juce::Array<juce::var>{});
    return juce::var(root);
}

LFOModule* lfoOf(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* m = dynamic_cast<LFOModule*>(node->getProcessor()))
            return m;
    return nullptr;
}

LfoCustomShapes untrustedShapes(const juce::var& state) {
    juce::AudioProcessorGraph graph;
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(patchWith(state), graph, true, false));
    auto* lfo = lfoOf(graph);
    EXPECT_NE(lfo, nullptr);
    return lfo != nullptr ? lfo->getCustomShapes() : LfoCustomShapes{};
}

} // namespace

TEST(LfoCustomShapesTrustTest, AnUntrustedPatchSetsTheShapes) {
    const auto shapes = untrustedShapes(twoShapes());
    ASSERT_EQ(shapes.shapes.size(), 2u);
    EXPECT_EQ(shapes.shapes[0].name, "fall");
    EXPECT_EQ(shapes.shapes[1].name, "rise");
    EXPECT_NEAR(shapes.shapes[1].position[0], 0.9f, 1e-5f);
    EXPECT_NEAR(shapes.shapes[0].curve.evaluate(0.0f), 1.0f, 1e-4f);
}

TEST(LfoCustomShapesTrustTest, ShapesPointsAndMapValuesAreCapped) {
    juce::Array<juce::var> points;
    for (int i = 0; i < 200; ++i)
        points.add(point(i / 199.0, 0.5));
    juce::Array<juce::var> shapes;
    for (int i = 0; i < 40; ++i)
        shapes.add(shape("s", {0.1, 0.2, 0.3, 0.4, 0.5, 0.6}, curve(points)));
    const auto result = untrustedShapes(shapeList(shapes));
    EXPECT_EQ((int)result.shapes.size(), 16);
    for (const auto& s : result.shapes) {
        EXPECT_LE((int)s.curve.points.size(), LfoCustomWave::kMaxPoints);
        EXPECT_EQ((int)s.position.size(), 3);
    }
}

TEST(LfoCustomShapesTrustTest, NamesAreShortAndWhitelisted) {
    const auto result = untrustedShapes(
        shapeList({shape("<script>alert(1)</script> and a very long name that keeps going", {0.5, 0.5}, swoop())}));
    ASSERT_EQ(result.shapes.size(), 1u);
    EXPECT_LE(result.shapes[0].name.length(), 24);
    EXPECT_EQ(
        result.shapes[0].name.removeCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -"),
        juce::String());
}

TEST(LfoCustomShapesTrustTest, OutOfRangeValuesAreClamped) {
    const auto result = untrustedShapes(shapeList(
        {shape("wild", {-5.0, 9.0, 4.0}, curve({point(-3.0, 7.0, 9.0), point(0.5, -3.0, -9.0), point(4.0, 2.0)}))}));
    ASSERT_EQ(result.shapes.size(), 1u);
    EXPECT_EQ(result.shapes[0].position[0], 0.0f);
    EXPECT_EQ(result.shapes[0].position[1], 1.0f);
    for (const auto& p : result.shapes[0].curve.points) {
        EXPECT_GE(p.y, 0.0f);
        EXPECT_LE(p.y, 1.0f);
        EXPECT_GE(p.bend, -1.0f);
        EXPECT_LE(p.bend, 1.0f);
    }
}

TEST(LfoCustomShapesTrustTest, AnUnusableShapeListLeavesTheCurrentShapesUntouched) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith(twoShapes()), graph, true, false));
    ASSERT_EQ(lfoOf(graph)->getCustomShapes().shapes.size(), 2u);
    const auto before = lfoOf(graph)->getCustomShapes();
    const auto json = AIStateMapper::graphToJSON(graph);
    const int id = (int)json.getDynamicObject()->getProperty("nodes")[0].getDynamicObject()->getProperty("id");

    for (const auto& garbage : {shapeList({}), shapeList({juce::var(3), juce::var("x")})}) {
        auto merge = patchWith(garbage, id);
        merge.getDynamicObject()->setProperty("mode", "merge");
        ASSERT_TRUE(AIStateMapper::applyJSONToGraph(merge, graph, false, false));
        EXPECT_EQ(lfoOf(graph)->getCustomShapes(), before) << juce::JSON::toString(garbage);
    }
}

TEST(LfoCustomShapesTrustTest, TheBareCurveFormatStillSetsOneShape) {
    const auto result = untrustedShapes(swoop());
    ASSERT_EQ(result.shapes.size(), 1u);
    EXPECT_EQ(result.shapes[0].curve, LfoCustomWave::fromVar(swoop()));
}

TEST(LfoCustomShapesTrustTest, UnknownKeysAreIgnored) {
    auto* s = new juce::DynamicObject();
    s->setProperty("name", "ok");
    s->setProperty("position", juce::Array<juce::var>{juce::var(0.3), juce::var(0.4)});
    s->setProperty("curve", swoop());
    s->setProperty("path", "/Users/someone/secret.wav");
    auto* o = new juce::DynamicObject();
    o->setProperty("version", 1);
    o->setProperty("shapes", juce::Array<juce::var>{juce::var(s)});
    o->setProperty("sampleFile", "/tmp/x.wav");
    const auto result = untrustedShapes(juce::var(o));
    ASSERT_EQ(result.shapes.size(), 1u);
    EXPECT_EQ(result.shapes[0].name, "ok");
    EXPECT_EQ(result.toVar().getDynamicObject()->getProperty("shapes").getArray()->size(), 1);
}

TEST(LfoCustomShapesTrustTest, TheTrustedPathRoundTripsTheShapeList) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith(twoShapes()), graph, true, true));
    const auto json = AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, reloaded, true, true));
    EXPECT_EQ(lfoOf(reloaded)->getCustomShapes(), lfoOf(graph)->getCustomShapes());
    EXPECT_EQ(lfoOf(reloaded)->getCustomShapes().shapes.size(), 2u);
}
