// FRO287: the card-knob ring-drag gesture end to end -- a real LFO -> attenuverter -> VCA.gain
// routing, a real synthesized mouseDown/drag/up delivered to the knob's CardKnobSlider, asserting
// the ATTENUVERTER's amount moved (never the knob's own gain param), and that undo restores it. A
// plain drag in the knob's centre (no Alt, not on the ring) must still move the knob -- the
// gesture must never steal an ordinary drag.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

juce::Slider* findKnob(ModuleComponent& card, const juce::String& componentId) {
    for (auto* child : card.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == componentId)
                return s;
    return nullptr;
}

juce::AudioParameterFloat* attenuverterAmountParam(juce::AudioProcessorGraph& graph,
                                                   juce::AudioProcessorGraph::NodeID attenId) {
    auto* node = graph.getNodeForId(attenId);
    if (node == nullptr)
        return nullptr;
    return dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"));
}

struct Fixture {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID lfoId, vcaId, attenId;
    ModuleComponent* vcaCard = nullptr;
    juce::Slider* gainKnob = nullptr;

    Fixture() {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);

        auto& graph = engine.getGraph();
        auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
        auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
        lfoId = lfoNode->nodeID;
        vcaId = vcaNode->nodeID;
        attenId = engine.addModRouting(lfoId, 0, vcaId, 1); // LFO CV -> atten -> VCA.gain (ch1)

        editor->updateComponents();
        sizeModuleComponents(*editor);
        editor->timerCallback(); // populates cachedModDisplayInfo

        vcaCard = findModuleComp(*editor, vcaNode->getProcessor());
        gainKnob = vcaCard != nullptr ? findKnob(*vcaCard, "Gain") : nullptr;
    }

    ~Fixture() { engine.shutdown(); }
};

} // namespace

TEST_F(ModuleComponentTest, AltDragOnKnobAdjustsAttenuverterAmountNotTheKnob) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    auto* amount = attenuverterAmountParam(f.engine.getGraph(), f.attenId);
    ASSERT_NE(amount, nullptr);
    const float amountBefore = amount->get();
    const double gainBefore = f.gainKnob->getValue();

    const auto centre = f.gainKnob->getLocalBounds().getCentre().toFloat();
    const auto altMods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);

    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, centre.toInt(), altMods));
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, (centre + juce::Point<float>(0, 40)).toInt(), altMods));
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, (centre + juce::Point<float>(0, 40)).toInt(), altMods));

    EXPECT_NE(amount->get(), amountBefore) << "the drag must adjust the attenuverter's amount";
    EXPECT_DOUBLE_EQ(f.gainKnob->getValue(), gainBefore) << "the knob's own value must never move during the gesture";
}

TEST_F(ModuleComponentTest, DragOnTheRingAnnulusAlsoAdjustsAmount) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    auto* amount = attenuverterAmountParam(f.engine.getGraph(), f.attenId);
    ASSERT_NE(amount, nullptr);
    const float amountBefore = amount->get();

    // Mirrors paintModulationRings' own geometry (ModuleComponentPaint.cpp): centre offset
    // (w/2, h/2-10), radius min(w,h)/2-11.
    const auto b = f.gainKnob->getLocalBounds().toFloat();
    const juce::Point<float> ringCentre{b.getCentreX(), b.getCentreY() - 10.0f};
    const float radius = std::min(b.getWidth(), b.getHeight()) / 2.0f - 11.0f;
    const auto onRing = (ringCentre + juce::Point<float>(radius, 0.0f)).toInt();

    const auto plain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, onRing, plain));
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, onRing + juce::Point<int>(0, 40), plain));
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, onRing + juce::Point<int>(0, 40), plain));

    EXPECT_NE(amount->get(), amountBefore) << "a drag started on the ring annulus must adjust amount";
}

TEST_F(ModuleComponentTest, UndoRestoresTheAmountAfterAGesture) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    auto* amount = attenuverterAmountParam(f.engine.getGraph(), f.attenId);
    ASSERT_NE(amount, nullptr);
    const float amountBefore = amount->get();

    const auto centre = f.gainKnob->getLocalBounds().getCentre().toFloat();
    const auto altMods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, centre.toInt(), altMods));
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, (centre + juce::Point<float>(0, 60)).toInt(), altMods));
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, (centre + juce::Point<float>(0, 60)).toInt(), altMods));
    ASSERT_NE(amount->get(), amountBefore);

    ASSERT_TRUE(f.undo.undo());
    auto* amountAfterUndo = attenuverterAmountParam(f.engine.getGraph(), f.attenId);
    ASSERT_NE(amountAfterUndo, nullptr);
    EXPECT_NEAR(amountAfterUndo->get(), amountBefore, 1e-4f);
}

TEST_F(ModuleComponentTest, HiddenKnobDrawsNoRingAndNoBandAndDoesNotCrash) {
    // FRO287: the depth band shares getModRingSliderIndex's hidden-page rule with the live ring --
    // a knob on an inactive tab page keeps its last bounds, so painting from them would land a
    // band on empty card (issue #180). paintModulationRings' `if (si < 0) continue;` skips both.
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    ASSERT_NE(f.vcaCard, nullptr);
    f.gainKnob->setVisible(false);

    ModulationTarget gainTarget;
    for (const auto& t : dynamic_cast<ModuleBase*>(f.vcaCard->getModule())->getModulationTargets())
        if (t.paramId == "gain")
            gainTarget = t;
    EXPECT_EQ(f.vcaCard->sliderIndexForModTarget(gainTarget), -1) << "a hidden knob must not offer a ring/band slot";

    juce::Image img(juce::Image::ARGB, f.vcaCard->getWidth(), f.vcaCard->getHeight(), true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(f.vcaCard->paint(g));
}

TEST_F(ModuleComponentTest, PlainDragInTheKnobCentreStillMovesTheKnob) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    const double gainBefore = f.gainKnob->getValue();

    const auto centre = f.gainKnob->getLocalBounds().getCentre().toFloat();
    const auto plain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, centre.toInt(), plain));
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, (centre + juce::Point<float>(0, 60)).toInt(), plain));
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, (centre + juce::Point<float>(0, 60)).toInt(), plain));

    EXPECT_NE(f.gainKnob->getValue(), gainBefore) << "an ordinary centre drag must not be stolen by the gesture";
}

// FRO312: the VCA's CV jack (drives Gain) is knob-bound and therefore hidden -- the ONLY way left
// to pick up its landed cable and redirect it is a click on the landing dot, right on the knob
// itself. A REAL synthesized mouseDown/drag/up delivered to the knob's CardKnobSlider, landing
// on a second LFO's output jack, must wire that new source in exactly like a real (visible) input
// jack always could -- proving the pickup gesture reaches the SAME GraphEditor::beginConnectionDrag/
// dragConnection/endConnectionDrag machinery, not a bespoke stand-in.
TEST_F(ModuleComponentTest, PickupGestureOnTheLandingDotStartsARealConnectionDrag) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    ASSERT_NE(f.vcaCard, nullptr);

    auto& graph = f.engine.getGraph();
    auto lfo2Node = graph.addNode(std::make_unique<LFOModule>());
    f.editor->updateComponents();
    sizeModuleComponents(*f.editor);
    auto* lfo2Comp = findModuleComp(*f.editor, lfo2Node->getProcessor());
    ASSERT_NE(lfo2Comp, nullptr);

    // Resolved dynamically rather than hardcoded: VCA's exact raw channel numbering is Source/
    // Modules territory and can shift independently of this test. Whatever channel Gain's
    // ModulationTarget names, it must be knob-bound -- there is no gutter dot to hit-test any more.
    auto* vcaBase = dynamic_cast<ModuleBase*>(f.vcaCard->getModule());
    ASSERT_NE(vcaBase, nullptr);
    int gainDestChannel = -1;
    for (const auto& t : vcaBase->getModulationTargets())
        if (t.paramId == "gain")
            gainDestChannel = t.channelIndex;
    ASSERT_GE(gainDestChannel, 0) << "expected a ModulationTarget driving VCA's Gain parameter";
    // isInputJackKnobBound takes a VISIBLE jack index, not the raw channel -- VCA's Dual I/O split
    // state can make the two differ (the CV jack's raw channel is fixed at 1, but its visible slot
    // moves with the Audio L/R jack count), so this maps through mapInputChannel first, exactly as
    // getPortCenter/drawnInputJackIndices do internally.
    const int gainVisibleJack = vcaBase->mapInputChannel(gainDestChannel).visibleJackIndex;
    EXPECT_TRUE(f.vcaCard->isInputJackKnobBound(gainVisibleJack))
        << "the Gain CV jack (raw channel " << gainDestChannel << ", visible jack " << gainVisibleJack
        << ") must be knob-bound for this test to be meaningful";

    // The landing point, converted from the card's own local frame (getModTargetKnobAnchor's
    // contract) into the knob's -- exactly what a real click on the dot looks like.
    const auto cardLocalAnchor = f.vcaCard->getModTargetKnobAnchor(gainDestChannel);
    ASSERT_TRUE(cardLocalAnchor.has_value());
    const auto knobLocalDown = f.gainKnob->getLocalPoint(f.vcaCard, cardLocalAnchor->roundToInt());

    const auto lfo2OutputGlobal = lfo2Comp->getBounds().getPosition() + lfo2Comp->getPortCenter(0, /*isInput*/ false);
    const auto knobLocalDrop = f.gainKnob->getLocalPoint(nullptr, lfo2OutputGlobal);

    const auto plain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    const int connectionsBefore = (int)graph.getConnections().size();

    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, knobLocalDown, plain));
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, knobLocalDrop, plain));
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, knobLocalDrop, plain));

    EXPECT_GT((int)graph.getConnections().size(), connectionsBefore)
        << "the pickup gesture must start a real INPUT connection drag and wire the new source when dropped";

    // The pre-existing LFO1 -> atten -> Gain routing is a separate thing entirely -- picking up
    // the landed cable to redirect it is a NEW drag, never a teardown of what was already there.
    EXPECT_NE(attenuverterAmountParam(graph, f.attenId), nullptr);
}
