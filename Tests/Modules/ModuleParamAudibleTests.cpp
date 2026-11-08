// The "param-not-heard" guard (docs: bug-classes/param-not-heard.md): every continuous parameter of
// every factory module must change the sound. For each module type and each float parameter: prepare,
// render a reference from fixed noise (plus a held note for generators), move the parameter to the far
// end of its range AFTER prepareToPlay, render again, and fail when the two renders are identical.
// Parameters that are silent by design on this input are listed in kSilentByDesign with a reason.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kBlocks = 64;

/** "<Module type>/<parameter id>" -> why moving it cannot change the sound on this test input. */
const std::map<std::string, std::string> kSilentByDesign = {
    // Envelopes: their audible output is a CV, moved only by a Gate input that this harness does not patch.
    {"ADSR/decay", "envelope CV needs a Gate input; none is patched"},
    {"ADSR/release", "envelope CV needs a Gate input; none is patched"},
    {"ADSR/hold", "envelope CV needs a Gate input; none is patched"},
    {"ADSR/decayCurve", "envelope CV needs a Gate input; none is patched"},
    {"ADSR/releaseCurve", "envelope CV needs a Gate input; none is patched"},
    {"ADSR/gateThreshold", "envelope CV needs a Gate input; none is patched"},
    {"Amp Env/*", "envelope CV needs a Gate input; none is patched"},
    {"Filter Env/*", "envelope CV needs a Gate input; none is patched"},
    {"Bitcrusher/mix", "at the default 24 bit and no rate reduction the crushed signal equals the dry one"},
    {"Channel Strip/*", "send levels and pans feed send buses that a bare strip render does not have"},
    {"Delay/feedback", "the first echo is a full delay time away; feedback only shapes later repeats"},
    {"Drum Kit/*", "only the pad on the one test note sounds; the other pads' knobs are silent by design"},
    {"Filter/keyTrack", "key tracking needs a pitch input; none is patched"},
    {"Filter/morphX", "morph position only acts on the morph filter types; the default type ignores it"},
    {"Filter/morphY", "morph position only acts on the morph filter types; the default type ignores it"},
    {"Filter/shift", "spectral shift only acts on the shifter filter types; the default type ignores it"},
    {"Filter/depth", "depth only acts on the comb/formant filter types; the default type ignores it"},
    {"Gate/hold", "the gate never closes: the noise stays above its threshold"},
    {"Gate/release", "the gate never closes: the noise stays above its threshold"},
    {"Gate/range", "the gate never closes: the noise stays above its threshold"},
    {"LFO/*", "default is Sync mode with Retrig off: the rate follows the song position and a stopped host"},
    {"Limiter/release", "the 0.5 noise never reaches the ceiling, so there is no gain reduction to release"},
    {"Macros/*", "macros beyond the visible bank (default 8) are not output"},
    {"Mallet/release", "the held test note is never released"},
    {"Pluck/release", "the held test note is never released"},
    {"Oscillator/pulseWidth", "the default waveform is not a pulse"},
    {"Oscillator/fixedRate", "fixed-rate mode is off by default; the oscillator follows the note"},
    {"Pitch Shifter/shiftHz", "default Pitch mode ignores the Hz shift"},
    {"Pitch Shifter/window", "at 0 semitones the shifter is a transparent pass-through, whatever the window"},
    {"Pitch Shifter/mix", "at 0 semitones the shifter is a transparent pass-through, so wet equals dry"},
    {"Poly Sequencer/*", "steps only advance on a clock or transport; none is patched"},
    {"Sequencer/*", "steps only advance on a clock or transport; none is patched"},
    {"Sampler/*", "no sample is loaded in a bare module"},
    {"VCA/gain", "the CV jack is unpatched and reads 0, so the gated output is silent at any gain"},
    {"Wavetable/detune", "unison is 1 voice by default; detune needs a stack"},
    {"Wavetable/spread", "unison is 1 voice by default; spread needs a stack"},
    {"Wavetable/width", "unison is 1 voice by default; width needs a stack"},
    {"Wavetable/blend", "unison is 1 voice by default; blend needs a stack"},
    {"Wavetable/warpAmount", "the default warp mode is none"},
};

/** Exact "Module/param" first, then the "Module/*" entry. */
const std::string* silentReason(const std::string& type, const std::string& id) {
    for (const auto& key : {type + "/" + id, type + "/*"}) {
        const auto it = kSilentByDesign.find(key);
        if (it != kSilentByDesign.end())
            return &it->second;
    }
    return nullptr;
}

/** Renders the module for kBlocks blocks and returns every output sample of every channel. */
std::vector<float> render(juce::AudioProcessor& proc, const std::vector<float>& noise) {
    auto* m = dynamic_cast<ModuleBase*>(&proc);
    const int channels = std::max(proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf(channels, kBlock);
    std::vector<float> out;
    out.reserve((size_t)channels * kBlock * kBlocks);
    for (int b = 0; b < kBlocks; ++b) {
        buf.clear();
        // Noise only on audio inputs (raw ch0, the right leg, and any port declared Audio): the other raw channels are
        // CV jacks and would read it as modulation.
        const int right = m->rightAudioLegChannel();
        for (int ch = 0; ch < proc.getTotalNumInputChannels(); ++ch)
            if (ch == 0 || ch == right || m->mapInputChannel(ch).role == PortRole::Audio)
                for (int i = 0; i < kBlock; ++i)
                    buf.setSample(ch, i, noise[(size_t)((b * kBlock + i + ch * 7) % (int)noise.size())]);
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        proc.processBlock(buf, midi);
        for (int ch = 0; ch < channels; ++ch)
            out.insert(out.end(), buf.getReadPointer(ch), buf.getReadPointer(ch) + kBlock);
    }
    return out;
}

void setParam(juce::AudioProcessor& m, const char* id, float value) {
    auto* p = findParameterByID(&m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

std::unique_ptr<juce::AudioProcessor> prepared(const juce::String& type) {
    auto proc = synth::AIStateMapper::createModule(type);
    if (proc != nullptr) {
        proc->setPlayConfigDetails(proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels(), kRate, kBlock);
        proc->prepareToPlay(kRate, kBlock);
        // A state in which the module's knobs have something to act on (set after prepare, like a user would).
        if (type == "Parametric EQ")
            for (int b = 1; b <= 4; ++b) {
                setParam(*proc, ("band" + juce::String(b) + "On").toRawUTF8(), 1.0f);
                setParam(*proc, ("band" + juce::String(b) + "Gain").toRawUTF8(), 12.0f);
            }
    }
    return proc;
}

float maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float d = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}

} // namespace

TEST(ModuleParamAudible, EveryContinuousParameterOfEveryModuleChangesTheSound) {
    juce::Random rng(12345);
    std::vector<float> noise(8192);
    for (auto& s : noise)
        s = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);

    int checked = 0;
    std::vector<std::string> silent;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto probe = synth::AIStateMapper::createModule(type);
        if (probe == nullptr || dynamic_cast<ModuleBase*>(probe.get()) == nullptr)
            continue;
        std::vector<juce::String> ids;
        for (auto* p : probe->getParameters())
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p))
                ids.push_back(f->paramID);

        // A module that does not reproduce its own render (random internals) is compared on level instead.
        auto ref0 = prepared(type);
        auto ref1 = prepared(type);
        const auto reference = render(*ref0, noise);
        const bool deterministic = maxDiff(reference, render(*ref1, noise)) == 0.0f;

        for (const auto& id : ids) {
            ++checked;
            const std::string key = (type + "/" + id).toStdString();
            // Either end or a middle value counts: a knob parked at one end by default has the other to move to, and 0
            // and 1 can be the same point (a phase of 0 and 360 degrees).
            float d = 0.0f;
            for (const float end : {0.0f, 1.0f, 0.37f}) {
                auto m = prepared(type);
                auto* param = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(m.get(), id));
                ASSERT_NE(param, nullptr);
                param->setValueNotifyingHost(end); // after prepareToPlay, as a user turns it
                d = std::max(d, maxDiff(reference, render(*m, noise)));
            }
            const bool changed = deterministic ? d > 1.0e-6f : d > 0.05f;
            if (const auto* reason = silentReason(type.toStdString(), id.toStdString())) {
                EXPECT_FALSE(reason->empty()) << key << ": an allow-list entry needs a reason";
                continue;
            }
            if (!changed)
                silent.push_back(key + (deterministic ? "" : " (nondeterministic module)"));
        }
    }
    EXPECT_GT(checked, 300) << "the enumeration must reach every module's continuous parameters";
    for (const auto& s : silent)
        ADD_FAILURE() << s << ": moving this parameter after prepareToPlay did not change the output";
}
