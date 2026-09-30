// Sidebar layout of an OPEN macro (docs/macros/ports.md#how-a-port-is-drawn): the hull grows down when the port rows
// outrun its members, a boundary cable ends on the port's outer jack (the hull border), and a cable dropped on that
// jack through the real mouse handlers lands on THAT port. The hull '+'/'-' buttons and the strip paint follow the same
// layout (macroHullPortLayout) and are covered further down.

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
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

// ============================================================================
// The hull's '+' / '-' at the strips' foot, and the strips painted under the port widgets.
// ============================================================================

namespace {

juce::MouseEvent editorClickAt(GraphEditor& editor, juce::Component& content, juce::Point<int> canvasPos) {
    const auto pos = editor.getLocalPoint(&content, canvasPos.toFloat());
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &editor, &editor, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1,
                            false);
}

juce::Component& contentOf(GraphEditor& editor, AudioEngine& engine, const juce::String& portUuid) {
    auto* widget = findComponent(editor, nodeIdForUuid(engine, portUuid));
    return *widget->getParentComponent();
}

std::vector<juce::String> menuTexts(juce::PopupMenu& menu) {
    std::vector<juce::String> texts;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        texts.push_back(it.getItem().text);
    return texts;
}

} // namespace

TEST(MacroPortSidebar, HullAddButtonSitsAtTheStripFootInsideTheHull) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    for (int i = 0; i < 3; ++i)
        addInlet(editor, macroId, "In " + juce::String(i));

    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    const auto in = editor.getMacroController().macroHullAddButtonBounds(macroId, true);
    const auto out = editor.getMacroController().macroHullAddButtonBounds(macroId, false);
    EXPECT_TRUE(hull.contains(in));
    EXPECT_TRUE(hull.contains(out));
    EXPECT_EQ(in.getY(), hull.getBottom() - 12);
    EXPECT_EQ(in.getX(), hull.getX() + 4);
    EXPECT_EQ(out.getRight(), hull.getRight() - 4);
    for (const auto& p : editor.getMacroController().macroHullPortLayout(macroId))
        EXPECT_GE(in.getY(), p.widgetBounds.getBottom()) << "the '+' is below every row";

    EXPECT_FALSE(editor.getMacroController().macroHullRemoveButtonBounds(macroId, true, 1.0f).isEmpty());
    EXPECT_TRUE(editor.getMacroController().macroHullRemoveButtonBounds(macroId, false, 1.0f).isEmpty())
        << "no output port: nothing to remove";
    EXPECT_TRUE(editor.getMacroController().macroHullRemoveButtonBounds(macroId, true, 0.4f).isEmpty())
        << "'-' hides below 50 percent zoom";
}

TEST(MacroPortSidebar, ClickingTheHullAddButtonOffersTheSameMenuAsTheCard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = addInlet(editor, macroId, "In A");
    auto& content = contentOf(editor, engine, uuid);

    juce::PopupMenu captured;
    bool shown = false;
    editor.setShowCanvasContextMenuHookForTest([&](juce::PopupMenu& m) {
        captured = m;
        shown = true;
    });

    const auto button = editor.getMacroController().macroHullAddButtonBounds(macroId, /*isInput=*/false);
    editor.mouseDown(editorClickAt(editor, content, button.getCentre()));
    ASSERT_TRUE(shown) << "a real mouseDown on the hull '+' opens the add-port menu";

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    auto cardMenu = card->buildAddPortMenu(false);
    EXPECT_EQ(menuTexts(captured), menuTexts(cardMenu));
    EXPECT_EQ(menuTexts(captured).size(), 4u);

    // Choosing an item adds the port on the clicked (output) side.
    juce::PopupMenu::MenuItemIterator it(captured);
    while (it.next())
        if (it.getItem().text == "Audio/CV - Mono")
            it.getItem().action();
    int outputs = 0;
    for (const auto& p : editor.getMacros().find(macroId)->ports)
        outputs += p.isInput ? 0 : 1;
    EXPECT_EQ(outputs, 1);

    editor.setShowCanvasContextMenuHookForTest(nullptr);
}

TEST(MacroPortSidebar, ClickingTheHullMinusRemovesTheBottomPortOnThatSide) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    addInlet(editor, macroId, "In A");
    addInlet(editor, macroId, "In B");
    const auto last = addInlet(editor, macroId, "In C");
    auto& content = contentOf(editor, engine, last);

    const auto minus = editor.getMacroController().macroHullRemoveButtonBounds(macroId, true, 1.0f);
    ASSERT_FALSE(minus.isEmpty());
    editor.mouseDown(editorClickAt(editor, content, minus.getCentre()));

    const auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 2u);
    for (const auto& p : macro->ports)
        EXPECT_NE(p.name, "In C") << "the bottom input is the one removed";
}

// The strip is painted by the canvas, under the port widgets: the same strip pixel is present with and without a
// widget in that row (the widget draws only its jacks and name, no background of its own).
TEST(MacroPortSidebar, HullStripsPaintUnderThePortWidgets) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = addInlet(editor, macroId, "In A");
    auto& content = contentOf(editor, engine, uuid);

    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    const auto entry = editor.getMacroController().macroHullPortLayout(macroId).front();
    // A pixel on the port's row, clear of its jacks (5px each side of the edges) and its name.
    const juce::Point<int> onRow{hull.getX() + 8, entry.outerJack.y - 6};
    // The same column, well below the last row: strip only, no widget.
    const juce::Point<int> belowRows{onRow.x, entry.outerJack.y + 4 * detail::kMacroPortRowHeight};
    const juce::Point<int> outsideHull{hull.getX() - 30, onRow.y};

    // Software image: a GPU/native-backed image reads back zeros on a headless Windows runner.
    // The fixed-width strip can push the hull's left edge into negative canvas x, so paint shifted right.
    constexpr int kShift = 60;
    juce::Image img(juce::Image::ARGB, hull.getRight() + 40 + kShift, hull.getBottom() + 40, true,
                    juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::translation((float)kShift, 0.0f));
        content.paintEntireComponent(g, false);
    }
    const auto outside = img.getPixelAt(outsideHull.x + kShift, outsideHull.y);
    const auto strip = img.getPixelAt(belowRows.x + kShift, belowRows.y);
    const auto underWidget = img.getPixelAt(onRow.x + kShift, onRow.y);
    EXPECT_NE(strip, outside) << "the strip is drawn inside the hull";
    EXPECT_EQ(underWidget, strip) << "the widget does not cover the strip on its own row";
}

// The strip fill spans from the hull top: beside the name pill, in the chip row, the strip is the same colour
// as further down, not the bare canvas. Only the port ROWS start below the chip row.
TEST(MacroPortSidebar, HullStripFillReachesTheHullTop) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = addInlet(editor, macroId, "In A");
    auto& content = contentOf(editor, engine, uuid);

    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    const auto [inW, outW] = editor.getMacroController().macroHullStripWidths(macroId);
    ASSERT_GT(outW, 30) << "the fixture's right strip must clear the 14px collapse button (6px from the edge)";
    // Right strip, inner side (clear of the collapse button and the rounded outer corner), inside the chip row.
    const int x = hull.getRight() - outW + 4;
    const int chipRowY = hull.getY() + 12;
    const int belowChipRowY = hull.getY() + detail::kMacroChipRowHeight + 6;
    const juce::Point<int> outsideHull{hull.getX() - 30, chipRowY};
    ASSERT_GT(inW, 0);

    constexpr int kShift = 60;
    juce::Image img(juce::Image::ARGB, hull.getRight() + 40 + kShift, hull.getBottom() + 40, true,
                    juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::translation((float)kShift, 0.0f));
        content.paintEntireComponent(g, false);
    }
    const auto canvas = img.getPixelAt(outsideHull.x + kShift, outsideHull.y);
    const auto stripBelow = img.getPixelAt(x + kShift, belowChipRowY);
    const auto stripAtTop = img.getPixelAt(x + kShift, chipRowY);
    ASSERT_NE(stripBelow, canvas) << "sanity: the strip is drawn below the chip row";
    EXPECT_NE(stripAtTop, canvas) << "the strip fill also covers the chip row, up to the hull top";
    EXPECT_EQ(stripAtTop, stripBelow) << "one continuous fill from the hull top down";
}

// Strip widths are fixed: they never depend on the port names, so a very long name is ellipsised in its column and
// the collapsed card's title column keeps its room.
TEST(MacroPortSidebar, CardStripsAreFixedSoTheCardTitleKeepsItsRoom) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    auto& controller = editor.getMacroController();
    const auto empty = controller.macroCardStripWidths(macroId);
    ASSERT_FALSE(addInlet(editor, macroId, "Filter Envelope Amount CV Extra Long Name").isEmpty());

    const auto [cardIn, cardOut] = controller.macroCardStripWidths(macroId);
    EXPECT_EQ(cardIn, detail::kMacroCardStripWidth);
    EXPECT_EQ(cardOut, cardIn) << "equal on both sides";
    EXPECT_EQ(empty.first, cardIn) << "reserved even with no ports";
    EXPECT_GE(synth::LayoutUtil::kSingleWidth - cardIn - cardOut, 100) << "title column keeps at least 100px";
}

// The collapsed card paints port names, title and member count with theme text colours, not hard-coded white,
// so they stay readable on the light Daylight theme (a white name on the pale strip was invisible).
TEST(MacroPortSidebar, CollapsedCardPortNamesAreDarkOnTheLightTheme) {
    synth::theme::AppLookAndFeel laf; // outlives the editor
    laf.applyTheme(synth::theme::makeDaylight());
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setLookAndFeel(&laf);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_FALSE(addInlet(editor, macroId, "Cutoff").isEmpty());

    auto* card = editor.getMacroController().getMacroCard(macroId);
    ASSERT_NE(card, nullptr);
    const auto entry = editor.getMacroController().macroCardPortLayout(macroId).front();
    ASSERT_FALSE(entry.labelArea.isEmpty());

    juce::Image img(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        card->paintEntireComponent(g, false);
    }
    // Software image: a native-backed one reads back zeros on a headless Windows runner.
    float darkest = 1.0f;
    for (int y = entry.labelArea.getY(); y < entry.labelArea.getBottom(); ++y)
        for (int x = entry.labelArea.getX(); x < entry.labelArea.getRight(); ++x)
            darkest = juce::jmin(darkest, img.getPixelAt(x, y).getPerceivedBrightness());
    EXPECT_LT(darkest, 0.5f) << "the port name reads dark against Daylight's pale strip";
    editor.setLookAndFeel(nullptr);
}
