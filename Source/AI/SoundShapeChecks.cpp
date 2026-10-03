// SoundShapeChecks.cpp -- the shape checks declared in SoundShapeChecks.h.
//
// Everything is read from the response JSON the way the plan reader sees it: node ids, insert ids and
// envelope ids share one namespace, a modulation names its source and dest by those ids, and a param
// the response leaves out stands at the module's own default (read from a real module, never
// restated here), so "sustain left out" is judged as the 0.7 it would build with.
#include "SoundShapeChecks.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"

#include <cmath>
#include <map>
#include <vector>

namespace synth::soundshape {

namespace {

// Thresholds of the three checks. The pluck ones follow the prompt's sound-design words.
constexpr double kPluckMaxAttack = 0.02;
constexpr double kPluckMaxDecay = 0.5;
constexpr double kPluckMaxSustain = 0.01;
constexpr double kAcidMaxSustain = 0.3;
constexpr double kAcidMinResonanceFraction = 0.6;

using IdMap = std::map<juce::int64, juce::var>; // id -> that node's params object (void when it has none)

bool isEnvelopeType(const juce::String& type) { return type == "ADSR" || type == "Amp Env" || type == "Filter Env"; }

bool readId(const juce::var& v, juce::int64& out) {
    if (v.isInt() || v.isInt64()) {
        out = static_cast<juce::int64>(v);
        return true;
    }
    if (v.isDouble() && std::floor(static_cast<double>(v)) == static_cast<double>(v)) {
        out = static_cast<juce::int64>(static_cast<double>(v));
        return true;
    }
    return false;
}

// `params[id]` when the response set it to a number, else the module's own default.
double valueOf(const char* moduleType, const juce::var& params, const char* id) {
    if (params.isObject() && params.hasProperty(id)) {
        const juce::var v = params.getProperty(id, {});
        if (v.isInt() || v.isInt64() || v.isDouble())
            return static_cast<double>(v);
    }
    auto probe = AIStateMapper::createModule(moduleType);
    if (probe != nullptr)
        for (auto* param : probe->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                ranged != nullptr && ranged->paramID == id)
                return ranged->convertFrom0to1(ranged->getDefaultValue());
    return 0.0;
}

// The raw value `fraction` of the way through a Filter parameter's own range.
double filterRangePoint(const char* paramId, double fraction) {
    auto probe = AIStateMapper::createModule("Filter");
    if (probe != nullptr)
        for (auto* param : probe->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                ranged != nullptr && ranged->paramID == paramId)
                return ranged->convertFrom0to1(static_cast<float>(fraction));
    return 0.0;
}

int filterCutoffChannel() {
    static const int channel = [] {
        auto probe = AIStateMapper::createModule("Filter");
        auto* module = dynamic_cast<ModuleBase*>(probe.get());
        return module != nullptr ? module->modulationChannelForParam("cutoff") : -1;
    }();
    return channel;
}

// Every envelope and Filter a response or patch names by id: ADSR/Filter nodes, and the envelope and
// Filter inserts of its addInstrumentTrack ops. `trackEnvelopes` also keeps the params of every
// track envelope, id or not, for the checks that do not need to address it.
struct Nodes {
    IdMap envelopes, filters;
    std::vector<juce::var> trackEnvelopes;
    std::vector<juce::var> oscillators; // params of every Oscillator: patch nodes and new tracks' instruments
    // A new track's Filter insert id -> that track's envelope id (-1 when the track gave its envelope none).
    // Only the track's own envelope is triggered by its notes, so it is the only valid source for that Filter.
    std::map<juce::int64, juce::int64> insertOwnEnvelope;
};

void collectPatchNodes(const juce::var& root, Nodes& out) {
    auto* nodes = root.getProperty("nodes", {}).getArray();
    if (nodes == nullptr)
        return;
    for (const auto& node : *nodes) {
        juce::int64 id = 0;
        if (!readId(node.getProperty("id", {}), id))
            continue;
        const juce::String type = node.getProperty("type", {}).toString();
        if (isEnvelopeType(type))
            out.envelopes[id] = node.getProperty("params", {});
        else if (type == "Filter")
            out.filters[id] = node.getProperty("params", {});
        else if (type == "Oscillator")
            out.oscillators.push_back(node.getProperty("params", {}));
    }
}

void collectTrackBuilds(const juce::var& root, Nodes& out) {
    auto* ops = root.getProperty("timelineOps", {}).getArray();
    if (ops == nullptr)
        return;
    for (const auto& op : *ops) {
        if (op.getProperty("op", {}).toString() != "addInstrumentTrack")
            continue;
        if (op.getProperty("instrument", {}).toString() == "Oscillator")
            out.oscillators.push_back(op.getProperty("instrumentParams", {}));
        juce::int64 envelopeId = -1;
        if (op.getProperty("instrument", {}).toString() != "Sampler") {
            const juce::var envelope = op.getProperty("envelope", {});
            const juce::var params = envelope.getProperty("params", {});
            out.trackEnvelopes.push_back(params);
            juce::int64 id = 0;
            if (readId(envelope.getProperty("id", {}), id)) {
                out.envelopes[id] = params;
                envelopeId = id;
            }
        }
        if (auto* inserts = op.getProperty("inserts", {}).getArray())
            for (const auto& insert : *inserts) {
                juce::int64 id = 0;
                if (insert.getProperty("type", {}).toString() == "Filter" && readId(insert.getProperty("id", {}), id)) {
                    out.filters[id] = insert.getProperty("params", {});
                    out.insertOwnEnvelope[id] = envelopeId;
                }
            }
    }
}

// The Oscillator's waveform param as the response sets it (a name, or the choice index), else its default.
bool isSawOrSquare(const juce::var& params) {
    const juce::var waveform = params.getProperty("waveform", {});
    if (waveform.isString())
        return waveform.toString().equalsIgnoreCase("Saw") || waveform.toString().equalsIgnoreCase("Square");
    if (waveform.isInt() || waveform.isDouble()) {
        auto probe = AIStateMapper::createModule("Oscillator");
        for (auto* param : probe->getParameters())
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param);
                choice != nullptr && choice->paramID == "waveform") {
                const int index = static_cast<int>(static_cast<double>(waveform));
                return index >= 0 && index < choice->choices.size() &&
                       (choice->choices[index] == "Saw" || choice->choices[index] == "Square");
            }
    }
    return false; // left out: the default is a Sine
}

// One modulation from an envelope onto a Filter's cutoff, with both ends' params.
struct CutoffModulation {
    juce::var envelopeParams, filterParams;
};

bool targetsCutoff(const juce::var& modulation) {
    const juce::var destParam = modulation.getProperty("destParam", {});
    if (destParam.isString())
        return destParam.toString() == "cutoff";
    juce::int64 port = 0;
    return readId(modulation.getProperty("destPort", {}), port) && port == filterCutoffChannel();
}

// All cutoff modulations in `response`, or the reason there are none.
juce::String findCutoffModulations(const juce::var& response, const juce::var& existingPatch,
                                   std::vector<CutoffModulation>& found) {
    Nodes nodes;
    collectPatchNodes(existingPatch, nodes);
    collectPatchNodes(response, nodes);
    collectTrackBuilds(response, nodes);

    auto* modulations = response.getProperty("modulations", {}).getArray();
    if (modulations == nullptr || modulations->isEmpty())
        return "the response has no modulation, so no envelope moves a filter";
    juce::String firstProblem;
    for (const auto& modulation : *modulations) {
        juce::int64 source = 0, dest = 0;
        const bool named =
            readId(modulation.getProperty("source", {}), source) && readId(modulation.getProperty("dest", {}), dest);
        const auto envelope = nodes.envelopes.find(source);
        const auto filter = nodes.filters.find(dest);
        juce::String problem;
        if (!named || envelope == nodes.envelopes.end())
            problem = "a modulation's source is not an envelope (an envelope id or an ADSR node)";
        else if (filter == nodes.filters.end())
            problem = "an envelope modulates something that is not a Filter";
        else if (!targetsCutoff(modulation))
            problem = "an envelope modulates a Filter parameter other than cutoff";
        else if (const auto own = nodes.insertOwnEnvelope.find(dest);
                 own != nodes.insertOwnEnvelope.end() && own->second != source)
            problem = "a new track's Filter is moved by another envelope, not the track's own (give the track's "
                      "envelope an id and use it as the source)";
        if (problem.isNotEmpty()) {
            firstProblem = firstProblem.isEmpty() ? problem : firstProblem;
            continue;
        }
        found.push_back({envelope->second, filter->second});
    }
    return found.empty() ? firstProblem : juce::String();
}

} // namespace

ShapeCheck checkPluck(const juce::var& response) {
    Nodes nodes;
    collectPatchNodes(response, nodes);
    collectTrackBuilds(response, nodes);
    std::vector<juce::var> candidates = nodes.trackEnvelopes;
    // Only the ADSR nodes this response itself adds; the track-envelope ids duplicate trackEnvelopes.
    Nodes patchOnly;
    collectPatchNodes(response, patchOnly);
    for (const auto& [id, params] : patchOnly.envelopes)
        candidates.push_back(params);
    if (candidates.empty())
        return {false, "no envelope in the response (no addInstrumentTrack envelope, no ADSR node)"};

    juce::String closest;
    for (const auto& params : candidates) {
        const double attack = valueOf("ADSR", params, "attack");
        const double decay = valueOf("ADSR", params, "decay");
        const double sustain = valueOf("ADSR", params, "sustain");
        if (sustain <= kPluckMaxSustain && decay <= kPluckMaxDecay && attack <= kPluckMaxAttack)
            return {true, "envelope attack " + juce::String(attack) + ", decay " + juce::String(decay) + ", sustain " +
                              juce::String(sustain)};
        if (closest.isEmpty())
            closest = "envelope attack " + juce::String(attack) + ", decay " + juce::String(decay) + ", sustain " +
                      juce::String(sustain) + " is not plucky (want sustain 0, decay <= 0.5, attack <= 0.02)";
    }
    return {false, closest};
}

ShapeCheck checkFilterEnvelope(const juce::var& response, const juce::var& existingPatch) {
    std::vector<CutoffModulation> found;
    if (const auto problem = findCutoffModulations(response, existingPatch, found); problem.isNotEmpty())
        return {false, problem};
    return {true, "an envelope modulates a Filter's cutoff"};
}

ShapeCheck checkAcid(const juce::var& response, const juce::var& existingPatch) {
    std::vector<CutoffModulation> found;
    if (const auto problem = findCutoffModulations(response, existingPatch, found); problem.isNotEmpty())
        return {false, problem};

    Nodes nodes;
    collectPatchNodes(existingPatch, nodes);
    collectPatchNodes(response, nodes);
    collectTrackBuilds(response, nodes);
    bool sawOrSquare = false;
    for (const auto& params : nodes.oscillators)
        sawOrSquare = sawOrSquare || isSawOrSquare(params);
    if (!sawOrSquare)
        return {false, "no Saw or Square oscillator (set instrumentParams {\"waveform\": \"Saw\"} on the track)"};

    const double minResonance = filterRangePoint("resonance", kAcidMinResonanceFraction);
    juce::String firstProblem;
    for (const auto& match : found) {
        const juce::var filterType = match.filterParams.getProperty("filterType", {});
        const bool lowPass24 =
            filterType.isVoid() || (filterType.isString() && filterType.toString().equalsIgnoreCase("LPF24")) ||
            ((filterType.isInt() || filterType.isDouble()) && static_cast<double>(filterType) == 0.0);
        const double resonance = valueOf("Filter", match.filterParams, "resonance");
        const double sustain = valueOf("ADSR", match.envelopeParams, "sustain");
        juce::String problem;
        if (!lowPass24)
            problem = "the filter is not the 24 dB low-pass (filterType LPF24)";
        else if (resonance < minResonance)
            problem = "filter resonance " + juce::String(resonance) + " is below " + juce::String(minResonance);
        else if (sustain > kAcidMaxSustain)
            problem = "envelope sustain " + juce::String(sustain) + " is above " + juce::String(kAcidMaxSustain);
        else
            return {true,
                    "LPF24, resonance " + juce::String(resonance) + ", envelope sustain " + juce::String(sustain)};
        firstProblem = firstProblem.isEmpty() ? problem : firstProblem;
    }
    return {false, firstProblem};
}

} // namespace synth::soundshape
