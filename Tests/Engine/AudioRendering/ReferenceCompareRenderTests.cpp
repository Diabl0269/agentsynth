// Black-box A/B render harness: the reference synth (loaded through our own hosting layer) against AgentSynth's
// Oscillator. Gated on AGENTSYNTH_REFERENCE_COMPARE_DIR (output folder) and AGENTSYNTH_REFERENCE_PLUGIN (path of the
// reference synth's VST3) so CI, which has neither, skips it. Comparison tooling, not a regression test.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/OscillatorModule.h"
#include "Plugin/Hosting/HostedPluginBackend.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

namespace {

juce::String referencePath() { return juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_REFERENCE_PLUGIN", {}); }

juce::File outDir() {
    const auto dir = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_REFERENCE_COMPARE_DIR", {});
    return dir.isEmpty() || referencePath().isEmpty() ? juce::File() : juce::File(dir);
}

std::unique_ptr<synth::HostedPluginModule> loadHosted(const juce::String& path, double sr, int bs,
                                                      synth::DefaultHostedPluginBackend& backend) {
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, path);
    if (found.isEmpty())
        return nullptr;
    auto module = std::make_unique<synth::HostedPluginModule>();
    module->prepareToPlay(sr, bs);
    module->loadPlugin(*found[0], backend);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!module->hasInstance() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    return module->hasInstance() ? std::move(module) : nullptr;
}

//==============================================================================
// Shared case description
//==============================================================================

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

struct NoteSpan {
    int on = 0;
    int off = 0;
    int pitch = 60;
};

int samples(double seconds) { return (int)std::lround(seconds * kSr); }

void addMidiForBlock(juce::MidiBuffer& midi, const std::vector<NoteSpan>& notes, int pos, int n) {
    for (const auto& note : notes) {
        if (note.off >= pos && note.off < pos + n)
            midi.addEvent(juce::MidiMessage::noteOff(1, note.pitch, 0.0f), note.off - pos);
        if (note.on >= pos && note.on < pos + n)
            midi.addEvent(juce::MidiMessage::noteOn(1, note.pitch, (juce::uint8)127), note.on - pos);
    }
}

void writeWav(const juce::File& file, const juce::AudioBuffer<float>& buffer) {
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), kSr, 2, 32, {}, 0));
    ASSERT_NE(writer, nullptr);
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

//==============================================================================
// AgentSynth side: Poly MIDI -> Oscillator (poly) -> [Filter] -> VCA, ADSR gating the VCA, Audio Output
//==============================================================================

struct AgentSetup {
    std::map<juce::String, double> osc; // paramID -> denormalised value; "waveform" 0 Sine 1 Square 2 Saw 3 Triangle
    std::map<juce::String, double> adsr;
    std::map<juce::String, double> filter; // used only when useFilter
    bool useFilter = false;
    bool wavetable = false; // Wavetable Oscillator module (default table) instead of Oscillator
};

void setAny(juce::AudioProcessor& proc, const juce::String& id, double value) {
    for (auto* param : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged != nullptr && ranged->paramID == id) {
            ranged->setValueNotifyingHost(ranged->convertTo0to1((float)value));
            return;
        }
    FAIL() << "no parameter " << id << " on " << proc.getName();
}

juce::var dumpParams(juce::AudioProcessor& proc) {
    auto* obj = new juce::DynamicObject();
    for (auto* param : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param)) {
            auto* entry = new juce::DynamicObject();
            entry->setProperty("value", ranged->convertFrom0to1(ranged->getValue()));
            entry->setProperty("text", ranged->getText(ranged->getValue(), 100));
            obj->setProperty(ranged->paramID, juce::var(entry));
        }
    return juce::var(obj);
}

juce::AudioBuffer<float> renderAgent(const AgentSetup& setup, const std::vector<NoteSpan>& notes, int total,
                                     juce::var& settingsOut) {
    using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;
    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(0, 2, kSr, kBlock);

    auto midiIn = graph.addNode(std::make_unique<IO>(IO::midiInputNode));
    auto out = graph.addNode(std::make_unique<IO>(IO::audioOutputNode));
    auto poly = graph.addNode(synth::AIStateMapper::createModule("Poly MIDI"));
    auto osc = graph.addNode(synth::AIStateMapper::createModule(setup.wavetable ? "Wavetable" : "Oscillator"));
    // Audio R fan: Oscillator 14-21, Wavetable 23-30.
    const int oscRight = setup.wavetable ? 23 : 14;
    auto adsr = graph.addNode(synth::AIStateMapper::createModule("ADSR"));
    auto vca = graph.addNode(synth::AIStateMapper::createModule("VCA"));
    auto filter = setup.useFilter ? graph.addNode(synth::AIStateMapper::createModule("Filter")) : nullptr;

    // Poly flips the port layout, so it goes first; the rest are plain settings.
    for (auto* node : {osc.get(), adsr.get(), vca.get(), filter.get()})
        if (node != nullptr)
            setAny(*node->getProcessor(), "poly", 1.0);
    for (const auto& [id, v] : setup.osc)
        setAny(*osc->getProcessor(), id, v);
    for (const auto& [id, v] : setup.adsr)
        setAny(*adsr->getProcessor(), id, v);
    for (const auto& [id, v] : setup.filter)
        if (filter != nullptr)
            setAny(*filter->getProcessor(), id, v);

    graph.addConnection({{midiIn->nodeID, kMidi}, {poly->nodeID, kMidi}});
    graph.addConnection({{midiIn->nodeID, kMidi}, {osc->nodeID, kMidi}});
    graph.addConnection({{midiIn->nodeID, kMidi}, {adsr->nodeID, kMidi}});
    auto wire = [&](auto from, int fromCh, auto to, int toCh) {
        EXPECT_TRUE(graph.addConnection({{from->nodeID, fromCh}, {to->nodeID, toCh}}));
    };
    // Poly MIDI: Pitch fan 0-7 and Gate fan 8-15. Oscillator Audio L fan 0-7, Audio R fan 14-21. Filter Audio R in/out
    // 11-18. VCA: audio L 0-7, gain CV 8-15, audio R 16-23; output L is ch0 and R is ch16.
    for (int v = 0; v < 8; ++v) {
        wire(poly, v, osc, v);
        wire(poly, 8 + v, adsr, v);
        wire(poly, 8 + v, osc, 20 + v);
        wire(adsr, v, vca, 8 + v);
        if (filter != nullptr) {
            wire(osc, v, filter, v);
            wire(osc, oscRight + v, filter, 11 + v);
            wire(filter, v, vca, v);
            wire(filter, 11 + v, vca, 16 + v);
        } else {
            wire(osc, v, vca, v);
            wire(osc, oscRight + v, vca, 16 + v);
        }
    }
    wire(vca, 0, out, 0);
    wire(vca, 16, out, 1);
    graph.prepareToPlay(kSr, kBlock);

    auto* settings = new juce::DynamicObject();
    settings->setProperty("oscillator", dumpParams(*osc->getProcessor()));
    settings->setProperty("adsr", dumpParams(*adsr->getProcessor()));
    settings->setProperty("vca", dumpParams(*vca->getProcessor()));
    if (filter != nullptr)
        settings->setProperty("filter", dumpParams(*filter->getProcessor()));
    settingsOut = juce::var(settings);

    juce::AudioBuffer<float> result(2, total);
    result.clear();
    for (int pos = 0; pos < total; pos += kBlock) {
        const int n = std::min(kBlock, total - pos);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        addMidiForBlock(midi, notes, pos, n);
        graph.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom(ch, pos, block, ch, 0, n);
    }
    graph.releaseResources();
    return result;
}

//==============================================================================
// Reference synth side
//==============================================================================

class ReferenceRig {
public:
    ReferenceRig(synth::DefaultHostedPluginBackend& backend)
        : module_(loadHosted(referencePath(), kSr, kBlock, backend)) {
        if (module_ != nullptr)
            inst_ = module_->getActiveInstanceForEditor();
    }
    bool ok() const { return inst_ != nullptr; }

    juce::AudioProcessorParameter* find(const juce::String& name) const {
        for (auto* p : inst_->getParameters())
            if (p->getName(100) == name)
                return p;
        return nullptr;
    }
    static double numeric(const juce::String& text) { return text.retainCharacters("0123456789.-").getDoubleValue(); }

    void record(const juce::String& name, float norm, const juce::String& how) {
        auto* p = find(name);
        auto* entry = new juce::DynamicObject();
        entry->setProperty("param", name);
        entry->setProperty("index", p->getParameterIndex());
        entry->setProperty("normalised", norm);
        entry->setProperty("text_readback", p->getText(norm, 100).trim());
        entry->setProperty("how", how);
        log_.add(juce::var(entry));
    }
    void setNorm(const juce::String& name, float norm, const juce::String& how = "normalised") {
        auto* p = find(name);
        ASSERT_NE(p, nullptr) << name;
        p->setValue(norm);
        record(name, norm, how);
    }
    // The normalised value whose displayed text is numerically closest to `target`.
    void setNumeric(const juce::String& name, double target) {
        auto* p = find(name);
        ASSERT_NE(p, nullptr) << name;
        float best = 0.0f;
        double bestDiff = 1.0e300;
        constexpr int kSteps = 8192;
        for (int i = 0; i < kSteps; ++i) {
            const float v = (float)i / (kSteps - 1);
            const double d = std::abs(numeric(p->getText(v, 100)) - target);
            if (d < bestDiff - 1.0e-9) {
                bestDiff = d;
                best = v;
            }
        }
        p->setValue(best);
        record(name, best, "text search for numeric " + juce::String(target));
    }
    void setText(const juce::String& name, const juce::String& wanted) {
        auto* p = find(name);
        ASSERT_NE(p, nullptr) << name;
        constexpr int kSteps = 4096;
        for (int i = 0; i < kSteps; ++i) {
            const float v = (float)i / (kSteps - 1);
            if (p->getText(v, 100).trim() == wanted) {
                p->setValue(v);
                record(name, v, "text search for \"" + wanted + "\"");
                return;
            }
        }
        FAIL() << "no value of " << name << " reads \"" << wanted << "\"";
    }
    juce::var log() const { return juce::var(log_); }

    juce::AudioBuffer<float> render(const std::vector<NoteSpan>& notes, int total, bool warm = true) {
        const int channels = std::max(module_->getTotalNumInputChannels(), module_->getTotalNumOutputChannels());
        if (warm)
            for (int i = 0; i < 16; ++i) {
                juce::AudioBuffer<float> silent(channels, kBlock);
                silent.clear();
                juce::MidiBuffer none;
                module_->processBlock(silent, none);
            }
        juce::AudioBuffer<float> result(2, total);
        result.clear();
        for (int pos = 0; pos < total; pos += kBlock) {
            const int n = std::min(kBlock, total - pos);
            juce::AudioBuffer<float> block(channels, kBlock);
            block.clear();
            juce::MidiBuffer midi;
            addMidiForBlock(midi, notes, pos, n);
            module_->processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch)
                result.copyFrom(ch, pos, block, ch, 0, n);
        }
        return result;
    }

private:
    std::unique_ptr<synth::HostedPluginModule> module_;
    juce::AudioPluginInstance* inst_ = nullptr;
    juce::Array<juce::var> log_;
};

// Level of `freq` in the left channel over [from, to), by the Goertzel recurrence.
double toneLevel(const juce::AudioBuffer<float>& b, int from, int to, double freq) {
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / kSr;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    const float* x = b.getReadPointer(0);
    for (int i = from; i < to; ++i) {
        const double s = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) * 2.0 / (to - from);
}

struct Case {
    juce::String name;
    std::vector<NoteSpan> notes;
    int total = 0;
    AgentSetup agent;
    std::function<void(ReferenceRig&)> reference;
    bool useReferenceFilter = false;
    juce::String referenceSkipReason; // non-empty: no reference file is rendered for this case
};

std::vector<NoteSpan> single(int pitch) { return {{samples(0.5), samples(2.5), pitch}}; }
std::vector<NoteSpan> chord() {
    std::vector<NoteSpan> notes;
    for (int pitch : {48, 55, 60, 63})
        notes.push_back({samples(0.5), samples(2.5), pitch});
    return notes;
}

// Renders both synths for one case into `dir`; false when the reference synth did not load.
bool renderCase(const juce::File& dir, synth::DefaultHostedPluginBackend& backend, const Case& c, juce::var& out) {
    auto* entry = new juce::DynamicObject();
    // AgentSynth
    juce::var agentSettings;
    const auto agentBuf = renderAgent(c.agent, c.notes, c.total, agentSettings);
    writeWav(dir.getChildFile("agentsynth_" + c.name + ".wav"), agentBuf);
    entry->setProperty("agentsynth_overrides", [&] {
        auto* o = new juce::DynamicObject();
        for (const auto* group : {&c.agent.osc, &c.agent.adsr, &c.agent.filter})
            for (const auto& [id, v] : *group)
                o->setProperty(id, v);
        return juce::var(o);
    }());
    entry->setProperty("agentsynth_all_params", agentSettings);
    // Reference synth
    ReferenceRig reference(backend);
    if (!reference.ok())
        return false;
    if (c.referenceSkipReason.isNotEmpty()) {
        entry->setProperty("reference_not_rendered", c.referenceSkipReason);
    } else {
        c.reference(reference);
        writeWav(dir.getChildFile("reference_" + c.name + ".wav"), reference.render(c.notes, c.total));
        entry->setProperty("reference_params_set", reference.log());
    }
    entry->setProperty("notes", [&] {
        juce::Array<juce::var> arr;
        for (const auto& n : c.notes) {
            auto* o = new juce::DynamicObject();
            o->setProperty("on_s", n.on / kSr);
            o->setProperty("off_s", n.off / kSr);
            o->setProperty("midi", n.pitch);
            arr.add(juce::var(o));
        }
        return juce::var(arr);
    }());
    out = juce::var(entry);
    return true;
}

} // namespace

TEST(ReferenceCompareRender, DumpReferenceParams) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    dir.createDirectory();
    synth::DefaultHostedPluginBackend backend;
    auto module = loadHosted(referencePath(), 48000.0, 256, backend);
    ASSERT_NE(module, nullptr) << "The reference synth did not load";
    auto* inst = module->getActiveInstanceForEditor();
    juce::String out;
    out << "# Reference synth: " << inst->getName() << " ins=" << inst->getTotalNumInputChannels()
        << " outs=" << inst->getTotalNumOutputChannels() << "\n";
    out << "# index | name | default(norm) | text at current(default) | texts at norm 0/.25/.5/.75/1\n";
    for (auto* p : inst->getParameters()) {
        out << p->getParameterIndex() << " | " << p->getName(100) << " | " << p->getDefaultValue() << " | "
            << p->getText(p->getValue(), 100) << " (cur " << p->getValue() << ") | ";
        for (float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            out << p->getText(v, 100) << " ; ";
        out << "\n";
    }
    if (auto* type = [&]() -> juce::AudioProcessorParameter* {
            for (auto* p : inst->getParameters())
                if (p->getName(100) == "Fil Type")
                    return p;
            return nullptr;
        }()) {
        out << "# Fil Type value list (step i of 95 -> text)\n";
        for (int i = 0; i < 96; ++i)
            out << i << " = " << type->getText((float)i / 95.0f, 100) << "\n";
    }
    EXPECT_TRUE(dir.getChildFile("reference_params.txt").replaceWithText(out));
}

TEST(ReferenceCompareRender, RenderAB) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    dir.createDirectory();
    synth::DefaultHostedPluginBackend backend;
    const int total = samples(3.5);

    // Which wavetable positions are a sine and a saw in the init patch's wavetable: probed by tone ratios at A4.
    float sineNorm = 0.0f, sawNorm = 0.0f; // only for the probe table
    {
        ReferenceRig probe(backend);
        ASSERT_TRUE(probe.ok());
        juce::String table = "# frame | norm | H2/H1 | H3/H1 | H1\n";
        double bestSine = 1.0e9, bestSaw = 1.0e9;
        int sineFrame = 0, sawFrame = 0;
        for (int frame = 1; frame <= 256; ++frame) {
            const float norm = (float)(frame - 1) / 255.0f;
            probe.setNorm("A WTPos", norm);
            probe.setNorm("A RandPhase", 0.0f);
            probe.setNorm("A Phase", 0.0f);
            const auto b = probe.render({{0, samples(0.5), 69}}, samples(0.6), frame == 1);
            const int from = samples(0.2), to = samples(0.5);
            const double h1 = toneLevel(b, from, to, 440.0), h2 = toneLevel(b, from, to, 880.0),
                         h3 = toneLevel(b, from, to, 1320.0);
            table << frame << " | " << norm << " | " << h2 / std::max(h1, 1e-9) << " | " << h3 / std::max(h1, 1e-9)
                  << " | " << h1 << "\n";
            if (h1 < 1.0e-6)
                continue;
            const double sineScore = (h2 + h3) / h1;
            const double sawScore = std::abs(h2 / h1 - 0.5) + std::abs(h3 / h1 - 1.0 / 3.0);
            if (sineScore < bestSine) {
                bestSine = sineScore;
                sineNorm = norm;
                sineFrame = frame;
            }
            if (sawScore < bestSaw) {
                bestSaw = sawScore;
                sawNorm = norm;
                sawFrame = frame;
            }
        }
        table << "# sine frame " << sineFrame << " (norm " << sineNorm << "), saw frame " << sawFrame << " (norm "
              << sawNorm << ")\n";
        dir.getChildFile("reference_wtpos_probe.txt").replaceWithText(table);
    }

    std::vector<Case> cases;
    auto add = [&](const juce::String& name, std::vector<NoteSpan> notes, int len, AgentSetup agent,
                   std::function<void(ReferenceRig&)> reference, bool filter = false) {
        cases.push_back({name, std::move(notes), len, std::move(agent), std::move(reference), filter});
    };
    auto sawA = [] {
        AgentSetup a;
        a.osc["waveform"] = 2;
        return a;
    };
    auto sineA = [] {
        AgentSetup a;
        a.osc["waveform"] = 0;
        return a;
    };
    // The init patch's wavetable has a single frame and it is a saw (see reference_wtpos_probe.txt), so the saw cases
    // leave the position alone. A sine needs a different wavetable, which no exposed parameter selects.
    auto referenceSaw = [](ReferenceRig&) {};
    auto referenceSine = [](ReferenceRig&) {};
    const juce::String noSine = "The reference synth's init wavetable is a single-frame saw and the wavetable choice "
                                "is not an exposed parameter";
    juce::ignoreUnused(referenceSine);
    auto referenceUnison = [=](ReferenceRig& r, int n) {
        referenceSaw(r);
        r.setNumeric("A Unison", n);
    };

    add("saw_c3", single(48), total, sawA(), referenceSaw);
    add("saw_a4", single(69), total, sawA(), referenceSaw);
    add("saw_c7", single(96), total, sawA(), referenceSaw);
    add("sine_a4", single(69), total, sineA(), referenceSine);
    cases.back().referenceSkipReason = noSine;
    {
        auto a5 = sawA();
        a5.osc["unison"] = 5;
        add("uni5_default_chord", chord(), total, a5, [=](ReferenceRig& r) { referenceUnison(r, 5); });
        auto a7 = sawA();
        a7.osc["unison"] = 7;
        add("uni7_default_chord", chord(), total, a7, [=](ReferenceRig& r) { referenceUnison(r, 7); });
        // The reference synth's default detune reads 0.25 (norm 0.5) and blend 75; AgentSynth detune is cents with norm
        // 0.5 = 25 cents.
        auto m = a7;
        m.osc["detune"] = 25.0;
        m.osc["blend"] = 0.75;
        add("uni7_matched_chord", chord(), total, m, [=](ReferenceRig& r) { referenceUnison(r, 7); });
        auto mw = m;
        mw.osc["width"] = 1.0;
        add("uni7_matched_width_chord", chord(), total, mw, [=](ReferenceRig& r) { referenceUnison(r, 7); });
    }
    add("uni1_chord", chord(), total, sawA(), [=](ReferenceRig& r) { referenceUnison(r, 1); });

    // Env cases: four 200 ms notes 300 ms apart, phase pinned on both synths.
    std::vector<NoteSpan> four;
    for (int i = 0; i < 4; ++i)
        four.push_back({samples(0.5 + 0.3 * i), samples(0.7 + 0.3 * i), 69});
    const int envLen = samples(2.5);
    auto envAgent = [&](AgentSetup a, double phase) {
        a.osc["phase"] = phase;
        a.osc["randomPhase"] = 0.0;
        a.adsr = {{"attack", 0.001}, {"decay", 0.0}, {"sustain", 1.0}, {"release", 0.015}};
        return a;
    };
    auto envReference = [=](float wtNorm, double phase) {
        return [=](ReferenceRig& r) {
            r.setNumeric("A RandPhase", 0.0);
            r.setNumeric("A Phase", phase);
            // Env time is 32 s * x^5: 1 ms attack, 0 decay, 0 dB sustain, release left at its 15 ms default.
            r.setNorm("Env1 Atk", std::pow(0.001f / 32.0f, 0.2f), "time = 32 s * x^5, 1 ms");
            r.setNorm("Env1 Dec", 0.0f);
            r.setNorm("Env1 Sus", 1.0f, "0 dB (100%)");
        };
    };
    add("env_a1_r15_sine", four, envLen, envAgent(sineA(), 0.0), envReference(0, 0.0));
    cases.back().referenceSkipReason = noSine;
    add("env_a1_r15_sine_p270", four, envLen, envAgent(sineA(), 270.0), envReference(0, 270.0));
    cases.back().referenceSkipReason = noSine;
    add("env_a1_r15_saw", four, envLen, envAgent(sawA(), 0.0), envReference(0, 0.0));

    for (int res : {0, 50, 90}) {
        AgentSetup a = sawA();
        a.useFilter = true;
        a.filter = {{"filterType", 0}, {"cutoff", 1000.0}, {"resonance", res / 100.0}};
        add("filter_lp24_r" + juce::String(res), single(36), total, a, [=](ReferenceRig& r) {
            referenceSaw(r);
            r.setNorm("Filter On", 1.0f);
            r.setText("Fil Type", "MG Low 24");
            r.setNumeric("Fil Cutoff", 1000.0);
            r.setNumeric("Fil Reso", res);
        });
    }
    add("noise_floor", {}, samples(3.0), sawA(), [](ReferenceRig&) {});

    auto* settingsAll = new juce::DynamicObject();
    for (const auto& c : cases) {
        juce::var entry;
        ASSERT_TRUE(renderCase(dir, backend, c, entry)) << "The reference synth failed to load for " << c.name;
        settingsAll->setProperty(c.name, entry);
    }
    auto* top = new juce::DynamicObject();
    top->setProperty("sample_rate", kSr);
    top->setProperty("block_size", kBlock);
    top->setProperty("midi_velocity", 127);
    top->setProperty("cases", juce::var(settingsAll));
    EXPECT_TRUE(dir.getChildFile("settings.json").replaceWithText(juce::JSON::toString(juce::var(top))));
}

// Extra renders: the reference synth sine from the Sub oscillator, and the alias sweep. Merges into the existing
// settings.json.
TEST(ReferenceCompareRender, RenderEnvSweep) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    synth::DefaultHostedPluginBackend backend;
    std::vector<NoteSpan> sweep;
    for (int i = 0; i < 16; ++i)
        sweep.push_back({samples(0.5 + 0.3 * i), samples(0.7 + 0.3 * i), 60 + i});
    // The 1 ms attack is the case compared everywhere; the longer attacks show how much of the onset's broadband
    // content belongs to the attack ramp.
    for (const double attack : {0.001, 0.002, 0.004}) {
        AgentSetup env;
        env.osc["waveform"] = 0;
        env.osc["phase"] = 0.0;
        env.osc["randomPhase"] = 0.0;
        env.adsr = {{"attack", attack}, {"decay", 0.0}, {"sustain", 1.0}, {"release", 0.015}};
        const juce::String name =
            attack == 0.001 ? "env_sweep_sine" : "env_sweep_sine_a" + juce::String(juce::roundToInt(attack * 1000.0));
        Case c{name,
               sweep,
               samples(5.6),
               env,
               [=](ReferenceRig& r) {
                   r.setNorm("Osc A On", 0.0f);
                   r.setNorm("Osc S On", 1.0f);
                   r.setText("SubOscShape", "Sine");
                   r.setNumeric("SubOscOctave", 0.0);
                   r.setNumeric("Sub Osc Level", 100.0);
                   r.setNorm("Env1 Atk", std::pow(static_cast<float>(attack) / 32.0f, 0.2f), "time = 32 s * x^5");
                   r.setNorm("Env1 Dec", 0.0f);
                   r.setNorm("Env1 Sus", 1.0f, "0 dB (100%)");
               },
               false,
               {}};
        juce::var entry;
        ASSERT_TRUE(renderCase(dir, backend, c, entry));
    }
}

TEST(ReferenceCompareRender, RenderExtra) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    synth::DefaultHostedPluginBackend backend;
    const auto settingsFile = dir.getChildFile("settings.json");
    juce::var top = juce::JSON::parse(settingsFile);
    ASSERT_TRUE(top.isObject()) << "run RenderAB first";
    auto* casesObj = top.getProperty("cases", {}).getDynamicObject();
    ASSERT_NE(casesObj, nullptr);

    std::vector<Case> cases;
    // Reference synth sine from the Sub oscillator (shape Sine, octave 0, level 100) with Osc A off. The Sub has no
    // phase control.
    auto referenceSub = [](ReferenceRig& r) {
        r.setNorm("Osc A On", 0.0f);
        r.setNorm("Osc S On", 1.0f);
        r.setText("SubOscShape", "Sine");
        r.setNumeric("SubOscOctave", 0.0);
        r.setNumeric("Sub Osc Level", 100.0);
    };
    {
        AgentSetup sine;
        sine.osc["waveform"] = 0;
        cases.push_back({"sine_a4", single(69), samples(3.5), sine, referenceSub, false, {}});
        std::vector<NoteSpan> four;
        for (int i = 0; i < 4; ++i)
            four.push_back({samples(0.5 + 0.3 * i), samples(0.7 + 0.3 * i), 69});
        AgentSetup env = sine;
        env.osc["phase"] = 0.0;
        env.osc["randomPhase"] = 0.0;
        env.adsr = {{"attack", 0.001}, {"decay", 0.0}, {"sustain", 1.0}, {"release", 0.015}};
        cases.push_back({"env_a1_r15_sine",
                         four,
                         samples(2.5),
                         env,
                         [=](ReferenceRig& r) {
                             referenceSub(r);
                             r.setNorm("Env1 Atk", std::pow(0.001f / 32.0f, 0.2f), "time = 32 s * x^5, 1 ms");
                             r.setNorm("Env1 Dec", 0.0f);
                             r.setNorm("Env1 Sus", 1.0f, "0 dB (100%)");
                         },
                         false,
                         {}});
    }

    // Unison stack shape: one low-partial note, long enough to resolve each voice's frequency.
    {
        const std::vector<NoteSpan> held{{samples(0.5), samples(10.5), 69}};
        for (int voices : {3, 5, 7}) {
            AgentSetup a;
            a.osc["waveform"] = 2;
            a.osc["unison"] = voices;
            a.osc["detune"] = 25.0;
            a.osc["blend"] = 0.75;
            a.osc["width"] = 1.0;
            cases.push_back({"stack_" + juce::String(voices),
                             held,
                             samples(11.0),
                             a,
                             [=](ReferenceRig& r) { r.setNumeric("A Unison", voices); },
                             false,
                             {}});
        }
    }

    // Alias sweep: one note, 1.5 s held, no unison, rand phase pinned to 0 on both synths.
    const std::vector<NoteSpan> none;
    for (int midi : {60, 72, 84, 96, 108}) {
        const std::vector<NoteSpan> note{{samples(0.5), samples(2.0), midi}};
        const int len = samples(3.0);
        for (const auto& [wave, index] : {std::pair<const char*, int>{"saw", 2}, {"square", 1}, {"triangle", 3}}) {
            AgentSetup a;
            a.osc["waveform"] = index;
            a.osc["randomPhase"] = 0.0;
            a.osc["unison"] = 1;
            const juce::String name = juce::String("alias_") + wave + "_" + juce::String(midi);
            const bool isSaw = juce::String(wave) == "saw";
            cases.push_back(
                {name, note, len, a,
                 [=](ReferenceRig& r) {
                     if (isSaw)
                         r.setNumeric("A RandPhase", 0.0);
                 },
                 false,
                 isSaw ? juce::String()
                       : juce::String("The reference synth has only the init saw table (no wavetable choice)")});
        }
        AgentSetup wt;
        wt.wavetable = true;
        wt.osc["randomPhase"] = 0.0;
        wt.osc["unison"] = 1;
        cases.push_back({"alias_wt_" + juce::String(midi), note, len, wt, [](ReferenceRig&) {}, false,
                         "The reference synth has no counterpart for the Wavetable module case"});
    }
    for (const auto& c : cases) {
        juce::var entry;
        ASSERT_TRUE(renderCase(dir, backend, c, entry)) << c.name;
        casesObj->setProperty(c.name, entry);
    }
    EXPECT_TRUE(settingsFile.replaceWithText(juce::JSON::toString(top)));
}

// Unison settings grid against the reference synth's init unison: which of Detune, Rand and Phase closes the gap.
TEST(ReferenceCompareRender, RenderUnisonGrid) {
    const auto dir = outDir();
    if (dir == juce::File())
        GTEST_SKIP() << "AGENTSYNTH_REFERENCE_COMPARE_DIR / AGENTSYNTH_REFERENCE_PLUGIN not set";
    dir.createDirectory();
    synth::DefaultHostedPluginBackend backend;
    for (double detune : {25.0, 35.0})
        for (double rand : {0.0, 100.0}) {
            AgentSetup a;
            a.osc["waveform"] = 2;
            a.osc["unison"] = 7;
            a.osc["detune"] = detune;
            a.osc["blend"] = 0.75;
            a.osc["width"] = 1.0;
            a.osc["randomPhase"] = rand;
            a.osc["phase"] = 180.0;
            const juce::String name = "grid_d" + juce::String((int)detune) + "_r" + juce::String((int)rand);
            Case c{name, chord(), samples(3.5), a, [](ReferenceRig& r) { r.setNumeric("A Unison", 7); }, false, {}};
            juce::var entry;
            ASSERT_TRUE(renderCase(dir, backend, c, entry)) << c.name;
        }
}
