// The unison a new Oscillator or Wavetable starts with (Detune 35 cents, Blend 75 %, Width 100 %): raising Unison
// from 1 to 7 must be heard at once, and a saved project, preset or patch must keep the sound it was stored with.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/OscillatorModule.h"
#include "Modules/UnisonVoicing.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace {

constexpr double kUnisonDefaultsRate = 48000.0;
constexpr int kUnisonDefaultsBlock = 512;
constexpr int kUnisonDefaultsFftOrder = 17;
constexpr int kUnisonDefaultsFftSize = 1 << kUnisonDefaultsFftOrder;

float unisonDefaultsActual(juce::AudioProcessor& m, const char* id) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    return p != nullptr ? p->convertFrom0to1(p->getValue()) : -12345.0f;
}

void unisonDefaultsSet(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

struct UnisonDefaultsStack {
    double centreShare = 0.0; ///< share of the mid power within 4 cents of the root's partials (1 = a single tone)
    double correlation = 0.0; ///< L/R correlation (1 = mono)
    double sideMid = 0.0;     ///< RMS of L-R over RMS of L+R
};

// Holds A3 on a module that reads its note from MIDI and measures both legs after a settling stretch.
UnisonDefaultsStack measureUnisonDefaults(juce::AudioProcessor& m, int numChannels, int rightChannel) {
    m.prepareToPlay(kUnisonDefaultsRate, kUnisonDefaultsBlock);
    juce::AudioBuffer<float> buf(numChannels, kUnisonDefaultsBlock);
    const int warm = 40;
    const int blocks = warm + kUnisonDefaultsFftSize / kUnisonDefaultsBlock;
    std::vector<float> l, r;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
        m.processBlock(buf, midi);
        if (b < warm)
            continue;
        l.insert(l.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kUnisonDefaultsBlock);
        r.insert(r.end(), buf.getReadPointer(rightChannel), buf.getReadPointer(rightChannel) + kUnisonDefaultsBlock);
    }

    UnisonDefaultsStack s;
    double ll = 0, rr = 0, lr = 0, side = 0, mid = 0;
    std::vector<float> fft(2 * (size_t)kUnisonDefaultsFftSize, 0.0f);
    for (int i = 0; i < kUnisonDefaultsFftSize; ++i) {
        const double a = l[(size_t)i], c = r[(size_t)i];
        ll += a * a;
        rr += c * c;
        lr += a * c;
        side += (a - c) * (a - c);
        mid += (a + c) * (a + c);
        const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / (kUnisonDefaultsFftSize - 1));
        fft[(size_t)i] = (float)(0.5 * (a + c) * w);
    }
    s.correlation = lr / std::max(std::sqrt(ll * rr), 1e-30);
    s.sideMid = std::sqrt(side / std::max(mid, 1e-30));
    juce::dsp::FFT(kUnisonDefaultsFftOrder).performRealOnlyForwardTransform(fft.data());
    double centre = 0.0, total = 0.0;
    for (int bin = 1; bin < kUnisonDefaultsFftSize / 2; ++bin) {
        const double hz = bin * kUnisonDefaultsRate / kUnisonDefaultsFftSize;
        const double power = (double)fft[(size_t)(2 * bin)] * fft[(size_t)(2 * bin)] +
                             (double)fft[(size_t)(2 * bin + 1)] * fft[(size_t)(2 * bin + 1)];
        for (int k = 1; k <= 4; ++k) {
            const double cents = std::abs(1200.0 * std::log2(hz / (220.0 * k)));
            if (cents < 4.0)
                centre += power;
            if (cents < 80.0)
                total += power;
        }
    }
    s.centreShare = total > 0.0 ? centre / total : 0.0;
    return s;
}

UnisonDefaultsStack unisonDefaultsOscillator(int unison) {
    OscillatorModule m;
    unisonDefaultsSet(m, "waveform", 2.0f); // Saw
    unisonDefaultsSet(m, "unison", (float)unison);
    return measureUnisonDefaults(m, OscillatorModule::kNumOutputs, OscillatorModule::kRightBase);
}

UnisonDefaultsStack unisonDefaultsWavetable(int unison) {
    WavetableOscillatorModule m;
    unisonDefaultsSet(m, "unison", (float)unison);
    return measureUnisonDefaults(m, WavetableOscillatorModule::kNumOutputs, WavetableOscillatorModule::kRightBase);
}

juce::var stripStoredKeys(const juce::var& json, std::initializer_list<const char*> keys) {
    juce::var copy = juce::JSON::parse(juce::JSON::toString(json));
    auto* params = copy["nodes"][0]["params"].getDynamicObject();
    for (const char* key : keys)
        params->removeProperty(key);
    return copy;
}

std::unique_ptr<juce::AudioProcessorGraph> loadTrusted(const juce::var& json) {
    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, *graph, /*clearExisting=*/true, /*trusted=*/true));
    return graph;
}

juce::AudioProcessor& firstNode(juce::AudioProcessorGraph& graph) {
    return *graph.getNodes().getFirst()->getProcessor();
}

} // namespace

TEST(UnisonDefaults, NewModulesStartWithTheMeasuredReferenceStack) {
    OscillatorModule osc;
    WavetableOscillatorModule wt;
    for (juce::AudioProcessor* m :
         {static_cast<juce::AudioProcessor*>(&osc), static_cast<juce::AudioProcessor*>(&wt)}) {
        EXPECT_NEAR(unisonDefaultsActual(*m, "detune"), 35.0f, 0.01f);
        EXPECT_NEAR(unisonDefaultsActual(*m, "blend"), 0.75f, 1e-4f);
        EXPECT_NEAR(unisonDefaultsActual(*m, "width"), 1.0f, 1e-4f);
        EXPECT_NEAR(unisonDefaultsActual(*m, "unison"), 1.0f, 1e-4f) << "Unison itself still starts at one voice";
    }
    // Rand starts full so the stack's voices open apart on every note instead of pulsing in phase.
    EXPECT_NEAR(unisonDefaultsActual(osc, "randomPhase"), 100.0f, 1e-3f);
    EXPECT_NEAR(unisonDefaultsActual(wt, "randomPhase"), 1.0f, 1e-4f);
}

// Regression test for FRO1226: a new Oscillator at Unison 7 sounded no different from Unison 1 (Detune 0, Width 0).
TEST(UnisonDefaults, DefaultOscillatorUnisonSevenIsHeardAsSpectralSpreadAndSideEnergy) {
    const auto one = unisonDefaultsOscillator(1);
    const auto seven = unisonDefaultsOscillator(7);
    EXPECT_GT(one.centreShare, 0.95) << "one voice is a single tone";
    EXPECT_NEAR(one.correlation, 1.0, 1e-6);
    EXPECT_LT(seven.centreShare, one.centreShare - 0.15) << "the stack spreads the spectrum around each partial";
    EXPECT_LT(seven.correlation, 0.3) << "the voices are panned apart";
    EXPECT_GT(seven.sideMid, 0.5) << "and carry real side energy";
}

TEST(UnisonDefaults, DefaultWavetableUnisonSevenIsHeardAsSpectralSpreadAndSideEnergy) {
    const auto one = unisonDefaultsWavetable(1);
    const auto seven = unisonDefaultsWavetable(7);
    EXPECT_GT(one.centreShare, 0.95);
    EXPECT_NEAR(one.correlation, 1.0, 1e-6);
    EXPECT_LT(seven.centreShare, one.centreShare - 0.15);
    EXPECT_LT(seven.correlation, 0.3);
    EXPECT_GT(seven.sideMid, 0.5);
}

TEST(UnisonDefaults, SavedProjectKeepsItsStoredUnisonDefaults) {
    // Stored values that equal the OLD defaults, and values that equal neither, both load as stored.
    for (const auto& stored :
         {std::array<float, 4>{0.0f, 0.25f, 0.0f, 0.0f}, std::array<float, 4>{12.0f, 0.5f, 0.3f, 0.4f}}) {
        juce::AudioProcessorGraph source;
        source.addNode(std::make_unique<OscillatorModule>());
        source.addNode(std::make_unique<WavetableOscillatorModule>());
        for (auto* node : source.getNodes()) {
            unisonDefaultsSet(*node->getProcessor(), "detune", stored[0]);
            unisonDefaultsSet(*node->getProcessor(), "blend", stored[1]);
            unisonDefaultsSet(*node->getProcessor(), "width", stored[2]);
            unisonDefaultsSet(*node->getProcessor(), "randomPhase", stored[3]);
        }
        const auto loaded = loadTrusted(synth::AIStateMapper::graphToJSON(source));
        ASSERT_EQ(loaded->getNumNodes(), 2);
        for (auto* node : loaded->getNodes()) {
            auto& m = *node->getProcessor();
            EXPECT_NEAR(unisonDefaultsActual(m, "detune"), stored[0], 0.05f) << m.getName();
            EXPECT_NEAR(unisonDefaultsActual(m, "blend"), stored[1], 1e-4f) << m.getName();
            EXPECT_NEAR(unisonDefaultsActual(m, "width"), stored[2], 1e-4f) << m.getName();
            EXPECT_NEAR(unisonDefaultsActual(m, "randomPhase"), stored[3], 1e-3f) << m.getName();
        }
    }
}

TEST(UnisonDefaults, AProjectMissingTheKeysKeepsTheOldDefaults) {
    for (const char* type : {"Oscillator", "Wavetable"}) {
        juce::AudioProcessorGraph source;
        if (juce::String(type) == "Oscillator")
            source.addNode(std::make_unique<OscillatorModule>());
        else
            source.addNode(std::make_unique<WavetableOscillatorModule>());
        const auto json = synth::AIStateMapper::graphToJSON(source);
        ASSERT_TRUE(json["nodes"][0]["params"].getDynamicObject()->hasProperty("detune"));

        const auto legacy = loadTrusted(stripStoredKeys(json, {"detune", "blend", "width", "randomPhase"}));
        auto& m = firstNode(*legacy);
        EXPECT_NEAR(unisonDefaultsActual(m, "detune"), 0.0f, 1e-3f) << type;
        EXPECT_NEAR(unisonDefaultsActual(m, "blend"), 1.0f, 1e-4f) << type;
        EXPECT_NEAR(unisonDefaultsActual(m, "width"), 0.0f, 1e-4f) << type;
        EXPECT_NEAR(unisonDefaultsActual(m, "randomPhase"), 0.0f, 1e-4f) << type << ": Rand was 0 before";

        const auto partial = loadTrusted(stripStoredKeys(json, {"width"}));
        EXPECT_NEAR(unisonDefaultsActual(firstNode(*partial), "detune"), 35.0f, 0.05f) << "a stored key is kept";
        EXPECT_NEAR(unisonDefaultsActual(firstNode(*partial), "width"), 0.0f, 1e-4f)
            << "only the missing key goes legacy";
    }
}

// The modules are held on the heap: a Wavetable carries large per-block buffers, and two of them on the stack
// overflow a Windows thread's 1 MB stack.
TEST(UnisonDefaults, BinaryStateMissingTheKeysKeepsTheOldDefaults) {
    auto legacyState = [](juce::AudioProcessor& fresh) {
        juce::MemoryBlock state;
        fresh.getStateInformation(state);
        auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int)state.getSize());
        EXPECT_NE(xml, nullptr);
        juce::MemoryBlock out;
        if (xml != nullptr) {
            for (const char* key : {"detune", "blend", "width", "randomPhase"})
                xml->removeAttribute(key);
            juce::AudioProcessor::copyXmlToBinary(*xml, out);
        }
        return out;
    };
    for (const bool wavetable : {false, true}) {
        auto makeModule = [wavetable]() -> std::unique_ptr<juce::AudioProcessor> {
            if (wavetable)
                return std::make_unique<WavetableOscillatorModule>();
            return std::make_unique<OscillatorModule>();
        };
        const auto state = legacyState(*makeModule());
        const auto target = makeModule();
        target->setStateInformation(state.getData(), (int)state.getSize());
        EXPECT_NEAR(unisonDefaultsActual(*target, "detune"), 0.0f, 1e-3f) << wavetable;
        EXPECT_NEAR(unisonDefaultsActual(*target, "blend"), 1.0f, 1e-4f) << wavetable;
        EXPECT_NEAR(unisonDefaultsActual(*target, "width"), 0.0f, 1e-4f) << wavetable;
        EXPECT_NEAR(unisonDefaultsActual(*target, "randomPhase"), 0.0f, 1e-4f) << wavetable;
    }
}

// A trusted patch (a stored project's own JSON) that leaves the keys out is loaded like a project: it gets the old
// defaults (a mono, centred stack), validation is untouched, and the keys it does carry apply as written.
TEST(UnisonDefaults, ATrustedPatchThatOmitsTheKeysGetsTheOldDefaultsAndOneThatSetsThemKeepsThem) {
    auto parse = [](const char* params) {
        return juce::JSON::parse(juce::String("{\"nodes\":[{\"id\":1,\"type\":\"Oscillator\",\"params\":") + params +
                                 "}],\"connections\":[]}");
    };
    juce::AudioProcessorGraph omitted;
    ASSERT_TRUE(
        synth::AIStateMapper::applyJSONToGraph(parse("{\"waveform\":\"Saw\",\"unison\":5}"), omitted, true, true));
    auto& a = firstNode(omitted);
    EXPECT_NEAR(unisonDefaultsActual(a, "detune"), 0.0f, 1e-3f);
    EXPECT_NEAR(unisonDefaultsActual(a, "blend"), 1.0f, 1e-4f);
    EXPECT_NEAR(unisonDefaultsActual(a, "width"), 0.0f, 1e-4f);
    EXPECT_NEAR(unisonDefaultsActual(a, "randomPhase"), 0.0f, 1e-4f);

    juce::AudioProcessorGraph written;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(
        parse("{\"unison\":5,\"detune\":35,\"blend\":0.75,\"width\":1.0,\"randomPhase\":100}"), written, true, true));
    auto& b = firstNode(written);
    EXPECT_NEAR(unisonDefaultsActual(b, "detune"), 35.0f, 0.5f);
    EXPECT_NEAR(unisonDefaultsActual(b, "blend"), 0.75f, 0.01f);
    EXPECT_NEAR(unisonDefaultsActual(b, "width"), 1.0f, 0.01f);
    EXPECT_NEAR(unisonDefaultsActual(b, "randomPhase"), 100.0f, 0.5f);
}

namespace {

juce::var unisonPatch(const char* type, const char* params) {
    return juce::JSON::parse(juce::String("{\"nodes\":[{\"id\":1,\"type\":\"") + type + "\",\"params\":" + params +
                             "}],\"connections\":[]}");
}

std::unique_ptr<juce::AudioProcessorGraph> loadPatch(const juce::var& json, bool trusted) {
    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, *graph, /*clearExisting=*/true, trusted));
    return graph;
}

} // namespace

// Regression test for FRO1361: a model-authored node never recorded Blend, Width or Rand, so it takes the module's
// current defaults for them (a wide stack); a trusted node keeps the legacy values. Detune 40 is kept either way.
TEST(UnisonDefaults, AModelAuthoredNodeTakesTheCurrentDefaultsForTheKeysItOmits) {
    for (const char* type : {"Oscillator", "Wavetable"}) {
        const auto patch = unisonPatch(type, "{\"unison\":7,\"detune\":40}");
        const bool osc = juce::String(type) == "Oscillator";
        const float freshRand = osc ? 100.0f : 1.0f;

        const auto untrusted = loadPatch(patch, /*trusted=*/false);
        auto& u = firstNode(*untrusted);
        EXPECT_NEAR(unisonDefaultsActual(u, "unison"), 7.0f, 1e-4f) << type;
        EXPECT_NEAR(unisonDefaultsActual(u, "detune"), 40.0f, 0.05f) << type;
        EXPECT_NEAR(unisonDefaultsActual(u, "blend"), 0.75f, 1e-4f) << type;
        EXPECT_NEAR(unisonDefaultsActual(u, "width"), 1.0f, 1e-4f) << type;
        EXPECT_NEAR(unisonDefaultsActual(u, "randomPhase"), freshRand, 1e-3f) << type;

        const auto trusted = loadPatch(patch, /*trusted=*/true);
        auto& t = firstNode(*trusted);
        EXPECT_NEAR(unisonDefaultsActual(t, "detune"), 40.0f, 0.05f) << type;
        EXPECT_NEAR(unisonDefaultsActual(t, "blend"), 1.0f, 1e-4f) << type;
        EXPECT_NEAR(unisonDefaultsActual(t, "width"), 0.0f, 1e-4f) << type;
        EXPECT_NEAR(unisonDefaultsActual(t, "randomPhase"), 0.0f, 1e-4f) << type;
    }
}

// Rendered, not heard: the same Unison 7 patch is clearly less correlated left to right when a model wrote it.
TEST(UnisonDefaults, AModelAuthoredUnisonSevenRendersWiderThanATrustedOne) {
    const auto patch = unisonPatch("Oscillator", "{\"waveform\":\"Saw\",\"unison\":7,\"detune\":40}");
    const auto untrusted = loadPatch(patch, false);
    const auto trusted = loadPatch(patch, true);
    const auto wide =
        measureUnisonDefaults(firstNode(*untrusted), OscillatorModule::kNumOutputs, OscillatorModule::kRightBase);
    const auto narrow =
        measureUnisonDefaults(firstNode(*trusted), OscillatorModule::kNumOutputs, OscillatorModule::kRightBase);
    EXPECT_LT(wide.correlation, 0.3);
    EXPECT_GT(narrow.correlation, 0.95) << "Width 0 keeps the legacy stack centred";
    EXPECT_LT(wide.correlation, narrow.correlation - 0.5);
}
