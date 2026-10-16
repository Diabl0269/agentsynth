// FilterCustomShapesTrustTests.cpp
//
// Model-authored patches may set a Filter's Custom shape list through FilterModule::setUntrustedExtraState:
// only bounded numbers (curve points, up to three map coordinates) and a short whitelisted name get
// through; everything else in the JSON is ignored. See docs/ai/patch-format.md#per-node-state and
// docs/ai/patch-safety.md.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/SamplerModule.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;
using synth::CustomFilterShapes;

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

juce::var simpleCurve() { return curve({point(0.0, 0.3), point(0.5, 0.9, 0.25), point(1.0, 0.1)}); }

juce::var twoShapes() {
    return shapeList({shape("low", {0.1, 0.2}, simpleCurve()), shape("high", {0.9, 0.7}, simpleCurve())});
}

juce::var patchWith(const juce::String& type, const juce::var& state, int id = 1) {
    auto* node = new juce::DynamicObject();
    node->setProperty("id", id);
    node->setProperty("type", type);
    if (!state.isVoid())
        node->setProperty("state", state);
    auto* root = new juce::DynamicObject();
    root->setProperty("nodes", juce::Array<juce::var>{juce::var(node)});
    root->setProperty("connections", juce::Array<juce::var>{});
    return juce::var(root);
}

FilterModule* filterOf(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* m = dynamic_cast<FilterModule*>(node->getProcessor()))
            return m;
    return nullptr;
}

/** Applies `state` to a Filter on the untrusted path and returns the resulting shapes. */
CustomFilterShapes untrustedShapes(const juce::var& state) {
    juce::AudioProcessorGraph graph;
    EXPECT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("Filter", state), graph, true, false));
    auto* filter = filterOf(graph);
    EXPECT_NE(filter, nullptr);
    return filter != nullptr ? filter->getCustomShapes() : CustomFilterShapes{};
}

} // namespace

TEST(FilterCustomShapesTrustTest, AnUntrustedPatchSetsTheShapes) {
    const auto shapes = untrustedShapes(twoShapes());
    ASSERT_EQ(shapes.shapes.size(), 2u);
    EXPECT_EQ(shapes.shapes[0].name, "low");
    EXPECT_EQ(shapes.shapes[1].name, "high");
    EXPECT_NEAR(shapes.shapes[0].position[0], 0.1f, 1e-5f);
    EXPECT_NEAR(shapes.shapes[1].position[1], 0.7f, 1e-5f);
    EXPECT_NEAR(shapes.shapes[0].curve.evaluate(0.0f), 0.3f, 1e-4f);
    EXPECT_FALSE(shapes.isDefault());
}

TEST(FilterCustomShapesTrustTest, OutOfRangeValuesAreClamped) {
    const auto shapes = untrustedShapes(shapeList(
        {shape("wild", {-5.0, 9.0, 4.0}, curve({point(-3.0, 7.0, 9.0), point(0.5, -3.0, -9.0), point(4.0, 2.0)}))}));
    ASSERT_EQ(shapes.shapes.size(), 1u);
    for (float c : shapes.shapes[0].position) {
        EXPECT_GE(c, 0.0f);
        EXPECT_LE(c, 1.0f);
    }
    EXPECT_EQ(shapes.shapes[0].position[0], 0.0f);
    EXPECT_EQ(shapes.shapes[0].position[1], 1.0f);
    ASSERT_GE(shapes.shapes[0].curve.points.size(), 2u);
    for (const auto& p : shapes.shapes[0].curve.points) {
        EXPECT_GE(p.x, 0.0f);
        EXPECT_LE(p.x, 1.0f);
        EXPECT_GE(p.y, 0.0f);
        EXPECT_LE(p.y, 1.0f);
        EXPECT_GE(p.bend, -1.0f);
        EXPECT_LE(p.bend, 1.0f);
    }
}

// A timelineOps Filter insert hands its "state" over through applyUntrustedState, outside any patch.
TEST(FilterCustomShapesTrustTest, AnInsertStateSetsAndClampsTheShapes) {
    auto filter = AIStateMapper::createModule("Filter");
    auto* module = dynamic_cast<FilterModule*>(filter.get());
    ASSERT_NE(module, nullptr);
    AIStateMapper::applyUntrustedState(filter.get(), twoShapes());
    ASSERT_EQ(module->getCustomShapes().shapes.size(), 2u);
    EXPECT_EQ(module->getCustomShapes().shapes[1].name, "high");

    AIStateMapper::applyUntrustedState(
        filter.get(), shapeList({shape("wild", {-5.0, 9.0}, curve({point(-3.0, 7.0, 9.0), point(4.0, -2.0)}))}));
    ASSERT_EQ(module->getCustomShapes().shapes.size(), 1u);
    EXPECT_EQ(module->getCustomShapes().shapes[0].position[0], 0.0f);
    EXPECT_EQ(module->getCustomShapes().shapes[0].position[1], 1.0f);
    for (const auto& p : module->getCustomShapes().shapes[0].curve.points) {
        EXPECT_GE(p.y, 0.0f);
        EXPECT_LE(p.y, 1.0f);
        EXPECT_GE(p.bend, -1.0f);
        EXPECT_LE(p.bend, 1.0f);
    }
}

TEST(FilterCustomShapesTrustTest, NoInsertStateLeavesAFreshFilterAtItsDefaults) {
    auto filter = AIStateMapper::createModule("Filter");
    AIStateMapper::applyUntrustedState(filter.get(), juce::var());
    EXPECT_TRUE(dynamic_cast<FilterModule*>(filter.get())->getCustomShapes().isDefault());
}

TEST(FilterCustomShapesTrustTest, PointsShapesAndMapValuesAreCapped) {
    juce::Array<juce::var> points;
    for (int i = 0; i < 200; ++i)
        points.add(point(i / 199.0, 0.5));
    juce::Array<juce::var> shapes;
    for (int i = 0; i < 40; ++i)
        shapes.add(shape("s", {0.1, 0.2, 0.3, 0.4, 0.5, 0.6}, curve(points)));
    const auto result = untrustedShapes(shapeList(shapes));
    EXPECT_EQ((int)result.shapes.size(), CustomFilterShapes::kMaxUntrustedShapes);
    EXPECT_EQ(CustomFilterShapes::kMaxUntrustedShapes, 16);
    for (const auto& s : result.shapes) {
        EXPECT_LE((int)s.curve.points.size(), synth::LfoCustomWave::kMaxPoints);
        EXPECT_EQ((int)s.position.size(), CustomFilterShapes::kMaxUntrustedMapValues);
    }
}

TEST(FilterCustomShapesTrustTest, MalformedStateLeavesTheCurrentShapesUntouched) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("Filter", twoShapes()), graph, true, false));
    ASSERT_EQ(filterOf(graph)->getCustomShapes().shapes.size(), 2u) << "precondition: the shapes were set";
    const auto before = filterOf(graph)->getCustomShapes();
    const auto json = AIStateMapper::graphToJSON(graph);
    const int id = (int)json.getDynamicObject()->getProperty("nodes")[0].getDynamicObject()->getProperty("id");

    auto emptyList = shapeList({});
    auto notObjects = shapeList({juce::var(3), juce::var("x")});
    for (const auto& garbage : {juce::var("not shapes"), juce::var(42), emptyList, notObjects, juce::var()}) {
        auto merge = patchWith("Filter", garbage, id);
        merge.getDynamicObject()->setProperty("mode", "merge");
        ASSERT_TRUE(AIStateMapper::applyJSONToGraph(merge, graph, false, false));
        EXPECT_EQ(filterOf(graph)->getCustomShapes(), before) << juce::JSON::toString(garbage);
    }
}

TEST(FilterCustomShapesTrustTest, UnknownKeysAreIgnored) {
    auto* extra = new juce::DynamicObject();
    extra->setProperty("version", 1);
    extra->setProperty("file", "/etc/passwd");
    auto* s = new juce::DynamicObject();
    s->setProperty("name", "ok");
    s->setProperty("position", juce::Array<juce::var>{juce::var(0.3), juce::var(0.4)});
    s->setProperty("curve", simpleCurve());
    s->setProperty("path", "/Users/someone/secret.wav");
    s->setProperty("note", "free text the model wants kept");
    extra->setProperty("shapes", juce::Array<juce::var>{juce::var(s)});
    extra->setProperty("sampleFile", "/tmp/x.wav");
    const auto shapes = untrustedShapes(juce::var(extra));
    ASSERT_EQ(shapes.shapes.size(), 1u);
    EXPECT_EQ(shapes.shapes[0].name, "ok");
    EXPECT_EQ(shapes, [] {
        CustomFilterShapes expected;
        expected.shapes.push_back(
            {"ok",
             CustomFilterShapes::fromVar(shapeList({shape("ok", {0.3, 0.4}, simpleCurve())})).shapes[0].curve,
             {0.3f, 0.4f}});
        return expected;
    }());
}

TEST(FilterCustomShapesTrustTest, NamesAreReducedToAShortWhitelist) {
    const juce::String nasty = "../../etc/pass wd<script>_ah-EE 9 and a very long tail that goes on";
    const auto shapes = untrustedShapes(shapeList({shape(nasty, {0.5, 0.5}, simpleCurve())}));
    ASSERT_EQ(shapes.shapes.size(), 1u);
    const auto& name = shapes.shapes[0].name;
    EXPECT_LE(name.length(), CustomFilterShapes::kMaxUntrustedNameLength);
    EXPECT_TRUE(name.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -")) << name;
    EXPECT_FALSE(name.containsAnyOf("./<>_")) << name;
    EXPECT_TRUE(name.startsWith("etcpass wdscript")) << name;
}

TEST(FilterCustomShapesTrustTest, TheTrustedPathRoundTripsAsBefore) {
    CustomFilterShapes trustedShapes;
    for (int i = 0; i < 40; ++i) // more than the AI cap, within the trusted one
        trustedShapes.shapes.push_back({"shape_" + juce::String(i), CustomFilterShapes::flatCurve(), {0.5f, 0.5f}});
    trustedShapes.sanitise();
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("Filter", trustedShapes.toVar()), graph, true, true));
    EXPECT_EQ(filterOf(graph)->getCustomShapes(), trustedShapes);
    const auto json = AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, reloaded, true, true));
    EXPECT_EQ(filterOf(reloaded)->getCustomShapes(), trustedShapes);
}

TEST(FilterCustomShapesTrustTest, AnUntrustedSamplerStateStillLoadsNoFile) {
    const juce::var state(new juce::DynamicObject());
    state.getDynamicObject()->setProperty(synth::module_file_keys::kSampleFile, "/tmp/does-not-matter.wav");
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("Sampler", state), graph, true, false));
    for (auto* node : graph.getNodes())
        if (auto* sampler = dynamic_cast<SamplerModule*>(node->getProcessor()))
            EXPECT_TRUE(sampler->getSampleFilePath().isEmpty());
}
