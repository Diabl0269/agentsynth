// Every MIDI-learnable control also takes a cable. Every default card is built and each control in its MIDI Learn
// registry that drives a continuous parameter must have a modulation target, or be listed with its reason in
// ModCableExemptions.h. The guard for the bug kind "a mechanism on one widget, missing on its siblings": a control
// that a person can map to a hardware knob but cannot patch an LFO into.

#include "ModCableExemptions.h"
#include "ModuleComponentTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <set>

TEST_F(ModuleComponentTest, EveryMidiLearnableControlOnEveryDefaultCardAlsoTakesAModCableOrIsAnExemption) {
    std::vector<juce::String> gaps;
    int checked = 0, exempt = 0;
    for (const auto& typeName : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto processor = synth::AIStateMapper::createModule(typeName);
        auto* mb = dynamic_cast<ModuleBase*>(processor.get());
        if (mb == nullptr)
            continue;
        AudioEngine engine;
        GraphEditor editor(engine);
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

        std::set<juce::String> targeted;
        for (const auto& target : mb->getModulationTargets())
            if (const auto* param = mb->parameterForModTarget(target))
                targeted.insert(param->paramID);

        std::set<juce::String> seen;
        for (const auto& entry : card.midiLearnableEntriesForTest()) {
            if (entry.hosted || !seen.insert(entry.paramId).second)
                continue; // a hosted plugin's parameters are automated through lanes, not cables
            const auto* param = findParameterByID(mb, entry.paramId);
            if (dynamic_cast<const juce::AudioParameterFloat*>(param) == nullptr &&
                dynamic_cast<const juce::AudioParameterInt*>(param) == nullptr)
                continue; // a switch or a picker: not a continuous input
            ++checked;
            if (targeted.count(entry.paramId) > 0)
                continue;
            if (modcable_test::exemptionReason(typeName, entry.paramId) != nullptr) {
                ++exempt;
                continue;
            }
            gaps.push_back(typeName + " / " + entry.paramId + " (\"" + param->getName(100) + "\")");
        }
    }
    EXPECT_GT(checked, 100) << "the sweep must reach the cards' learnable controls";
    EXPECT_GT(exempt, 0) << "the Drum Kit's pad knobs are exempt for now";
    EXPECT_TRUE(gaps.empty()) << "MIDI-learnable controls with no modulation target:\n"
                              << juce::StringArray(gaps.data(), (int)gaps.size()).joinIntoString("\n");
}
