// ModuleComponentEnvelopeCard tests: readout formatting, the graph disclosure toggle (the card body's
// Envelope view, open by default), the Time/Tempo switch, the curve model built from ADSRModule's params, two-way sync
// between the graph and the attack/hold/decay/sustain/release/*Curve parameters (driven through REAL synthesized mouse
// events, not the model's primitives directly -- see CurveEditorComponent's dragFrozenRange_
// doc comment on why a direct dragNodeTo() call exercises different geometry than a real drag),
// undo-gesture bracketing, and the playhead stage/segment mapping.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/ADSRModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
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

CurveEditorComponent* findEnvelopeCurveEditor(ModuleComponent& comp) {
    for (auto* child : comp.getChildren())
        if (auto* curve = dynamic_cast<CurveEditorComponent*>(child))
            return curve;
    return nullptr;
}

juce::MouseEvent curveMouseEvent(juce::Component& comp, juce::Point<float> pos, bool dragged,
                                 juce::Point<float> downPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1,
                            dragged);
}

// Opens the envelope graph the same way a click would -- via onClick() directly, not
// triggerClick(): triggerClick() posts an async command message with no message pump to deliver
// it headless (the same workaround every other headless button test in this suite already uses).
CurveEditorComponent* openEnvelopeGraph(ModuleComponent& comp) {
    auto* toggle = findToggleByText(comp, "Show Envelope Graph");
    if (toggle == nullptr)
        return nullptr;
    toggle->setToggleState(true, juce::sendNotificationSync);
    return findEnvelopeCurveEditor(comp);
}

int countChildrenOfType(ModuleComponent& comp, bool wantToggle) {
    int count = 0;
    for (auto* child : comp.getChildren()) {
        if (wantToggle && dynamic_cast<juce::ToggleButton*>(child) != nullptr)
            ++count;
        else if (!wantToggle && dynamic_cast<juce::ComboBox*>(child) != nullptr)
            ++count;
    }
    return count;
}

} // namespace

TEST_F(ModuleComponentTest, AdsrEnvelopeGraphIsOpenByDefaultAndShrinksTheCardWhenClosed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* curve = findEnvelopeCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    EXPECT_TRUE(curve->isVisible()) << "the designed default layout opens the envelope view";
    EXPECT_EQ(curve, moduleComponent.getCardBody()->findView(synth::CardView::Envelope))
        << "the editor is the card body's view, not a second panel";

    const int openHeight = moduleComponent.getHeight();
    auto* toggle = findToggleByText(moduleComponent, "Show Envelope Graph");
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->getToggleState());
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(curve->getBounds()));

    toggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(curve->isVisible());
    EXPECT_LT(moduleComponent.getHeight(), openHeight) << "closing the graph must shrink the card";

    toggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(curve->isVisible());
    EXPECT_EQ(moduleComponent.getHeight(), openHeight) << "opening the graph must give the height back";
}

TEST_F(ModuleComponentTest, AdsrEnvelopeCurveModelMatchesDefaultParams) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* curve = findEnvelopeCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    const auto& model = curve->getModel();

    ASSERT_EQ(model.getNumNodes(), 5);
    EXPECT_DOUBLE_EQ(model.getNode(0).x, 0.0);
    EXPECT_FLOAT_EQ(model.getNode(0).y, 0.0f);
    EXPECT_NEAR(model.segmentDuration(0), 0.001, 1e-6) << "attack default";
    EXPECT_NEAR(model.segmentDuration(1), 0.0, 1e-6) << "hold default";
    EXPECT_NEAR(model.segmentDuration(2), 1.0, 1e-6) << "decay default";
    EXPECT_NEAR(model.segmentDuration(3), 0.015, 1e-6) << "release default";
    EXPECT_FLOAT_EQ(model.getNode(3).y, 1.0f) << "sustain default";
    EXPECT_FLOAT_EQ(model.getBend(0), -0.3f) << "attackCurve default";
    EXPECT_FLOAT_EQ(model.getBend(2), 0.65f) << "decayCurve default";
    EXPECT_FLOAT_EQ(model.getBend(3), 0.65f) << "releaseCurve default";
    EXPECT_FALSE(model.isBendable(1)) << "the hold plateau (level 1 -> 1) has no curve param behind it";
}

// The three curve params are edited ONLY through the graph's bend handles -- they are not in
// the generic auto-slider UI entirely.
TEST_F(ModuleComponentTest, AdsrCurveParamsAreNotExposedAsSliders) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            for (const char* curveId : {"Attack Curve", "Decay Curve", "Release Curve"})
                EXPECT_NE(slider->getComponentID(), juce::String(curveId));
}

// A real node drag (mouseDown + mouseDrag + mouseUp, not a direct dragNodeTo() call) must write
// the corresponding AudioParameterFloat, and the whole gesture must cost exactly one undo entry.
TEST_F(ModuleComponentTest, DraggingTheAttackNodeWritesTheAttackParamAsOneUndoStep) {
    AudioEngine engine;
    GraphEditor editor(engine);
    // captureBeforeState/pushSnapshotFromCapture diff the ENGINE's own graph (AIStateMapper::
    // graphToJSON), so the processor must be a real node in it -- a bare ADSRModule off to the
    // side would leave that snapshot unchanged by the drag and no undo step would ever be pushed.
    auto* adsr = new ADSRModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(adsr));
    ASSERT_NE(node, nullptr);
    auto& processor = *adsr;
    AppUndoManager undoManager;
    ModuleComponent moduleComponent(adsr, node->nodeID, editor, &undoManager);

    auto* curve = openEnvelopeGraph(moduleComponent);
    ASSERT_NE(curve, nullptr);
    ASSERT_GT(curve->getWidth(), 0);
    ASSERT_GT(curve->getHeight(), 0);

    auto* attackParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&processor, "attack"));
    ASSERT_NE(attackParam, nullptr);
    const float before = attackParam->get();

    CurveEditorGeometry geometry(curve->getModel(), curve->getLocalBounds().toFloat());
    const auto nodePos = geometry.nodePosition(1);                    // attack-peak node
    const auto targetPos = nodePos + juce::Point<float>(20.0f, 0.0f); // drag right: longer attack

    const int serialBefore = undoManager.getEditSerial();

    curve->mouseDown(curveMouseEvent(*curve, nodePos, false, nodePos));
    curve->mouseDrag(curveMouseEvent(*curve, targetPos, true, nodePos));
    curve->mouseUp(curveMouseEvent(*curve, targetPos, true, nodePos));

    EXPECT_GT(attackParam->get(), before) << "dragging the attack node right must raise the attack param";
    EXPECT_EQ(undoManager.getEditSerial(), serialBefore + 1) << "one whole drag must cost exactly one undo step";

    // The knob mirrors the graph: SliderParameterAttachment listens on the same parameter.
    juce::Slider* attackSlider = nullptr;
    for (auto* child : moduleComponent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            if (slider->getComponentID() == "Attack")
                attackSlider = slider;
    ASSERT_NE(attackSlider, nullptr);
    EXPECT_NEAR(attackSlider->getValue(), attackParam->get(), 1e-4);
}

// A bend-handle drag must write the matching curve parameter (segment 2 = decay -> decayCurve).
TEST_F(ModuleComponentTest, DraggingTheDecayBendHandleWritesDecayCurve) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    // Default sustain is 1.0, which makes the decay segment (hold-end level 1.0 -> sustain) flat
    // -- bendHandlePosition() deliberately returns nullopt for a flat segment (no visible bend),
    // so a non-default sustain is needed for a decay bend handle to exist at all.
    auto* sustainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&processor, "sustain"));
    ASSERT_NE(sustainParam, nullptr);
    sustainParam->setValueNotifyingHost(sustainParam->convertTo0to1(0.5f));

    auto* curve = openEnvelopeGraph(moduleComponent);
    ASSERT_NE(curve, nullptr);

    auto* decayCurveParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&processor, "decayCurve"));
    ASSERT_NE(decayCurveParam, nullptr);
    const float before = decayCurveParam->get();

    CurveEditorGeometry geometry(curve->getModel(), curve->getLocalBounds().toFloat());
    const auto handlePos = geometry.bendHandlePosition(2); // decay segment
    ASSERT_TRUE(handlePos.has_value());
    const auto targetPos = *handlePos + juce::Point<float>(0.0f, 30.0f);

    curve->mouseDown(curveMouseEvent(*curve, *handlePos, false, *handlePos));
    curve->mouseDrag(curveMouseEvent(*curve, targetPos, true, *handlePos));
    curve->mouseUp(curveMouseEvent(*curve, targetPos, true, *handlePos));

    EXPECT_NE(decayCurveParam->get(), before) << "dragging the decay bend handle must change decayCurve";
}

// Reverse sync: an external parameter write (automation/undo/preset load — simulated here via
// setValueNotifyingHost, which is exactly what those paths do) must update the curve model.
TEST_F(ModuleComponentTest, SettingTheSustainParamExternallyUpdatesTheCurveModel) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* curve = findEnvelopeCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);

    auto* sustainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&processor, "sustain"));
    ASSERT_NE(sustainParam, nullptr);
    sustainParam->setValueNotifyingHost(sustainParam->convertTo0to1(0.25f));

    EXPECT_NEAR(curve->getModel().getNode(3).y, 0.25f, 1e-4) << "the graph must reflect a knob/automation change";
}

TEST_F(ModuleComponentTest, EnvelopePlayheadMapsStageToSegmentOnlyWhenGraphIsOpen) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    processor.prepareToPlay(44100.0, 512);
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* curve = findEnvelopeCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);

    // Trigger a note so the playhead leaves Idle, then run the module a bit so the generator is
    // mid-attack when we sample it.
    juce::AudioBuffer<float> buf(1, 16);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    processor.processBlock(buf, midi);

    // Closed: the timer-driven poll must not touch the playhead (no visible curve to show it on).
    auto* toggle = findToggleByText(moduleComponent, "Show Envelope Graph");
    ASSERT_NE(toggle, nullptr);
    toggle->setToggleState(false, juce::sendNotificationSync);
    ASSERT_FALSE(curve->isVisible());
    moduleComponent.timerCallback();
    EXPECT_FALSE(curve->getPlayhead().has_value()) << "a closed graph must not show a playhead";

    toggle->setToggleState(true, juce::sendNotificationSync);
    ASSERT_TRUE(curve->isVisible());

    moduleComponent.timerCallback();
    ASSERT_TRUE(curve->getPlayhead().has_value()) << "an in-flight note with the graph open must show a playhead";
    EXPECT_EQ(curve->getPlayhead()->segment, 0) << "a freshly triggered note is in the Attack stage (segment 0)";
}

// tempoSync and the four divisions are ordinary items of the layout: the Time/Tempo switch is a segmented
// switch (not a toggle), and each division is a combo that shares its stage's cell, so a fresh card has
// exactly the four division combos and no generic toggle for tempoSync.
TEST_F(ModuleComponentTest, AdsrTempoSyncIsASwitchAndTheFourDivisionsAreCombosInTheStageCells) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_EQ(countChildrenOfType(moduleComponent, /*wantToggle*/ false), 4)
        << "attackDiv/holdDiv/decayDiv/releaseDiv, one per stage";
    auto* body = moduleComponent.getCardBody();
    ASSERT_NE(body, nullptr);
    EXPECT_NE(dynamic_cast<synth::ui::CardSegmentedSwitch*>(body->findWidget("tempoSync")), nullptr);
}

TEST_F(ModuleComponentTest, AdsrTimeTempoSwitchDefaultsToTimeAndWritesAndFollowsTheRealTempoSyncParam) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ADSRModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* tempoSyncParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&processor, "tempoSync"));
    ASSERT_NE(tempoSyncParam, nullptr);
    ASSERT_FALSE(tempoSyncParam->get()) << "tempoSync defaults false: Time";

    auto* sw = dynamic_cast<synth::ui::CardSegmentedSwitch*>(moduleComponent.getCardBody()->findWidget("tempoSync"));
    ASSERT_NE(sw, nullptr);
    ASSERT_EQ(sw->getNumSegments(), 2);
    EXPECT_EQ(sw->getSegment(0)->getButtonText(), "Time");
    EXPECT_EQ(sw->getSegment(1)->getButtonText(), "Tempo");
    EXPECT_EQ(sw->getSelectedIndex(), 0);

    sw->setSelectedIndex(1, juce::sendNotificationSync);
    EXPECT_TRUE(tempoSyncParam->get()) << "picking Tempo must write tempoSync=true";
    sw->setSelectedIndex(0, juce::sendNotificationSync);
    EXPECT_FALSE(tempoSyncParam->get()) << "picking Time must write tempoSync=false";

    // Reverse sync: an external write (automation/undo/preset load) must move the switch.
    tempoSyncParam->setValueNotifyingHost(1.0f);
    EXPECT_EQ(sw->getSelectedIndex(), 1);
}
