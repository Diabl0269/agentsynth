// FRO288: an AttenuverterChain cable whose destination is a bound, visible knob re-anchors its
// endpoint onto that knob's modulation-ring start point instead of the gutter jack
// (GraphEditor::reanchorCablesToKnobTargets, GraphEditorModHover.cpp) --
// docs/layout/cables.md#knob-landing. DirectCV cables and a hidden-page knob keep the jack.

#include "GraphEditorTestHelpers.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

struct LfoFlangerFixture {
    ModuleComponent* lfoComp = nullptr;
    ModuleComponent* flangerComp = nullptr;
    juce::AudioProcessorGraph::NodeID lfoId, flangerId;
    juce::Slider* rateKnob = nullptr;
};

LfoFlangerFixture makeLfoFlangerFixture(AudioEngine& engine, GraphEditor& editor) {
    LfoFlangerFixture f;
    auto lfoNode = engine.getGraph().addNode(std::make_unique<LFOModule>());
    auto flangerNode = engine.getGraph().addNode(std::make_unique<FlangerModule>());
    editor.updateComponents();

    if (auto* content = editor.getChildComponent(0))
        for (auto* child : content->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == lfoNode->getProcessor())
                    f.lfoComp = mod;
                if (mod->getModule() == flangerNode->getProcessor())
                    f.flangerComp = mod;
            }
    f.lfoId = lfoNode->nodeID;
    f.flangerId = flangerNode->nodeID;
    if (f.lfoComp != nullptr)
        f.lfoComp->setTopLeftPosition(0, 0);
    if (f.flangerComp != nullptr) {
        f.flangerComp->setTopLeftPosition(400, 0);
        for (auto* child : f.flangerComp->getChildren())
            if (auto* s = dynamic_cast<juce::Slider*>(child))
                if (s->getComponentID() == "Rate (Hz)")
                    f.rateKnob = s;
    }
    return f;
}

} // namespace

TEST_F(GraphEditorTest, AttenuverterChainCableLandsOnTheTargetKnobsRingStartPoint) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto f = makeLfoFlangerFixture(engine, editor);
    ASSERT_NE(f.lfoComp, nullptr);
    ASSERT_NE(f.flangerComp, nullptr);
    ASSERT_NE(f.rateKnob, nullptr);

    // Drop an LFO cable on the Rate (Hz) knob -- the same path GraphEditorTests.cpp's
    // DroppingACableOnAUnitLabelledKnobCreatesAModRouting exercises -- to get a real, engine-created
    // AttenuverterChain routing at raw channel 2.
    const auto knobPoint = f.flangerComp->getBounds().getPosition() + f.rateKnob->getBounds().getCentre();
    editor.beginConnectionDrag(f.lfoComp, 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
    editor.dragConnection(knobPoint);
    editor.endConnectionDrag(knobPoint);

    const auto* cable = [&]() -> const GraphEditor::VisibleCable* {
        for (const auto& c : editor.buildVisibleCables())
            if (c.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain && c.destNodeId == f.flangerId.uid &&
                c.destChannel == 2)
                return &c;
        return nullptr;
    }();
    ASSERT_NE(cable, nullptr) << "the drop must have created an AttenuverterChain routing to channel 2";
    EXPECT_TRUE(cable->landsOnKnob);

    const auto expectedAnchor =
        f.flangerComp->getBounds().getPosition().toFloat() + *f.flangerComp->getModTargetKnobAnchor(2);
    EXPECT_NEAR(cable->p2.x, expectedAnchor.x, 0.5f);
    EXPECT_NEAR(cable->p2.y, expectedAnchor.y, 0.5f);

    // The anchor must be somewhere ON the knob (inside the card), not at the gutter jack far to the
    // card's left -- a coarse but effective regression guard against silently falling back to the
    // jack.
    const auto knobLocalCentre = f.rateKnob->getBounds().getCentre().toFloat();
    const auto cardLocalP2 = cable->p2 - f.flangerComp->getBounds().getPosition().toFloat();
    EXPECT_LT(cardLocalP2.getDistanceFrom(knobLocalCentre), f.rateKnob->getWidth());

    // getCableAt must hit the RE-ANCHORED cable near the knob (not miss because it's still looking
    // for the old jack position).
    auto hit = editor.getCableAt(cable->p2, GraphEditor::kCableHitTolerance);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->destNodeId, f.flangerId.uid);
    EXPECT_EQ(hit->destChannel, 2);
}

TEST_F(GraphEditorTest, DirectCVCableKeepsTheGutterJackNotTheKnob) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto f = makeLfoFlangerFixture(engine, editor);
    ASSERT_NE(f.lfoComp, nullptr);
    ASSERT_NE(f.flangerComp, nullptr);

    // A raw connection straight into the Rate CV channel, bypassing addModRouting -- the engine
    // classifies this as DirectCV (no AttenuverterModule in the path), which must never be
    // re-anchored onto the knob.
    engine.getGraph().addConnection({{f.lfoId, 0}, {f.flangerId, 2}});
    editor.timerCallback(); // refreshes cachedModRoutings, which pass 2 of rebuildVisibleCables reads

    bool foundDirect = false;
    for (const auto& c : editor.buildVisibleCables()) {
        if (c.kind != GraphEditor::VisibleCable::Kind::ModRouting || c.destNodeId != f.flangerId.uid ||
            c.destChannel != 2)
            continue;
        foundDirect = true;
        EXPECT_FALSE(c.landsOnKnob);
        // Still lands at the gutter jack (the module's port position), well outside the knob.
        const auto knobCentreCanvas =
            f.flangerComp->getBounds().getPosition().toFloat() + f.rateKnob->getBounds().getCentre().toFloat();
        EXPECT_GT(c.p2.getDistanceFrom(knobCentreCanvas), (float)f.rateKnob->getWidth());
    }
    EXPECT_TRUE(foundDirect) << "expected a DirectCV ModRouting cable to Flanger channel 2";
}
