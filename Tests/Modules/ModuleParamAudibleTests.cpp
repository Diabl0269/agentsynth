// The "param-not-heard" guard (docs: bug-classes/param-not-heard.md): every continuous parameter of
// every factory module must change the sound. For each module type, each float parameter and each
// VARIANT of that module (a state in which its knobs have something to act on, set before
// prepareToPlay): render a reference from fixed noise bursts plus a note, move the parameter after
// prepareToPlay to either end and a middle value, render again, and call the parameter heard when
// any variant's output changes. A parameter heard in no variant fails, unless it is in kSilentByDesign
// with a reason that can be checked; an allow-list entry that some variant does hear fails as stale.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/Lfo/LfoCustomShapes.h"
#include "Modules/ModuleBase.h"
#include "Modules/SamplerModule.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kBlocks = 64;
constexpr int kReleaseBlock = 32; // the note off, and the end of any held CV

using Setup = std::function<void(juce::AudioProcessor&)>;

/** One state a module is rendered in. Settings go in before prepareToPlay; the knob under test moves after. */
struct Variant {
    std::string name;
    Setup setup;
    std::vector<std::pair<int, float>> held; // raw input channel -> constant (a patched CV or Gate) until the release
    bool audio = true;                       // noise bursts on the audio inputs
    bool note = true;                        // MIDI notes held until kReleaseBlock
    std::vector<int> notes{57};              // which notes (a Drum Kit needs one per pad)
};

/** "<Module type>/<parameter id>" -> why no variant can make moving it audible, with where to check. */
const std::map<std::string, std::string> kSilentByDesign = {};

/** A host that is playing, so tempo-synced modules run. */
class TestHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setBpm(120.0);
        info.setIsPlaying(true);
        info.setPpqPosition(ppq);
        return info;
    }
    double ppq = 0.0;
};

/** Sets a parameter to a plain value (a choice index, a bool as 0/1); a missing id is a typo in this file. */
void set(juce::AudioProcessor& m, const char* id, float plain) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(plain));
}

Variant plain(const char* name, Setup setup) { return Variant{name, std::move(setup), {}, true, true, {57}}; }

/** A short noisy WAV in the temp folder, for the Sampler and the Drum Kit's file pads. */
juce::File testWav() {
    static const juce::File file = [] {
        auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("param-audible-test.wav");
        f.deleteFile();
        juce::AudioBuffer<float> data(2, 22050);
        juce::Random rng(7);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < data.getNumSamples(); ++i)
                data.setSample(ch, i,
                               0.4f * std::sin(0.05f * (float)i * (1.0f + 0.3f * (float)ch)) *
                                       (1.0f - (float)i / (float)data.getNumSamples()) +
                                   0.05f * (rng.nextFloat() - 0.5f));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(f.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), kRate, 2, 24, {}, 0));
        if (writer != nullptr) {
            stream.release();
            writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples());
        }
        return f;
    }();
    return file;
}

std::vector<Variant> envelopeVariants() {
    std::vector<Variant> v;
    v.push_back(plain("sustain dips", [](juce::AudioProcessor& m) { set(m, "sustain", 0.3f); }));
    Variant gate{"gate CV", {}, {{0, 0.6f}}, false, false, {}};
    v.push_back(gate);
    return v;
}

std::vector<Variant> generatorVariants(const std::string& type) {
    std::vector<Variant> v;
    if (type == "Oscillator") {
        v.push_back(plain("square", [](juce::AudioProcessor& m) { set(m, "waveform", 1); }));
        v.push_back(plain("fixed rate", [](juce::AudioProcessor& m) { set(m, "rateMode", 1); }));
        v.push_back(plain("unison 4", [](juce::AudioProcessor& m) { set(m, "unison", 4); }));
    } else if (type == "Wavetable") {
        v.push_back(plain("unison 4", [](juce::AudioProcessor& m) { set(m, "unison", 4); }));
        v.push_back(plain("warp PWM", [](juce::AudioProcessor& m) { set(m, "warp", 4); }));
    }
    return v;
}

std::vector<Variant> effectVariants(const std::string& type) {
    std::vector<Variant> v;
    if (type == "Bitcrusher")
        v.push_back(plain("crushed", [](juce::AudioProcessor& m) {
            set(m, "depth", 6);
            set(m, "rate", 4);
        }));
    else if (type == "Pitch Shifter") {
        v.push_back(plain("up a fifth", [](juce::AudioProcessor& m) { set(m, "pitch", 7); }));
        v.push_back(plain("frequency mode", [](juce::AudioProcessor& m) {
            set(m, "shiftMode", 1);
            set(m, "shiftHz", 100);
        }));
    } else if (type == "Limiter")
        v.push_back(plain("hot input", [](juce::AudioProcessor& m) { set(m, "ceiling", -12); }));
    else if (type == "Filter") {
        v.push_back(plain("custom type", [](juce::AudioProcessor& m) { set(m, "filterType", 7); }));
        v.push_back(Variant{"pitch patched",
                            [](juce::AudioProcessor& m) { set(m, "keyTrack", 50); },
                            {{FilterModule::kPitchBase, 880.0f}},
                            true,
                            true,
                            {57}});
    } else if (type == "Parametric EQ") {
        v.push_back(plain("bands on, +12 dB", [](juce::AudioProcessor& m) {
            for (int b = 1; b <= 4; ++b) {
                set(m, ("band" + juce::String(b) + "On").toRawUTF8(), 1);
                set(m, ("band" + juce::String(b) + "Gain").toRawUTF8(), 12);
            }
        }));
        // Slots 5-24 are off by default like every slot; on, their knobs are heard.
        v.push_back(plain("slots 5-24 on, +12 dB", [](juce::AudioProcessor& m) {
            for (int b = 5; b <= 24; ++b) {
                set(m, ("band" + juce::String(b) + "On").toRawUTF8(), 1);
                set(m, ("band" + juce::String(b) + "Gain").toRawUTF8(), 12);
            }
        }));
    }
    return v;
}

std::vector<Variant> controlVariants(const std::string& type) {
    std::vector<Variant> v;
    if (type == "VCA")
        v.push_back(Variant{"CV patched high", {}, {{1, 1.0f}}, true, true, {57}});
    else if (type == "Noise")
        v.push_back(Variant{"no input", {}, {}, false, true, {57}}); // its two jacks are Color and Level CV
    else if (type == "Sample & Hold") {
        v.push_back(plain("sampling the input", [](juce::AudioProcessor& m) {
            set(m, "source", 0);
            set(m, "rate", 40);
        }));
        v.push_back(plain("external clock", [](juce::AudioProcessor& m) {
            set(m, "source", 0);
            set(m, "clock", 1);
        }));
    } else if (type == "Macros")
        v.push_back(plain("16 macros", [](juce::AudioProcessor& m) { set(m, "macroCount", 16); }));
    else if (type == "LFO") {
        v.push_back(plain("Hz mode", [](juce::AudioProcessor& m) { set(m, "mode", 0); }));
        v.push_back(plain("sample and hold", [](juce::AudioProcessor& m) {
            set(m, "mode", 0);
            set(m, "shape", 4);
            set(m, "rateHz", 20);
            dynamic_cast<LFOModule&>(m).seedRandom(7); // the same steps every render
        }));
        v.push_back(plain("custom, two shapes", [](juce::AudioProcessor& m) {
            set(m, "mode", 0);
            set(m, "shape", LFOModule::kCustomShapeIndex);
            synth::LfoCustomShapes list;
            list.shapes.push_back(
                {"low", synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::RampUp), {0.1f, 0.1f}});
            list.shapes.push_back(
                {"high", synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::RampDown), {0.9f, 0.9f}});
            dynamic_cast<LFOModule&>(m).setCustomShapes(list);
        }));
        v.push_back(plain("Hz, retrig", [](juce::AudioProcessor& m) {
            set(m, "mode", 0);
            set(m, "retrig", 1);
        }));
        for (auto& variant : v)
            variant.audio = false; // its first three jacks are Rate, Level and Glide CV: noise there is modulation
    }
    return v;
}

std::vector<Variant> hostedVariants(const std::string& type) {
    std::vector<Variant> v;
    if (type == "Sequencer" || type == "Poly Sequencer")
        v.push_back(plain("running", [](juce::AudioProcessor& m) {
            auto& seq = dynamic_cast<synth::SequencerModuleBase&>(m);
            auto pattern = seq.getPattern();
            pattern.seed = 1; // a fixed seed, so Chance rolls the same dice every render
            seq.setPattern(pattern);
            set(m, "run", 1);
        }));
    else if (type == "Channel Strip")
        v.push_back(plain("four sends", [](juce::AudioProcessor& m) {
            for (int i = 0; i < 4; ++i)
                dynamic_cast<ChannelStripModule&>(m).addSend();
        }));
    else if (type == "Sampler") {
        v.push_back(plain("sample",
                          [](juce::AudioProcessor& m) { dynamic_cast<SamplerModule&>(m).loadSampleFile(testWav()); }));
        v.push_back(plain("granular", [](juce::AudioProcessor& m) {
            dynamic_cast<SamplerModule&>(m).loadSampleFile(testWav());
            set(m, "playMode", 1);
            set(m, "spray", 0); // no spray: the grain positions are not random, so the render repeats
        }));
        Variant instrument = plain("instrument, two notes", [](juce::AudioProcessor& m) {
            dynamic_cast<SamplerModule&>(m).loadInstrumentFromFiles({testWav().getFullPathName()});
        });
        instrument.notes = {57, 64}; // the second note glides from the first
        v.push_back(instrument);
    } else if (type == "Drum Kit") {
        Variant all =
            plain("all 32 pads", [](juce::AudioProcessor& m) { dynamic_cast<DrumKitModule&>(m).setPadCount(32); });
        Variant files = plain("32 file pads", [](juce::AudioProcessor& m) {
            auto& kit = dynamic_cast<DrumKitModule&>(m);
            kit.setPadCount(32);
            for (int i = 0; i < 32; ++i)
                kit.setPadFile(i, testWav());
        });
        all.notes.clear();
        const auto table = synth::drums::defaultPadTable();
        for (const auto& pad : table.pads)
            all.notes.push_back(pad.note);
        files.notes = all.notes;
        // A closed or pedal hat in the same block chokes the open hat, so the open hat also sounds alone.
        Variant openHat = all;
        openHat.notes.clear();
        for (const auto& pad : table.pads)
            if (pad.sound == 46)
                openHat.notes.push_back(pad.note);
        v.push_back(all);
        v.push_back(files);
        v.push_back(openHat);
    }
    return v;
}

/** The default state first, then the extra states this module needs. */
std::vector<Variant> variantsFor(const std::string& type) {
    std::vector<Variant> v{plain("default", {})};
    for (auto extra :
         type == "ADSR" || type == "Amp Env" || type == "Filter Env" ? envelopeVariants() : std::vector<Variant>{})
        v.push_back(std::move(extra));
    for (auto* group : {&generatorVariants, &effectVariants, &controlVariants, &hostedVariants})
        for (auto extra : (*group)(type))
            v.push_back(std::move(extra));
    return v;
}

/** Renders kBlocks blocks and returns every output sample of every channel. */
std::vector<float> render(juce::AudioProcessor& proc, const Variant& variant, const std::vector<float>& noise) {
    auto* m = dynamic_cast<ModuleBase*>(&proc);
    const int channels = std::max(proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    const int right = m->rightAudioLegChannel();
    TestHead head;
    proc.setPlayHead(&head);
    juce::AudioBuffer<float> buf(channels, kBlock);
    std::vector<float> out;
    out.reserve((size_t)channels * kBlock * kBlocks);
    std::vector<float> events; // kept apart, so the channel blocks stay aligned for the statistics
    for (int b = 0; b < kBlocks; ++b) {
        buf.clear();
        // Bursts, so gates and limiters open and close. Audio goes on raw ch0, the right leg and any port
        // declared Audio, never on a jack declared Gate, CV, Pitch or Sidechain (it would read as modulation).
        const bool burst = variant.audio && (b / 4) % 2 == 0;
        for (int ch = 0; burst && ch < proc.getTotalNumInputChannels(); ++ch) {
            const PortRole role = m->mapInputChannel(ch).role;
            const bool audioJack = role == PortRole::Audio || ((ch == 0 || ch == right) && role == PortRole::Other);
            for (int i = 0; audioJack && i < kBlock; ++i)
                buf.setSample(ch, i, noise[(size_t)((b * kBlock + i + ch * 7) % (int)noise.size())]);
        }
        for (const auto& [ch, value] : variant.held)
            if (b < kReleaseBlock && ch < channels)
                juce::FloatVectorOperations::fill(buf.getWritePointer(ch), value, kBlock);
        juce::MidiBuffer midi;
        for (const int note : variant.note ? variant.notes : std::vector<int>{}) {
            if (b == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
            if (b == kReleaseBlock)
                midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        }
        proc.processBlock(buf, midi);
        // What a module emits as MIDI (the sequencers) is its output too.
        for (const auto meta : midi) {
            const auto* raw = meta.data;
            events.push_back((float)meta.samplePosition * 1.0e-3f); // scaled: events must not swamp the statistics
            for (int k = 0; k < 3; ++k)
                events.push_back(k < meta.numBytes ? (float)raw[k] * 1.0e-3f : 0.0f);
        }
        head.ppq += kBlock / kRate * 2.0; // 120 bpm
        for (int ch = 0; ch < channels; ++ch)
            out.insert(out.end(), buf.getReadPointer(ch), buf.getReadPointer(ch) + kBlock);
    }
    proc.setPlayHead(nullptr);
    out.insert(out.end(), events.begin(), events.end());
    return out;
}

std::unique_ptr<juce::AudioProcessor> prepared(const juce::String& type, const Variant& variant) {
    auto proc = synth::AIStateMapper::createModule(type);
    if (proc == nullptr)
        return proc;
    proc->setPlayConfigDetails(proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels(), kRate, kBlock);
    if (variant.setup)
        variant.setup(*proc);
    proc->prepareToPlay(kRate, kBlock);
    return proc;
}

float maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float d = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}

/** Level and roughness (RMS of the first difference) of a render: what a module with random internals still repeats. */
struct Stats {
    double rms = 0.0, rough = 0.0;
};

Stats statsOf(const std::vector<float>& x) {
    Stats st;
    for (size_t i = 0; i < x.size(); ++i) {
        st.rms += (double)x[i] * x[i];
        if (i % kBlock != 0) // within one channel's block, not across the join to the next
            st.rough += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    }
    const double n = std::max<double>(1.0, (double)x.size());
    st.rms = std::sqrt(st.rms / n);
    st.rough = std::sqrt(st.rough / n);
    return st;
}

double logRatio(double a, double b) { return std::abs(std::log((a + 1.0e-6) / (b + 1.0e-6))); }

/** A variant's reference render. A module that does not reproduce its own render (random internals) is judged on
    Stats instead, against how far two untouched renders already differ. */
struct Reference {
    std::vector<float> out;
    bool deterministic = true;
    double rmsJitter = 0.0, roughJitter = 0.0;
};

Reference referenceFor(const juce::String& type, const Variant& variant, const std::vector<float>& noise) {
    Reference r;
    auto a = prepared(type, variant);
    auto b = prepared(type, variant);
    r.out = render(*a, variant, noise);
    const auto second = render(*b, variant, noise);
    r.deterministic = maxDiff(r.out, second) == 0.0f;
    if (r.deterministic)
        return r;
    // Random internals: how far untouched renders drift from this one, over several of them.
    for (int k = 0; k < 6; ++k) {
        auto extra = prepared(type, variant);
        const auto again = statsOf(render(*extra, variant, noise)), mine = statsOf(r.out);
        r.rmsJitter = std::max(r.rmsJitter, logRatio(mine.rms, again.rms));
        r.roughJitter = std::max(r.roughJitter, logRatio(mine.rough, again.rough));
    }
    return r;
}

bool differs(const Reference& ref, const std::vector<float>& moved) {
    if (ref.deterministic)
        return maxDiff(ref.out, moved) > 1.0e-6f;
    const Stats before = statsOf(ref.out), after = statsOf(moved);
    return logRatio(before.rms, after.rms) > std::max(0.02, 6.0 * ref.rmsJitter) ||
           logRatio(before.rough, after.rough) > std::max(0.02, 6.0 * ref.roughJitter);
}

/** True when moving `id` after prepareToPlay changes the output of `variant`. */
bool heardIn(const juce::String& type, const juce::String& id, const Variant& variant, const Reference& ref,
             const std::vector<float>& noise) {
    // Either end or a middle value: a knob parked at one end has the other to move to, and 0 and 1 can be the same
    // point (a phase of 0 and 360 degrees).
    for (const float to : {0.0f, 1.0f, 0.37f}) {
        auto m = prepared(type, variant);
        auto* param = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(m.get(), id));
        param->setValueNotifyingHost(to);
        const auto moved = render(*m, variant, noise);
        if (differs(ref, moved))
            return true;
    }
    return false;
}

std::vector<float> testNoise() {
    juce::Random rng(12345);
    std::vector<float> noise(8192);
    for (auto& s : noise)
        s = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);
    return noise;
}

} // namespace

TEST(ModuleParamAudible, EveryContinuousParameterOfEveryModuleChangesTheSound) {
    const auto noise = testNoise();
    int checked = 0;
    for (const auto& [key, reason] : kSilentByDesign) {
        EXPECT_FALSE(reason.empty()) << key << ": an allow-list entry needs a reason";
        EXPECT_NE(key.substr(key.size() - 2), "/*") << key << ": allow-list single parameters, never a whole module";
    }
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto probe = synth::AIStateMapper::createModule(type);
        if (probe == nullptr || dynamic_cast<ModuleBase*>(probe.get()) == nullptr)
            continue;
        std::vector<juce::String> ids;
        for (auto* p : probe->getParameters())
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p))
                ids.push_back(f->paramID);

        const auto variants = variantsFor(type.toStdString());
        std::vector<Reference> refs;
        for (const auto& v : variants)
            refs.push_back(referenceFor(type, v, noise));

        for (const auto& id : ids) {
            ++checked;
            std::string heardBy, tried;
            for (size_t i = 0; i < variants.size() && heardBy.empty(); ++i) {
                if (heardIn(type, id, variants[i], refs[i], noise))
                    heardBy = variants[i].name;
                tried += (tried.empty() ? "" : ", ") + variants[i].name + (refs[i].deterministic ? "" : " (random)");
            }
            const std::string key = (type + "/" + id).toStdString();
            const bool listed = kSilentByDesign.count(key) > 0;
            if (heardBy.empty() && !listed)
                ADD_FAILURE()
                    << key
                    << ": moving this parameter after prepareToPlay did not change the output in any state (tried: "
                    << tried << ")";
            if (!heardBy.empty() && listed)
                ADD_FAILURE() << key << ": heard in state '" << heardBy << "', so its allow-list entry is stale";
        }
    }
    EXPECT_GT(checked, 300) << "the enumeration must reach every module's continuous parameters";
}
