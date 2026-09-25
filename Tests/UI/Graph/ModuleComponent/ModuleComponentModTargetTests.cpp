// ModuleComponent modulation-target binding: the knob a ring is drawn on and the knob a dropped
// cable lands on are resolved through the target's BOUND PARAMETER (ModuleBase::parameterForModTarget),
// never by comparing the jack label with the knob's name. A Flanger's Rate jack is labelled "Rate"
// and its knob "Rate (Hz)"; matching those two strings found nothing, so the card drew no ring and
// swallowed no drop on every module whose jack labels carry no unit.

#include "ModuleComponentTestFixture.h"

#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/FlangerModule.h"
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

} // namespace

TEST_F(ModuleComponentTest, UnitLabelledKnobResolvesFromItsJackTarget) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FlangerModule flanger;
    ModuleComponent card(&flanger, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* rateKnob = findKnob(card, "Rate (Hz)");
    ASSERT_NE(rateKnob, nullptr);

    ModulationTarget rate;
    for (const auto& t : flanger.getModulationTargets())
        if (t.name == "Rate")
            rate = t;
    ASSERT_EQ(rate.channelIndex, 2);

    const int si = card.sliderIndexForModTarget(rate);
    ASSERT_GE(si, 0) << "the Rate jack must find the Rate (Hz) knob";
    EXPECT_EQ(card.getModRingSliderIndex("Rate (Hz)"), si);
    EXPECT_EQ(card.getModRingSliderIndex("Rate"), -1) << "there is no knob called plain \"Rate\" - the label alone "
                                                         "was never enough, which is the bug paramId fixes";
}

TEST_F(ModuleComponentTest, DropOnAUnitLabelledKnobReportsItsCVJack) {
    AudioEngine engine;
    GraphEditor editor(engine);
    FlangerModule flanger;
    ModuleComponent card(&flanger, juce::AudioProcessorGraph::NodeID(1), editor);

    struct Expect {
        const char* knob;
        int channel;
    };
    // Every knob of the card, including the three jacks the Flanger gained (Centre Delay,
    // Feedback, Mix), and the two that always existed but never accepted a drop.
    for (const auto& e : {Expect{"Rate (Hz)", 2}, Expect{"Depth", 3}, Expect{"Centre Delay (ms)", 4},
                          Expect{"Feedback", 5}, Expect{"Mix", 6}}) {
        SCOPED_TRACE(e.knob);
        auto* knob = findKnob(card, e.knob);
        ASSERT_NE(knob, nullptr);
        const auto hit = card.getModTargetPortForPoint(knob->getBounds().getCentre());
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->index, e.channel);
        EXPECT_TRUE(hit->isInput);
        EXPECT_FALSE(hit->isMidi);
    }

    // Level is the shared output stage and has no CV jack, so it is not a drop target.
    auto* level = findKnob(card, "Level");
    ASSERT_NE(level, nullptr);
    EXPECT_FALSE(card.getModTargetPortForPoint(level->getBounds().getCentre()).has_value());
}

TEST_F(ModuleComponentTest, CompressorKnobsAreDropTargetsNow) {
    AudioEngine engine;
    GraphEditor editor(engine);
    CompressorModule comp;
    ModuleComponent card(&comp, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* threshold = findKnob(card, "Threshold (dB)");
    ASSERT_NE(threshold, nullptr);
    const auto hit = card.getModTargetPortForPoint(threshold->getBounds().getCentre());
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->index, 2);
}

TEST_F(ModuleComponentTest, VCAGainKnobIsTheCVJacksTarget) {
    AudioEngine engine;
    GraphEditor editor(engine);
    VCAModule vca;
    ModuleComponent card(&vca, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* gain = findKnob(card, "Gain");
    ASSERT_NE(gain, nullptr);
    const auto hit = card.getModTargetPortForPoint(gain->getBounds().getCentre());
    ASSERT_TRUE(hit.has_value()) << "the VCA's CV jack (labelled \"CV\") drives Gain";
    EXPECT_EQ(hit->index, 1);
    EXPECT_GE(card.sliderIndexForModTarget(vca.getModulationTargets()[0]), 0);
}
