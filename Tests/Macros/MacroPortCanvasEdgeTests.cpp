// MacroPortCanvasEdgeTests.cpp
// An open macro whose hull sits at the canvas's left edge must keep every input port reachable: the port widget
// overhangs the hull border, and nothing left of x=0 is painted or clickable. Adding a port (through the controller,
// through a real cable drop, to an inner or an outer macro) slides the whole macro right in the same undo record;
// a macro that already clears the edge is never moved, and opening a project never moves anything. See
// docs/layout/layout.md ("Canvas edge and pinned units").

#include "MacroContainer/MacroContainerTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>

namespace {

constexpr int kOverhang = synth::LayoutUtil::kMacroPortOverhang;

// Every node's saved position (by uuid) and every macro's bounds: what a slide, an undo or a load must get exactly
// right.
struct Snapshot {
    std::map<juce::String, juce::Point<int>> nodes;
    std::map<juce::String, juce::Rectangle<int>> macros;
    bool operator==(const Snapshot& other) const { return nodes == other.nodes && macros == other.macros; }
};

struct EdgeCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    EdgeCanvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }

    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    NodeID filter(int x, int y) { return addModuleAt(editor, engine, std::make_unique<FilterModule>(), x, y); }

    juce::String group(std::vector<NodeID> ids) {
        editor.setSelectedNodes(ids);
        return ctl().groupSelectionIntoMacro();
    }

    // An open macro exactly as a project load leaves it: members at the saved positions, no controller call.
    juce::String openMacroAt(int x, int y) {
        const auto a = osc(x, y);
        const auto b = filter(x + 300, y);
        synth::Macro macro;
        macro.name = "Macro";
        macro.collapsed = false;
        macro.bounds = {x, y, 160, 60};
        macro.members = {uuidOf(engine, a), uuidOf(engine, b)};
        const auto id = editor.getMacros().add(macro);
        editor.updateComponents();
        return id;
    }

    // Moves every module of `uuids` by `dx` as a project saved elsewhere would have placed them.
    void shiftNodes(const std::vector<juce::String>& uuids, int dx) {
        for (const auto& uuid : uuids)
            if (auto* node = engine.getGraph().getNodeForId(nodeIdForUuid(engine, uuid)))
                node->properties.set("x", static_cast<int>(node->properties["x"]) + dx);
        editor.updateComponents();
    }

    Snapshot snapshot() {
        Snapshot s;
        for (auto* node : engine.getGraph().getNodes()) {
            const auto uuid = node->properties["uuid"].toString();
            if (uuid.isNotEmpty())
                s.nodes[uuid] = {static_cast<int>(node->properties["x"]), static_cast<int>(node->properties["y"])};
        }
        for (const auto& macro : editor.getMacros().getAll())
            s.macros[macro.id] = macro.bounds;
        return s;
    }

    std::vector<juce::String> nonPortMembers(const juce::String& macroId) {
        std::vector<juce::String> out;
        const auto* macro = editor.getMacros().find(macroId);
        for (const auto& uuid : macro->members)
            if (!macro->memberIsPort(uuid))
                out.push_back(uuid);
        return out;
    }

    juce::String addInput(const juce::String& macroId) {
        return ctl().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    }

    ModuleComponent* widgetFor(const juce::String& portUuid) {
        return findComponent(editor, nodeIdForUuid(engine, portUuid));
    }

    // The real component hit test through the canvas content: a point left of x=0 is outside it.
    bool canvasHits(ModuleComponent& widget, juce::Point<int> widgetLocal) {
        auto* content = widget.getParentComponent();
        if (content == nullptr)
            return false;
        auto* hit = content->getComponentAt(content->getLocalPoint(&widget, widgetLocal));
        return hit == &widget || widget.isParentOf(hit);
    }

    void expectPortReachable(const juce::String& portUuid) {
        auto* widget = widgetFor(portUuid);
        ASSERT_NE(widget, nullptr);
        EXPECT_GE(widget->getX(), 0);
        EXPECT_GE(widget->getY(), 0);
        EXPECT_TRUE(canvasHits(*widget, widget->getLocalBounds().getCentre()));
        EXPECT_TRUE(canvasHits(*widget, widget->getPortCenter(0, true)));
    }

    // Every non-port member moved right by exactly `dx` and not down.
    void expectSlid(const Snapshot& before, const std::vector<juce::String>& uuids, int dx) {
        const auto after = snapshot();
        for (const auto& uuid : uuids) {
            EXPECT_EQ(after.nodes.at(uuid).x, before.nodes.at(uuid).x + dx) << uuid;
            EXPECT_EQ(after.nodes.at(uuid).y, before.nodes.at(uuid).y) << uuid;
        }
    }

    // One undo restores every position and macro bound exactly; one redo re-applies them.
    void expectOneUndoRoundTrip(const Snapshot& before) {
        const auto after = snapshot();
        ASSERT_NE(after, before) << "premise: the change moved something";
        ASSERT_TRUE(undo.undo());
        EXPECT_EQ(snapshot(), before);
        ASSERT_TRUE(undo.redo());
        EXPECT_EQ(snapshot(), after);
    }
};

juce::MouseEvent realMouseEvent(juce::Component& eventComp, juce::Point<int> localPos, juce::Point<int> downPos,
                                bool wasDragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &eventComp, &eventComp, juce::Time::getCurrentTime(), downPos.toFloat(),
                            juce::Time::getCurrentTime(), 1, wasDragged);
}

// A full real press-drag-release from `src`'s output jack 0 to `dst`'s input jack 0. The press point stays fixed for
// the whole gesture, as JUCE holds it.
void dragRealCable(ModuleComponent& src, ModuleComponent& dst) {
    const auto srcJack = src.getPortCenter(0, false);
    src.mouseDown(realMouseEvent(src, srcJack, srcJack, false));
    const auto target = src.getLocalPoint(nullptr, dst.localPointToGlobal(dst.getPortCenter(0, true)));
    src.mouseDrag(realMouseEvent(src, target, srcJack, true));
    src.mouseUp(realMouseEvent(src, target, srcJack, true));
}

} // namespace

// Regression test for FRO432: a port added to an open macro whose hull sits past the canvas's left edge landed at
// negative x, where it was clipped and could not be clicked.
TEST(MacroPortCanvasEdge, AddingAPortSlidesAnEdgeMacroIntoViewAsOneUndoStep) {
    EdgeCanvas c;
    const auto macroId = c.openMacroAt(20, 300);
    const auto members = c.nonPortMembers(macroId);
    const auto hullBefore = c.ctl().macroHullBounds(macroId);
    ASSERT_LT(hullBefore.getX(), 0) << "premise: the hull starts left of the canvas";
    const auto before = c.snapshot();
    c.undo.clearUndoHistory();

    const auto portUuid = c.addInput(macroId);
    ASSERT_FALSE(portUuid.isEmpty());

    const int shortfall = kOverhang - hullBefore.getX();
    c.expectSlid(before, members, shortfall);
    EXPECT_EQ(c.ctl().macroHullBounds(macroId).getX(), kOverhang);
    c.expectPortReachable(portUuid);

    c.expectOneUndoRoundTrip(before);
}

TEST(MacroPortCanvasEdge, DroppingACableOnAnEdgeMacroWithTheRealMouseSlidesItIntoView) {
    EdgeCanvas c;
    const auto macroId = c.openMacroAt(20, 300);
    const auto members = c.nonPortMembers(macroId);
    const auto lfoId = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 1400, 300);
    const auto hullBefore = c.ctl().macroHullBounds(macroId);
    ASSERT_LT(hullBefore.getX(), 0);
    auto* lfoComp = findComponent(c.editor, lfoId);
    auto* target = findComponent(c.editor, nodeIdForUuid(c.engine, members[1])); // the Filter, inside the macro
    ASSERT_NE(lfoComp, nullptr);
    ASSERT_NE(target, nullptr);
    const auto before = c.snapshot();
    c.undo.clearUndoHistory();

    dragRealCable(*lfoComp, *target);

    const auto* macro = c.editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u) << "the drop created one port";
    ASSERT_TRUE(macro->ports[0].isInput);
    c.expectSlid(before, members, kOverhang - hullBefore.getX());
    c.expectPortReachable(macro->ports[0].nodeUuid);

    c.expectOneUndoRoundTrip(before);
}

// Ports a routed send mints (the Mod Matrix and mixer sends go through the programmatic routing seam) slide the macro
// in just like a hand-made one.
TEST(MacroPortCanvasEdge, APortMintedByProgrammaticRoutingSlidesAnEdgeMacroIntoView) {
    EdgeCanvas c;
    const auto macroId = c.openMacroAt(20, 300);
    const auto members = c.nonPortMembers(macroId);
    const auto ext = c.osc(1400, 300);
    const auto target = nodeIdForUuid(c.engine, members[1]);
    const auto hullBefore = c.ctl().macroHullBounds(macroId);
    ASSERT_LT(hullBefore.getX(), 0);
    const auto before = c.snapshot();

    c.ctl().applyProgrammaticConnectionChange(
        /*autoCreatePorts=*/true, [&] { return c.engine.getGraph().addConnection({{ext, 0}, {target, 0}}); });
    c.editor.updateComponents();

    const auto* macro = c.editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    c.expectSlid(before, members, kOverhang - hullBefore.getX());
    c.expectPortReachable(macro->ports[0].nodeUuid);
}

// The outer hull is what reaches past the edge; adding a port to the outer or to the inner macro slides both.
TEST(MacroPortCanvasEdge, NestedMacrosSlideTogetherWhicheverMacroGetsThePort) {
    for (const bool portOnInner : {false, true}) {
        SCOPED_TRACE(portOnInner ? "port on inner" : "port on outer");
        EdgeCanvas c;
        const auto a = c.osc(400, 300);
        const auto b = c.osc(700, 300);
        const auto e = c.osc(400, 900);
        const auto outer = c.group({a, b, e});
        c.ctl().setMacroCollapsed(outer, false);
        const auto inner = c.group({a, b});
        ASSERT_FALSE(inner.isEmpty());
        c.ctl().setMacroCollapsed(inner, false);
        ASSERT_EQ(c.editor.getMacros().parentOf(inner), outer);
        std::vector<juce::String> all;
        for (const auto id : {a, b, e})
            all.push_back(uuidOf(c.engine, id));
        c.shiftNodes(all, -(c.ctl().macroHullBounds(outer).getX() + 40));
        const auto outerBefore = c.ctl().macroHullBounds(outer);
        ASSERT_EQ(outerBefore.getX(), -40) << "premise: the outer hull starts left of the canvas";
        const auto before = c.snapshot();
        c.undo.clearUndoHistory();

        const auto portUuid = c.addInput(portOnInner ? inner : outer);

        c.expectSlid(before, all, kOverhang - outerBefore.getX());
        EXPECT_EQ(c.ctl().macroHullBounds(outer).getX(), kOverhang);
        EXPECT_GE(c.ctl().macroHullBounds(inner).getX(), kOverhang);
        c.expectPortReachable(portUuid);

        c.expectOneUndoRoundTrip(before);
    }
}

// A hull whose widget already fits is left alone; a hull one pixel short of fitting slides by exactly one pixel.
TEST(MacroPortCanvasEdge, AMacroAlreadyOnTheCanvasIsNotMovedByANewPort) {
    EdgeCanvas probe;
    const auto probeMacro = probe.openMacroAt(300, 300);
    const int hullOffset = probe.ctl().macroHullBounds(probeMacro).getX() - 300;

    for (const int hullX : {kOverhang, 190, 1000}) {
        SCOPED_TRACE(hullX);
        EdgeCanvas c;
        const auto macroId = c.openMacroAt(hullX - hullOffset, 300);
        ASSERT_EQ(c.ctl().macroHullBounds(macroId).getX(), hullX);
        const auto before = c.snapshot();

        const auto portUuid = c.addInput(macroId);

        EXPECT_EQ(c.snapshot().nodes.size(), before.nodes.size() + 1);
        c.expectSlid(before, c.nonPortMembers(macroId), 0);
        EXPECT_EQ(c.snapshot().macros, before.macros);
        c.expectPortReachable(portUuid);
    }
}

TEST(MacroPortCanvasEdge, AHullOnePixelShortOfTheMarginSlidesByOnePixel) {
    EdgeCanvas probe;
    const auto probeMacro = probe.openMacroAt(300, 300);
    const int hullOffset = probe.ctl().macroHullBounds(probeMacro).getX() - 300;
    EdgeCanvas c;
    const auto macroId = c.openMacroAt(kOverhang - 1 - hullOffset, 300);
    const auto before = c.snapshot();

    const auto portUuid = c.addInput(macroId);

    c.expectSlid(before, c.nonPortMembers(macroId), 1);
    c.expectPortReachable(portUuid);
}

// Opening a project is not a slide: an open macro already left of the canvas and a collapsed macro far to the right
// come back with every node position and macro bound exactly as saved.
TEST(MacroPortCanvasEdge, OpeningAProjectNeverMovesAnEdgeMacroOrACollapsedMacro) {
    EdgeCanvas saved;
    const auto open = saved.group({saved.osc(400, 300), saved.filter(700, 300)});
    saved.ctl().setMacroCollapsed(open, false);
    ASSERT_FALSE(saved.addInput(open).isEmpty());
    const auto far = saved.group({saved.osc(5000, 900), saved.filter(5300, 900)});
    ASSERT_FALSE(far.isEmpty());
    ASSERT_TRUE(saved.editor.getMacros().find(far)->collapsed);
    saved.shiftNodes(saved.nonPortMembers(open), -(saved.ctl().macroHullBounds(open).getX() + 40));
    ASSERT_LT(saved.ctl().macroHullBounds(open).getX(), 0) << "premise: the saved hull is left of the canvas";
    const auto json = synth::AIStateMapper::graphToJSON(saved.engine.getGraph());
    const auto macrosJson = saved.editor.getMacros().toVar();
    const auto savedSnapshot = saved.snapshot();

    EdgeCanvas loaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, loaded.engine.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/true));
    ASSERT_TRUE(loaded.editor.getMacros().fromVar(macrosJson));
    loaded.editor.updateComponents();

    EXPECT_EQ(loaded.snapshot(), savedSnapshot);
    EXPECT_LT(loaded.ctl().macroHullBounds(open).getX(), 0) << "the open hull was not slid on open";
    synth::AIStateMapper::graphToJSON(loaded.engine.getGraph());
    EXPECT_EQ(loaded.snapshot(), savedSnapshot);
}
