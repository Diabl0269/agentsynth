// Click audit: renders the idle-to-active edges of the audio modules headless and fails on a click.
//
// A click is a sample-to-sample step far above what the signal's own slope allows. Two audits:
//
//  * ProcessorEdges: every audio-processing module is fed a steady sine while one edge happens
//    between two blocks: bypass on / off, mute on / off, or one float parameter jumped from its
//    minimum to its maximum (and back). The largest step in the blocks after the edge must stay
//    within a few times the largest step the same output shows once it is steady (before the edge
//    or after it has settled), so a module that gains or loses level keeps a proportional limit.
//  * SourceNoteEdges: a source played through an ADSR-gated VCA with fast staccato notes whose
//    note-ons and note-offs land mid-block. The largest step must stay within the sine's own slope
//    plus the envelope's attack slope.
//
// Every case prints its largest step (an "[audit]" line and a gtest property) so a run reads as a
// table. A genuine click that cannot be fixed small is skipped by name with its reason, never
// hidden by a looser limit. The case lists are written out here, in the style of
// Tests/Timeline/AutomationZipperTests.cpp, whose per-module tone and CV setup they follow.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioRenderingTestHelpers.h"
#include "Modules/NoiseModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include <iostream>
#include <vector>

namespace {

constexpr float kToneHz = 440.0f;
constexpr float kToneAmp = 0.5f;
constexpr int kSettleBlocks = 12;     // start-up transients (filter state, delay fill) before the reference
constexpr int kReferenceBlocks = 4;   // the steady blocks measured before the edge
constexpr int kEdgeBlocks = 40;       // blocks rendered after the edge (about 0.45 s)
constexpr int kTailBlocks = 8;        // last blocks of the run, taken as the settled output
constexpr float kTolerance = 4.0f;    // edge step allowed per unit of steady step
constexpr float kStepFloor = 1.0e-4f; // below this a steady output counts as silent

struct ProcessorCase {
    const char* factoryName;
    int audioInChannels;                                 // leading channels fed the tone
    std::vector<std::pair<int, float>> constantChannels; // gates / steady CV held for the whole run
    const char* skipReason = nullptr;                    // set: the whole module is skipped by name
};

/** The audio-processing modules (the whole FX set plus Filter, VCA, Voice Mixer, Attenuverter,
    Channel Strip and Master) with the same tone and CV setup the zipper suite uses. */
const std::vector<ProcessorCase>& processorCases() {
    static const std::vector<ProcessorCase> cases = {
        {"Filter", 1, {}},        {"VCA", 1, {{1, 1.0f}}},   {"Voice Mixer", 1, {}}, {"Attenuverter", 1, {}},
        {"Channel Strip", 5, {}}, {"Master", 4, {}},         {"Delay", 2, {}},       {"Reverb", 2, {}},
        {"Chorus", 2, {}},        {"Flanger", 2, {}},        {"Phaser", 2, {}},      {"Distortion", 2, {}},
        {"Limiter", 2, {}},       {"Pitch Shifter", 2, {}},  {"Compressor", 2, {}},  {"Bitcrusher", 2, {}},
        {"Parametric EQ", 2, {}}, {"Ring Modulator", 2, {}}, {"Gate", 2, {}},
    };
    return cases;
}

// Audit findings that click today. Each names the module, the edge and why; the case still renders and
// prints its step, is skipped while it clicks, and FAILS once it stops clicking so the entry gets removed.
constexpr const char* kHardMute = "mute clears or restores the block in one step with no gain ramp";
constexpr const char* kHardBypass = "bypass switches between the wet and dry paths in one step with no crossfade";
constexpr const char* kDelayJump = "the delay read position jumps with the parameter; the tap is not crossfaded";
constexpr const char* kCeilingJump =
    "the brickwall ceiling is applied unsmoothed (by design), so a drop clamps in one step";
constexpr const char* kBlockRateGain = "the gain computer follows the parameter at block rate, so the gain steps";

struct KnownClick {
    const char* module;
    const char* edge;
    const char* reason;
};

const std::vector<KnownClick>& knownClicks() {
    static const std::vector<KnownClick> clicks = {
        {"Filter", "bypass_on", kHardBypass},
        {"Filter", "bypass_off", kHardBypass},
        {"Filter", "mute_on", kHardMute},
        {"VCA", "bypass_on", kHardBypass},
        {"VCA", "bypass_off", kHardBypass},
        {"VCA", "mute_on", kHardMute},
        {"VCA", "mute_off", kHardMute},
        {"Channel Strip", "mute_on", kHardMute},
        {"Channel Strip", "mute_off", kHardMute},
        {"Master", "mute_on", kHardMute},
        {"Master", "mute_off", kHardMute},
        {"Delay", "bypass_on", kHardBypass},
        {"Delay", "bypass_off", kHardBypass},
        {"Delay", "mute_on", kHardMute},
        {"Delay", "mute_off", kHardMute},
        {"Delay", "time_min_to_max", kDelayJump},
        {"Delay", "time_max_to_min", kDelayJump},
        {"Reverb", "bypass_on", kHardBypass},
        {"Reverb", "mute_on", kHardMute},
        {"Reverb", "mute_off", kHardMute},
        {"Chorus", "bypass_off", kHardBypass},
        {"Chorus", "mute_on", kHardMute},
        {"Chorus", "mute_off", kHardMute},
        {"Chorus", "centreDelay_min_to_max", kDelayJump},
        {"Chorus", "centreDelay_max_to_min", kDelayJump},
        {"Flanger", "bypass_on", kHardBypass},
        {"Flanger", "bypass_off", kHardBypass},
        {"Flanger", "mute_off", kHardMute},
        {"Flanger", "centreDelay_min_to_max", kDelayJump},
        {"Flanger", "centreDelay_max_to_min", kDelayJump},
        {"Phaser", "bypass_on", kHardBypass},
        {"Phaser", "bypass_off", kHardBypass},
        {"Phaser", "mute_on", kHardMute},
        {"Phaser", "mute_off", kHardMute},
        {"Distortion", "bypass_on", kHardBypass},
        {"Distortion", "bypass_off", kHardBypass},
        {"Distortion", "mute_on", kHardMute},
        {"Distortion", "mute_off", kHardMute},
        {"Limiter", "bypass_off", kHardBypass},
        {"Limiter", "mute_on", kHardMute},
        {"Limiter", "mute_off", kHardMute},
        {"Limiter", "ceiling_max_to_min", kCeilingJump},
        {"Pitch Shifter", "mute_on", kHardMute},
        {"Pitch Shifter", "mute_off", kHardMute},
        {"Compressor", "bypass_on", kHardBypass},
        {"Compressor", "mute_on", kHardMute},
        {"Compressor", "mute_off", kHardMute},
        {"Compressor", "threshold_min_to_max", kBlockRateGain},
        {"Compressor", "threshold_max_to_min", kBlockRateGain},
        {"Compressor", "ratio_min_to_max", kBlockRateGain},
        {"Compressor", "attack_max_to_min", kBlockRateGain},
        {"Bitcrusher", "mute_on", kHardMute},
        {"Bitcrusher", "mute_off", kHardMute},
        {"Parametric EQ", "mute_on", kHardMute},
        {"Parametric EQ", "mute_off", kHardMute},
        {"Ring Modulator", "bypass_on", kHardBypass},
        {"Ring Modulator", "bypass_off", kHardBypass},
        {"Ring Modulator", "mute_on", kHardMute},
        {"Ring Modulator", "mute_off", kHardMute},
        {"Gate", "bypass_off", kHardBypass},
        {"Gate", "mute_on", kHardMute},
    };
    return clicks;
}

const char* knownClickReason(const char* module, const juce::String& edge) {
    for (const auto& k : knownClicks())
        if (juce::String(k.module) == module && edge == k.edge)
            return k.reason;
    return nullptr;
}

enum class EdgeKind { BypassOn, BypassOff, MuteOn, MuteOff, ParamMinToMax, ParamMaxToMin };

struct EdgeCase {
    const ProcessorCase* module;
    EdgeKind kind;
    juce::String paramId; // set for the two parameter edges
};

juce::String edgeName(const EdgeCase& c) {
    switch (c.kind) {
    case EdgeKind::BypassOn:
        return "bypass_on";
    case EdgeKind::BypassOff:
        return "bypass_off";
    case EdgeKind::MuteOn:
        return "mute_on";
    case EdgeKind::MuteOff:
        return "mute_off";
    case EdgeKind::ParamMinToMax:
        return c.paramId + "_min_to_max";
    case EdgeKind::ParamMaxToMin:
        return c.paramId + "_max_to_min";
    }
    return {};
}

std::vector<EdgeCase> makeEdgeCases() {
    std::vector<EdgeCase> out;
    for (const auto& module : processorCases()) {
        auto probe = synth::AIStateMapper::createModule(module.factoryName);
        if (probe == nullptr)
            continue;
        out.push_back({&module, EdgeKind::BypassOn, {}});
        out.push_back({&module, EdgeKind::BypassOff, {}});
        if (findParameterByID(probe.get(), "muted") != nullptr) {
            out.push_back({&module, EdgeKind::MuteOn, {}});
            out.push_back({&module, EdgeKind::MuteOff, {}});
        }
        for (auto* p : probe->getParameters())
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p)) {
                out.push_back({&module, EdgeKind::ParamMinToMax, f->paramID});
                out.push_back({&module, EdgeKind::ParamMaxToMin, f->paramID});
            }
    }
    return out;
}

juce::String sanitise(const juce::String& s) {
    juce::String out;
    for (auto c : s)
        out += (juce::CharacterFunctions::isLetterOrDigit(c) ? juce::String::charToString(c) : juce::String("_"));
    return out;
}

void setBool(juce::AudioProcessor& m, const char* id, bool on) {
    if (auto* p = findParameterByID(&m, id))
        p->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

void setNormalised(juce::AudioProcessor& m, const juce::String& id, float normalised) {
    if (auto* p = findParameterByID(&m, id))
        p->setValueNotifyingHost(normalised);
}

/** Largest |x[n] - x[n-1]| over `block`, continuing from `previous` (updated). */
float stepsIn(const juce::AudioBuffer<float>& block, int ch, float& previous, bool& havePrevious) {
    float worst = 0.0f;
    const float* data = block.getReadPointer(ch);
    for (int i = 0; i < block.getNumSamples(); ++i) {
        if (havePrevious)
            worst = std::max(worst, std::abs(data[i] - previous));
        previous = data[i];
        havePrevious = true;
    }
    return worst;
}

void reportAudit(const juce::String& label, float step, float limit) {
    std::cout << "[audit] " << label << " largest step " << step << " (limit " << limit << ")" << std::endl;
    ::testing::Test::RecordProperty("largest_step", std::to_string(step));
}

} // namespace

class ProcessorEdgeClickAudit : public ::testing::TestWithParam<EdgeCase> {};

TEST_P(ProcessorEdgeClickAudit, EdgeProducesNoClick) {
    const auto& c = GetParam();
    auto processor = synth::AIStateMapper::createModule(c.module->factoryName);
    ASSERT_NE(processor, nullptr);

    // The "from" side of the edge is applied before the module starts, the "to" side between blocks.
    juce::RangedAudioParameter* param =
        c.paramId.isNotEmpty() ? findParameterByID(processor.get(), c.paramId) : nullptr;
    switch (c.kind) {
    case EdgeKind::BypassOff:
        setBool(*processor, "bypassed", true);
        break;
    case EdgeKind::MuteOff:
        setBool(*processor, "muted", true);
        break;
    case EdgeKind::ParamMinToMax:
        ASSERT_NE(param, nullptr);
        param->setValueNotifyingHost(0.0f);
        break;
    case EdgeKind::ParamMaxToMin:
        ASSERT_NE(param, nullptr);
        param->setValueNotifyingHost(1.0f);
        break;
    default:
        break;
    }

    processor->setPlayConfigDetails(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels(),
                                    kSampleRate, kBlockSize);
    processor->prepareToPlay(kSampleRate, kBlockSize);
    const int numChannels = std::max(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels());
    const int outChannels = processor->getTotalNumOutputChannels();
    ASSERT_GT(numChannels, 0);

    juce::AudioBuffer<float> buffer(numChannels, kBlockSize);
    double phase = 0.0;
    const double phaseStep = 2.0 * juce::MathConstants<double>::pi * kToneHz / kSampleRate;

    float referenceStep = 0.0f, edgeStep = 0.0f, settledStep = 0.0f;
    std::vector<float> previous((size_t)outChannels, 0.0f);
    std::vector<char> have((size_t)outChannels, 0);

    const int totalBlocks = kSettleBlocks + kEdgeBlocks;
    for (int block = 0; block < totalBlocks; ++block) {
        if (block == kSettleBlocks) {
            switch (c.kind) {
            case EdgeKind::BypassOn:
                setBool(*processor, "bypassed", true);
                break;
            case EdgeKind::BypassOff:
                setBool(*processor, "bypassed", false);
                break;
            case EdgeKind::MuteOn:
                setBool(*processor, "muted", true);
                break;
            case EdgeKind::MuteOff:
                setBool(*processor, "muted", false);
                break;
            case EdgeKind::ParamMinToMax:
                param->setValueNotifyingHost(1.0f);
                break;
            case EdgeKind::ParamMaxToMin:
                param->setValueNotifyingHost(0.0f);
                break;
            }
        }

        buffer.clear();
        for (int ch = 0; ch < c.module->audioInChannels && ch < numChannels; ++ch) {
            auto* data = buffer.getWritePointer(ch);
            double p = phase;
            for (int i = 0; i < kBlockSize; ++i, p += phaseStep)
                data[i] = kToneAmp * (float)std::sin(p);
        }
        phase += phaseStep * kBlockSize;
        for (const auto& constant : c.module->constantChannels)
            if (constant.first < numChannels)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(constant.first), constant.second, kBlockSize);

        juce::MidiBuffer midi;
        processor->processBlock(buffer, midi);

        for (int ch = 0; ch < outChannels; ++ch) {
            for (int i = 0; i < kBlockSize; ++i)
                ASSERT_TRUE(std::isfinite(buffer.getSample(ch, i))) << "non-finite sample, block " << block;
            bool seen = have[(size_t)ch] != 0;
            const float s = stepsIn(buffer, ch, previous[(size_t)ch], seen);
            have[(size_t)ch] = seen;
            if (block >= kSettleBlocks - kReferenceBlocks && block < kSettleBlocks)
                referenceStep = std::max(referenceStep, s);
            else if (block >= kSettleBlocks)
                edgeStep = std::max(edgeStep, s);
            if (block >= totalBlocks - kTailBlocks)
                settledStep = std::max(settledStep, s);
        }
    }

    const float steady = std::max({referenceStep, settledStep, kStepFloor});
    const float limit = kTolerance * steady;
    const juce::String label = juce::String(c.module->factoryName) + " / " + edgeName(c);
    reportAudit(label, edgeStep, limit);
    if (const char* known = knownClickReason(c.module->factoryName, edgeName(c))) {
        if (edgeStep <= limit)
            FAIL() << label << " no longer clicks; remove its entry from knownClicks()";
        GTEST_SKIP() << "known click (" << known << "): largest step " << edgeStep << " against a steady step of "
                     << steady;
    }
    EXPECT_LE(edgeStep, limit) << label << " clicks: largest step " << edgeStep << " against a steady step of "
                               << steady;
}

INSTANTIATE_TEST_SUITE_P(
    EveryProcessorEdge, ProcessorEdgeClickAudit, ::testing::ValuesIn(makeEdgeCases()),
    [](const ::testing::TestParamInfo<EdgeCase>& info) {
        return (sanitise(info.param.module->factoryName) + "_" + sanitise(edgeName(info.param))).toStdString();
    });

// ---------------------------------------------------------------------------------------------
// Sources through an ADSR-gated VCA, notes landing mid-block.
// ---------------------------------------------------------------------------------------------

namespace {

struct SourceCase {
    const char* name;
    const char* skipReason = nullptr;
};

const std::vector<SourceCase>& sourceCases() {
    static const std::vector<SourceCase> cases = {
        {"Oscillator"},
        {"Wavetable"},
        {"Noise", "noise has no slope bound, so a step cannot be told from signal; its CV gating is covered by "
                  "NoiseModuleTest.LevelCVThatStartsMidBlockActsWithinThatBlock"},
    };
    return cases;
}

std::unique_ptr<juce::AudioProcessor> makeSource(const juce::String& name) {
    if (name == "Wavetable")
        return std::make_unique<WavetableOscillatorModule>();
    if (name == "Noise")
        return std::make_unique<NoiseModule>();
    return std::make_unique<OscillatorModule>();
}

} // namespace

class SourceNoteClickAudit : public ::testing::TestWithParam<SourceCase> {};

// Regression test for FRO744: note-ons landing mid-block clicked because the VCA missed an
// envelope that idles at 0 and fires part-way through the block. The first case, Oscillator, is that
// repro chain (Oscillator -> VCA, ADSR with a 1 ms attack on the VCA CV).
TEST_P(SourceNoteClickAudit, StaccatoNotesFollowTheEnvelopeWithoutAStep) {
    const auto& c = GetParam();
    if (c.skipReason != nullptr)
        GTEST_SKIP() << c.skipReason;

    auto source = makeSource(c.name);
    ADSRModule adsr;
    VCAModule vca;
    constexpr float kAttackSeconds = 0.001f;
    setParamValue(&adsr, "attack", kAttackSeconds);
    setParamValue(&adsr, "decay", 0.0735f);
    setParamValue(&adsr, "sustain", 0.0f);
    setParamValue(&adsr, "release", 0.015f);
    source->setPlayConfigDetails(0, source->getTotalNumOutputChannels(), kSampleRate, kBlockSize);
    source->prepareToPlay(kSampleRate, kBlockSize);
    adsr.prepareToPlay(kSampleRate, kBlockSize);
    vca.prepareToPlay(kSampleRate, kBlockSize);

    const int pitches[16] = {68, 52, 58, 51, 53, 48, 66, 69, 37, 46, 65, 41, 71, 53, 69, 39};
    const double step = 0.125 * kSampleRate; // 16th notes at 120 bpm, so most onsets are mid-block
    const int total = static_cast<int>(step * 16);
    const auto channelsOf = [](juce::AudioProcessor& p) {
        return std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    };

    float worstStep = 0.0f, peak = 0.0f;
    float previous = 0.0f;
    bool havePrevious = false;
    for (int pos = 0; pos < total; pos += kBlockSize) {
        const int n = std::min(kBlockSize, total - pos);
        juce::MidiBuffer midi;
        for (int i = 0; i < 16; ++i) {
            const int on = static_cast<int>(std::lround(i * step));
            const int off = static_cast<int>(std::lround((i + 1) * step));
            if (off >= pos && off < pos + n)
                midi.addEvent(juce::MidiMessage::noteOff(1, pitches[i], 0.0f), off - pos);
            if (on >= pos && on < pos + n)
                midi.addEvent(juce::MidiMessage::noteOn(1, pitches[i], 0.8f), on - pos);
        }
        juce::AudioBuffer<float> srcBuf(channelsOf(*source), n), adsrBuf(channelsOf(adsr), n),
            vcaBuf(channelsOf(vca), n);
        srcBuf.clear();
        adsrBuf.clear();
        vcaBuf.clear();
        source->processBlock(srcBuf, midi);
        adsr.processBlock(adsrBuf, midi);
        vcaBuf.copyFrom(0, 0, srcBuf, 0, 0, n);
        vcaBuf.copyFrom(1, 0, adsrBuf, 0, 0, n);
        juce::MidiBuffer none;
        vca.processBlock(vcaBuf, none);
        worstStep = std::max(worstStep, stepsIn(vcaBuf, 0, previous, havePrevious));
        peak = std::max(peak, vcaBuf.getMagnitude(0, 0, n));
    }

    // Natural slope of a sine at the highest played pitch, plus the envelope ramping 0 to 1 over the attack.
    const float highestHz = 440.0f * std::pow(2.0f, (71 - 69) / 12.0f);
    const float natural = peak * (2.0f * juce::MathConstants<float>::pi * highestHz / (float)kSampleRate +
                                  1.0f / (kAttackSeconds * (float)kSampleRate));
    const float limit = 1.5f * natural;
    reportAudit(juce::String(c.name) + " -> ADSR/VCA staccato", worstStep, limit);
    EXPECT_GT(peak, 0.05f) << c.name << " produced no signal";
    EXPECT_LE(worstStep, limit) << c.name << " steps at a note edge";
}

INSTANTIATE_TEST_SUITE_P(EverySource, SourceNoteClickAudit, ::testing::ValuesIn(sourceCases()),
                         [](const ::testing::TestParamInfo<SourceCase>& info) {
                             return sanitise(info.param.name).toStdString();
                         });
