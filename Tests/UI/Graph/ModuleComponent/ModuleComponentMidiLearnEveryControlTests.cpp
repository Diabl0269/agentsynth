// Right-click MIDI Learn on every control, walked from the widgets. Every default card is built and every knob or
// fader under it, at any depth (a card body, an instrument's own section, a nested panel), must be in the card's
// learnable registry under the continuous parameter it shows. The guard for the bug kind "a mechanism on one widget,
// missing on its siblings": a control built outside the card body (the Drum Kit's pad knobs) cannot slip past it.

#include "ModuleComponentTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <set>
#include <typeinfo>

namespace {

void collectSliders(juce::Component& parent, std::vector<juce::Slider*>& out) {
    for (auto* child : parent.getChildren()) {
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            out.push_back(slider);
        collectSliders(*child, out);
    }
}

juce::String describe(const juce::Slider& slider) {
    return slider.getComponentID() + " / title \"" + slider.getTitle() + "\" / " + typeid(slider).name();
}

} // namespace

TEST_F(ModuleComponentTest, EveryKnobAndFaderOnEveryDefaultCardOffersMidiLearn) {
    std::vector<juce::String> gaps;
    int learnable = 0;
    for (const auto& typeName : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto processor = synth::AIStateMapper::createModule(typeName);
        auto* mb = dynamic_cast<ModuleBase*>(processor.get());
        if (mb == nullptr)
            continue;
        AudioEngine engine;
        GraphEditor editor(engine);
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

        std::vector<juce::Slider*> sliders;
        collectSliders(card, sliders);
        for (auto* slider : sliders) {
            auto* param = card.findMidiLearnableParamForTest(slider);
            if (param != nullptr) {
                ++learnable;
                continue;
            }
            gaps.push_back(typeName + " / " + describe(*slider));
        }
    }
    EXPECT_GT(learnable, 100) << "the sweep must reach the cards' controls";
    EXPECT_TRUE(gaps.empty()) << "knobs and faders with no MIDI Learn:\n"
                              << juce::StringArray(gaps.data(), (int)gaps.size()).joinIntoString("\n");
}

// Regression test for FRO1322: the Drum Kit's pad knobs were the one set of knobs on a card with no MIDI Learn.
TEST_F(ModuleComponentTest, TheDrumKitsPadKnobsAreLearnableUnderThePickedPadsParameters) {
    auto processor = synth::AIStateMapper::createModule("Drum Kit");
    ASSERT_NE(processor, nullptr);
    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
    std::vector<juce::Slider*> sliders;
    collectSliders(card, sliders);
    std::set<juce::String> padParams;
    for (auto* slider : sliders)
        if (auto* param = card.findMidiLearnableParamForTest(slider);
            param != nullptr && param->paramID.startsWith("pad"))
            padParams.insert(param->paramID);
    EXPECT_EQ(padParams, (std::set<juce::String>{"pad1Level", "pad1Tune", "pad1Decay", "pad1Start"}));
}
