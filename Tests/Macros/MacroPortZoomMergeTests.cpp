// Zoomed out, an open macro's port widget merges its two jacks into one dot on the hull outline and its strip
// narrows to a rail (docs/macros/ports.md#how-a-port-is-drawn). Canvas geometry (hull, widget bounds) never moves;
// only the interior jack's position, the painted strip and the cables anchored on that jack follow the zoom.

#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <gtest/gtest.h>

#include "MacroPortWidgetTestHelpers.h" // shared fixtures + graph/lookup helpers

namespace {

struct Fixture {
    AudioEngine engine;
    GraphEditor editor{engine};
    juce::String macroId;
    NodeID memberA, memberB;
    juce::String inUuid, outUuid;
    ModuleComponent* inWidget = nullptr;
    ModuleComponent* outWidget = nullptr;

    Fixture() {
        editor.setSize(1600, 1200);
        memberA = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
        memberB = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 300);
        editor.setSelectedNodes({memberA, memberB});
        macroId = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(macroId, false);
        auto& c = editor.getMacroController();
        inUuid = c.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
        outUuid = c.addMacroPort(macroId, false, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Out");
        inWidget = findComponent(editor, nodeIdForUuid(engine, inUuid));
        outWidget = findComponent(editor, nodeIdForUuid(engine, outUuid));
    }

    /** Wheel-zooms out (the real path) until the canvas scale is at or below `target`. */
    void zoomOutTo(float target) {
        while (zoom() > target)
            editor.zoomAroundCentre(-1.0f);
    }
    float zoom() const { return inWidget->getParentComponent()->getTransform().getScaleFactor(); }
    juce::Point<int> canvasPoint(ModuleComponent* w, juce::Point<int> local) const { return w->getPosition() + local; }
};

} // namespace

TEST(MacroPortZoomMerge, InteriorJackSitsOnTheBoundaryJackWhenZoomedOutAndWhereItWasWhenZoomedIn) {
    Fixture f;
    ASSERT_NE(f.inWidget, nullptr);
    ASSERT_NE(f.outWidget, nullptr);
    ASSERT_FLOAT_EQ(f.zoom(), 1.0f);
    const auto hull = f.editor.getMacroController().macroHullBounds(f.macroId);
    const auto [inW, outW] = f.editor.getMacroController().macroHullStripWidths(f.macroId);

    // Zoom 1: unchanged -- boundary jack on the outline, interior jack 5px inside the strip's inner edge.
    EXPECT_EQ(f.canvasPoint(f.inWidget, f.inWidget->getPortCenter(0, true)).x, hull.getX());
    EXPECT_EQ(f.canvasPoint(f.inWidget, f.inWidget->getPortCenter(0, false)).x, hull.getX() + inW - 5);
    EXPECT_EQ(f.canvasPoint(f.outWidget, f.outWidget->getPortCenter(0, false)).x, hull.getRight());
    EXPECT_EQ(f.canvasPoint(f.outWidget, f.outWidget->getPortCenter(0, true)).x, hull.getRight() - outW + 5);

    f.zoomOutTo(0.3f);
    ASSERT_LE(f.zoom(), 0.3f);
    ASSERT_EQ(f.editor.getMacroController().macroHullBounds(f.macroId), hull) << "canvas geometry is zoom-independent";
    const auto inBoundary = f.canvasPoint(f.inWidget, f.inWidget->getPortCenter(0, true));
    const auto inInterior = f.canvasPoint(f.inWidget, f.inWidget->getPortCenter(0, false));
    EXPECT_EQ(inBoundary.x, hull.getX());
    EXPECT_EQ(inInterior, inBoundary);
    const auto outBoundary = f.canvasPoint(f.outWidget, f.outWidget->getPortCenter(0, false));
    const auto outInterior = f.canvasPoint(f.outWidget, f.outWidget->getPortCenter(0, true));
    EXPECT_EQ(outBoundary.x, hull.getRight());
    EXPECT_EQ(outInterior, outBoundary);
}

TEST(MacroPortZoomMerge, ACableIntoAnOutputPortsInteriorJackEndsOnTheBoundaryWhenZoomedOut) {
    Fixture f;
    ASSERT_NE(f.outWidget, nullptr);
    const auto outId = f.outWidget->getNodeId();
    f.editor.connectPorts(f.memberA, 0, outId, 0, false, false);
    const auto hull = f.editor.getMacroController().macroHullBounds(f.macroId);
    const auto [inW, outW] = f.editor.getMacroController().macroHullStripWidths(f.macroId);

    auto endOfCableIntoPort = [&]() {
        for (const auto& cable : f.editor.buildVisibleCables())
            if (cable.destNodeId == outId.uid)
                return std::optional<juce::Point<float>>(cable.p2);
        return std::optional<juce::Point<float>>();
    };
    auto zoomedIn = endOfCableIntoPort(); // primes the memo at zoom 1
    ASSERT_TRUE(zoomedIn.has_value());
    EXPECT_EQ((int)zoomedIn->x, hull.getRight() - outW + 5);

    f.zoomOutTo(0.3f); // a real wheel zoom must drop the memoized endpoints
    auto zoomedOut = endOfCableIntoPort();
    ASSERT_TRUE(zoomedOut.has_value());
    EXPECT_EQ((int)zoomedOut->x, hull.getRight());
}

TEST(MacroPortZoomMerge, StripPaintsAsARailWhenZoomedOutAndFullWidthWhenZoomedIn) {
    Fixture f;
    const auto hull = f.editor.getMacroController().macroHullBounds(f.macroId);
    const juce::Colour canvas(0xff010203);
    auto pixelAt = [&](float zoom) {
        juce::Image image(juce::Image::ARGB, 1600, 1200, true, juce::SoftwareImageType());
        juce::Graphics g(image);
        g.fillAll(canvas);
        detail::paintMacroPortStrips(g, f.editor, zoom);
        return image.getPixelAt(hull.getX() + 40, hull.getCentreY());
    };
    const auto railPixel = pixelAt(0.3f);
    EXPECT_EQ(railPixel, canvas) << "40px in from the outline is past the rail";
    EXPECT_NE(pixelAt(1.0f), canvas) << "at full zoom the strip covers it";

    juce::Image image(juce::Image::ARGB, 1600, 1200, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.fillAll(canvas);
    detail::paintMacroPortStrips(g, f.editor, 0.3f);
    EXPECT_NE(image.getPixelAt(hull.getX() + 4, hull.getCentreY()), canvas) << "the rail itself is shaded";
}

TEST(MacroPortZoomMerge, PressingTheMergedDotOfAnOutputPortDragsFromTheBoundaryJack) {
    Fixture f;
    ASSERT_NE(f.outWidget, nullptr);
    f.zoomOutTo(0.3f);
    auto extFilter = addModuleAt(f.editor, f.engine, std::make_unique<FilterModule>(), 1300, 300);
    auto* ext = findComponent(f.editor, extFilter);
    ASSERT_NE(ext, nullptr);

    // Both jacks coincide; the press lands a few px inside the dot, within reach of both.
    const auto dot = f.outWidget->getPortCenter(0, false);
    const auto press = dot + juce::Point<int>(-3, 0);
    const auto port = f.outWidget->getPortForPoint(press);
    ASSERT_TRUE(port.has_value());
    EXPECT_FALSE(port->isInput) << "the boundary (output) jack wins on the merged dot";

    // Real mouse path: press, drag onto the external Filter's input jack, release.
    const juce::ModifierKeys left(juce::ModifierKeys::leftButtonModifier);
    auto ev = [&](juce::Point<int> pos, bool dragged) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), left, 0.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, f.outWidget, f.outWidget, juce::Time::getCurrentTime(),
                                press.toFloat(), juce::Time::getCurrentTime(), 1, dragged);
    };
    f.outWidget->mouseDown(ev(press, false));
    const auto target = f.outWidget->getLocalPoint(nullptr, ext->localPointToGlobal(ext->getPortCenter(0, true)));
    f.outWidget->mouseDrag(ev(target, true));
    f.outWidget->mouseUp(ev(target, true));

    bool connected = false;
    for (const auto& c : f.engine.getGraph().getConnections())
        connected |= c.source.nodeID == f.outWidget->getNodeId() && c.destination.nodeID == extFilter;
    EXPECT_TRUE(connected) << "the cable left the port's output (boundary) jack";
}

TEST(MacroPortZoomMerge, PressingTheMergedDotOfAnInputPortPicksTheBoundaryInputJack) {
    Fixture f;
    ASSERT_NE(f.inWidget, nullptr);
    f.zoomOutTo(0.3f);
    const auto press = f.inWidget->getPortCenter(0, true) + juce::Point<int>(3, 0);
    const auto port = f.inWidget->getPortForPoint(press);
    ASSERT_TRUE(port.has_value());
    EXPECT_TRUE(port->isInput);
}

TEST(MacroPortZoomMerge, AddButtonIsClickableOnlyWhereTheRemoveButtonIs) {
    Fixture f;
    auto& c = f.editor.getMacroController();
    const auto plus = c.macroHullAddButtonBounds(f.macroId, true);
    EXPECT_TRUE(c.macroHullPortButtonAt(plus.getCentre(), 1.0f).has_value());
    EXPECT_FALSE(c.macroHullPortButtonAt(plus.getCentre(), 0.3f).has_value());
}

TEST(MacroPortZoomMerge, MergeFactorHelpersHitBothEnds) {
    EXPECT_FLOAT_EQ(detail::macroStripPaintedWidth(96.0f, 1.0f), 96.0f);
    EXPECT_FLOAT_EQ(detail::macroStripPaintedWidth(96.0f, 0.0f), detail::kMacroStripRailWidth);
    EXPECT_FLOAT_EQ(detail::macroPortInteriorJackX(5.0f, 91.0f, 1.0f), 91.0f);
    EXPECT_FLOAT_EQ(detail::macroPortInteriorJackX(5.0f, 91.0f, 0.0f), 5.0f);
}
