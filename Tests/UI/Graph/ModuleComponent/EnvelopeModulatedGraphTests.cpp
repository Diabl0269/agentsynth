// EnvelopeModulatedGraphTests.cpp: the ADSR card's envelope graph draws the stage times and sustain the sound
// plays (knob plus CV, published by the processor once per block), and a graph drag still edits the stored knobs.
// Sits beside EnvelopeCardTempoSyncTests.cpp.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/ADSRModule.h"
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

// The ADSR's parameter-CV jacks (ADSRModule's kAttackChannel .. kSustainChannel).
constexpr int kAttackCv = 9;
constexpr int kDecayCv = 11;
constexpr int kSustainCv = 12;

float storedValue(juce::AudioProcessor& p, const char* id) {
    auto* param = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&p, id));
    return param != nullptr ? param->get() : -1.0f;
}

void setStored(juce::AudioProcessor& p, const char* id, float value) {
    auto* param = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&p, id));
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

// Renders one block with a constant CV on each (channel, value) pair.
void renderWithCv(ADSRModule& adsr, std::initializer_list<std::pair<int, float>> cvs) {
    juce::AudioBuffer<float> buffer(juce::jmax(adsr.getTotalNumInputChannels(), adsr.getTotalNumOutputChannels()), 64);
    buffer.clear();
    for (const auto& [channel, value] : cvs)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), value, buffer.getNumSamples());
    juce::MidiBuffer midi;
    adsr.processBlock(buffer, midi);
}

CurveEditorComponent* openGraph(ModuleComponent& comp) {
    for (auto* child : comp.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            if (toggle->getButtonText() == "Show Envelope Graph")
                toggle->setToggleState(true, juce::sendNotificationSync);
    for (auto* child : comp.getChildren())
        if (auto* curve = dynamic_cast<CurveEditorComponent*>(child))
            return curve;
    return nullptr;
}

juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<float> pos, bool dragged, juce::Point<float> downPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1,
                            dragged);
}

} // namespace

// Regression test for FRO2446: the stage getters returned nothing but the knobs, so a CV on a stage moved the sound
// and left the graph where it was.
TEST(EnvelopeModulatedGetters, PublishKnobPlusCvOncePerBlockAndTheKnobBeforeAnyBlock) {
    ADSRModule adsr;
    setStored(adsr, "attack", 0.1f);
    setStored(adsr, "sustain", 0.5f);
    adsr.prepareToPlay(48000.0, 64);
    EXPECT_FLOAT_EQ(adsr.getModulatedAttack(), 0.1f);
    EXPECT_FLOAT_EQ(adsr.getModulatedSustain(), 0.5f);

    renderWithCv(adsr, {{kAttackCv, 0.2f}, {kSustainCv, -0.2f}});
    EXPECT_GT(adsr.getModulatedAttack(), 0.1f + 0.5f);
    EXPECT_NEAR(adsr.getModulatedSustain(), 0.3f, 1e-4f);
    EXPECT_FLOAT_EQ(storedValue(adsr, "attack"), 0.1f) << "modulation never rewrites the knob";
}

// Regression test for FRO2446: the graph drew the stored attack/sustain, not the modulated ones.
TEST_F(ModuleComponentTest, EnvelopeGraphDrawsTheModulatedStagesAndSustain) {
    AudioEngine engine;
    auto* adsr = new ADSRModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(adsr));
    ASSERT_NE(node, nullptr);
    GraphEditor editor(engine);
    ModuleComponent comp(adsr, node->nodeID, editor);
    adsr->prepareToPlay(48000.0, 64);
    auto* curve = openGraph(comp);
    ASSERT_NE(curve, nullptr);
    EXPECT_NEAR(curve->getModel().segmentDuration(0), storedValue(*adsr, "attack"), 1e-6);

    renderWithCv(*adsr, {{kAttackCv, 0.2f}, {kSustainCv, -0.2f}});
    comp.timerCallback();

    EXPECT_NEAR(curve->getModel().segmentDuration(0), adsr->getModulatedAttack(), 1e-5);
    EXPECT_GT(curve->getModel().segmentDuration(0), storedValue(*adsr, "attack") + 0.5);
    EXPECT_NEAR(curve->getModel().getNode(3).y, adsr->getModulatedSustain(), 1e-5);
    EXPECT_LT(curve->getModel().getNode(3).y, storedValue(*adsr, "sustain"));
}

// Regression test for FRO2446: with the graph showing modulated values, a drag must move only the dragged stage's
// knob by the drag's own amount, keep the handle under the pointer, and leave the other (modulated) knobs alone.
TEST_F(ModuleComponentTest, DraggingAModulatedEnvelopeNodeShiftsOnlyItsKnobAndKeepsTheHandleUnderThePointer) {
    AudioEngine engine;
    auto* adsr = new ADSRModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(adsr));
    ASSERT_NE(node, nullptr);
    GraphEditor editor(engine);
    AppUndoManager undoManager;
    ModuleComponent comp(adsr, node->nodeID, editor, &undoManager);
    adsr->prepareToPlay(48000.0, 64);
    auto* curve = openGraph(comp);
    ASSERT_NE(curve, nullptr);
    ASSERT_GT(curve->getWidth(), 0);

    setStored(*adsr, "attack", 0.1f);
    setStored(*adsr, "hold", 0.1f); // no zero-width segment, so pixels map to time evenly
    setStored(*adsr, "decay", 0.1f);
    renderWithCv(*adsr, {{kAttackCv, 0.1f}, {kDecayCv, 0.2f}});
    comp.timerCallback();
    const double shownAttack = curve->getModel().segmentDuration(0);
    ASSERT_GT(shownAttack, 0.1 + 0.2);

    CurveEditorGeometry geometry(curve->getModel(), curve->getLocalBounds().toFloat());
    const auto from = geometry.nodePosition(1);
    const auto to = from + juce::Point<float>(30.0f, 0.0f);
    curve->mouseDown(mouseAt(*curve, from, false, from));
    curve->mouseDrag(mouseAt(*curve, to, true, from));

    // The editor freezes the visible time range for the span of a drag, so measure against the same range.
    synth::ui::CurveGeometryConfig frozen;
    frozen.explicitVisibleRange = geometry.getVisibleRange();
    CurveEditorGeometry during(curve->getModel(), curve->getLocalBounds().toFloat(), frozen);
    EXPECT_NEAR(during.nodePosition(1).x, to.x, 1.5f) << "the handle follows the pointer";
    const double draggedBy = curve->getModel().segmentDuration(0) - shownAttack;
    ASSERT_GT(draggedBy, 0.0);
    EXPECT_NEAR(storedValue(*adsr, "attack"), 0.1 + draggedBy, 1e-4) << "the knob moves by the drag, not to the sum";
    EXPECT_FLOAT_EQ(storedValue(*adsr, "decay"), 0.1f) << "an undragged modulated stage keeps its knob";

    curve->mouseUp(mouseAt(*curve, to, true, from));
}
