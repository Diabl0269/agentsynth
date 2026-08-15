// Standing rule (issue #122): every module whose OUTPUT carries audio must expose a level
// control — either the shared output-level stage (ModuleBase::addOutputLevelParameter) or its
// own long-standing level/gain parameter.
//
// This file is the enforcement. It is deliberately a hand-maintained table cross-checked
// against the live module factory: adding a new module to the factory without classifying it
// here fails EveryFactoryModuleIsClassified, which is the point. The failure message tells you
// which of the three buckets to put it in.
//
// See docs/fx_modules.md § Output Level and docs/Module_Development_Guide.md § 2.

#include "AI/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include <vector>

namespace {

enum class LevelPolicy {
    SharedStage,    // adopted ModuleBase::addOutputLevelParameter() — paramID "outputLevel"
    OwnParameter,   // predates the shared stage and keeps its own level/gain parameter
    NoLevelByDesign // output is CV / gate / MIDI, or the module is not a ModuleBase at all
};

struct ModuleLevelPolicy {
    const char* moduleName;
    LevelPolicy policy;
    const char* ownParamID; // only meaningful for OwnParameter
    const char* rationale;  // only meaningful for NoLevelByDesign
};

// Every module the factory can build. Keep alphabetical-ish within each bucket.
const std::vector<ModuleLevelPolicy>& levelPolicies() {
    static const std::vector<ModuleLevelPolicy> policies = {
        // --- Shared output-level stage (issue #122) ---
        {"Distortion", LevelPolicy::SharedStage, nullptr, nullptr},
        {"Delay", LevelPolicy::SharedStage, nullptr, nullptr},
        {"Reverb", LevelPolicy::SharedStage, nullptr, nullptr},
        {"Chorus", LevelPolicy::SharedStage, nullptr, nullptr},
        {"Phaser", LevelPolicy::SharedStage, nullptr, nullptr},
        {"Flanger", LevelPolicy::SharedStage, nullptr, nullptr},
        {"Filter", LevelPolicy::SharedStage, nullptr, nullptr},
        {"Bitcrusher", LevelPolicy::SharedStage, nullptr, nullptr},
        {"Pitch Shifter", LevelPolicy::SharedStage, nullptr, nullptr},

        // --- Own level/gain parameter, predating the shared stage ---
        // These must NOT also adopt the shared stage: two knobs doing the same job on one panel.
        {"Oscillator", LevelPolicy::OwnParameter, "level", nullptr},
        {"Wavetable", LevelPolicy::OwnParameter, "level", nullptr},
        {"Noise", LevelPolicy::OwnParameter, "level", nullptr},
        {"Sampler", LevelPolicy::OwnParameter, "level", nullptr},
        {"Voice Mixer", LevelPolicy::OwnParameter, "level", nullptr},
        {"VCA", LevelPolicy::OwnParameter, "gain", nullptr},
        {"Parametric EQ", LevelPolicy::OwnParameter, "outputGain", nullptr},
        {"Compressor", LevelPolicy::OwnParameter, "makeupGain", nullptr},
        {"Limiter", LevelPolicy::OwnParameter, "inputGain", nullptr},

        // --- No audio-output level by design ---
        {"LFO", LevelPolicy::NoLevelByDesign, nullptr,
         "CV output. Its own 'level' sets modulation depth; the audio stage would be meaningless here."},
        {"ADSR", LevelPolicy::NoLevelByDesign, nullptr, "Envelope CV output."},
        {"Amp Env", LevelPolicy::NoLevelByDesign, nullptr, "Envelope CV output."},
        {"Filter Env", LevelPolicy::NoLevelByDesign, nullptr, "Envelope CV output."},
        {"Envelope Follower", LevelPolicy::NoLevelByDesign, nullptr,
         "Audio in, unipolar [0,1] CV out — scaling the envelope is the follower's Sensitivity, not a level."},
        {"Sample & Hold", LevelPolicy::NoLevelByDesign, nullptr, "CV output; already has its own CV 'level'."},
        {"Macros", LevelPolicy::NoLevelByDesign, nullptr, "Bank of assignable CV knobs."},
        {"Math", LevelPolicy::NoLevelByDesign, nullptr,
         "Signal-agnostic CV/audio utility — a level would silently rescale CV used for pitch or gates."},
        {"Sequencer", LevelPolicy::NoLevelByDesign, nullptr, "Pitch/gate CV and MIDI output."},
        {"Poly Sequencer", LevelPolicy::NoLevelByDesign, nullptr, "Pitch/gate CV and MIDI output."},
        {"MIDI Keyboard", LevelPolicy::NoLevelByDesign, nullptr, "MIDI output."},
        {"Poly MIDI", LevelPolicy::NoLevelByDesign, nullptr, "Pitch/gate CV output — scaling pitch CV detunes."},
        {"External MIDI", LevelPolicy::NoLevelByDesign, nullptr, "MIDI output."},
        {"Audio Input", LevelPolicy::NoLevelByDesign, nullptr,
         "A tap on the device's input (TL6-2) — trimming what the interface delivers belongs on the "
         "interface, and a level here would silently rescale a signal the user is monitoring."},
        {"Audio Output", LevelPolicy::NoLevelByDesign, nullptr, "Graph I/O node, not a ModuleBase."},
        {"Midi Input", LevelPolicy::NoLevelByDesign, nullptr, "Graph I/O node, not a ModuleBase."},
    };
    return policies;
}

// The module names the factory advertises, harvested from the AI schema, which emits one
// "#### <name>" heading per factory entry. Attenuverter / Mod Slot are excluded there by design.
std::set<juce::String> factoryModuleNames() {
    std::set<juce::String> names;
    const juce::String schema = synth::AIStateMapper::getModuleSchema();
    juce::StringArray lines;
    lines.addLines(schema);
    for (const auto& line : lines)
        if (line.startsWith("#### "))
            names.insert(line.substring(5).trim());
    return names;
}

} // namespace

TEST(ModuleAdoptionTests, EveryAudioOutputModuleHasALevelControl) {
    for (const auto& entry : levelPolicies()) {
        auto processor = synth::AIStateMapper::createModule(entry.moduleName);
        ASSERT_NE(processor, nullptr) << entry.moduleName << " could not be constructed";

        auto* module = dynamic_cast<ModuleBase*>(processor.get());

        switch (entry.policy) {
        case LevelPolicy::SharedStage:
            ASSERT_NE(module, nullptr) << entry.moduleName << " is not a ModuleBase";
            EXPECT_TRUE(module->hasOutputLevel())
                << entry.moduleName
                << " outputs audio but has no level control. Add addOutputLevelParameter() as the last "
                   "addParameter() call, prepareOutputLevel(sampleRate) in prepareToPlay, and "
                   "applyOutputLevel(buffer, numAudioChannels) at the end of the normal processBlock path.";
            break;

        case LevelPolicy::OwnParameter:
            ASSERT_NE(module, nullptr) << entry.moduleName << " is not a ModuleBase";
            EXPECT_NE(findParameterByID(processor.get(), entry.ownParamID), nullptr)
                << entry.moduleName << " lost its '" << entry.ownParamID
                << "' parameter. Either restore it or move the module to the shared output-level stage.";
            EXPECT_FALSE(module->hasOutputLevel())
                << entry.moduleName << " already has '" << entry.ownParamID
                << "' — adopting the shared stage too would put two level knobs on one panel.";
            break;

        case LevelPolicy::NoLevelByDesign:
            if (module != nullptr)
                EXPECT_FALSE(module->hasOutputLevel())
                    << entry.moduleName << " adopted the audio output-level stage, but: " << entry.rationale;
            break;
        }
    }
}

TEST(ModuleAdoptionTests, EveryFactoryModuleIsClassified) {
    // The tripwire. A new module in the factory that nobody classified lands here.
    std::set<juce::String> classified;
    for (const auto& entry : levelPolicies())
        classified.insert(juce::String(entry.moduleName));

    for (const auto& name : factoryModuleNames())
        EXPECT_TRUE(classified.count(name) > 0)
            << "Module '" << name
            << "' is in the module factory but is not classified in Tests/ModuleAdoptionTests.cpp. "
               "If its output carries audio it MUST have a level control — add it to the SharedStage bucket "
               "(and call addOutputLevelParameter/prepareOutputLevel/applyOutputLevel in the module). If it "
               "outputs CV/gate/MIDI, add it to NoLevelByDesign with a one-line rationale. See issue #122 and "
               "docs/fx_modules.md.";
}

TEST(ModuleAdoptionTests, ClassificationTableHasNoStaleEntries) {
    // The mirror of the tripwire: a renamed or removed module leaves a dead row behind, which
    // would otherwise quietly stop enforcing anything.
    const auto factoryNames = factoryModuleNames();
    for (const auto& entry : levelPolicies())
        EXPECT_TRUE(factoryNames.count(juce::String(entry.moduleName)) > 0)
            << "Classification table lists '" << entry.moduleName
            << "', which the module factory no longer advertises. Remove or rename the row.";
}
