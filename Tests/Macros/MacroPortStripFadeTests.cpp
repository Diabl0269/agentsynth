// Fixed sidebar strip widths and the zoom fade of port names (docs/macros/ports.md#how-a-port-is-drawn): a strip's
// width never depends on ports, names or zoom, the name alpha is a pure function of zoom, and the '-' button is
// clickable only once it is at least half faded in.

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

#include "MacroPortWidgetTestHelpers.h" // shared fixtures + graph/lookup helpers

namespace {

juce::String addPort(GraphEditor& editor, const juce::String& macroId, bool isInput, const juce::String& name) {
    return editor.getMacroController().addMacroPort(macroId, isInput, synth::MacroPortKind::AudioCV,
                                                    MacroPortShape::Mono, 1, name);
}

} // namespace

TEST(MacroPortStripFade, HullWidthIsIdenticalWithZeroOneAndManyPortsIncludingALongName) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    auto& controller = editor.getMacroController();
    controller.setMacroCollapsed(macroId, false);

    const int emptyWidth = controller.macroHullBounds(macroId).getWidth();
    ASSERT_GT(emptyWidth, 0);
    ASSERT_FALSE(addPort(editor, macroId, true, "In").isEmpty());
    EXPECT_EQ(controller.macroHullBounds(macroId).getWidth(), emptyWidth) << "one port";
    ASSERT_FALSE(addPort(editor, macroId, true, "Filter Envelope Amount CV Extra Long Name").isEmpty());
    for (int i = 0; i < 4; ++i)
        ASSERT_FALSE(addPort(editor, macroId, i % 2 == 0, "Port " + juce::String(i)).isEmpty());
    EXPECT_EQ(controller.macroHullBounds(macroId).getWidth(), emptyWidth) << "many ports, one very long name";

    const auto [inW, outW] = controller.macroHullStripWidths(macroId);
    EXPECT_EQ(inW, outW) << "in-width equals out-width";
    EXPECT_EQ(inW, detail::kMacroHullStripWidth);
    EXPECT_EQ(detail::kMacroCardStripWidth, 88);
    EXPECT_EQ(detail::kMacroHullStripWidth, 96);
}

TEST(MacroPortStripFade, CardStripWidthsAreFixedAndTheTitleColumnKeepsRoom) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    auto& controller = editor.getMacroController();
    const auto before = controller.macroCardStripWidths(macroId);
    ASSERT_FALSE(addPort(editor, macroId, false, "A very long output port name that cannot fit").isEmpty());
    const auto after = controller.macroCardStripWidths(macroId);
    EXPECT_EQ(before, after);
    EXPECT_EQ(after.first, after.second);
    EXPECT_EQ(after.first, detail::kMacroCardStripWidth);
    EXPECT_GE(synth::LayoutUtil::kSingleWidth - after.first - after.second, 100);
}

TEST(MacroPortStripFade, NameAlphaIsAPureEasedFunctionOfZoom) {
    EXPECT_FLOAT_EQ(detail::macroPortNameAlphaAtZoom(0.1f), 0.0f);
    EXPECT_FLOAT_EQ(detail::macroPortNameAlphaAtZoom(0.5f), 0.0f);
    EXPECT_FLOAT_EQ(detail::macroPortNameAlphaAtZoom(0.7f), 1.0f);
    EXPECT_FLOAT_EQ(detail::macroPortNameAlphaAtZoom(2.0f), 1.0f);
    EXPECT_NEAR(detail::macroPortNameAlphaAtZoom(0.6f), 0.5f, 1.0e-5f);
    float previous = detail::macroPortNameAlphaAtZoom(0.5f);
    for (float zoom = 0.51f; zoom < 0.7f; zoom += 0.01f) {
        const float alpha = detail::macroPortNameAlphaAtZoom(zoom);
        EXPECT_GT(alpha, previous) << "zoom " << zoom;
        previous = alpha;
    }
}

TEST(MacroPortStripFade, HullMinusIsAbsentAtPointFiveFiveAndPresentAtPointSixFive) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    auto& controller = editor.getMacroController();
    controller.setMacroCollapsed(macroId, false);
    ASSERT_FALSE(addPort(editor, macroId, true, "In").isEmpty());

    EXPECT_TRUE(controller.macroHullRemoveButtonBounds(macroId, true, 0.55f).isEmpty());
    const auto minus = controller.macroHullRemoveButtonBounds(macroId, true, 0.65f);
    EXPECT_FALSE(minus.isEmpty());
    EXPECT_FALSE(controller.macroHullPortButtonAt(minus.getCentre(), 0.55f).has_value());
    EXPECT_TRUE(controller.macroHullPortButtonAt(minus.getCentre(), 0.65f).has_value());
}

TEST(MacroPortStripFade, CardMinusIsAbsentAtPointFiveFiveAndPresentAtPointSixFive) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_FALSE(addPort(editor, macroId, true, "In").isEmpty());
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    auto* content = card->getParentComponent();
    ASSERT_NE(content, nullptr);

    content->setTransform(juce::AffineTransform::scale(0.55f));
    EXPECT_TRUE(card->getRemovePortButtonBoundsForTest(true).isEmpty());
    content->setTransform(juce::AffineTransform::scale(0.65f));
    EXPECT_FALSE(card->getRemovePortButtonBoundsForTest(true).isEmpty());
    content->setTransform({});
}

TEST(MacroPortStripFade, MacroPortWidgetsAreNotRasterFrozenWhileZooming) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    auto& controller = editor.getMacroController();
    controller.setMacroCollapsed(macroId, false);
    ASSERT_FALSE(addPort(editor, macroId, true, "In").isEmpty());

    int widgets = 0, cards = 0;
    for (auto* comp : editor.getModuleComponents()) {
        ASSERT_NE(comp, nullptr);
        const auto ownership = controller.macroPortOwnerFor(comp->getNodeId());
        comp->setRasterFrozen(true);
        if (ownership.port != nullptr) {
            ++widgets;
            EXPECT_FALSE(comp->isRasterFrozen()) << "a docked port widget repaints live during a zoom";
        } else {
            ++cards;
            EXPECT_TRUE(comp->isRasterFrozen()) << "an ordinary card is still frozen";
        }
        comp->setRasterFrozen(false);
    }
    EXPECT_EQ(widgets, 1);
    EXPECT_GT(cards, 0);
}
