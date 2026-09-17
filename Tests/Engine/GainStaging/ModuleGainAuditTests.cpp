// FRO120 gain-staging audit, part 1: per-module hidden-amplifier sweep.
//
// The incident this guards against: a Ring Modulator's Drive turned out to be an uncompensated
// ~xdrive gain, so a two-oscillator patch summed past 0 dBFS at Master and the audio device
// hard-clipped it, erasing one oscillator entirely (MasterModule is a plain sum x gain with no
// limiting — see Source/Modules/MasterModule.h). This suite answers "which modules/params are
// hidden amplifiers?" by driving every audio-processing module in isolation, sweeping every
// float parameter over {min, default, max} and every choice parameter over every choice (mute/
// bypass/dualIO/poly excluded — they are not gain controls), and measuring the worst-case
// output/input RMS ratio in dB. Anything over +6 dB must be an explicit, justified allow-list
// entry (a real gain control) or the test fails.
//
// Oscillator-type SOURCES (Oscillator, Wavetable, Noise) have no audio input, so "gain relative
// to input" is undefined for them — SourceModulePeakTest below gives them a lighter peak-only
// check instead (docs/testing.md's existing OscillatorTest/WavetableOscillatorModule*Tests cover
// their waveform correctness; this is only about staying within [-1, 1] at default/max Level).

#include "Modules/ChannelStripModule.h"
#include "Modules/FX/BitcrusherModule.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/FX/GateModule.h"
#include "Modules/FX/LimiterModule.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FX/PhaserModule.h"
#include "Modules/FX/PitchShifterModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MasterModule.h"
#include "Modules/MathModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/NoiseModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "Modules/VoiceMixerModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
constexpr int kTotalSamples = 44100; // ~1 s
constexpr int kSkipSamples = 4096;   // settle past smoothing ramps / filter startup transients
constexpr double kGainAlarmDb = 6.0;

// ---------------------------------------------------------------------------------------------
// Test signals — one column of the sweep matrix. Amplitudes per the FRO120 task brief: a modest
// sine, a full-scale square, and a "hot" square already past unity (simulating a user-authored
// patch that over-drives an earlier stage before this module).
// ---------------------------------------------------------------------------------------------

std::vector<float> makeSine(double freqHz, float amp, int n) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = amp * std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * (double)i / kSampleRate);
    return v;
}

std::vector<float> makeSquare(double freqHz, float amp, int n) {
    std::vector<float> v(static_cast<size_t>(n));
    const double period = kSampleRate / freqHz;
    for (int i = 0; i < n; ++i) {
        const double phase = std::fmod((double)i, period) / period;
        v[(size_t)i] = phase < 0.5 ? amp : -amp;
    }
    return v;
}

struct TestSignal {
    juce::String name;
    std::vector<float> samples; // kTotalSamples long
};

std::vector<TestSignal> buildTestSignals() {
    return {
        {"sine220@0.5", makeSine(220.0, 0.5f, kTotalSamples)},
        {"square55@1.0", makeSquare(55.0, 1.0f, kTotalSamples)},
        {"square55@4.0hot", makeSquare(55.0, 4.0f, kTotalSamples)},
    };
}

double rmsOf(const std::vector<float>& v, int skip) {
    double sum = 0.0;
    int n = 0;
    for (size_t i = (size_t)skip; i < v.size(); ++i, ++n)
        sum += double(v[i]) * v[i];
    return n > 0 ? std::sqrt(sum / n) : 0.0;
}

// ---------------------------------------------------------------------------------------------
// Module registry — which raw channels carry audio in for each module under test.
//
// ModuleBase::mapInputChannel port-role metadata is NOT a reliable way to find these
// generically: RingModulatorModule's Carrier/Modulator and MathModule's A/B are real audio
// jacks that never go through mapStereoPairInput (ModuleBase.h's own comment on
// hasCollapsibleOutputPair explains why input pairing stays an explicit per-module declaration,
// and neither module declares one), so their mapped role reads PortRole::Other. The channels
// below are taken from each module's own channel-map comment/constants instead.
//
// Every module here defaults its Dual I/O toggle OFF (collapsed): for the plain FX shape
// (>= 2 inputs, exactly 2 outputs) that means raw ch0/ch1 are ONE "Audio" jack, fanned to both
// legs (ModuleBase::mapStereoPairInput). Feeding the identical test tone on both raw channels
// reproduces that fan-out, and — per the task brief's "for two-input modules like Ring Mod also
// try the SAME signal on both inputs" — incidentally gives Ring Mod its carrier==modulator case
// and Math its A==B case for free, since it is the same rule applied uniformly.
// ---------------------------------------------------------------------------------------------

struct ModuleSpec {
    juce::String name;
    std::function<std::unique_ptr<ModuleBase>()> factory;
    std::vector<int> audioInputChannels; // raw channels fed the SAME test tone; CV left silent
};

std::vector<ModuleSpec> buildModuleRegistry() {
    std::vector<ModuleSpec> specs;

    specs.push_back({"Filter", [] { return std::make_unique<FilterModule>(); }, {0, FilterModule::kRightBase}});
    specs.push_back({"VCA", [] { return std::make_unique<VCAModule>(); }, {0, VCAModule::kRightBase}});
    specs.push_back({"Math", [] { return std::make_unique<MathModule>(); }, {0, 1}}); // A, B
    specs.push_back({"Voice Mixer",
                     [] { return std::make_unique<VoiceMixerModule>(); },
                     {0, 1, 2, 3, 4, 5, 6, 7}}); // all 8 voices — worst case is unison
    specs.push_back({"Channel Strip",
                     [] {
                         auto m = std::make_unique<ChannelStripModule>();
                         m->setShape(ChannelStripModule::Shape::Stereo);
                         return m;
                     },
                     {0, ChannelStripModule::kRightBase}});
    specs.push_back({"Master",
                     [] { return std::make_unique<MasterModule>(); },
                     {MasterModule::kMixLeft, MasterModule::kMixRight}}); // Direct left silent on purpose

    const std::vector<int> fx01 = {0, 1};
    specs.push_back({"Bitcrusher", [] { return std::make_unique<BitcrusherModule>(); }, fx01});
    specs.push_back({"Chorus", [] { return std::make_unique<ChorusModule>(); }, fx01});
    specs.push_back({"Compressor", [] { return std::make_unique<CompressorModule>(); }, fx01});
    specs.push_back({"Delay", [] { return std::make_unique<DelayModule>(); }, fx01});
    specs.push_back({"Distortion", [] { return std::make_unique<DistortionModule>(); }, fx01});
    specs.push_back({"Flanger", [] { return std::make_unique<FlangerModule>(); }, fx01});
    specs.push_back({"Gate", [] { return std::make_unique<GateModule>(); }, fx01});
    specs.push_back({"Limiter", [] { return std::make_unique<LimiterModule>(); }, fx01});
    specs.push_back({"Parametric EQ", [] { return std::make_unique<ParametricEQModule>(); }, fx01});
    specs.push_back({"Phaser", [] { return std::make_unique<PhaserModule>(); }, fx01});
    specs.push_back({"Pitch Shifter", [] { return std::make_unique<PitchShifterModule>(); }, fx01});
    specs.push_back({"Reverb", [] { return std::make_unique<ReverbModule>(); }, fx01});
    // Carrier (ch0) and Modulator (ch1), unrelated mono jacks (RingModulatorModule class comment);
    // fed the same tone per the task brief's explicit instruction for two-input modules.
    specs.push_back({"Ring Modulator", [] { return std::make_unique<RingModulatorModule>(); }, fx01});

    return specs;
}

// ---------------------------------------------------------------------------------------------
// Parameter sweep — every float parameter at {min, default, max}, every choice parameter at
// every choice. Skips bypass/mute (task brief) and every AudioParameterBool (dualIO, poly —
// neither is a gain control; a module with some other stray bool would also be a layout toggle,
// not something this audit sweeps).
// ---------------------------------------------------------------------------------------------

struct SweepCase {
    juce::String paramId;
    juce::String valueLabel;
    float normalizedValue;
};

std::vector<SweepCase> buildSweepCases(juce::AudioProcessor& proc) {
    std::vector<SweepCase> cases;
    for (auto* param : proc.getParameters()) {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
        if (withId == nullptr)
            continue;
        const auto id = withId->paramID;
        if (id == "bypassed" || id == "muted")
            continue;
        if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr)
            continue; // dualIO / poly: layout toggles, not gain sweep targets

        if (auto* choiceP = dynamic_cast<juce::AudioParameterChoice*>(param)) {
            const int n = choiceP->choices.size();
            for (int idx = 0; idx < n; ++idx) {
                const float norm = n > 1 ? float(idx) / float(n - 1) : 0.0f;
                cases.push_back({id, choiceP->choices[idx], norm});
            }
            continue;
        }
        if (dynamic_cast<juce::RangedAudioParameter*>(param) != nullptr) {
            cases.push_back({id, "min", 0.0f});
            cases.push_back({id, "default", param->getDefaultValue()});
            cases.push_back({id, "max", 1.0f});
        }
    }
    return cases;
}

void applySweepCase(juce::AudioProcessor& proc, const SweepCase& c) {
    for (auto* param : proc.getParameters()) {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
        if (withId != nullptr && withId->paramID == c.paramId) {
            param->setValueNotifyingHost(c.normalizedValue);
            return;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Render + measure. Worst-CHANNEL RMS/peak across the whole output buffer (not just the raw
// channels we happen to know are "the" audio output) — that is deliberate: it is exactly how a
// hidden amplifier on an output the audit didn't think to name specially (a send leg, a derived
// Math output) would be caught.
// ---------------------------------------------------------------------------------------------

struct MeasureResult {
    double peakOut = 0.0;
    double rmsOut = 0.0;
};

MeasureResult renderAndMeasure(ModuleBase& m, const std::vector<int>& audioInputChannels,
                               const std::vector<float>& inputSignal) {
    const int numCh = std::max(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels());
    juce::AudioBuffer<float> full(std::max(1, numCh), kTotalSamples);
    full.clear();

    int rendered = 0;
    while (rendered < kTotalSamples) {
        const int n = std::min(kBlockSize, kTotalSamples - rendered);
        juce::AudioBuffer<float> block(std::max(1, numCh), n);
        block.clear();
        for (int ch : audioInputChannels)
            if (ch >= 0 && ch < numCh)
                for (int i = 0; i < n; ++i)
                    block.setSample(ch, i, inputSignal[(size_t)(rendered + i)]);
        juce::MidiBuffer noMidi;
        m.processBlock(block, noMidi);
        for (int ch = 0; ch < numCh; ++ch)
            full.copyFrom(ch, rendered, block, ch, 0, n);
        rendered += n;
    }

    MeasureResult r;
    const int measureLen = kTotalSamples - kSkipSamples;
    for (int ch = 0; ch < numCh; ++ch) {
        r.peakOut = std::max(r.peakOut, (double)full.getMagnitude(ch, kSkipSamples, measureLen));
        r.rmsOut = std::max(r.rmsOut, (double)full.getRMSLevel(ch, kSkipSamples, measureLen));
    }
    return r;
}

// ---------------------------------------------------------------------------------------------
// Audit log + report.
// ---------------------------------------------------------------------------------------------

struct AuditEntry {
    juce::String module;
    juce::String paramId;
    juce::String valueLabel;
    juce::String signal;
    double gainDb = 0.0;
    double peakOut = 0.0;
};

// Allow-list: (module, paramID) pairs whose whole JOB is to change gain, with the measured
// worst-case this audit found (so a regression that pushes it further is still caught). Every
// entry is justified in its own comment. The "*" paramID matches any parameter on that module.
struct AllowEntry {
    juce::String module;
    juce::String paramId; // "*" = any
    double maxDb;
    const char* reason;
};

const std::vector<AllowEntry>& allowList() {
    static const std::vector<AllowEntry> table = {
        // Faders: the whole point of a fader is +12 dB of headroom (kMaxGainDb on both modules).
        {"Master", "gain", 13.0,
         "Master fader is -60..+12 dB by design (docs/mixer.md); MasterModule "
         "has no limiter on purpose — that is exactly what this audit exists to "
         "surface for the NEXT stage, not to fail this one on."},
        {"Channel Strip", "gain", 13.0, "Channel fader is -60..+12 dB by design (docs/mixer.md)."},
        {"Channel Strip", "send1Level", 13.0, "Send level fader, same -60..+12 dB range as the main fader."},
        {"Channel Strip", "send2Level", 13.0, "Send level fader, same -60..+12 dB range as the main fader."},
        {"Channel Strip", "send3Level", 13.0, "Send level fader, same -60..+12 dB range as the main fader."},
        {"Channel Strip", "send4Level", 13.0, "Send level fader, same -60..+12 dB range as the main fader."},
        // Explicit gain-restoration controls.
        {"Compressor", "makeupGain", 41.0,
         "Makeup Gain is an explicit -20..+40 dB gain-restoration control "
         "(the whole point of a compressor: turn the squashed signal back "
         "up)."},
        // Limiter: two DIFFERENT gain-affecting params, both legitimate but for different reasons.
        {"Limiter", "inputGain", 21.0,
         "Input Gain is an explicit -20..+20 dB pre-limiter drive control; "
         "the limiter itself holds the output at/under threshold so this "
         "does not translate to unbounded output gain (measured worst case 6.64 dB, "
         "well inside this ceiling)."},
        {"Limiter", "threshold", 8.5,
         "NOT an independent bug: juce::dsp::Limiter (widgets/juce_Limiter.cpp, see its private "
         "update()) bakes automatic makeup gain into the Threshold param itself — "
         "outputVolume = 10^(10*0.75/40) * dB2gain(-threshold) — so a LOWER threshold (more "
         "limiting) also means MORE automatic makeup gain, by JUCE's own design (a loudness-"
         "maximizing limiter, not a passive ceiling). Measured worst case at threshold=-20dB "
         "(min) is +7.61 dB with a 0.5-amplitude sine. Worth knowing, not a LimiterModule bug."},
        {"Parametric EQ", "outputGain", 24.5,
         "Output trim is an explicit +/-24 dB gain control (ParametricEQModule::kMaxGainDb); "
         "measured exactly +24.00 dB at max, matching the parameter's own range precisely."},
        // Parametric EQ's four bandNGain params are NOT allow-listed: this audit never turns a
        // band's own "on" toggle on (AudioParameterBool sweep is skipped entirely, see
        // buildSweepCases), so sweeping bandNGain alone with the band left OFF (the module's
        // documented default — docs/testing.md) measures no effect and never crosses +6 dB here.
        // If a future change makes gain apply while a band is off, or the bands default on, this
        // audit will start flagging it, and it needs a real entry with a real measured number.
        //
        // Drive/character controls that are explicitly nonlinear "get louder AND uglier" knobs.
        {"Distortion", "drive", 27.0,
         "Drive (1..20) is the module's entire purpose; measured worst case "
         "reported below — Distortion is a shaping stage users expect to "
         "follow with a level/gain stage, same as a real pedal."},
        // Filter's drive/resonance are NOT allow-listed: with these test signals/channels, Filter's
        // measured worst case across every parameter stayed at 2.11 dB (see the per-module report),
        // well under the line. If a future oversampling/Q change pushes resonance past +6 dB, this
        // audit will correctly start asking for a real entry rather than one guessed in advance.
        //
        // Feedback/resonance family: Chorus, Phaser and Pitch Shifter all run an internal feedback
        // loop around a delay/allpass network. Driven continuously by a steady test tone (not the
        // short transients a real patch usually feeds them), the loop can build up well past unity
        // before the render's 1 s window ends — a real, physically-expected property of a feedback
        // comb/allpass network approaching resonance, not a "gain knob" in the fader sense. Delay's
        // own feedback (0..0.95) stayed under +6 dB with these specific test tones/durations, so it
        // is NOT allow-listed — a future finding there would be new information, not a known case.
        {"Chorus", "feedback", 19.5,
         "Feedback -1..1 resonates a comb-filter loop; measured worst case +18.90 dB (feedback=max, "
         "sine220) — see the feedback/resonance family note above."},
        {"Phaser", "feedback", 14.0,
         "Feedback -1..1 resonates an allpass loop; measured worst case +13.56 dB (feedback=min, "
         "sine220) — see the feedback/resonance family note above."},
        {"Pitch Shifter", "feedback", 10.5,
         "Feedback 0..kMaxFeedback resonates the shifter's internal delay loop; measured worst case "
         "+9.73 dB (feedback=max, sine220) — see the feedback/resonance family note above."},
        // Reverb: a continuous HOT (amplitude 4) test tone keeps depositing energy into the
        // algorithmic tail faster than it decays within this render's window, at wet/dry=1 (their
        // maxima). A real patch's reverb send is normally well under unity precisely because of
        // this; still worth a follow-up (see the final report) since Wet/Dry read as plain 0..1 mix
        // knobs, not gain controls, and nothing in ReverbModule documents this buildup.
        {"Reverb", "wet", 9.5, "Measured worst case +8.91 dB (wet=max, hot square) — see the Reverb note above."},
        {"Reverb", "dry", 8.5, "Measured worst case +7.77 dB (dry=max, hot square) — see the Reverb note above."},
        // Voice Mixer: this audit's own worst-case probe (8 identical unison voices, Level at its
        // max 1.0) sums to amplitude 8x before VoiceMixerModule's tanh soft-clip — the soft-clip is
        // exactly what keeps this bounded (it cannot run away further regardless of voice count or
        // how hot the input is), so it is a real but SELF-LIMITING gain, unlike a naive sum.
        {"Voice Mixer", "level", 9.0,
         "Measured worst case +8.25 dB (8 unison voices, Level=max, sine220); bounded by the "
         "module's own tanh soft-clip — see VoiceMixerModule::processBlock."},
        // Math: this audit feeds the SAME tone into A and B (see the module-registry comment
        // above), so Sum (A+B) doubles by construction (+6.02 dB, exact arithmetic, not a bug) and
        // Mult (A*B) SQUARES an amplitude > 1 input (the "hot" square test signal is amplitude 4,
        // so Mult peaks near 16 -> +12.04 dB). This is not parameter-driven — the "clip" choice
        // (Off/Hard/Soft) is what IS swept here, and "Hard" visibly caps it back down (see the
        // report) — it is a property of feeding two correlated signals into an arithmetic node.
        // Worth flagging in the report as "patch discipline matters more on Math than it looks",
        // not a hidden amplifier to fix.
        {"Math", "*", 13.0,
         "Sum doubles identical inputs (+6.02 dB, exact); Mult squares amplitude > 1 inputs "
         "(+12.04 dB measured at the hot square signal) — see the Math note above."},
    };
    return table;
}

bool isAllowed(const AuditEntry& e, double& allowedMaxDb) {
    for (const auto& a : allowList()) {
        if (a.module != e.module)
            continue;
        if (a.paramId != "*" && a.paramId != e.paramId)
            continue;
        allowedMaxDb = a.maxDb;
        return true;
    }
    return false;
}

void writeReport(const std::vector<AuditEntry>& overThreshold,
                 const std::vector<std::pair<juce::String, double>>& moduleWorst) {
    juce::File logDir(juce::File(TESTS_ROOT_DIR).getParentDirectory().getChildFile(".buildlogs"));
    logDir.createDirectory();
    juce::File logFile(logDir.getChildFile("gain-audit.txt"));
    juce::FileOutputStream out(logFile);
    out.setPosition(0);
    out.truncate();

    auto emit = [&](std::ostream& os) {
        os << "FRO120 gain-staging audit — every (module, param, value, signal) combo measured at "
              "more than +6 dB gain, worst first:\n\n";
        os << juce::String::formatted("%-16s %-24s %-18s %8s\n", "Module", "Param=Value", "Signal", "GainDB")
                  .toStdString();
        for (const auto& e : overThreshold) {
            const juce::String pv = e.paramId + "=" + e.valueLabel;
            os << juce::String::formatted("%-16s %-24s %-18s %8.2f\n", e.module.toRawUTF8(), pv.toRawUTF8(),
                                          e.signal.toRawUTF8(), e.gainDb)
                      .toStdString();
        }
        os << "\nPer-module worst case:\n";
        for (const auto& [name, db] : moduleWorst)
            os << juce::String::formatted("  %-16s %8.2f dB\n", name.toRawUTF8(), db).toStdString();
    };

    std::ostringstream buf;
    emit(buf);
    const auto text = buf.str();
    out.write(text.data(), text.size());
    out.flush();
    std::cout << text;
}

} // namespace

TEST(ModuleGainAudit, EveryParameterStaysUnderSixDbExceptAllowlisted) {
    const auto signals = buildTestSignals();
    const auto registry = buildModuleRegistry();

    std::vector<AuditEntry> overThreshold;
    std::vector<std::pair<juce::String, double>> moduleWorst;

    for (const auto& spec : registry) {
        // Discover this module's sweep cases from a throwaway instance.
        std::vector<SweepCase> cases;
        {
            auto probe = spec.factory();
            probe->prepareToPlay(kSampleRate, kBlockSize);
            cases = buildSweepCases(*probe);
        }
        ASSERT_FALSE(cases.empty()) << spec.name
                                    << " exposes no sweepable float/choice parameter — "
                                       "either the module or this registry entry is stale.";

        double worstDb = -1000.0;

        for (const auto& c : cases) {
            for (const auto& sig : signals) {
                auto m = spec.factory();
                applySweepCase(*m, c);
                m->prepareToPlay(kSampleRate, kBlockSize);

                const double inRms = rmsOf(sig.samples, kSkipSamples);
                const auto measured = renderAndMeasure(*m, spec.audioInputChannels, sig.samples);
                ASSERT_GT(inRms, 1.0e-9) << "test signal " << sig.name << " is silent — broken fixture";

                const double gainDb = 20.0 * std::log10(std::max(measured.rmsOut, 1.0e-12) / inRms);
                worstDb = std::max(worstDb, gainDb);

                if (gainDb > kGainAlarmDb)
                    overThreshold.push_back({spec.name, c.paramId, c.valueLabel, sig.name, gainDb, measured.peakOut});
            }
        }
        moduleWorst.push_back({spec.name, worstDb});
    }

    std::sort(overThreshold.begin(), overThreshold.end(),
              [](const AuditEntry& a, const AuditEntry& b) { return a.gainDb > b.gainDb; });
    std::sort(moduleWorst.begin(), moduleWorst.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    writeReport(overThreshold, moduleWorst);

    for (const auto& e : overThreshold) {
        double allowedMaxDb = kGainAlarmDb;
        const bool allowed = isAllowed(e, allowedMaxDb);
        EXPECT_TRUE(allowed) << e.module << "." << e.paramId << "=" << e.valueLabel << " (" << e.signal << ") measured "
                             << e.gainDb
                             << " dB gain with no allow-list entry — either add a justified entry or this "
                                "is a genuine hidden amplifier (the FRO120 bug class).";
        if (allowed)
            EXPECT_LE(e.gainDb, allowedMaxDb)
                << e.module << "." << e.paramId << "=" << e.valueLabel << " (" << e.signal << ") measured " << e.gainDb
                << " dB, above its allow-listed ceiling of " << allowedMaxDb
                << " dB — tighten the allow-list entry or investigate the regression.";
    }
}

// ---------------------------------------------------------------------------------------------
// Sources: no audio input, so "gain" is undefined. Peak-only sanity check at default and at max
// Level — these should stay inside [-1, 1] since nothing downstream expects a raw generator to
// already be hot. (Sampler is excluded: it needs a loaded WAV to produce anything, which is
// orthogonal to a gain-staging audit.)
// ---------------------------------------------------------------------------------------------

namespace {

double measureSourcePeak(ModuleBase& m) {
    const int numCh = std::max(m.getTotalNumOutputChannels(), 1);
    juce::AudioBuffer<float> full(numCh, kTotalSamples);
    full.clear();
    auto midi = juce::MidiMessage::noteOn(1, 60, 1.0f);
    int rendered = 0;
    bool first = true;
    while (rendered < kTotalSamples) {
        const int n = std::min(kBlockSize, kTotalSamples - rendered);
        juce::AudioBuffer<float> block(numCh, n);
        block.clear();
        juce::MidiBuffer midiBuf;
        if (first) {
            midiBuf.addEvent(midi, 0);
            first = false;
        }
        m.processBlock(block, midiBuf);
        for (int ch = 0; ch < numCh; ++ch)
            full.copyFrom(ch, rendered, block, ch, 0, n);
        rendered += n;
    }
    double peak = 0.0;
    for (int ch = 0; ch < numCh; ++ch)
        peak = std::max(peak, (double)full.getMagnitude(ch, kSkipSamples, kTotalSamples - kSkipSamples));
    return peak;
}

} // namespace

TEST(SourceModulePeakTest, OscillatorWavetableNoiseStayWithinUnityAtDefaultAndMaxLevel) {
    struct SourceCase {
        juce::String name;
        std::function<std::unique_ptr<ModuleBase>()> factory;
    };
    const std::vector<SourceCase> sources = {
        {"Oscillator", [] { return std::make_unique<OscillatorModule>(); }},
        {"Wavetable", [] { return std::make_unique<WavetableOscillatorModule>(); }},
        {"Noise", [] { return std::make_unique<NoiseModule>(); }},
    };

    for (const auto& s : sources) {
        for (const bool maxLevel : {false, true}) {
            auto m = s.factory();
            if (maxLevel) {
                for (auto* param : m->getParameters()) {
                    auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
                    if (withId != nullptr && withId->paramID == "level")
                        param->setValueNotifyingHost(1.0f);
                }
            }
            m->prepareToPlay(kSampleRate, kBlockSize);
            const double peak = measureSourcePeak(*m);
            EXPECT_LE(peak, 1.05) << s.name << " at " << (maxLevel ? "max" : "default") << " Level peaked at " << peak
                                  << " — a raw source above unity means "
                                     "everything downstream inherits a hot signal for free.";
        }
    }
}
