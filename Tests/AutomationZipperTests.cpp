// Zipper-noise enforcement for timeline automation.
//
// The applier writes a parameter's BASE value straight through
// `param->setValue(param->convertTo0to1(v))`, once per block (or per control-rate slice). Any
// float parameter that reaches a DSP coefficient raw — no SmoothedValue between `param->get()`
// and the sample loop — therefore steps at block rate, and a stepped gain / delay time / filter
// coefficient is a click.
//
// This suite renders every audio-producing factory module while sweeping one float parameter
// across its full range at exactly that write rate, and asserts the output stays finite and free
// of discontinuities. It is built the same way WavetableWarpAliasTest is: the case list is
// derived from the live module factory and each module's live parameter list, so a module or a
// parameter added later is exercised automatically — it cannot ship untested. The one escape
// hatch is moduleConfigs() below, where a module is either configured for the test or explicitly
// excluded with a reason; AutomationZipperCoverage fails the build if a factory module with float
// parameters appears in neither.

#include "AI/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumBlocks = 50;
// The ramp above walks the range 2% per block, which a broken parameter survives simply because
// each individual step is small. These blocks follow it with the case smoothing actually exists
// for: one VERTICAL automation segment, the parameter written straight back to its minimum at a
// single block boundary — a lane edit, a preset recall, or the first block after a locate.
constexpr int kJumpBlocks = 12;
constexpr int kJumpSegment = 3; // blocks held at each extreme, so kJumpBlocks covers four jumps
constexpr float kTestToneHz = 440.0f;
constexpr float kTestToneAmp = 0.5f;

// The first blocks carry each module's own start-up transient (filter state settling from zero,
// an envelope's attack, a delay line filling), which is not a zipper. They are still rendered and
// still checked for NaN/Inf — only the discontinuity measurement starts afterwards.
constexpr int kSettleBlocks = 2;

// A 440 Hz sine at 0.5 slews at most 2*pi*440/48000*0.5 ~= 0.029 per sample, so anything at or
// near this bound is a step, not signal. Modules whose output is a staircase by construction
// override it in moduleConfigs() with a reason.
constexpr float kDefaultDeltaBound = 0.6f;

/** A parameter preset applied before prepareToPlay: `value` is in the parameter's own plain
    units (an index for Choice/Int, 0/1 for Bool), converted through convertTo0to1. */
struct ParamPreset {
    const char* paramId;
    float value;
};

/** Per-parameter relaxation of the discontinuity bound. NEVER relaxes the NaN/Inf check. */
struct ParamBound {
    const char* paramId;
    float bound;
    const char* reason;
};

struct ModuleConfig {
    const char* factoryName;

    /** When set, the module is not rendered; `reason` says why. */
    bool excluded = false;
    const char* reason = "";

    /** Leading channels fed the 440 Hz test tone. 0 marks a source (oscillator, noise, LFO). */
    int audioInChannels = 0;

    /** Extra input channels held at a constant value — gates and steady CV. */
    std::vector<std::pair<int, float>> constantChannels{};

    /** Hold a MIDI note for the whole render (MIDI-driven sources and envelopes). */
    bool holdMidiNote = false;

    /** Module-wide discontinuity bound; see kDefaultDeltaBound. */
    float deltaBound = kDefaultDeltaBound;

    /** Non-float parameters that have to be set for the module to emit a continuous signal. */
    std::vector<ParamPreset> presets{};

    std::vector<ParamBound> paramBounds{};
};

// ---------------------------------------------------------------------------------------------
// The exclusion list. Every entry says why the module cannot be swept here; none of them means
// "this module is allowed to zipper".
// ---------------------------------------------------------------------------------------------
//
//   Sequencer / Poly Sequencer  declare zero audio channels — every float they own (BPM, gate
//                               length, F.Env amount) times or fills a discrete MIDI event, so
//                               there is no continuous signal for a step to land in.
//   Sampler                     renders silence until a file is loaded, and loading one from a
//                               parameterised suite would make it a filesystem test.
//                               SamplerModuleTests covers it; its Level knob is smoothed.
//
// Modules with no float parameters at all (Poly MIDI, MIDI Keyboard, External MIDI, Math,
// Track In, and the three AudioGraphIOProcessor endpoints) are outside the suite by
// construction and must NOT be listed here — AutomationZipperCoverage rejects stale entries.

// The one place a band's gain is genuinely allowed to step. The EQ rewrites its biquads once per
// block from 20 ms-smoothed values, which covers a knob drag and any automation curve with a sane
// slope (the 50-block ramp above passes at the default bound). It cannot cover an INSTANT jump
// across the full +/-24 dB range: 20 ms is only two blocks, and half of a 48 dB coefficient swap
// still lands in one sample. Removing that would take per-sample coefficient interpolation, which
// costs a biquad per band per sample — and the 20 ms cannot simply be lengthened, because the two
// bell bands are CV targets and have to track an LFO (ParametricEQCV.*ModulatesTheFirstBell).
constexpr const char* kEqGainJumpReason =
    "a full-range instantaneous +/-24 dB jump swaps the whole biquad at a block boundary; the "
    "block-rate coefficient update is documented in ParametricEQModule and cannot absorb it "
    "without per-sample coefficient interpolation";

const std::vector<ModuleConfig>& moduleConfigs() {
    static const std::vector<ModuleConfig> configs = {
        // ---- sources -------------------------------------------------------------------------
        {/*factoryName*/ "Oscillator", false, "", 0, {}, true},
        {/*factoryName*/ "Wavetable", false, "", 0, {}, true},
        {/*factoryName*/ "Noise",
         false,
         "",
         0,
         {},
         false,
         // White noise is uncorrelated sample to sample: |x[n] - x[n-1]| reaches 2 by design, so
         // the discontinuity bound cannot say anything here. The NaN/Inf check still applies, and
         // Color/Level smoothing is what keeps the *envelope* of that noise from stepping.
         2.2f},
        {/*factoryName*/ "LFO",
         false,
         "",
         0,
         {},
         false,
         kDefaultDeltaBound,
         // Sync mode ignores Rate (Hz) entirely; switch to free-running so the sweep is real.
         {{"mode", 0.0f}}},
        {/*factoryName*/ "Macros", false, "", 0, {}, false},
        {/*factoryName*/ "Sample & Hold",
         false,
         "",
         0,
         {},
         false,
         // A sample & hold emits a staircase — that IS the module. Level/Offset smoothing shapes
         // the steps' height, it cannot remove them, so only the NaN/Inf check is meaningful.
         2.2f},

        // ---- envelopes -----------------------------------------------------------------------
        // Sustain is lifted off its 0.0 default so the envelope actually holds a level for the
        // Attack/Decay/Release sweeps to be visible against.
        {/*factoryName*/ "ADSR", false, "", 0, {}, true, kDefaultDeltaBound, {{"sustain", 0.8f}}},
        {/*factoryName*/ "Amp Env", false, "", 0, {}, true, kDefaultDeltaBound, {{"sustain", 0.8f}}},
        {/*factoryName*/ "Filter Env", false, "", 0, {}, true, kDefaultDeltaBound, {{"sustain", 0.8f}}},
        {/*factoryName*/ "Envelope Follower", false, "", 1},

        // ---- processors ----------------------------------------------------------------------
        {/*factoryName*/ "Filter", false, "", 1},
        {/*factoryName*/ "VCA", false, "", 1, {{1, 1.0f}}}, // ch1 = gate CV, held open
        {/*factoryName*/ "Voice Mixer", false, "", 1},
        {/*factoryName*/ "Attenuverter", false, "", 1},
        {/*factoryName*/ "Mod Slot", false, "", 1}, // the same AttenuverterModule under its UI name

        // ---- FX ------------------------------------------------------------------------------
        {/*factoryName*/ "Delay", false, "", 2},
        {/*factoryName*/ "Reverb", false, "", 2},
        {/*factoryName*/ "Chorus", false, "", 2},
        {/*factoryName*/ "Flanger", false, "", 2},
        {/*factoryName*/ "Phaser", false, "", 2},
        {/*factoryName*/ "Distortion", false, "", 2},
        {/*factoryName*/ "Limiter", false, "", 2},
        {/*factoryName*/ "Pitch Shifter", false, "", 2},
        {/*factoryName*/ "Compressor",
         false,
         "",
         2,
         {},
         false,
         kDefaultDeltaBound,
         {},
         {{"makeupGain", 4.0f,
           "Makeup Gain tops out at +40 dB, i.e. a 100x scaling of the whole signal, so the test "
           "tone's own slew rate passes the default bound long before anything is wrong. A step "
           "would still register as roughly twice the peak."}}},
        {/*factoryName*/ "Bitcrusher",
         false,
         "",
         2,
         {},
         false,
         // Rate Reduction is a sample-and-hold and Bit Depth is a quantiser: at the bottom of
         // either range the output is a staircase with steps approaching full scale. That is the
         // effect, not a zipper.
         2.2f},
        {/*factoryName*/ "Parametric EQ",
         false,
         "",
         2,
         {},
         false,
         kDefaultDeltaBound,
         // All four bands ship disabled (a fresh EQ is a straight wire), so nothing would be
         // swept unless they are switched on here.
         {{"band1On", 1.0f}, {"band2On", 1.0f}, {"band3On", 1.0f}, {"band4On", 1.0f}},
         {{"band1Gain", 2.5f, kEqGainJumpReason},
          {"band2Gain", 2.5f, kEqGainJumpReason},
          {"band3Gain", 2.5f, kEqGainJumpReason},
          {"band4Gain", 2.5f, kEqGainJumpReason}}},

        {/*factoryName*/ "Comparator",
         false,
         "",
         1,
         {},
         false,
         // A comparator IS a threshold detector: its gate output flips rail-to-rail whenever the
         // input crosses the (swept) threshold. The steps are the function, so only the NaN/Inf
         // check is meaningful here.
         2.2f},
        {/*factoryName*/ "Ring Modulator", false, "", 2},

        // ---- excluded ------------------------------------------------------------------------
        {/*factoryName*/ "Sequencer", true,
         "declares zero audio channels; BPM / Gate / F.Env only time or fill discrete MIDI events"},
        {/*factoryName*/ "Poly Sequencer", true,
         "declares zero audio channels; BPM / Gate only time discrete MIDI events"},
        {/*factoryName*/ "Sampler", true,
         "renders silence until a sample file is loaded; covered by SamplerModuleTests"},
    };
    return configs;
}

const ModuleConfig* configFor(const juce::String& factoryName) {
    for (const auto& c : moduleConfigs())
        if (factoryName == c.factoryName)
            return &c;
    return nullptr;
}

/** Every float parameter of a processor, in declaration order. */
std::vector<juce::AudioParameterFloat*> floatParamsOf(juce::AudioProcessor& processor) {
    std::vector<juce::AudioParameterFloat*> out;
    for (auto* p : processor.getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p))
            out.push_back(f);
    return out;
}

struct ZipperCase {
    juce::String module;
    juce::String paramId;
};

/** (module x float parameter) for every non-excluded configured module, read off the live
    factory rather than a hand-maintained list — that is what makes the suite self-extending. */
std::vector<ZipperCase> makeCases() {
    std::vector<ZipperCase> cases;
    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames()) {
        const auto* config = configFor(name);
        if (config == nullptr || config->excluded)
            continue;
        auto module = synth::AIStateMapper::createModule(name);
        if (module == nullptr)
            continue;
        for (auto* p : floatParamsOf(*module))
            cases.push_back({name, p->paramID});
    }
    return cases;
}

juce::String sanitise(const juce::String& s) {
    juce::String out;
    for (auto c : s)
        out += (juce::CharacterFunctions::isLetterOrDigit(c) ? juce::String::charToString(c) : juce::String("_"));
    return out;
}

void applyPresets(juce::AudioProcessor& processor, const ModuleConfig& config) {
    for (const auto& preset : config.presets) {
        auto* p = findParameterByID(&processor, preset.paramId);
        ASSERT_NE(p, nullptr) << "preset names a parameter " << config.factoryName
                              << " does not have: " << preset.paramId;
        p->setValueNotifyingHost(p->convertTo0to1(preset.value));
    }
}

float boundFor(const ModuleConfig& config, const juce::String& paramId) {
    for (const auto& b : config.paramBounds)
        if (paramId == b.paramId)
            return b.bound;
    return config.deltaBound;
}

} // namespace

class AutomationZipperTest : public ::testing::TestWithParam<ZipperCase> {};

// The core property: sweeping one automatable float across its full range at the applier's own
// write rate must not put a discontinuity — or a NaN — into the module's output.
TEST_P(AutomationZipperTest, SweepingTheParameterProducesNoDiscontinuity) {
    const auto& testCase = GetParam();
    const auto* config = configFor(testCase.module);
    ASSERT_NE(config, nullptr);

    auto processor = synth::AIStateMapper::createModule(testCase.module);
    ASSERT_NE(processor, nullptr) << "factory returned nothing for " << testCase.module;

    applyPresets(*processor, *config);

    // Held as a RangedAudioParameter, exactly as AutomationApplier::Binding does — that is the
    // pointer type whose public setValue() the applier writes through.
    juce::RangedAudioParameter* param = findParameterByID(processor.get(), testCase.paramId);
    ASSERT_NE(param, nullptr) << testCase.module << " has no float parameter " << testCase.paramId;
    ASSERT_NE(dynamic_cast<juce::AudioParameterFloat*>(param), nullptr);
    const auto& range = param->getNormalisableRange();

    processor->setPlayConfigDetails(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels(),
                                    kSampleRate, kBlockSize);
    processor->prepareToPlay(kSampleRate, kBlockSize);

    const int numChannels = std::max(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels());
    ASSERT_GT(numChannels, 0) << testCase.module << " declares no channels";
    const int outChannels = processor->getTotalNumOutputChannels();

    juce::AudioBuffer<float> buffer(numChannels, kBlockSize);
    std::vector<float> previousSample((size_t)outChannels, 0.0f);
    std::vector<bool> havePrevious((size_t)outChannels, false);

    double tonePhase = 0.0;
    const double phaseStep = 2.0 * juce::MathConstants<double>::pi * kTestToneHz / kSampleRate;

    float worstDelta = 0.0f;
    int worstChannel = -1;
    int worstBlock = -1;

    for (int block = 0; block < kNumBlocks + kJumpBlocks; ++block) {
        // The applier's exact write path: a plain store of the normalised base value, once per
        // block. Blocks [0, kNumBlocks) walk the full range; the rest square-wave between the two
        // extremes every kJumpSegment blocks. Four jump instants rather than one, because a
        // single one can land where the test tone happens to be near a zero crossing and hide a
        // step behind it.
        const float t = block < kNumBlocks ? (float)block / (float)(kNumBlocks - 1)
                                           : (float)((((block - kNumBlocks) / kJumpSegment) % 2 == 0) ? 0 : 1);
        param->setValue(param->convertTo0to1(range.start + t * (range.end - range.start)));

        buffer.clear();
        for (int ch = 0; ch < config->audioInChannels && ch < numChannels; ++ch) {
            auto* data = buffer.getWritePointer(ch);
            double phase = tonePhase;
            for (int i = 0; i < kBlockSize; ++i) {
                data[i] = kTestToneAmp * (float)std::sin(phase);
                phase += phaseStep;
            }
        }
        for (const auto& constant : config->constantChannels)
            if (constant.first < numChannels)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(constant.first), constant.second, kBlockSize);
        tonePhase += phaseStep * kBlockSize;

        juce::MidiBuffer midi;
        if (config->holdMidiNote && block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);

        processor->processBlock(buffer, midi);

        for (int ch = 0; ch < outChannels; ++ch) {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < kBlockSize; ++i) {
                ASSERT_TRUE(std::isfinite(data[i]))
                    << testCase.module << " / " << testCase.paramId << ": non-finite sample on channel " << ch
                    << " at block " << block << " sample " << i;
            }

            if (block < kSettleBlocks) {
                previousSample[(size_t)ch] = data[kBlockSize - 1];
                havePrevious[(size_t)ch] = true;
                continue;
            }

            for (int i = 0; i < kBlockSize; ++i) {
                if (havePrevious[(size_t)ch]) {
                    const float delta = std::abs(data[i] - previousSample[(size_t)ch]);
                    if (delta > worstDelta) {
                        worstDelta = delta;
                        worstChannel = ch;
                        worstBlock = block;
                    }
                }
                previousSample[(size_t)ch] = data[i];
                havePrevious[(size_t)ch] = true;
            }
        }
    }

    const float bound = boundFor(*config, testCase.paramId);
    EXPECT_LE(worstDelta, bound) << testCase.module << " / " << testCase.paramId
                                 << " steps its output: worst inter-sample delta " << worstDelta << " > " << bound
                                 << " (channel " << worstChannel << ", block " << worstBlock
                                 << "). Smooth the parameter (juce::SmoothedValue, snapped at "
                                    "prepareToPlay) or, if the step is inherent, document it and raise the bound "
                                    "for this case in Tests/AutomationZipperTests.cpp.";
}

INSTANTIATE_TEST_SUITE_P(EveryAutomatableFloatParam, AutomationZipperTest, ::testing::ValuesIn(makeCases()),
                         [](const ::testing::TestParamInfo<ZipperCase>& info) {
                             return (sanitise(info.param.module) + "_" + sanitise(info.param.paramId)).toStdString();
                         });

// The enforcement half, and the reason this file is not just a list of hand-written cases: a
// module registered in the factory with float parameters has to be either configured above or
// excluded above. Adding one without doing either fails here rather than shipping unswept.
TEST(AutomationZipperCoverage, EveryFactoryModuleWithFloatParamsIsCoveredOrExcluded) {
    juce::StringArray uncovered;
    juce::StringArray stale;

    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto module = synth::AIStateMapper::createModule(name);
        if (module == nullptr)
            continue;

        const bool hasFloatParams = !floatParamsOf(*module).empty();
        const auto* config = configFor(name);

        if (hasFloatParams && config == nullptr)
            uncovered.add(name);
        if (!hasFloatParams && config != nullptr)
            stale.add(name);
    }

    EXPECT_TRUE(uncovered.isEmpty()) << "these factory modules own automatable float parameters but appear in "
                                        "neither the config table nor the exclusion list in "
                                        "Tests/AutomationZipperTests.cpp: "
                                     << uncovered.joinIntoString(", ").toStdString();
    EXPECT_TRUE(stale.isEmpty()) << "these entries in Tests/AutomationZipperTests.cpp name modules with no float "
                                    "parameters and should be deleted: "
                                 << stale.joinIntoString(", ").toStdString();
}

// Every name in the table has to resolve through the factory — a rename upstream must not leave a
// silently dead entry behind (which would take its module's coverage with it).
TEST(AutomationZipperCoverage, EveryConfiguredModuleNameExistsInTheFactory) {
    const auto factoryNames = synth::AIStateMapper::moduleFactoryTypeNames();
    for (const auto& config : moduleConfigs())
        EXPECT_TRUE(factoryNames.contains(config.factoryName))
            << config.factoryName << " is configured in Tests/AutomationZipperTests.cpp but is not a factory module";
}

// Excluded entries must carry a reason, and configured ones must not pretend to.
TEST(AutomationZipperCoverage, EveryExclusionCarriesAReason) {
    for (const auto& config : moduleConfigs()) {
        if (config.excluded)
            EXPECT_GT(juce::String(config.reason).length(), 20)
                << config.factoryName << " is excluded without a usable justification";
        for (const auto& bound : config.paramBounds)
            EXPECT_GT(juce::String(bound.reason).length(), 20)
                << config.factoryName << " / " << bound.paramId << " raises the delta bound without a justification";
    }
}
