// The mod dot end to end: a real LFO -> attenuverter -> VCA.gain routing, real mouse and key events
// delivered to the knob's CardKnobSlider and the dot's Tab-stop button. A plain press on the landing dot
// of a modulated knob drags the amount of the source chosen last (never the knob's own value, one undo
// step); Cmd keeps the old cable re-drag; an unmodulated knob's dot still starts a cable drag.

#include "../ModuleComponent/ModuleComponentTestFixture.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

juce::Slider* findKnob(ModuleComponent& card, const juce::String& componentId) {
    for (auto* child : card.getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == componentId)
                return s;
    return nullptr;
}

juce::AudioParameterFloat* amountParam(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"))
                           : nullptr;
}

void setAmount(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id, float value) {
    if (auto* p = amountParam(graph, id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

void showGainAsKnob(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID vcaId) {
    synth::CardParamItem gain;
    gain.paramId = "gain";
    gain.widget = synth::CardWidget::Knob;
    synth::CardSection section;
    section.id = "main";
    section.items.emplace_back(gain);
    synth::CardLayout layout;
    layout.sections = {section};
    synth::setCardLayoutOverride(graph, nullptr, vcaId, layout);
}

const auto kPlain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
const auto kCmd = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);

struct Fixture {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID lfoId, vcaId, attenId;
    ModuleComponent* vcaCard = nullptr;
    juce::Slider* gainKnob = nullptr;
    int gainChannel = -1;

    explicit Fixture(bool modulated = true) {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto& graph = engine.getGraph();
        auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
        auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
        lfoId = lfoNode->nodeID;
        vcaId = vcaNode->nodeID;
        showGainAsKnob(graph, vcaId);
        for (const auto& t : dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->getModulationTargets())
            if (t.paramId == "gain")
                gainChannel = t.channelIndex;
        if (modulated) {
            attenId = engine.addModRouting(lfoId, 0, vcaId, gainChannel);
            setAmount(graph, attenId, 0.5f);
        }
        refresh();
        vcaCard = findModuleComp(*editor, vcaNode->getProcessor());
        gainKnob = vcaCard != nullptr ? findKnob(*vcaCard, "Gain") : nullptr;
    }

    ~Fixture() { engine.shutdown(); }

    void refresh() {
        editor->updateComponents();
        sizeModuleComponents(*editor);
        editor->timerCallback();
    }

    float amount(juce::AudioProcessorGraph::NodeID id) { return amountParam(engine.getGraph(), id)->get(); }

    // The landing dot, in the knob's own frame -- where a real press on it arrives.
    juce::Point<int> dotInKnob() const {
        const auto anchor = vcaCard->getModTargetKnobAnchor(gainChannel);
        return gainKnob->getLocalPoint(vcaCard, anchor->roundToInt());
    }

    void pressDragRelease(juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys mods = kPlain) {
        gainKnob->mouseDown(makeModuleClickWithMods(*gainKnob, from, mods));
        gainKnob->mouseDrag(makeModuleClickWithMods(*gainKnob, to, mods));
        gainKnob->mouseUp(makeModuleClickWithMods(*gainKnob, to, mods));
    }

    // A second LFO routed onto the same knob; returns its attenuverter.
    juce::AudioProcessorGraph::NodeID addSecondLfo() {
        auto node = engine.getGraph().addNode(std::make_unique<LFOModule>());
        const auto atten = engine.addModRouting(node->nodeID, 0, vcaId, gainChannel);
        setAmount(engine.getGraph(), atten, -0.25f);
        refresh();
        return atten;
    }
};

} // namespace

TEST_F(ModuleComponentTest, DraggingTheDotChangesTheAmountNotTheKnobAndUndoesInOneStep) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    const double gainBefore = f.gainKnob->getValue();
    const auto dot = f.dotInKnob();

    f.pressDragRelease(dot, dot + juce::Point<int>(0, 20)); // 20 px down: less

    EXPECT_NEAR(f.amount(f.attenId), 0.5f - 0.20f, 0.005f);
    EXPECT_DOUBLE_EQ(f.gainKnob->getValue(), gainBefore) << "the knob's own value never moves";
    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole drag is one undo step";
}

TEST_F(ModuleComponentTest, DraggingTheDotUpRaisesTheAmountAndClampsAtOne) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    const auto dot = f.dotInKnob();
    f.pressDragRelease(dot, dot + juce::Point<int>(0, -200));
    EXPECT_NEAR(f.amount(f.attenId), 1.0f, 1e-4f);
}

TEST_F(ModuleComponentTest, ADotPressThatBarelyMovesIsAClickAndPushesNoUndoStep) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    int clicks = 0;
    juce::Component* clickAnchor = nullptr;
    f.editor->getModDot().onModDotClicked = [&](juce::AudioProcessorGraph::NodeID card, int channel,
                                                juce::Component& anchor) {
        ++clicks;
        clickAnchor = &anchor;
        EXPECT_EQ(card, f.vcaId);
        EXPECT_EQ(channel, f.gainChannel);
    };
    const auto dot = f.dotInKnob();

    f.pressDragRelease(dot, dot + juce::Point<int>(0, 2)); // under the 3 px threshold

    EXPECT_EQ(clicks, 1);
    EXPECT_EQ(clickAnchor, f.vcaCard->getModDotButton(f.gainChannel));
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-5f);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, AClickWithNoHandlerSetDoesNothing) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    const auto dot = f.dotInKnob();
    f.pressDragRelease(dot, dot);
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-5f);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, TheDragChangesOnlyTheLastChosenSource) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    const auto second = f.addSecondLfo();
    const auto dot = f.dotInKnob();

    f.pressDragRelease(dot, dot + juce::Point<int>(0, -10)); // default: the first routing
    EXPECT_NEAR(f.amount(f.attenId), 0.6f, 0.005f);
    EXPECT_NEAR(f.amount(second), -0.25f, 1e-4f);

    f.editor->getModDot().setLastChosen(f.vcaId, f.gainChannel, second);
    f.pressDragRelease(dot, dot + juce::Point<int>(0, -10));
    EXPECT_NEAR(f.amount(second), -0.15f, 0.005f);
    EXPECT_NEAR(f.amount(f.attenId), 0.6f, 0.005f);
}

TEST_F(ModuleComponentTest, ALastChosenSourceThatNoLongerExistsFallsBackToTheFirst) {
    Fixture f;
    f.editor->getModDot().setLastChosen(f.vcaId, f.gainChannel, juce::AudioProcessorGraph::NodeID(9999));
    EXPECT_EQ(f.editor->getModDot().chosenAttenuverter(f.vcaId, f.gainChannel), f.attenId);
}

TEST_F(ModuleComponentTest, EscapeDuringTheDragRestoresTheStartingAmountWithNoUndoStep) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    const auto dot = f.dotInKnob();
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, -30), kPlain));
    ASSERT_NEAR(f.amount(f.attenId), 0.8f, 0.005f);

    EXPECT_TRUE(f.gainKnob->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);

    // The rest of the press is swallowed: more movement changes nothing, the release is no click.
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, -60), kPlain));
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, -60), kPlain));
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, CmdPressOnTheDotStillRedragsTheCable) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    auto& graph = f.engine.getGraph();
    auto lfo2Node = graph.addNode(std::make_unique<LFOModule>());
    f.refresh();
    auto* lfo2Comp = findModuleComp(*f.editor, lfo2Node->getProcessor());
    ASSERT_NE(lfo2Comp, nullptr);
    const auto lfo2OutputGlobal = lfo2Comp->getBounds().getPosition() + lfo2Comp->getPortCenter(0, /*isInput*/ false);
    const auto drop = f.gainKnob->getLocalPoint(nullptr, lfo2OutputGlobal);
    const int before = (int)graph.getConnections().size();

    f.pressDragRelease(f.dotInKnob(), drop, kCmd);

    EXPECT_GT((int)graph.getConnections().size(), before) << "Cmd keeps the cable re-drag";
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-5f) << "and it adjusted no amount";
}

TEST_F(ModuleComponentTest, ADotPressOnAnUnmodulatedKnobStillStartsACableDrag) {
    Fixture f(/*modulated*/ false);
    ASSERT_NE(f.gainKnob, nullptr);
    auto& graph = f.engine.getGraph();
    auto lfo2Node = graph.addNode(std::make_unique<LFOModule>());
    f.refresh();
    auto* lfo2Comp = findModuleComp(*f.editor, lfo2Node->getProcessor());
    ASSERT_NE(lfo2Comp, nullptr);
    const auto lfo2OutputGlobal = lfo2Comp->getBounds().getPosition() + lfo2Comp->getPortCenter(0, /*isInput*/ false);
    const auto drop = f.gainKnob->getLocalPoint(nullptr, lfo2OutputGlobal);
    const int before = (int)graph.getConnections().size();

    // Where the dot would be: an unmodulated card has no anchor to ask, so use the modulated geometry.
    const auto dot = f.dotInKnob();
    f.pressDragRelease(dot, drop);

    EXPECT_GT((int)graph.getConnections().size(), before);
}

TEST_F(ModuleComponentTest, TheDotButtonExistsOnlyWithARoutingAndIsNamedAndTipped) {
    Fixture bare(/*modulated*/ false);
    ASSERT_NE(bare.vcaCard, nullptr);
    EXPECT_EQ(bare.vcaCard->getModDotButton(bare.gainChannel), nullptr);

    Fixture f;
    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->getTitle(), "Gain modulation, 1 source");
    EXPECT_EQ(button->getTooltip(), "Modulation sources for Gain");
    EXPECT_TRUE(button->getWantsKeyboardFocus());

    f.addSecondLfo();
    EXPECT_EQ(f.vcaCard->getModDotButton(f.gainChannel)->getTitle(), "Gain modulation, 2 sources");

    // Centred on the landing dot.
    EXPECT_EQ(button->getBounds().getCentre(), f.vcaCard->getModTargetKnobAnchor(f.gainChannel)->roundToInt());
}

TEST_F(ModuleComponentTest, TheDotButtonGoesWithItsKnobWhenTheKnobIsHidden) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    ASSERT_NE(f.vcaCard->getModDotButton(f.gainChannel), nullptr);
    f.gainKnob->setVisible(false);
    f.vcaCard->syncModDotButtons();
    EXPECT_EQ(f.vcaCard->getModDotButton(f.gainChannel), nullptr);
}

TEST_F(ModuleComponentTest, UpOnTheDotButtonRaisesTheAmountByOnePercentAsOneUndoStep) {
    Fixture f;
    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);

    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_NEAR(f.amount(f.attenId), 0.51f, 1e-4f);
    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(f.amount(f.attenId), 0.41f, 1e-4f);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(f.attenId), 0.51f, 1e-4f) << "each key press is its own undo step";
    EXPECT_TRUE(f.editor->getModDot().getTooltip().isShown());
}

TEST_F(ModuleComponentTest, ReturnOnTheDotButtonCallsTheClickHook) {
    Fixture f;
    int clicks = 0;
    f.editor->getModDot().onModDotClicked = [&](auto, int, juce::Component&) { ++clicks; };
    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(clicks, 1);
}

TEST_F(ModuleComponentTest, TabVisitsTheDotRightAfterItsKnob) {
    Fixture f;
    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);
    const auto stops = f.vcaCard->getKeyboardControls();
    const auto knobAt = std::find(stops.begin(), stops.end(), static_cast<juce::Component*>(f.gainKnob));
    ASSERT_NE(knobAt, stops.end());
    ASSERT_NE(knobAt + 1, stops.end());
    EXPECT_EQ(*(knobAt + 1), static_cast<juce::Component*>(button));
}

TEST_F(ModuleComponentTest, TheDragShowsTheTooltipAndReleaseHidesIt) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    const auto dot = f.dotInKnob();
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, -10), kPlain));
    auto& tip = f.editor->getModDot().getTooltip();
    EXPECT_TRUE(tip.isShown());
    EXPECT_TRUE(tip.getText().contains("+60%")) << tip.getText();
    EXPECT_TRUE(tip.getText().contains("LFO")) << tip.getText();
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, -10), kPlain));
    EXPECT_FALSE(tip.isShown());
}

TEST_F(ModuleComponentTest, ATickWithUnchangedRoutingsDoesNotRebuildTheDotButtons) {
    Fixture f;
    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);
    button->setVisible(false); // a sync would show it again
    f.editor->timerCallback();
    EXPECT_FALSE(button->isVisible()) << "no routing changed, so the tick did not walk the cards";

    f.addSecondLfo(); // a routing change does
    EXPECT_NE(f.vcaCard->getModDotButton(f.gainChannel), nullptr);
}
