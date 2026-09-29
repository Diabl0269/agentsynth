// Sidebar layout of an OPEN macro (docs/macros/ports.md#how-a-port-is-drawn): the hull grows down when the port rows
// outrun its members, a boundary cable ends on the port's outer jack (the hull border), and a cable dropped on that
// jack through the real mouse handlers lands on THAT port. The hull '+'/'-' buttons and the strip paint follow the same
// layout (macroHullPortLayout) and are covered further down.

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <gtest/gtest.h>

#include "MacroPortWidgetTestHelpers.h" // shared fixtures + graph/lookup helpers

namespace {

juce::MouseEvent makeSidebarMouseEvent(juce::Component& comp, juce::Point<float> position, bool wasDragged,
                                       juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos, juce::Time::getCurrentTime(), 1,
                            wasDragged);
}

bool connected(AudioEngine& engine, NodeID src, NodeID dst) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == src && c.destination.nodeID == dst)
            return true;
    return false;
}

juce::String addInlet(GraphEditor& editor, const juce::String& macroId, const juce::String& name) {
    return editor.getMacroController().addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                                    MacroPortShape::Mono, 1, name);
}

} // namespace

TEST(MacroPortSidebar, HullGrowsDownWhenRowsOutrunTheMembers) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto before = editor.getMacroController().macroHullBounds(macroId);

    constexpr int kPorts = 40;
    for (int i = 0; i < kPorts; ++i)
        ASSERT_FALSE(addInlet(editor, macroId, "In " + juce::String(i)).isEmpty());

    const auto after = editor.getMacroController().macroHullBounds(macroId);
    const int rowsBottom = after.getY() + 30 + kPorts * detail::kMacroPortRowHeight + detail::kMacroPortStripFooter;
    ASSERT_GT(rowsBottom, before.getBottom()) << "the fixture must have more rows than the members are tall";
    EXPECT_EQ(after.getBottom(), rowsBottom) << "the hull outline grows DOWN to hold every row and the footer";
    EXPECT_EQ(after.getY(), before.getY()) << "the top edge stays put";

    const auto layout = editor.getMacroController().macroHullPortLayout(macroId);
    ASSERT_EQ(layout.size(), (size_t)kPorts);
    EXPECT_LE(layout.back().widgetBounds.getBottom() + detail::kMacroPortStripFooter, after.getBottom());
}

TEST(MacroPortSidebar, BoundaryCableEndsOnTheOuterJackOfAnOpenMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    addInlet(editor, macroId, "In A");
    const auto uuid = addInlet(editor, macroId, "In B");
    const auto portNodeId = nodeIdForUuid(engine, uuid);
    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    editor.connectPorts(extOsc, 0, portNodeId, 0, /*isMidi=*/false, /*recordUndo=*/false);

    std::optional<MacroGroupController::MacroHullPort> entry;
    for (const auto& p : editor.getMacroController().macroHullPortLayout(macroId))
        if (p.nodeUuid == uuid)
            entry = p;
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->row, 1);

    bool found = false;
    for (const auto& cable : editor.buildVisibleCables()) {
        if (cable.id.srcUid == extOsc.uid && cable.id.dstUid == portNodeId.uid) {
            found = true;
            EXPECT_FLOAT_EQ(cable.p2.x, (float)entry->outerJack.x) << "the cable lands on the hull border";
            EXPECT_FLOAT_EQ(cable.p2.y, (float)entry->outerJack.y) << "on the port's own row";
        }
    }
    EXPECT_TRUE(found);
}

// The real mouse path: press on an external module's output jack, release exactly on an existing inlet's outer jack
// (the hull's left border) through ModuleComponent::mouseDown/mouseUp. The cable must land on THAT port, and no new
// port may be minted.
TEST(MacroPortSidebar, DroppingACableOnAnOuterJackThroughTheRealMouseLandsOnThatPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    const auto firstUuid = addInlet(editor, macroId, "In A");
    const auto secondUuid = addInlet(editor, macroId, "In B");
    const auto firstNode = nodeIdForUuid(engine, firstUuid);
    const auto secondNode = nodeIdForUuid(engine, secondUuid);

    auto extId = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto* extComp = findComponent(editor, extId);
    ASSERT_NE(extComp, nullptr);

    juce::Point<int> outer;
    for (const auto& p : editor.getMacroController().macroHullPortLayout(macroId))
        if (p.nodeUuid == secondUuid)
            outer = p.outerJack;
    auto* widget = findComponent(editor, secondNode);
    ASSERT_NE(widget, nullptr);
    auto* content = widget->getParentComponent();
    ASSERT_NE(content, nullptr);
    const auto targetScreen = content->localPointToGlobal(outer.toFloat());

    const auto pressPos = extComp->getPortCenter(0, /*isInput=*/false).toFloat();
    extComp->mouseDown(makeSidebarMouseEvent(*extComp, pressPos, false, pressPos));
    extComp->mouseUp(makeSidebarMouseEvent(*extComp, extComp->getLocalPoint(nullptr, targetScreen), true, pressPos));

    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 2u) << "landing on an existing jack mints no port";
    EXPECT_TRUE(connected(engine, extId, secondNode)) << "the cable lands on the port whose jack was hit";
    EXPECT_FALSE(connected(engine, extId, firstNode));
}
