// Shared rig of the black-box A/B render harness (ReferenceCompare*RenderTests.cpp): the reference synth loaded through
// our own hosting layer, AgentSynth's Oscillator graph, and the case/render plumbing. Test-only, included by the
// ReferenceCompare render tests alone.
#pragma once

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

namespace referencecompare {

inline juce::String referencePath() {
    return juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_REFERENCE_PLUGIN", {});
}

inline juce::File outDir() {
    const auto dir = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_REFERENCE_COMPARE_DIR", {});
    return dir.isEmpty() || referencePath().isEmpty() ? juce::File() : juce::File(dir);
}

inline std::unique_ptr<synth::HostedPluginModule> loadHosted(const juce::String& path, double sr, int bs,
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

inline int samples(double seconds) { return (int)std::lround(seconds * kSr); }

inline void addMidiForBlock(juce::MidiBuffer& midi, const std::vector<NoteSpan>& notes, int pos, int n) {
    for (const auto& note : notes) {
        if (note.off >= pos && note.off < pos + n)
            midi.addEvent(juce::MidiMessage::noteOff(1, note.pitch, 0.0f), note.off - pos);
        if (note.on >= pos && note.on < pos + n)
            midi.addEvent(juce::MidiMessage::noteOn(1, note.pitch, (juce::uint8)127), note.on - pos);
    }
}

inline void writeWav(const juce::File& file, const juce::AudioBuffer<float>& buffer) {
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

inline void setAny(juce::AudioProcessor& proc, const juce::String& id, double value) {
    for (auto* param : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged != nullptr && ranged->paramID == id) {
            ranged->setValueNotifyingHost(ranged->convertTo0to1((float)value));
            return;
        }
    FAIL() << "no parameter " << id << " on " << proc.getName();
}

inline juce::var dumpParams(juce::AudioProcessor& proc) {
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

inline juce::AudioBuffer<float> renderAgent(const AgentSetup& setup, const std::vector<NoteSpan>& notes, int total,
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
inline double toneLevel(const juce::AudioBuffer<float>& b, int from, int to, double freq) {
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

inline std::vector<NoteSpan> single(int pitch) { return {{samples(0.5), samples(2.5), pitch}}; }
inline std::vector<NoteSpan> chord() {
    std::vector<NoteSpan> notes;
    for (int pitch : {48, 55, 60, 63})
        notes.push_back({samples(0.5), samples(2.5), pitch});
    return notes;
}

// Renders both synths for one case into `dir`; false when the reference synth did not load.
inline bool renderCase(const juce::File& dir, synth::DefaultHostedPluginBackend& backend, const Case& c,
                       juce::var& out) {
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

} // namespace referencecompare
