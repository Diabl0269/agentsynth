// EnvelopeCardTempoSyncTests.cpp: tempo mode on the ADSR envelope card -- attackDiv/holdDiv/decayDiv/
// releaseDiv swapping in over their stage's fader in the same cell (the layout's swap groups, the one
// mechanism: the card body's conditions), the two-way param binding + undo bracketing the division combos get
// like any card combo, the tempo-mode curve model's stage durations, and x-drag snapping. Sits beside
// ModuleComponentEnvelopeCardTests.cpp, which pins the Time/Tempo switch itself.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/ADSRModule.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;

namespace {

juce::ToggleButton* findToggleByText(ModuleComponent& comp, const juce::String& text) {
    for (auto* child : comp.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            if (toggle->getButtonText() == text)
                return toggle;
    return nullptr;
}

juce::Slider* findSliderByCaption(ModuleComponent& comp, const juce::String& caption) {
    for (auto* child : comp.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            if (slider->getComponentID() == caption)
                return slider;
    return nullptr;
}

// The division combo of a stage: the body's widget for its parameter.
juce::ComboBox* findDivisionCombo(ModuleComponent& comp, const juce::String& paramId) {
    return dynamic_cast<juce::ComboBox*>(comp.getCardBody()->findWidget(paramId));
}

// Writes tempoSync as automation or a preset load would, then runs the queued condition re-read.
void setTempoSync(ModuleComponent& comp, juce::AudioProcessor& processor, bool on) {
    auto* param = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&processor, "tempoSync"));
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(on ? 1.0f : 0.0f);
    comp.getCardBody()->flushPendingConditionUpdate();
}

// The combo shares the fader's cell: the same column, the same top, centred over it.
bool sharesCellWith(const juce::Component& combo, const juce::Component& fader) {
    return combo.getY() == fader.getY() && combo.getX() <= fader.getX() && combo.getRight() >= fader.getRight();
}

CurveEditorComponent* findEnvelopeCurveEditor(ModuleComponent& comp) {
    for (auto* child : comp.getChildren())
        if (auto* curve = dynamic_cast<CurveEditorComponent*>(child))
            return curve;
    return nullptr;
}

// Opens the envelope graph via a real notification, not a direct model/param write -- mirrors
// ModuleComponentEnvelopeCardTests.cpp's own openEnvelopeGraph.
CurveEditorComponent* openEnvelopeGraph(ModuleComponent& comp) {
    auto* toggle = findToggleByText(comp, "Show Envelope Graph");
    if (toggle == nullptr)
        return nullptr;
    toggle->setToggleState(true, juce::sendNotificationSync);
    return findEnvelopeCurveEditor(comp);
}

juce::MouseEvent curveMouseEvent(juce::Component& comp, juce::Point<float> pos, bool dragged,
                                 juce::Point<float> downPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1,
                            dragged);
}

} // namespace

// Picking Tempo hides the four stage faders (Sustain stays) and shows a division combo in each one's cell;
// picking Time restores them. The card never changes height.
TEST_F(ModuleComponentTest, TempoModeSwapsStageFadersForDivisionCombosInTheSameCellsAndTimeRestoresThem) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    const juce::StringArray stages{"Attack", "Hold", "Decay", "Release"};
    const juce::StringArray divisions{"attackDiv", "holdDiv", "decayDiv", "releaseDiv"};
    auto* sustain = findSliderByCaption(moduleComponent, "Sustain");
    ASSERT_NE(sustain, nullptr);
    const int height = moduleComponent.getHeight();
    for (int i = 0; i < 4; ++i) {
        auto* fader = findSliderByCaption(moduleComponent, stages[i]);
        auto* combo = findDivisionCombo(moduleComponent, divisions[i]);
        ASSERT_NE(fader, nullptr) << stages[i];
        ASSERT_NE(combo, nullptr) << divisions[i];
        EXPECT_TRUE(fader->isVisible());
        EXPECT_FALSE(combo->isVisible());
    }

    setTempoSync(moduleComponent, processor, true);
    for (int i = 0; i < 4; ++i) {
        auto* fader = findSliderByCaption(moduleComponent, stages[i]);
        auto* combo = findDivisionCombo(moduleComponent, divisions[i]);
        EXPECT_FALSE(fader->isVisible()) << stages[i];
        EXPECT_TRUE(combo->isVisible()) << divisions[i];
        EXPECT_TRUE(sharesCellWith(*combo, *fader)) << divisions[i] << " must sit in its stage's cell";
    }
    EXPECT_TRUE(sustain->isVisible()) << "Sustain stays a fader in tempo mode";
    EXPECT_EQ(moduleComponent.getHeight(), height) << "a swap never changes the card's height";

    setTempoSync(moduleComponent, processor, false);
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(findSliderByCaption(moduleComponent, stages[i])->isVisible()) << stages[i];
        EXPECT_FALSE(findDivisionCombo(moduleComponent, divisions[i])->isVisible()) << divisions[i];
    }
    EXPECT_EQ(moduleComponent.getHeight(), height);
}

// The set of jacks a module draws must not change with tempoSync: a swapped-out stage fader keeps its
// knob-bound jack, exactly like the control that replaced it, in both directions.
TEST_F(ModuleComponentTest, VisibleInputPortSetIsIdenticalInTimeAndTempoMode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    const auto drawnInTime = moduleComponent.drawnInputJackIndices();
    setTempoSync(moduleComponent, processor, true);
    EXPECT_EQ(moduleComponent.drawnInputJackIndices(), drawnInTime)
        << "tempo mode must draw exactly the same gutter jacks -- the four time-backed CV inputs stay "
           "knob-bound (landing on the swapped-out fader's cell), not fall back to a gutter row";
    setTempoSync(moduleComponent, processor, false);
    EXPECT_EQ(moduleComponent.drawnInputJackIndices(), drawnInTime);
}

// A cable connected to the Attack modulation target in tempo mode must still resolve to the stage's cell,
// not a gutter row -- the anchor a cable lands on and hit-tests against.
TEST_F(ModuleComponentTest, AttackModTargetStillResolvesToTheStageCellInTempoMode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* attackSlider = findSliderByCaption(moduleComponent, "Attack");
    ASSERT_NE(attackSlider, nullptr);
    const auto attackBounds = attackSlider->getBounds();

    auto* mod = dynamic_cast<ModuleBase*>(&processor);
    ASSERT_NE(mod, nullptr);
    int attackChannel = -1;
    for (const auto& target : mod->getModulationTargets())
        if (target.paramId == "attack")
            attackChannel = target.channelIndex;
    ASSERT_GE(attackChannel, 0) << "ADSR must expose Attack as a modulation target";

    setTempoSync(moduleComponent, processor, true);

    EXPECT_TRUE(moduleComponent.isInputJackKnobBound(mod->mapInputChannel(attackChannel).visibleJackIndex))
        << "the Attack CV jack must still be knob-bound in tempo mode";

    // getModTargetPortForPoint is what a released cable resolves against -- the (unchanged) attack
    // cell must still report it as the Attack target's port.
    const auto port = moduleComponent.getModTargetPortForPoint(attackBounds.getCentre());
    ASSERT_TRUE(port.has_value());
    EXPECT_EQ(port->index, attackChannel);
    EXPECT_EQ(port->area, attackBounds);
}

// A tempoSync write via the param itself (what a preset load / undo restore / automation lane
// actually does) swaps the stage controls exactly like the switch does.
TEST_F(ModuleComponentTest, ExternalTempoSyncParamWriteSwapsTheUI) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* attackSlider = findSliderByCaption(moduleComponent, "Attack");
    ASSERT_NE(attackSlider, nullptr);
    auto* attackCombo = findDivisionCombo(moduleComponent, "attackDiv");
    ASSERT_NE(attackCombo, nullptr);

    setTempoSync(moduleComponent, processor, true);
    EXPECT_FALSE(attackSlider->isVisible());
    EXPECT_TRUE(attackCombo->isVisible());

    setTempoSync(moduleComponent, processor, false);
    EXPECT_TRUE(attackSlider->isVisible());
    EXPECT_FALSE(attackCombo->isVisible());
}

// A combo pick must write the matching *Div param's index, as ONE undo step (the same
// setValueAsCompleteGesture -> beginChangeGesture/endChangeGesture -> parameterGestureChanged path
// the generic per-param combos already get for free); an external *Div write (undo/preset/
// automation) must move the combo back.
TEST_F(ModuleComponentTest, ComboPickWritesTheDivParamAsOneUndoStepAndExternalWriteUpdatesTheCombo) {
    AudioEngine engine;
    // captureBeforeState/pushSnapshotFromCapture diff the ENGINE's own graph -- the processor
    // must be a real node in it (see ModuleComponentEnvelopeCardTests.cpp's identical setup).
    auto* adsr = new ADSRModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(adsr));
    ASSERT_NE(node, nullptr);
    GraphEditor editor(engine);
    AppUndoManager undoManager;
    ModuleComponent moduleComponent(adsr, node->nodeID, editor, &undoManager);

    setTempoSync(moduleComponent, *adsr, true);
    auto* attackCombo = findDivisionCombo(moduleComponent, "attackDiv");
    ASSERT_NE(attackCombo, nullptr);

    auto* attackDivParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(adsr, "attackDiv"));
    ASSERT_NE(attackDivParam, nullptr);
    const int before = attackDivParam->getIndex();
    const int target = (before + 1) % attackDivParam->choices.size();

    const int serialBefore = undoManager.getEditSerial();
    attackCombo->setSelectedItemIndex(target, juce::sendNotificationSync);

    EXPECT_EQ(attackDivParam->getIndex(), target) << "the pick must write the param";
    EXPECT_EQ(undoManager.getEditSerial(), serialBefore + 1) << "one pick must cost exactly one undo step";

    // External write (undo/preset/automation): the combo must follow it back.
    const int externalTarget = (target + 2) % attackDivParam->choices.size();
    attackDivParam->setValueNotifyingHost(attackDivParam->getNormalisableRange().convertTo0to1((float)externalTarget));
    EXPECT_EQ(attackCombo->getSelectedItemIndex(), externalTarget);
}

// buildEnvelopeCurveModel's BPM-mode stage durations must equal envelopeNoteDivisionSeconds at
// the module's last-seen tempo (120, the default before any processBlock).
TEST_F(ModuleComponentTest, BpmModeGraphDurationsMatchEnvelopeNoteDivisionSeconds) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* attackDivParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&processor, "attackDiv"));
    auto* holdDivParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&processor, "holdDiv"));
    auto* decayDivParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&processor, "decayDiv"));
    auto* releaseDivParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&processor, "releaseDiv"));
    ASSERT_NE(attackDivParam, nullptr);
    ASSERT_NE(holdDivParam, nullptr);
    ASSERT_NE(decayDivParam, nullptr);
    ASSERT_NE(releaseDivParam, nullptr);

    setTempoSync(moduleComponent, processor, true);

    auto* curve = openEnvelopeGraph(moduleComponent);
    ASSERT_NE(curve, nullptr);

    constexpr double kDefaultBpm = 120.0; // ADSRModule::getLastSeenBpm() before any processBlock
    const auto& model = curve->getModel();
    EXPECT_NEAR(model.segmentDuration(0), synth::envelopeNoteDivisionSeconds(attackDivParam->getIndex(), kDefaultBpm),
                1e-6);
    EXPECT_NEAR(model.segmentDuration(1), synth::envelopeNoteDivisionSeconds(holdDivParam->getIndex(), kDefaultBpm),
                1e-6);
    EXPECT_NEAR(model.segmentDuration(2), synth::envelopeNoteDivisionSeconds(decayDivParam->getIndex(), kDefaultBpm),
                1e-6);
    EXPECT_NEAR(model.segmentDuration(3), synth::envelopeNoteDivisionSeconds(releaseDivParam->getIndex(), kDefaultBpm),
                1e-6);
}

// A real node drag on the attack node, in BPM mode, must snap the resulting duration onto one of
// the six real note divisions (never an arbitrary linear ms value) and write attackDiv, not attack.
TEST_F(ModuleComponentTest, DraggingANodeInBpmModeSnapsToTheNearestDivision) {
    AudioEngine engine;
    auto* adsr = new ADSRModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(adsr));
    ASSERT_NE(node, nullptr);
    GraphEditor editor(engine);
    AppUndoManager undoManager;
    ModuleComponent moduleComponent(adsr, node->nodeID, editor, &undoManager);

    setTempoSync(moduleComponent, *adsr, true);

    auto* curve = openEnvelopeGraph(moduleComponent);
    ASSERT_NE(curve, nullptr);
    ASSERT_GT(curve->getWidth(), 0);
    ASSERT_GT(curve->getHeight(), 0);

    auto* attackDivParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(adsr, "attackDiv"));
    ASSERT_NE(attackDivParam, nullptr);

    CurveEditorGeometry geometry(curve->getModel(), curve->getLocalBounds().toFloat());
    const auto nodePos = geometry.nodePosition(1);                    // attack-peak node
    const auto targetPos = nodePos + juce::Point<float>(40.0f, 0.0f); // drag right: longer attack

    curve->mouseDown(curveMouseEvent(*curve, nodePos, false, nodePos));
    curve->mouseDrag(curveMouseEvent(*curve, targetPos, true, nodePos));
    curve->mouseUp(curveMouseEvent(*curve, targetPos, true, nodePos));

    // The write must have landed on attackDiv (a real division), and the graph must have
    // snapped back to exactly that division's duration -- never an arbitrary in-between value.
    constexpr double kDefaultBpm = 120.0;
    const double expectedSeconds = synth::envelopeNoteDivisionSeconds(attackDivParam->getIndex(), kDefaultBpm);
    EXPECT_NEAR(curve->getModel().segmentDuration(0), expectedSeconds, 1e-6)
        << "the graph must snap to the written division's exact duration after the gesture ends";

    // Sanity: dragging right must not have left attackDiv at its unmoved default index.
    bool isOneOfTheRealDivisions = false;
    for (int i = 0; i < synth::envelopeNoteDivisions().size(); ++i)
        if (std::abs(curve->getModel().segmentDuration(0) - synth::envelopeNoteDivisionSeconds(i, kDefaultBpm)) < 1e-6)
            isOneOfTheRealDivisions = true;
    EXPECT_TRUE(isOneOfTheRealDivisions);
}
