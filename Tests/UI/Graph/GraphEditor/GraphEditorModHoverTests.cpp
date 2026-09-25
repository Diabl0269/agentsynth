// FRO288: the two-directional hover correlation between a knob-landing cable and the knob it lands
// on (GraphEditor::HoveredModTarget / setHoveredModTarget, GraphEditorModHover.cpp) --
// docs/modules/modulation.md#modulation-rings-on-knobs.

#include "GraphEditorTestHelpers.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

struct HoverFixture {
    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* flangerComp = nullptr;
    juce::AudioProcessorGraph::NodeID flangerId;
};

HoverFixture wireLfoToFlangerRate(AudioEngine& engine, GraphEditor& editor) {
    HoverFixture f;
    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto flangerNode = engine.getGraph().addNode(std::make_unique<FlangerModule>());
    editor.updateComponents();

    juce::Slider* rate = nullptr;
    if (auto* content = editor.getChildComponent(0))
        for (auto* child : content->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    f.lfoComp = mod;
                if (mod->getModule() == flangerNode->getProcessor()) {
                    f.flangerComp = mod;
                    for (auto* c : mod->getChildren())
                        if (auto* s = dynamic_cast<juce::Slider*>(c))
                            if (s->getComponentID() == "Rate (Hz)")
                                rate = s;
                }
            }
    f.lfoComp->setTopLeftPosition(0, 0);
    f.flangerComp->setTopLeftPosition(400, 0);
    f.flangerId = flangerNode->nodeID;

    const auto knobPoint = f.flangerComp->getBounds().getPosition() + rate->getBounds().getCentre();
    editor.beginConnectionDrag(f.lfoComp, 0, false, false, {0, 0});
    editor.dragConnection(knobPoint);
    editor.endConnectionDrag(knobPoint);
    editor.timerCallback(); // refreshes cachedModDisplayInfo, which firstAttenuverterForParam reads
    return f;
}

} // namespace

TEST_F(GraphEditorTest, HoveringAKnobLandingCableSetsTheHoveredModTarget) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);
    auto f = wireLfoToFlangerRate(engine, editor);

    const GraphEditor::VisibleCable* cable = nullptr;
    for (const auto& c : editor.buildVisibleCables())
        if (c.landsOnKnob && c.destNodeId == f.flangerId.uid)
            cable = &c;
    ASSERT_NE(cable, nullptr);

    EXPECT_FALSE(editor.getHoveredModTarget().has_value());

    // Simulate the exact resolution mouseMove performs (a synthesized event through JUCE's own
    // dispatch is fixture-heavy for little extra coverage; GraphEditorModDropHintTests.cpp and
    // GraphEditorTests.cpp's own hover tests take the same "drive the handler with content-space
    // coordinates" shortcut for canvas mouse behaviour).
    auto localPos = editor.getChildComponent(0)->getLocalPoint(&editor, cable->p2.toInt());
    juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), localPos.toFloat(), juce::ModifierKeys(),
                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &editor, &editor, juce::Time::getCurrentTime(), localPos.toFloat(),
                       juce::Time::getCurrentTime(), 0, false);
    editor.mouseMove(e);

    ASSERT_TRUE(editor.getHoveredModTarget().has_value());
    EXPECT_EQ(editor.getHoveredModTarget()->nodeId, f.flangerId);
    EXPECT_EQ(editor.getHoveredModTarget()->channel, 2);

    editor.mouseExit(e);
    EXPECT_FALSE(editor.getHoveredModTarget().has_value());
}

TEST_F(GraphEditorTest, HoveringTheKnobItselfSetsAndClearsTheHoveredModTarget) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);
    auto f = wireLfoToFlangerRate(engine, editor);

    juce::Slider* rate = nullptr;
    for (auto* c : f.flangerComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(c))
            if (s->getComponentID() == "Rate (Hz)")
                rate = s;
    ASSERT_NE(rate, nullptr);

    EXPECT_FALSE(editor.getHoveredModTarget().has_value());

    rate->mouseEnter(juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), rate->getLocalBounds().getCentre().toFloat(),
        juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, rate, rate, juce::Time::getCurrentTime(),
        rate->getLocalBounds().getCentre().toFloat(), juce::Time::getCurrentTime(), 0, false));

    ASSERT_TRUE(editor.getHoveredModTarget().has_value());
    EXPECT_EQ(editor.getHoveredModTarget()->nodeId, f.flangerId);
    EXPECT_EQ(editor.getHoveredModTarget()->channel, 2);

    // ...and the knob-landing cable into that knob paints as hovered (the reverse direction).
    auto knobCableHovered = [&] {
        for (const auto& c : editor.buildVisibleCables())
            if (c.landsOnKnob && c.destNodeId == f.flangerId.uid)
                return editor.isCableHovered(c);
        return false;
    };
    EXPECT_TRUE(knobCableHovered());

    rate->mouseExit(juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), rate->getLocalBounds().getCentre().toFloat(),
        juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, rate, rate, juce::Time::getCurrentTime(),
        rate->getLocalBounds().getCentre().toFloat(), juce::Time::getCurrentTime(), 0, false));
    EXPECT_FALSE(editor.getHoveredModTarget().has_value());
    EXPECT_FALSE(knobCableHovered());
}

// Renders the LFO -> Flanger Rate patch (cable landing on the knob, depth band, hover chip)
// headlessly so a human can eyeball it without driving the GUI app. The PNG is only written when
// MOD_KNOB_PNG is set, so a normal CI run never touches the filesystem.
TEST_F(GraphEditorTest, ModulatedKnobRendersToPngForVisualInspection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);
    synth::theme::AppLookAndFeel lf;
    editor.setLookAndFeel(&lf);
    auto f = wireLfoToFlangerRate(engine, editor);

    for (const auto& info : editor.getCachedModDisplayInfo())
        if (auto* node = engine.getGraph().getNodeForId(info.attenuverterNodeID))
            for (auto* p : node->getProcessor()->getParameters())
                if (auto* fp = dynamic_cast<juce::AudioParameterFloat*>(p); fp && fp->getParameterID() == "amount")
                    fp->setValueNotifyingHost(fp->convertTo0to1(0.63f));
    editor.timerCallback();
    editor.setHoveredModTarget(GraphEditor::HoveredModTarget{f.flangerId, 2});

    juce::Image img(juce::Image::ARGB, 800, 420, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(editor.paintEntireComponent(g, true));
    editor.setLookAndFeel(nullptr);

    const char* pngPath = std::getenv("MOD_KNOB_PNG");
    if (pngPath == nullptr || juce::String(pngPath).isEmpty())
        GTEST_SKIP() << "set MOD_KNOB_PNG=<path> to write the rendered patch for visual inspection";
    juce::File outFile(pngPath);
    outFile.deleteFile();
    juce::FileOutputStream stream(outFile);
    juce::PNGImageFormat().writeImageToStream(img, stream);
}
