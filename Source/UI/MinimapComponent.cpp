#include "MinimapComponent.h"
#include "Theme/AppLookAndFeel.h"

namespace synth::ui {

//==============================================================================
MinimapComponent::MinimapComponent() {
    setInterceptsMouseClicks(true, false);
    setTooltip("Minimap - click or drag to navigate, scroll to zoom");
}

void MinimapComponent::setModel(MinimapModel model) {
    if (model_ == model)
        return;
    model_ = std::move(model);
    repaint();
}

void MinimapComponent::setViewport(juce::Rectangle<float> viewport) {
    if (model_.viewport == viewport)
        return;
    model_.viewport = viewport;
    repaint();
}

//==============================================================================
// Pure geometry helpers
//==============================================================================

juce::Rectangle<float> MinimapComponent::computeWorldBounds(const MinimapModel& model) noexcept {
    // Union the viewport (when meaningful) with every node's bounds. Starting from an empty
    // viewport and unioning it in unconditionally would drag the origin into the union even when
    // nothing is actually there, so it only seeds the accumulator when non-empty.
    bool haveBounds = !model.viewport.isEmpty();
    juce::Rectangle<float> bounds = haveBounds ? model.viewport : juce::Rectangle<float>();

    for (const auto& node : model.nodes) {
        bounds = haveBounds ? bounds.getUnion(node.bounds) : node.bounds;
        haveBounds = true;
    }

    // Nothing at all (no viewport, no nodes): fall through with a zero rect at the origin, which
    // the margin + min-span clamp below turns into a kMinWorldSpan square centred on the origin.
    bounds = bounds.expanded(kWorldMargin);

    const auto centre = bounds.getCentre();
    if (bounds.getWidth() < kMinWorldSpan)
        bounds.setWidth(kMinWorldSpan);
    if (bounds.getHeight() < kMinWorldSpan)
        bounds.setHeight(kMinWorldSpan);
    bounds.setCentre(centre);

    return bounds;
}

juce::AffineTransform MinimapComponent::computeWorldToMap(juce::Rectangle<float> world,
                                                          juce::Rectangle<float> mapArea) noexcept {
    if (world.getWidth() <= 0.0f || world.getHeight() <= 0.0f || mapArea.getWidth() <= 0.0f ||
        mapArea.getHeight() <= 0.0f)
        return {}; // degenerate input — identity rather than a divide-by-zero scale

    const float scale = juce::jmin(mapArea.getWidth() / world.getWidth(), mapArea.getHeight() / world.getHeight());
    const float scaledW = world.getWidth() * scale;
    const float scaledH = world.getHeight() * scale;
    // Centre the (possibly non-square, since aspect is preserved) scaled world rect in mapArea.
    const float offsetX = mapArea.getX() + (mapArea.getWidth() - scaledW) * 0.5f;
    const float offsetY = mapArea.getY() + (mapArea.getHeight() - scaledH) * 0.5f;

    return juce::AffineTransform::translation(-world.getX(), -world.getY()).scaled(scale).translated(offsetX, offsetY);
}

juce::Point<float> MinimapComponent::mapToWorld(juce::Point<float> mapPoint, juce::Rectangle<float> world,
                                                juce::Rectangle<float> mapArea) noexcept {
    return mapPoint.transformedBy(computeWorldToMap(world, mapArea).inverted());
}

//==============================================================================
juce::Rectangle<float> MinimapComponent::getMapArea() const { return getLocalBounds().toFloat().reduced(4.0f); }

void MinimapComponent::navigateTo(juce::Point<float> localPos) {
    if (!onNavigate)
        return;
    onNavigate(mapToWorld(localPos, computeWorldBounds(model_), getMapArea()));
}

//==============================================================================
void MinimapComponent::paint(juce::Graphics& g) {
    auto* lnf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // Headless tests install the stock JUCE LnF; fall back to the token defaults (see
    // StatusBarComponent::paint for the same shape) so colour resolution stays exercised.
    juce::Colour bg0, bg1, border, accent;
    float cornerRadius, borderWidth;
    if (lnf) {
        const auto& c = lnf->getTheme().colors;
        const auto& m = lnf->getTheme().metrics;
        bg0 = c.bg0;
        bg1 = c.bg1;
        border = c.border;
        accent = c.accent;
        cornerRadius = m.cornerRadius;
        borderWidth = m.borderWidth;
    } else {
        bg0 = juce::Colours::black;
        bg1 = juce::Colour(0xff13161b);
        border = juce::Colours::grey;
        accent = juce::Colours::cyan;
        cornerRadius = 8.0f;
        borderWidth = 1.0f;
    }

    const auto bounds = getLocalBounds().toFloat();

    // Floating panel background + frame.
    g.setColour(bg1.withAlpha(0.92f));
    g.fillRoundedRectangle(bounds, cornerRadius);
    g.setColour(border);
    g.drawRoundedRectangle(bounds.reduced(borderWidth * 0.5f), cornerRadius, borderWidth);

    const auto mapArea = getMapArea();
    const auto world = computeWorldBounds(model_);
    const auto transform = computeWorldToMap(world, mapArea);

    // Cables: thin straight lines (this is a thumbnail, not the bezier the canvas draws).
    for (const auto& cable : model_.cables) {
        const auto p1 = cable.p1.transformedBy(transform);
        const auto p2 = cable.p2.transformedBy(transform);
        g.setColour(cable.colour.withAlpha(0.5f));
        g.drawLine(p1.x, p1.y, p2.x, p2.y, 1.0f);
    }

    // Nodes: filled rounded rects, clamped to a minimum on-screen size so tiny/zoomed-out modules
    // stay visible, selection stroked in the accent colour.
    constexpr float kMinNodeSize = 2.0f;
    constexpr float kNodeCornerRadius = 1.5f;
    for (const auto& node : model_.nodes) {
        auto r = node.bounds.transformedBy(transform);
        const auto centre = r.getCentre();
        if (r.getWidth() < kMinNodeSize)
            r.setWidth(kMinNodeSize);
        if (r.getHeight() < kMinNodeSize)
            r.setHeight(kMinNodeSize);
        r.setCentre(centre);

        g.setColour(node.colour);
        g.fillRoundedRectangle(r, kNodeCornerRadius);
        if (node.selected) {
            g.setColour(accent);
            g.drawRoundedRectangle(r, kNodeCornerRadius, 1.5f);
        }
    }

    // Viewport: dim everything outside it with a translucent wash (even-odd fill between the map
    // area and the viewport rect leaves a "hole" over the viewport), then stroke the viewport.
    const auto viewportMap = model_.viewport.transformedBy(transform).getIntersection(mapArea);

    juce::Path wash;
    wash.addRectangle(mapArea);
    if (!viewportMap.isEmpty())
        wash.addRectangle(viewportMap);
    wash.setUsingNonZeroWinding(false); // even-odd: the viewport rect punches a hole in the wash
    g.setColour(bg0.withAlpha(0.45f));
    g.fillPath(wash);

    if (!viewportMap.isEmpty()) {
        g.setColour(accent);
        g.drawRect(viewportMap, 1.5f);
    }
}

//==============================================================================
void MinimapComponent::mouseDown(const juce::MouseEvent& e) { navigateTo(e.position); }
void MinimapComponent::mouseDrag(const juce::MouseEvent& e) { navigateTo(e.position); }

void MinimapComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    if (onZoom)
        onZoom(wheel.deltaY);
}

} // namespace synth::ui
