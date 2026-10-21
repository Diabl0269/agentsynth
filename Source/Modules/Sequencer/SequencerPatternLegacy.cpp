#include "SequencerPatternLegacy.h"
#include "SequencerPatternParse.h"

namespace synth {

namespace {

constexpr int kLegacySteps = 8;

const juce::var* find(const juce::NamedValueSet& params, const juce::String& key) {
    return params.getVarPointer(juce::Identifier(key));
}

// The old chord list was a choice parameter: a model wrote its name (any case) or its index.
bool readLegacyChord(const juce::var& v, SequencerChord& out) {
    if (v.isString()) {
        const auto text = v.toString().trim();
        for (int i = 0; i < kLegacySequencerChordCount; ++i)
            if (chordId((SequencerChord)i).equalsIgnoreCase(text)) {
                out = (SequencerChord)i;
                return true;
            }
        if (!text.containsOnly("0123456789"))
            return false;
    }
    int index = 0;
    if (!sequencer_detail::readInt(v, 0, kLegacySequencerChordCount - 1, index))
        return false;
    out = (SequencerChord)index;
    return true;
}

bool adoptGate(SequencerStep& step, const juce::NamedValueSet& params, const juce::String& key) {
    float gate = 0.0f;
    const auto* v = find(params, key);
    if (v == nullptr || !sequencer_detail::readFloat(*v, 0.1f, 1.0f, gate))
        return false;
    step.gate = gate;
    return true;
}

bool adoptMonoStep(SequencerStep& step, const juce::NamedValueSet& params, int n) {
    bool any = adoptGate(step, params, "Gate " + juce::String(n));
    int pitch = 0;
    if (const auto* v = find(params, "Pitch " + juce::String(n));
        v != nullptr && sequencer_detail::readInt(*v, 0, 127, pitch)) {
        step.on = pitch > 0;
        if (pitch > 0)
            step.pitch = (std::uint8_t)pitch;
        any = true;
    }
    float filter = 0.0f;
    if (const auto* v = find(params, "F.Env " + juce::String(n));
        v != nullptr && sequencer_detail::readFloat(*v, 0.0f, 1.0f, filter)) {
        step.filter = filter;
        any = true;
    }
    return any;
}

bool adoptPolyStep(SequencerStep& step, const juce::NamedValueSet& params, int n) {
    bool any = adoptGate(step, params, "Gate " + juce::String(n));
    const juce::String prefix = "Step " + juce::String(n) + " ";
    int root = 0;
    if (const auto* v = find(params, prefix + "Root"); v != nullptr && sequencer_detail::readInt(*v, 0, 127, root)) {
        step.pitch = (std::uint8_t)root;
        any = true;
    }
    SequencerChord chord = SequencerChord::Unison;
    if (const auto* v = find(params, prefix + "Chord"); v != nullptr && readLegacyChord(*v, chord)) {
        step.chord = chord;
        any = true;
    }
    return any;
}

} // namespace

bool adoptLegacyStepParams(SequencerPattern& pattern, const juce::NamedValueSet& params, bool poly) {
    bool any = false;
    for (int n = 1; n <= kLegacySteps; ++n) {
        auto& step = pattern.steps[(size_t)(n - 1)];
        any = (poly ? adoptPolyStep(step, params, n) : adoptMonoStep(step, params, n)) || any;
    }
    return any;
}

bool isLegacyStepParamId(const juce::String& id, bool poly) {
    for (int n = 1; n <= kLegacySteps; ++n) {
        const juce::String index(n);
        if (id == "Gate " + index)
            return true;
        if (poly ? (id == "Step " + index + " Root" || id == "Step " + index + " Chord")
                 : (id == "Pitch " + index || id == "F.Env " + index))
            return true;
    }
    return false;
}

} // namespace synth
