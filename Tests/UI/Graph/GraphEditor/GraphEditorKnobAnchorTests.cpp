// FRO288/FRO312/FRO313: a cable whose destination is a bound, visible knob re-anchors its endpoint
// onto that knob's modulation-ring LANDING point (just outside the drawn arc, not on it — FRO313)
// instead of the gutter jack (GraphEditor::reanchorCablesToKnobTargets, GraphEditorModHover.cpp) --
// docs/layout/cables.md#knob-landing. FRO312 hides a knob-bound jack's gutter dot entirely, so
// EVERY cable kind lands on the knob now, not just AttenuverterChain; only a hidden-page knob keeps
// the jack.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
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

// FRO312: the Rate CV jack is knob-bound (Rate (Hz) resolves a knob for it), so it draws no
// gutter dot at all -- a raw DirectCV connection into it (bypassing addModRouting entirely) has
// nowhere else to land any more and lands on the knob exactly like an AttenuverterChain routing
// does. This replaces the pre-FRO312 `DirectCVCableKeepsTheGutterJackNotTheKnob` expectation, which
// depended on the gutter jack still existing.
TEST_F(GraphEditorTest, DirectCVCableAlsoLandsOnTheKnobNowThatItsJackIsHidden) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto f = makeLfoFlangerFixture(engine, editor);
    ASSERT_NE(f.lfoComp, nullptr);
    ASSERT_NE(f.flangerComp, nullptr);
    ASSERT_NE(f.rateKnob, nullptr);

    // A raw connection straight into the Rate CV channel, bypassing addModRouting -- the engine
    // classifies this as DirectCV (no AttenuverterModule in the path).
    engine.getGraph().addConnection({{f.lfoId, 0}, {f.flangerId, 2}});
    editor.timerCallback(); // refreshes cachedModRoutings, which pass 2 of rebuildVisibleCables reads

    bool foundDirect = false;
    for (const auto& c : editor.buildVisibleCables()) {
        if (c.kind != GraphEditor::VisibleCable::Kind::ModRouting || c.destNodeId != f.flangerId.uid ||
            c.destChannel != 2)
            continue;
        foundDirect = true;
        EXPECT_TRUE(c.landsOnKnob) << "the jack this used to land on is hidden now -- there is nowhere else to go";
        const auto expectedAnchor =
            f.flangerComp->getBounds().getPosition().toFloat() + *f.flangerComp->getModTargetKnobAnchor(2);
        EXPECT_NEAR(c.p2.x, expectedAnchor.x, 0.5f);
        EXPECT_NEAR(c.p2.y, expectedAnchor.y, 0.5f);
    }
    EXPECT_TRUE(foundDirect) << "expected a DirectCV ModRouting cable to Flanger channel 2";
}

// FRO313: the landing anchor sits just OUTSIDE the ring's own drawn arc -- never on it, so the
// dot GraphEditorCables.cpp draws there can never visually overlap the arc.
TEST_F(GraphEditorTest, KnobLandingAnchorSitsOutsideTheRingArc) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FlangerModule flanger;
    ModuleComponent card(&flanger, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::Slider* rateKnob = nullptr;
    for (auto* child : card.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Rate (Hz)")
                rateKnob = s;
    ASSERT_NE(rateKnob, nullptr);

    const auto anchor = card.getModTargetKnobAnchor(2);
    ASSERT_TRUE(anchor.has_value());

    const auto b = rateKnob->getBounds().toFloat();
    const juce::Point<float> ringCentre{b.getCentreX(), b.getCentreY() - 10.0f};
    const float ringRadius = std::min(b.getWidth(), b.getHeight()) / 2.0f - 11.0f;

    static const synth::theme::Metrics kDefaultMetrics{};
    const float minExpectedDistance =
        ringRadius + kDefaultMetrics.knobRingWidth * 0.5f + ModuleComponent::kKnobLandingDotDiameter * 0.5f;

    EXPECT_GE(anchor->getDistanceFrom(ringCentre), minExpectedDistance)
        << "the landing dot must clear the ring's own drawn arc, not sit on it";
}
