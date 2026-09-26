// FRO314 registry-wide sweep: every rotary knob on every module card must accept a dropped
// modulation cable. The bug this guards against: OscillatorModule's Level/Waveform/.../Pan
// targets had no paramId (falling back to fragile jack-label/knob-name string matching) and its
// Unison/Detune knobs had no CV jack -- and no test iterated the WHOLE module factory, so a new
// module (or a new knob on an old one) could ship the same gap silently. This test builds a real
// ModuleComponent card for every module the factory can create and checks, per
// docs/modules/modulation.md#every-continuous-parameter-is-a-target:
//   (a) every declared ModulationTarget resolves to a bound knob (sliderIndexForModTarget >= 0);
//   (b) every visible rotary knob bound to a Float/Int parameter has a ModulationTarget pointing
//       at it (the inverse direction -- a knob with no jack at all).
//
// Legitimate exclusions are the explicit, commented kSkippedModuleTypes below -- never silent.

#include "ModuleComponentTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "Modules/ThresholdMeterSource.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <set>
#include <vector>

namespace {

// Module factory type names this sweep does not apply to, each with the reason it is not a gap.
const std::set<juce::String> kSkippedModuleTypes = {
    // A/B are signal inputs, not parameter-CV mod targets -- MathModule.h's own comment on
    // getModulationTargets documents the convention this test would otherwise contradict.
    "Math",
    // Paged tab card: a knob on an inactive Wavetable tab page is legitimately invisible
    // (getModRingSliderIndex() == -1 by design -- docs/modules/modulation.md#modulation-rings-on-knobs),
    // so a single-page sweep here would false-positive on every tab but whichever is default.
    // Covered instead by the dedicated per-tab suites (Tests/UI/.../ModuleComponentWavetableTests.cpp,
    // Tests/Modules/WavetableOscillatorModule/*).
    "Wavetable",
    // Port widgets / graph plumbing with no ModuleComponent knobs at all.
    "Macros",
    "Macro In",
    "Macro Out",
    "Macro Midi In",
    "Macro Midi Out",
    "Audio Input",
    "Audio Output",
    "Midi Input",
    "MIDI Keyboard",
    "Poly MIDI",
    "External MIDI",
    "Track In",
    "Track Audio",
    "Rec Tap",
    // Third-party plugin parameters live on the inner AudioPluginInstance and are automated via
    // dedicated "Add lane..." UI, never a ModuleComponent knob -- docs/modules/modulation.md's
    // "Hosted plugin parameters as automation lanes" section.
    "Hosted Plugin",
    // Per-step pattern data, not a continuous mod target.
    "Sequencer",
    "Poly Sequencer",
    // Mixer-panel gain stages (Master/bus/channel gain, VoiceMixer per-voice level). Adding CV
    // jacks to these is out of scope for this sweep: ChannelStripModule's channel layout is
    // frozen-once-set with fixed send legs on raw ch8-15 (Source/Modules/CLAUDE.md), so a CV jack
    // there needs its own careful channel-map change, not a drive-by fix here. Tracked as a
    // follow-up rather than folded into FRO314 silently -- see the task's final report.
    "Master",
    "Voice Mixer",
    "Channel Strip",
};

bool isContinuousParam(const juce::RangedAudioParameter* param) {
    return dynamic_cast<const juce::AudioParameterFloat*>(param) != nullptr ||
           dynamic_cast<const juce::AudioParameterInt*>(param) != nullptr;
}

// The module's own "outputLevel" parameter, when it has opted into the shared stage
// (ModuleBase::addOutputLevelParameter). docs/modules/modulation.md is explicit that this one
// never gets a CV jack ("nor does the shared output Level stage") -- see
// docs/modules/fx-modules.md#output-level-shared-stage for why: a level control almost every
// audio module carries by composition, not a hand-authored per-module knob.
bool isSharedOutputLevelParam(const ModuleBase& mb, const juce::RangedAudioParameter& param) {
    for (auto* p : mb.getParameters())
        if (auto* ranged = dynamic_cast<const juce::RangedAudioParameter*>(p))
            if (ranged == &param)
                return ranged->paramID == "outputLevel";
    return false;
}

// Every visible rotary knob's componentID, when it is bound to a Float/Int parameter that ISN'T
// the shared output Level stage. Choice/Bool knobs (waveform pickers, on/off toggles) are excluded
// by TYPE, not by an allow-list guess. FRO312 closed the last known gap here (Sampler "rootNote"),
// so there is no per-module deferred-gap escape hatch left to check against.
std::set<juce::String> continuousKnobParamNames(const ModuleBase& mb, ModuleComponent& card) {
    std::set<juce::String> names;
    for (auto* child : card.getChildren()) {
        auto* slider = dynamic_cast<juce::Slider*>(child);
        if (slider == nullptr || slider->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag)
            continue;
        const auto id = slider->getComponentID();
        for (auto* p : mb.getParameters()) {
            auto* ranged = dynamic_cast<const juce::RangedAudioParameter*>(p);
            if (ranged != nullptr && ranged->getName(100) == id && isContinuousParam(ranged) &&
                !isSharedOutputLevelParam(mb, *ranged)) {
                names.insert(id);
                break;
            }
        }
    }
    return names;
}

// Sets every "Poly" bool parameter on the module (Oscillator/ADSR/Filter/Noise all name theirs
// this way) so the sweep also covers the poly channel-set variant of getModulationTargets().
void setPolyIfPresent(ModuleBase& mb, bool poly) {
    for (auto* p : mb.getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(p))
            if (boolParam->name == "Poly")
                *boolParam = poly;
}

// A target check (a) must not require a bound knob for -- not a gap, just nothing a rotary Slider
// could ever represent:
//   - The target names NO parameter at all (`parameterForModTarget` returns null) -- a bare CV
//     jack such as Oscillator's Pitch, which is consumed directly in processBlock and was never
//     going to grow a knob (ModulationTarget's own doc comment: "Left empty for a jack with no
//     knob"). Nothing to drop ON in the first place, so there is no ring to fail to find.
//   - The bound parameter is Choice or Bool (e.g. Oscillator's Waveform) -- rendered as a combo
//     box / toggle by createControls()'s generic loop, never a rotary Slider, so
//     sliderIndexForModTarget can never resolve one. Per docs/modules/modulation.md, only
//     CONTINUOUS (Float/Int) parameters are covered by "every continuous parameter is a target";
//     Waveform predates that rule and is an accepted legacy exception, not something this sweep
//     re-litigates.
//   - The bound parameter is a `ThresholdMeterSource`'s own threshold id (ADSR "gateThreshold",
//     Comparator/Sample & Hold "trigThreshold") EXCEPT Sample & Hold's, which alone keeps its
//     generic slider -- ModuleComponent.cpp's shouldSkipGenericFloatSlider skips building one for
//     the rest, because the value lives inside ThresholdControlComponent instead; that widget's
//     own drop anchor is what getModTargetPortForPoint special-cases already.
//   - ADSR's three Curve amounts (FRO112, attackCurve/decayCurve/releaseCurve): edited only via
//     the envelope graph's bend handles. The CV jack itself is real -- this ticket added it and
//     Tests/Modules/ADSR/ADSRCVTests.cpp proves the parameter reads it -- but a bend-handle drop
//     anchor (matching Threshold's) lives in ModuleComponentPaint.cpp, out of scope here since
//     Source/UI/Graph/** is another session's concurrent edit in this worktree (see this task's
//     final report).
bool targetHasNoGenericKnobByDesign(const ModuleBase& mb, const ModulationTarget& target) {
    const auto* param = mb.parameterForModTarget(target);
    if (param == nullptr)
        return true;
    if (!isContinuousParam(param))
        return true;
    if (auto* threshold = dynamic_cast<const ThresholdMeterSource*>(&mb))
        if (mb.getModuleType() != ModuleType::SampleHold && param->paramID == threshold->getThresholdParamID())
            return true;
    return param->paramID == "attackCurve" || param->paramID == "decayCurve" || param->paramID == "releaseCurve";
}

bool hasPolyParameter(const ModuleBase& mb) {
    for (auto* p : mb.getParameters())
        if (auto* boolParam = dynamic_cast<const juce::AudioParameterBool*>(p))
            if (boolParam->name == "Poly")
                return true;
    return false;
}

} // namespace

TEST_F(ModuleComponentTest, EveryModuleTypeKnobAcceptsADroppedModulationCable) {
    for (const auto& typeName : synth::AIStateMapper::moduleFactoryTypeNames()) {
        if (kSkippedModuleTypes.count(typeName) > 0)
            continue;

        // One processor, reused across every voice mode this type has: safe now that
        // ModuleComponent::detachFromProcessor() correctly removes its own parameter listener
        // even for a card built directly on a processor that was never added to a graph, which is
        // exactly this test's construction below (FRO312 fixed the dangling-listener
        // use-after-free that used to make that unsafe -- see ModuleComponentLifecycleTests.cpp).
        auto processor = synth::AIStateMapper::createModule(typeName);
        auto* mb = dynamic_cast<ModuleBase*>(processor.get());
        if (mb == nullptr)
            continue; // A graph-IO node (e.g. the raw MIDI input processor), not a module card.

        std::vector<bool> voiceModes = {false};
        if (hasPolyParameter(*mb))
            voiceModes.push_back(true);

        AudioEngine engine;
        GraphEditor editor(engine);

        for (const bool poly : voiceModes) {
            SCOPED_TRACE((typeName + (poly ? " (poly)" : " (mono)")).toStdString());
            setPolyIfPresent(*mb, poly);

            ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

            // (a) every declared target must resolve to a bound, visible knob -- except the
            // documented no-generic-knob-by-design exceptions above.
            for (const auto& target : mb->getModulationTargets()) {
                if (targetHasNoGenericKnobByDesign(*mb, target))
                    continue;
                SCOPED_TRACE(target.name.toStdString());
                EXPECT_GE(card.sliderIndexForModTarget(target), 0)
                    << "jack \"" << target.name << "\" (channel " << target.channelIndex
                    << ") has no bound knob -- a dropped cable would find nothing to ring";
            }

            // (b) every continuous-parameter knob must be reachable from SOME target.
            std::set<juce::String> targetedParamNames;
            for (const auto& target : mb->getModulationTargets())
                if (const auto* param = mb->parameterForModTarget(target))
                    targetedParamNames.insert(param->getName(100));

            for (const auto& knobParamName : continuousKnobParamNames(*mb, card)) {
                EXPECT_TRUE(targetedParamNames.count(knobParamName) > 0)
                    << "knob \"" << knobParamName << "\" has a continuous parameter but no CV jack "
                    << "targets it -- it refuses a dropped modulation cable";
            }
        }
    }
}
