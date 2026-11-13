// GraphEditorCablePaintOrderTests.cpp
//
// Which layer a cable is drawn on: the wire dragged out of a jack is always in front of every module card, and a
// connected cable is in front of the cards only with the "Draw cables on top of modules" preference on
// (docs/layout/cables.md#cable-layer). Images use SoftwareImageType(): the default native image type reads back
// zeros on a Windows runner.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

namespace {

constexpr int kWidth = 1600;
constexpr int kHeight = 900;

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID osc, filter, card;

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(kWidth, kHeight);
        osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 60, 300);
        filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1000, 300);
    }

    juce::Component& content() { return *editor.getChildComponent(0); }

    ModuleComponent* componentFor(NodeID id) {
        for (auto* mc : editor.getModuleComponents())
            if (mc->getNodeId() == id)
                return mc;
        return nullptr;
    }

    // Puts a card (the one the wire has to cross) so that `p` is well inside it.
    void placeCardAround(juce::Point<float> p) {
        card = addModuleAt(editor, engine, std::make_unique<DelayModule>(), (int)p.x - 60, (int)p.y - 40);
        editor.finishCardGlideForTest();
    }

    juce::Image render() {
        juce::Image image(juce::Image::RGB, kWidth, kHeight, true, juce::SoftwareImageType());
        juce::Graphics g(image);
        content().paintEntireComponent(g, false);
        return image;
    }

    std::optional<GraphEditor::VisibleCable> cable() {
        for (const auto& c : editor.buildVisibleCables())
            if (c.id.srcUid == osc.uid && c.id.dstUid == filter.uid)
                return c;
        return std::nullopt;
    }
};

juce::Point<float> pointAlong(juce::Point<float> p1, juce::Point<float> p2, float t) {
    const auto path = GraphEditor::buildCablePath(p1, p2);
    return path.getPointAlongPath(path.getLength() * t);
}

} // namespace

// Regression test for FRO1056: the wire being dragged out of a jack was drawn behind the cards it crossed.
TEST(GraphEditorCablePaintOrder, ADraggedWireIsDrawnOverTheCardsItCrosses) {
    Canvas c;
    c.placeCardAround({600.0f, 400.0f});
    auto* card = c.componentFor(c.card);
    auto* osc = c.componentFor(c.osc);
    ASSERT_NE(card, nullptr);
    ASSERT_NE(osc, nullptr);

    const auto before = c.render();

    const auto jack = (osc->getBounds().getPosition() + osc->getPortCenter(0, false)).toFloat();
    const auto mouse = card->getBounds().getCentre();
    c.editor.beginConnectionDrag(osc, 0, false, false, {0, 0});
    c.editor.dragConnection(mouse);

    const auto onWire = pointAlong(jack, mouse.toFloat(), 0.9f).toInt();
    ASSERT_TRUE(card->getBounds().contains(onWire)) << "the sampled wire point has to lie over the card";

    const auto during = c.render();
    EXPECT_NE(during.getPixelAt(onWire.x, onWire.y), before.getPixelAt(onWire.x, onWire.y))
        << "the drag wire is painted over the card";
}

TEST(GraphEditorCablePaintOrder, ACablePassesBehindACardByDefault) {
    Canvas c;
    c.engine.getGraph().addConnection({{c.osc, 0}, {c.filter, 0}});
    c.editor.updateComponents();
    auto cable = c.cable();
    ASSERT_TRUE(cable.has_value());
    const auto mid = pointAlong(cable->p1, cable->p2, 0.5f);
    c.placeCardAround(mid);
    ASSERT_FALSE(c.editor.cablesOnTop);

    const auto behind = c.render();

    c.engine.getGraph().removeConnection({{c.osc, 0}, {c.filter, 0}});
    c.editor.updateComponents();
    const auto noCable = c.render();

    const auto p = mid.toInt();
    ASSERT_TRUE(c.componentFor(c.card)->getBounds().contains(p));
    EXPECT_EQ(behind.getPixelAt(p.x, p.y), noCable.getPixelAt(p.x, p.y)) << "the card covers the cable";
}

TEST(GraphEditorCablePaintOrder, ACableIsDrawnOverACardWhenCablesOnTopIsOn) {
    Canvas c;
    c.engine.getGraph().addConnection({{c.osc, 0}, {c.filter, 0}});
    c.editor.updateComponents();
    auto cable = c.cable();
    ASSERT_TRUE(cable.has_value());
    const auto mid = pointAlong(cable->p1, cable->p2, 0.5f);
    c.placeCardAround(mid);

    const auto behind = c.render();
    c.editor.cablesOnTop = true;
    const auto onTop = c.render();

    const auto p = mid.toInt();
    ASSERT_TRUE(c.componentFor(c.card)->getBounds().contains(p));
    EXPECT_NE(onTop.getPixelAt(p.x, p.y), behind.getPixelAt(p.x, p.y)) << "the cable is painted over the card";
}

// Painting order is independent of where a click lands: the cable is found on its curve with the preference on.
TEST(GraphEditorCablePaintOrder, ACableIsStillFoundUnderTheMouseWhenCablesOnTopIsOn) {
    Canvas c;
    c.engine.getGraph().addConnection({{c.osc, 0}, {c.filter, 0}});
    c.editor.updateComponents();
    c.editor.cablesOnTop = true;
    auto cable = c.cable();
    ASSERT_TRUE(cable.has_value());
    const auto mid = pointAlong(cable->p1, cable->p2, 0.5f);

    auto hit = c.editor.getCableAt(mid);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->id.srcUid, c.osc.uid);
    EXPECT_EQ(hit->id.dstUid, c.filter.uid);
}
