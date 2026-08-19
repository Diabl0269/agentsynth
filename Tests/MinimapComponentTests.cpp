// MinimapComponentTests.cpp
// Headless unit tests for synth::ui::MinimapComponent / MinimapModel (issue #159).

#include "../Source/UI/MinimapComponent.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::MinimapComponent;
using synth::ui::MinimapModel;

namespace {

// Builds a MouseEvent by hand (no precedent for this elsewhere in the suite): headless tests have
// no real OS mouse source, but MouseInputSource is copyable and Desktop always exposes one.
juce::MouseEvent makeMouseEvent(juce::Component& comp, juce::Point<float> position) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), position,
                            juce::Time::getCurrentTime(), 1, false);
}

MinimapModel::Node makeNode(juce::Rectangle<float> bounds, juce::Colour colour = juce::Colours::red,
                            bool selected = false) {
    MinimapModel::Node n;
    n.bounds = bounds;
    n.colour = colour;
    n.selected = selected;
    return n;
}

MinimapModel::Cable makeCable(juce::Point<float> p1, juce::Point<float> p2,
                              juce::Colour colour = juce::Colours::green) {
    MinimapModel::Cable c;
    c.p1 = p1;
    c.p2 = p2;
    c.colour = colour;
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// computeWorldBounds
// ---------------------------------------------------------------------------

// An entirely empty model (no nodes, no viewport) must fall back to a kMinWorldSpan square
// centred on the origin, not a degenerate zero-size rect.
TEST(MinimapComponentTest, ComputeWorldBounds_EmptyModel_ReturnsMinSpanSquareAtOrigin) {
    MinimapModel model;
    const auto bounds = MinimapComponent::computeWorldBounds(model);

    EXPECT_FLOAT_EQ(bounds.getWidth(), MinimapComponent::kMinWorldSpan);
    EXPECT_FLOAT_EQ(bounds.getHeight(), MinimapComponent::kMinWorldSpan);
    EXPECT_NEAR(bounds.getCentreX(), 0.0f, 0.01f);
    EXPECT_NEAR(bounds.getCentreY(), 0.0f, 0.01f);
}

// With nodes only (viewport empty), the result must contain every node's bounds.
TEST(MinimapComponentTest, ComputeWorldBounds_NodesOnly_ContainsEveryNode) {
    MinimapModel model;
    model.nodes.push_back(makeNode({10.0f, 10.0f, 50.0f, 50.0f}));
    model.nodes.push_back(makeNode({600.0f, 800.0f, 40.0f, 20.0f}));

    const auto bounds = MinimapComponent::computeWorldBounds(model);
    for (const auto& node : model.nodes)
        EXPECT_TRUE(bounds.contains(node.bounds)) << node.bounds.toString();
}

// With a viewport only (no nodes), the result must contain the viewport.
TEST(MinimapComponentTest, ComputeWorldBounds_ViewportOnly_ContainsViewport) {
    MinimapModel model;
    model.viewport = {100.0f, 100.0f, 300.0f, 300.0f};

    const auto bounds = MinimapComponent::computeWorldBounds(model);
    EXPECT_TRUE(bounds.contains(model.viewport));
}

// With both nodes and a viewport, the result must contain both.
TEST(MinimapComponentTest, ComputeWorldBounds_NodesAndViewport_ContainsBoth) {
    MinimapModel model;
    model.nodes.push_back(makeNode({-500.0f, -500.0f, 30.0f, 30.0f}));
    model.viewport = {0.0f, 0.0f, 200.0f, 150.0f};

    const auto bounds = MinimapComponent::computeWorldBounds(model);
    EXPECT_TRUE(bounds.contains(model.nodes[0].bounds));
    EXPECT_TRUE(bounds.contains(model.viewport));
}

// A single small node far from the origin must be clamped out to at least kMinWorldSpan per axis,
// while staying centred on that node (not on the origin).
TEST(MinimapComponentTest, ComputeWorldBounds_SingleSmallNode_ClampedAndCentredOnNode) {
    MinimapModel model;
    model.nodes.push_back(makeNode({1000.0f, -2000.0f, 10.0f, 10.0f}));

    const auto bounds = MinimapComponent::computeWorldBounds(model);
    EXPECT_GE(bounds.getWidth(), MinimapComponent::kMinWorldSpan);
    EXPECT_GE(bounds.getHeight(), MinimapComponent::kMinWorldSpan);
    EXPECT_NEAR(bounds.getCentreX(), model.nodes[0].bounds.getCentreX(), 0.01f);
    EXPECT_NEAR(bounds.getCentreY(), model.nodes[0].bounds.getCentreY(), 0.01f);
}

// ---------------------------------------------------------------------------
// computeWorldToMap
// ---------------------------------------------------------------------------

// A square world dropped into a wide (non-square) map area must scale uniformly, not stretch:
// two equal-length perpendicular segments in world space must stay equal length after transform.
TEST(MinimapComponentTest, ComputeWorldToMap_PreservesAspectRatio) {
    const juce::Rectangle<float> world(0.0f, 0.0f, 1000.0f, 1000.0f);
    const juce::Rectangle<float> mapArea(0.0f, 0.0f, 400.0f, 100.0f); // wide, non-square

    const auto transform = MinimapComponent::computeWorldToMap(world, mapArea);

    const juce::Point<float> origin(500.0f, 500.0f);
    const juce::Point<float> horiz(600.0f, 500.0f);
    const juce::Point<float> vert(500.0f, 600.0f);

    const auto o = origin.transformedBy(transform);
    const auto h = horiz.transformedBy(transform);
    const auto v = vert.transformedBy(transform);

    EXPECT_NEAR(o.getDistanceFrom(h), o.getDistanceFrom(v), 0.01f)
        << "a uniform scale must move equal-length perpendicular segments the same distance";
}

// The transformed world rect must land entirely inside mapArea and be centred there.
TEST(MinimapComponentTest, ComputeWorldToMap_MapsInsideMapAreaAndCentred) {
    const juce::Rectangle<float> world(-200.0f, -100.0f, 800.0f, 600.0f);
    const juce::Rectangle<float> mapArea(4.0f, 4.0f, 212.0f, 142.0f);

    const auto transform = MinimapComponent::computeWorldToMap(world, mapArea);
    const auto topLeft = world.getTopLeft().transformedBy(transform);
    const auto bottomRight = world.getBottomRight().transformedBy(transform);
    const juce::Rectangle<float> mapped(topLeft, bottomRight);

    EXPECT_TRUE(mapArea.expanded(0.01f).contains(mapped))
        << "mapped=" << mapped.toString() << " mapArea=" << mapArea.toString();
    EXPECT_NEAR(mapped.getCentreX(), mapArea.getCentreX(), 0.01f);
    EXPECT_NEAR(mapped.getCentreY(), mapArea.getCentreY(), 0.01f);
}

// Degenerate (zero-width) world input must not divide by zero — identity-safe, no NaN/inf.
TEST(MinimapComponentTest, ComputeWorldToMap_ZeroWidthWorld_NoNaNOrInf) {
    const juce::Rectangle<float> world(0.0f, 0.0f, 0.0f, 100.0f);
    const juce::Rectangle<float> mapArea(0.0f, 0.0f, 200.0f, 200.0f);

    const auto transform = MinimapComponent::computeWorldToMap(world, mapArea);
    const auto p = juce::Point<float>(5.0f, 5.0f).transformedBy(transform);

    EXPECT_FALSE(std::isnan(p.x));
    EXPECT_FALSE(std::isnan(p.y));
    EXPECT_FALSE(std::isinf(p.x));
    EXPECT_FALSE(std::isinf(p.y));
}

// Degenerate (zero-height) map area input must not divide by zero — identity-safe, no NaN/inf.
TEST(MinimapComponentTest, ComputeWorldToMap_ZeroHeightMapArea_NoNaNOrInf) {
    const juce::Rectangle<float> world(0.0f, 0.0f, 100.0f, 100.0f);
    const juce::Rectangle<float> mapArea(0.0f, 0.0f, 200.0f, 0.0f);

    const auto transform = MinimapComponent::computeWorldToMap(world, mapArea);
    const auto p = juce::Point<float>(5.0f, 5.0f).transformedBy(transform);

    EXPECT_FALSE(std::isnan(p.x));
    EXPECT_FALSE(std::isnan(p.y));
    EXPECT_FALSE(std::isinf(p.x));
    EXPECT_FALSE(std::isinf(p.y));
}

// ---------------------------------------------------------------------------
// mapToWorld — true inverse of computeWorldToMap
// ---------------------------------------------------------------------------

TEST(MinimapComponentTest, MapToWorld_RoundTripsSeveralPoints) {
    const juce::Rectangle<float> world(-200.0f, -100.0f, 800.0f, 600.0f);
    const juce::Rectangle<float> mapArea(4.0f, 4.0f, 212.0f, 142.0f);
    const auto forward = MinimapComponent::computeWorldToMap(world, mapArea);

    const juce::Point<float> worldPoints[] = {
        world.getTopLeft(), world.getBottomRight(), world.getCentre(), {-50.0f, 250.0f}, {600.0f, -80.0f}};

    for (const auto& wp : worldPoints) {
        const auto mapped = wp.transformedBy(forward);
        const auto back = MinimapComponent::mapToWorld(mapped, world, mapArea);
        EXPECT_NEAR(back.x, wp.x, 0.01f);
        EXPECT_NEAR(back.y, wp.y, 0.01f);
    }
}

// ---------------------------------------------------------------------------
// MinimapModel::operator==/!=
// ---------------------------------------------------------------------------

TEST(MinimapModelTest, IdenticalModelsAreEqual) {
    MinimapModel a;
    a.viewport = {0.0f, 0.0f, 100.0f, 100.0f};
    a.nodes.push_back(makeNode({1.0f, 1.0f, 10.0f, 10.0f}));
    a.cables.push_back(makeCable({0.0f, 0.0f}, {5.0f, 5.0f}));

    MinimapModel b = a;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(MinimapModelTest, DifferingViewportMakesModelsUnequal) {
    MinimapModel a, b;
    a.viewport = {0.0f, 0.0f, 100.0f, 100.0f};
    b.viewport = {0.0f, 0.0f, 200.0f, 100.0f};
    EXPECT_TRUE(a != b);
}

TEST(MinimapModelTest, DifferingNodeCountMakesModelsUnequal) {
    MinimapModel a, b;
    a.nodes.push_back(makeNode({0.0f, 0.0f, 10.0f, 10.0f}));
    b.nodes.push_back(makeNode({0.0f, 0.0f, 10.0f, 10.0f}));
    b.nodes.push_back(makeNode({50.0f, 50.0f, 10.0f, 10.0f}));
    EXPECT_TRUE(a != b);
}

TEST(MinimapModelTest, MovedNodeWithSameCountMakesModelsUnequal) {
    MinimapModel a, b;
    a.nodes.push_back(makeNode({0.0f, 0.0f, 10.0f, 10.0f}));
    b.nodes.push_back(makeNode({1.0f, 0.0f, 10.0f, 10.0f})); // shifted by 1px
    EXPECT_TRUE(a != b);
}

TEST(MinimapModelTest, NodeDifferingOnlyInSelectedMakesModelsUnequal) {
    MinimapModel a, b;
    a.nodes.push_back(makeNode({0.0f, 0.0f, 10.0f, 10.0f}, juce::Colours::red, false));
    b.nodes.push_back(makeNode({0.0f, 0.0f, 10.0f, 10.0f}, juce::Colours::red, true));
    EXPECT_TRUE(a != b);
}

TEST(MinimapModelTest, CableDifferingOnlyInColourMakesModelsUnequal) {
    MinimapModel a, b;
    a.cables.push_back(makeCable({0.0f, 0.0f}, {10.0f, 10.0f}, juce::Colours::red));
    b.cables.push_back(makeCable({0.0f, 0.0f}, {10.0f, 10.0f}, juce::Colours::blue));
    EXPECT_TRUE(a != b);
}

// ---------------------------------------------------------------------------
// setModel — change detection, observed via getModel()
// ---------------------------------------------------------------------------

TEST(MinimapComponentTest, SetModel_DifferentModelIsReflectedByGetModel) {
    MinimapComponent comp;

    MinimapModel model1;
    model1.viewport = {0.0f, 0.0f, 100.0f, 100.0f};
    comp.setModel(model1);
    EXPECT_TRUE(comp.getModel() == model1);

    MinimapModel model2;
    model2.viewport = {10.0f, 10.0f, 300.0f, 300.0f};
    model2.nodes.push_back(makeNode({5.0f, 5.0f, 20.0f, 20.0f}));
    comp.setModel(model2);
    EXPECT_TRUE(comp.getModel() == model2);
}

TEST(MinimapComponentTest, SetModel_EqualModelLeavesGetModelUnchanged) {
    MinimapComponent comp;

    MinimapModel model;
    model.viewport = {5.0f, 5.0f, 50.0f, 50.0f};
    model.nodes.push_back(makeNode({1.0f, 1.0f, 10.0f, 10.0f}, juce::Colours::red, true));
    comp.setModel(model);
    ASSERT_TRUE(comp.getModel() == model);

    // Handing back an equal (but distinct) copy must be a safe no-op and getModel() must still
    // report the same data.
    MinimapModel equalCopy = model;
    EXPECT_NO_THROW(comp.setModel(equalCopy));
    EXPECT_TRUE(comp.getModel() == model);
}

// ---------------------------------------------------------------------------
// setViewport — updates only the viewport
// ---------------------------------------------------------------------------

TEST(MinimapComponentTest, SetViewport_UpdatesOnlyViewport) {
    MinimapComponent comp;

    MinimapModel model;
    model.viewport = {0.0f, 0.0f, 10.0f, 10.0f};
    model.nodes.push_back(makeNode({1.0f, 1.0f, 5.0f, 5.0f}));
    model.cables.push_back(makeCable({0.0f, 0.0f}, {2.0f, 2.0f}));
    comp.setModel(model);

    const juce::Rectangle<float> newViewport(50.0f, 60.0f, 200.0f, 150.0f);
    comp.setViewport(newViewport);

    const auto& result = comp.getModel();
    EXPECT_TRUE(result.viewport == newViewport);
    EXPECT_EQ(result.nodes, model.nodes);
    EXPECT_EQ(result.cables, model.cables);
}

// ---------------------------------------------------------------------------
// Component-level: construction, painting, tooltip
// ---------------------------------------------------------------------------

TEST(MinimapComponentTest, ConstructsHeadlesslyAtRealisticSizeWithoutCrash) {
    EXPECT_NO_THROW({
        MinimapComponent comp;
        comp.setSize(220, 150);
    });
}

TEST(MinimapComponentTest, RendersNonEmptyImageAfterSetModel) {
    MinimapComponent comp;
    comp.setSize(220, 150);

    MinimapModel model;
    model.viewport = {0.0f, 0.0f, 800.0f, 600.0f};
    model.nodes.push_back(makeNode({0.0f, 0.0f, 100.0f, 100.0f}, juce::Colours::red));
    model.nodes.push_back(makeNode({500.0f, 400.0f, 80.0f, 60.0f}, juce::Colours::blue));
    comp.setModel(model);

    const juce::Image img = comp.createComponentSnapshot(comp.getLocalBounds());
    ASSERT_TRUE(img.isValid());
    EXPECT_EQ(img.getWidth(), 220);
    EXPECT_EQ(img.getHeight(), 150);

    bool hasOpaque = false;
    for (int y = 0; y < img.getHeight() && !hasOpaque; ++y)
        for (int x = 0; x < img.getWidth() && !hasOpaque; ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                hasOpaque = true;
    EXPECT_TRUE(hasOpaque);
}

TEST(MinimapComponentTest, HasNonEmptyTooltip) {
    MinimapComponent comp;
    EXPECT_FALSE(comp.getTooltip().isEmpty());
}

// Hovering the map must advertise the show/hide shortcut, the same way the toolbar buttons do.
TEST(MinimapComponentTest, ShortcutHintAppearsInTooltip) {
    MinimapComponent comp;
    comp.setShortcutHint("Cmd+K");

    const auto tooltip = comp.getTooltip();
    EXPECT_TRUE(tooltip.contains("Cmd+K")) << tooltip;
    EXPECT_TRUE(tooltip.contains("Hide Minimap")) << tooltip;
    // The navigation gestures must survive alongside the shortcut, not be replaced by it.
    EXPECT_TRUE(tooltip.contains("navigate")) << tooltip;
}

// A rebind replaces the advertised key rather than appending to it.
TEST(MinimapComponentTest, ShortcutHintIsReplacedOnRebind) {
    MinimapComponent comp;
    comp.setShortcutHint("Cmd+K");
    comp.setShortcutHint("Cmd+J");

    const auto tooltip = comp.getTooltip();
    EXPECT_TRUE(tooltip.contains("Cmd+J")) << tooltip;
    EXPECT_FALSE(tooltip.contains("Cmd+K")) << tooltip;
}

// An unbound action must degrade to the plain description, with no empty "()" left behind.
TEST(MinimapComponentTest, EmptyShortcutHintOmitsTheShortcut) {
    MinimapComponent comp;
    comp.setShortcutHint("");

    const auto tooltip = comp.getTooltip();
    EXPECT_FALSE(tooltip.isEmpty());
    EXPECT_FALSE(tooltip.contains("(")) << tooltip;
}

// ---------------------------------------------------------------------------
// Mouse interaction — onNavigate
// ---------------------------------------------------------------------------

// A click at the centre of the component must fire onNavigate with a canvas point near the
// centre of the world bounds. The expectation is derived from mapToWorld itself (not a magic
// number), since getMapArea()'s symmetric 4px inset keeps the component's centre and the map
// area's centre identical.
TEST(MinimapComponentTest, MouseDownAtCentreFiresOnNavigateNearWorldCentre) {
    MinimapComponent comp;
    comp.setSize(220, 150);

    MinimapModel model;
    model.viewport = {0.0f, 0.0f, 800.0f, 600.0f};
    comp.setModel(model);

    juce::Point<float> firedPoint;
    bool fired = false;
    comp.onNavigate = [&](juce::Point<float> p) {
        fired = true;
        firedPoint = p;
    };

    const auto centre = comp.getLocalBounds().toFloat().getCentre();
    const auto event = makeMouseEvent(comp, centre);
    comp.mouseDown(event);

    ASSERT_TRUE(fired);

    const auto world = MinimapComponent::computeWorldBounds(model);
    const auto mapArea = comp.getLocalBounds().toFloat().reduced(4.0f);
    const auto expected = MinimapComponent::mapToWorld(centre, world, mapArea);

    EXPECT_NEAR(firedPoint.x, expected.x, 3.0f);
    EXPECT_NEAR(firedPoint.y, expected.y, 3.0f);
}

// Dragging must also fire onNavigate (not just the initial mouseDown).
TEST(MinimapComponentTest, MouseDragFiresOnNavigate) {
    MinimapComponent comp;
    comp.setSize(220, 150);

    MinimapModel model;
    model.viewport = {0.0f, 0.0f, 800.0f, 600.0f};
    comp.setModel(model);

    int callCount = 0;
    comp.onNavigate = [&](juce::Point<float>) { ++callCount; };

    const auto centre = comp.getLocalBounds().toFloat().getCentre();
    comp.mouseDown(makeMouseEvent(comp, centre));
    EXPECT_EQ(callCount, 1);

    const auto dragPos = centre + juce::Point<float>(15.0f, 10.0f);
    comp.mouseDrag(makeMouseEvent(comp, dragPos));
    EXPECT_EQ(callCount, 2);
}

// ---------------------------------------------------------------------------
// Callbacks safely no-op when unset
// ---------------------------------------------------------------------------

TEST(MinimapComponentTest, MouseAndWheelEventsAreSafeWithoutCallbacks) {
    MinimapComponent comp;
    comp.setSize(220, 150);

    MinimapModel model;
    model.viewport = {0.0f, 0.0f, 800.0f, 600.0f};
    comp.setModel(model);

    // onNavigate / onZoom deliberately left as default-constructed (null) std::function.
    const auto centre = comp.getLocalBounds().toFloat().getCentre();
    const auto event = makeMouseEvent(comp, centre);

    EXPECT_NO_THROW(comp.mouseDown(event));
    EXPECT_NO_THROW(comp.mouseDrag(event));

    juce::MouseWheelDetails wheel{}; // value-init: the struct has no default member initialisers
    wheel.deltaY = 0.5f;
    EXPECT_NO_THROW(comp.mouseWheelMove(event, wheel));
}
