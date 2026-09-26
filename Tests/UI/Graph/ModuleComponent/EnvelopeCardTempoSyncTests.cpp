// EnvelopeCardTempoSyncTests.cpp (FRO118): the BPM-mode note-division pickers on the ADSR
// envelope card -- attackDiv/holdDiv/decayDiv/releaseDiv swapping in over their matching knob's
// own grid cell, the two-way param binding + undo bracketing that swap reuses from the generic
// per-param combo idiom, the BPM-mode curve model's stage durations, and x-drag snapping. Sits
// beside ModuleComponentEnvelopeCardTests.cpp (FRO112/FRO113/FRO117), which that file's own
// AdsrTempoSyncAndDivParamsAreExcludedFromTheGenericGrid test pins to zero ComboBox children for
// a fresh MS-default card -- these pickers are therefore built LAZILY (see
// ensureEnvelopeDivCombosCreated in ModuleComponentEnvelopeCard.cpp), only once a card actually
// enters BPM mode, so that test is unaffected by this one.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/ADSRModule.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"
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

juce::TextButton* findTextButtonByText(ModuleComponent& comp, const juce::String& text) {
    for (auto* child : comp.getChildren())
        if (auto* btn = dynamic_cast<juce::TextButton*>(child))
            if (btn->getButtonText() == text)
                return btn;
    return nullptr;
}

juce::Slider* findSliderByCaption(ModuleComponent& comp, const juce::String& caption) {
    for (auto* child : comp.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            if (slider->getComponentID() == caption)
                return slider;
    return nullptr;
}

// The div combo for a given knob caption is identified by bounds, not content -- every combo
// shares the same six-item list (synth::envelopeNoteDivisions()), so bounds (kept in lockstep
// with the matching slider by applyEnvelopeDivComboBounds) is the one distinguishing signal a
// test outside the class can read.
juce::ComboBox* findComboAtBounds(ModuleComponent& comp, juce::Rectangle<int> bounds) {
    for (auto* child : comp.getChildren())
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
            if (combo->getBounds() == bounds)
                return combo;
    return nullptr;
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

// Real mouseDown+mouseUp on a button's centre -- Button::mouseDown forces buttonState to "over
// and down" itself (Button::updateState(true, true)), and Button::mouseUp captures wasDown/wasOver
// from THAT forced state before doing any real hit-test, so a synthesized click fires the same
// internalClickCallback a real click would without needing a real Desktop mouse peer. Per
// docs/development/test-patterns.md's "test the real mouse path": this drives the actual
// mouseDown/mouseUp entry points rather than calling setToggleState/triggerClick as a stand-in.
juce::MouseEvent buttonMouseEvent(juce::Component& comp, juce::Point<float> pos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

void clickButton(juce::Button& button) {
    // Button re-declares mouseDown/mouseUp as PROTECTED overrides of Component's public virtuals
    // -- access control is resolved against the expression's static type, so calling through a
    // juce::Component& reference (still dispatching virtually to Button's override) is how
    // external code reaches them at all, real mouse peer or not.
    auto& asComponent = static_cast<juce::Component&>(button);
    const auto centre = button.getLocalBounds().getCentre().toFloat();
    asComponent.mouseDown(buttonMouseEvent(asComponent, centre));
    asComponent.mouseUp(buttonMouseEvent(asComponent, centre));
}

juce::MouseEvent curveMouseEvent(juce::Component& comp, juce::Point<float> pos, bool dragged,
                                 juce::Point<float> downPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1,
                            dragged);
}

} // namespace

// A real click on BPM must hide the four time knobs (SUS stays a knob) and show a combo in each
// one's exact grid cell; a real click on MS must restore the knobs and hide the combos again.
TEST_F(ModuleComponentTest, BpmClickSwapsKnobsForCombosInTheSameBoundsAndMsRestoresThem) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* attackSlider = findSliderByCaption(moduleComponent, "Attack");
    auto* holdSlider = findSliderByCaption(moduleComponent, "Hold");
    auto* decaySlider = findSliderByCaption(moduleComponent, "Decay");
    auto* releaseSlider = findSliderByCaption(moduleComponent, "Release");
    auto* sustainSlider = findSliderByCaption(moduleComponent, "Sustain");
    ASSERT_NE(attackSlider, nullptr);
    ASSERT_NE(holdSlider, nullptr);
    ASSERT_NE(decaySlider, nullptr);
    ASSERT_NE(releaseSlider, nullptr);
    ASSERT_NE(sustainSlider, nullptr);

    auto* bpmButton = findTextButtonByText(moduleComponent, "BPM");
    auto* msButton = findTextButtonByText(moduleComponent, "MS");
    ASSERT_NE(bpmButton, nullptr);
    ASSERT_NE(msButton, nullptr);

    clickButton(*bpmButton);

    EXPECT_TRUE(bpmButton->getToggleState()) << "the real click must have registered";
    EXPECT_FALSE(attackSlider->isVisible());
    EXPECT_FALSE(holdSlider->isVisible());
    EXPECT_FALSE(decaySlider->isVisible());
    EXPECT_FALSE(releaseSlider->isVisible());
    EXPECT_TRUE(sustainSlider->isVisible()) << "SUS stays a knob in BPM mode";

    // Read the sliders' bounds only NOW, after the mode swap has fully re-laid the card: a hidden
    // knob is no longer knob-bound for its own modulation-target CV jack (getModRingSliderIndex's
    // documented isVisible() rule, shared with the Wavetable tab strip), so the jack falls back to
    // an ordinary gutter row and the whole body shifts down -- the combo must land in the SAME
    // (now-current) cell the knob is in, not a pixel position snapshotted before that shift.
    auto* attackCombo = findComboAtBounds(moduleComponent, attackSlider->getBounds());
    auto* holdCombo = findComboAtBounds(moduleComponent, holdSlider->getBounds());
    auto* decayCombo = findComboAtBounds(moduleComponent, decaySlider->getBounds());
    auto* releaseCombo = findComboAtBounds(moduleComponent, releaseSlider->getBounds());
    ASSERT_NE(attackCombo, nullptr) << "a combo must occupy the attack knob's exact cell";
    ASSERT_NE(holdCombo, nullptr);
    ASSERT_NE(decayCombo, nullptr);
    ASSERT_NE(releaseCombo, nullptr);
    EXPECT_TRUE(attackCombo->isVisible());
    EXPECT_TRUE(holdCombo->isVisible());
    EXPECT_TRUE(decayCombo->isVisible());
    EXPECT_TRUE(releaseCombo->isVisible());

    clickButton(*msButton);

    EXPECT_TRUE(msButton->getToggleState());
    EXPECT_TRUE(attackSlider->isVisible());
    EXPECT_TRUE(holdSlider->isVisible());
    EXPECT_TRUE(decaySlider->isVisible());
    EXPECT_TRUE(releaseSlider->isVisible());
    EXPECT_FALSE(attackCombo->isVisible()) << "MS must hide the combos again";
    EXPECT_FALSE(holdCombo->isVisible());
    EXPECT_FALSE(decayCombo->isVisible());
    EXPECT_FALSE(releaseCombo->isVisible());
}

// A tempoSync write via the param itself (what a preset load / undo restore / automation lane
// actually does) must swap the UI exactly like the toggle click does.
TEST_F(ModuleComponentTest, ExternalTempoSyncParamWriteSwapsTheUI) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* attackSlider = findSliderByCaption(moduleComponent, "Attack");
    ASSERT_NE(attackSlider, nullptr);

    auto* tempoSyncParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&processor, "tempoSync"));
    ASSERT_NE(tempoSyncParam, nullptr);

    tempoSyncParam->setValueNotifyingHost(1.0f);

    EXPECT_FALSE(attackSlider->isVisible());
    auto* attackCombo = findComboAtBounds(moduleComponent, attackSlider->getBounds());
    ASSERT_NE(attackCombo, nullptr);
    EXPECT_TRUE(attackCombo->isVisible());

    tempoSyncParam->setValueNotifyingHost(0.0f);
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

    auto* tempoSyncParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(adsr, "tempoSync"));
    ASSERT_NE(tempoSyncParam, nullptr);
    tempoSyncParam->setValueNotifyingHost(1.0f);

    auto* attackSlider = findSliderByCaption(moduleComponent, "Attack");
    ASSERT_NE(attackSlider, nullptr);
    auto* attackCombo = findComboAtBounds(moduleComponent, attackSlider->getBounds());
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

    auto* tempoSyncParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&processor, "tempoSync"));
    ASSERT_NE(tempoSyncParam, nullptr);
    tempoSyncParam->setValueNotifyingHost(1.0f);

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

    auto* tempoSyncParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(adsr, "tempoSync"));
    ASSERT_NE(tempoSyncParam, nullptr);
    tempoSyncParam->setValueNotifyingHost(1.0f);

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
