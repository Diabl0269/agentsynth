// Concern: the value text of the Oscillator, LFO and ADSR knobs that carry a percent, a time in
// milliseconds or a phase angle -- on the parameter itself and on the card's knob -- and that typed
// text reads back.
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"
#include "Modules/ADSRModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace {

const juce::String kDegree = juce::String(juce::CharPointer_UTF8("\xc2\xb0"));

template <typename Module>
struct ReadoutCard {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    ModuleComponent* card = nullptr;

    ReadoutCard() {
        undo.setGraphEditor(&editor);
        editor.setSize(1200, 900);
        auto node = engine.getGraph().addNode(std::make_unique<Module>());
        node->properties.set("x", 40);
        node->properties.set("y", 40);
        editor.updateComponents();
        for (auto* c : editor.getModuleComponents())
            if (c != nullptr && c->getNodeId() == node->nodeID)
                card = c;
    }

    juce::RangedAudioParameter* param(const juce::String& id) {
        for (auto* p : card->getModule()->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
                return r;
        return nullptr;
    }

    /** The card knob's readout for the parameter's real value, and the parameter's own text. */
    void expectReadout(const juce::String& id, const juce::String& title, float value, const juce::String& expected) {
        auto* p = param(id);
        ASSERT_NE(p, nullptr) << id;
        EXPECT_EQ(p->getText(p->convertTo0to1(value), 100), expected) << id;
        for (auto* stop : card->getKeyboardControls())
            if (stop->getTitle() == title) {
                auto* knob = dynamic_cast<synth::ui::CardKnobSlider*>(stop);
                ASSERT_NE(knob, nullptr);
                EXPECT_EQ(knob->getTextFromValue(value), expected) << id;
                return;
            }
        ADD_FAILURE() << "no card knob titled " << title;
    }

    float typed(const juce::String& id, const juce::String& text) {
        auto* p = param(id);
        return p->convertFrom0to1(p->getValueForText(text));
    }
};

} // namespace

TEST(ModuleComponentReadout, OscillatorPulseWidthAndGlideReadAsPercentAndTime) {
    ReadoutCard<OscillatorModule> f;
    ASSERT_NE(f.card, nullptr);
    f.expectReadout("pulseWidth", "Pulse Width", 50.0f, "50 %");
    f.expectReadout("pulseWidth", "Pulse Width", 12.4f, "12 %");
    f.expectReadout("glide", "Glide", 0.0f, "0.0 ms");
    f.expectReadout("glide", "Glide", 120.0f, "120 ms");
    f.expectReadout("glide", "Glide", 1500.0f, "1.50 s");
    EXPECT_NEAR(f.typed("pulseWidth", "30 %"), 30.0f, 1.0e-3f);
    EXPECT_NEAR(f.typed("glide", "120 ms"), 120.0f, 1.0e-2f);
    EXPECT_NEAR(f.typed("glide", "1.5 s"), 1500.0f, 1.0e-1f);
    EXPECT_NEAR(f.typed("glide", "300"), 300.0f, 1.0e-1f);
}

TEST(ModuleComponentReadout, LfoPhaseAndFadeInReadAsDegreesAndTime) {
    ReadoutCard<LFOModule> f;
    ASSERT_NE(f.card, nullptr);
    f.expectReadout("phase", "Phase", 90.0f, "90" + kDegree);
    f.expectReadout("phase", "Phase", 0.0f, "0" + kDegree);
    f.expectReadout("fadeIn", "Fade In", 250.0f, "250 ms");
    f.expectReadout("fadeIn", "Fade In", 4000.0f, "4.00 s");
    EXPECT_NEAR(f.typed("phase", "90" + kDegree), 90.0f, 1.0e-2f);
    EXPECT_NEAR(f.typed("phase", "45 deg"), 45.0f, 1.0e-2f);
    EXPECT_NEAR(f.typed("fadeIn", "2 s"), 2000.0f, 1.0e-1f);
}

TEST(ModuleComponentReadout, AdsrVelocityReadsAsPercent) {
    ReadoutCard<ADSRModule> f;
    ASSERT_NE(f.card, nullptr);
    f.expectReadout("velocity", "Velocity", 0.0f, "0 %");
    f.expectReadout("velocity", "Velocity", 75.0f, "75 %");
    EXPECT_NEAR(f.typed("velocity", "60%"), 60.0f, 1.0e-3f);
}

TEST(ModuleComponentReadout, FilterKeyTrackReadsAsPercent) {
    ReadoutCard<FilterModule> f;
    ASSERT_NE(f.card, nullptr);
    f.expectReadout("keyTrack", "Key Track", 0.0f, "0 %");
    f.expectReadout("keyTrack", "Key Track", 100.0f, "100 %");
    EXPECT_NEAR(f.typed("keyTrack", "50%"), 50.0f, 1.0e-3f);
}
