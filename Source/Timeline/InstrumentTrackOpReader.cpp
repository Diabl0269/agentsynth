// InstrumentTrackOpReader.cpp -- the field checks of a timelineOps `addInstrumentTrack` op. Every
// string literal here is ASCII, for the reason TimelineOps.cpp gives (the narrow juce::String
// constructor takes ASCII, and these strings reach a chat card).
#include "InstrumentTrackOpReader.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"

#include <cstdint>
#include <limits>

namespace synth {

namespace {

// Present-but-wrong is rejected; absent is fine. Same shape as TimelineOps.cpp's readInt, which
// is file-local there by design.
bool isOptionalInt(const juce::var& v) {
    if (v.isVoid() || v.isInt())
        return true;
    if (!v.isInt64())
        return false;
    const auto wide = static_cast<std::int64_t>(static_cast<juce::int64>(v));
    return wide >= std::numeric_limits<int>::min() && wide <= std::numeric_limits<int>::max();
}

bool isAuthorableInstrument(const juce::String& type) {
    for (const char* candidate : TimelineOps::kAuthorableInstrumentTypes)
        if (type == candidate)
            return true;
    return false;
}

juce::String instrumentListText() {
    juce::StringArray names;
    for (const char* candidate : TimelineOps::kAuthorableInstrumentTypes)
        names.add("\"" + juce::String(candidate) + "\"");
    return names.joinIntoString(", ");
}

// The MIDI sources docs/timeline/add-track.md names: they generate notes rather than process
// audio, so none can sit in an audio chain. Most are non-authorable anyway; listed so the rule
// does not lean on that.
bool isMidiSourceType(const juce::String& type) {
    return type == "Track In" || type == "External MIDI" || type == "MIDI Keyboard";
}

/** One insert: a closed object whose type is an authorable audio-in/audio-out module and whose
 *  params pass the SAME untrusted per-node check validatePatch runs (never a looser copy). */
juce::String readInsert(int index, const juce::var& insertVar, InstrumentTrackInsert& out) {
    const juce::String at = "insert " + juce::String(index) + " ";
    auto* insertObj = insertVar.getDynamicObject();
    if (insertObj == nullptr)
        return at + "is not an object.";
    for (int i = 0; i < insertObj->getProperties().size(); ++i) {
        const juce::String key = insertObj->getProperties().getName(i).toString();
        if (key != "type" && key != "id" && key != "params")
            return at + "has an unknown field \"" + key + "\". An insert accepts only \"type\", \"id\" and \"params\".";
    }

    const juce::var typeVar = insertObj->getProperty("type");
    if (!typeVar.isString() || typeVar.toString().isEmpty())
        return at + "needs a string \"type\" naming a module.";
    out.type = typeVar.toString();
    if (!AIStateMapper::moduleFactoryTypeNames().contains(out.type))
        return at + "asks for unknown module type \"" + out.type + "\".";
    if (!AIStateMapper::authorableModuleTypes().contains(out.type))
        return at + "asks for \"" + out.type + "\", which only the app itself may create.";
    if (isMidiSourceType(out.type))
        return at + "asks for \"" + out.type + "\", a MIDI source - an insert must process audio.";

    // Instantiated (and thrown away) rather than looked up in a list, so the audio-in/audio-out
    // rule follows the module's real channel shape. Not cached: a batch holds at most a handful.
    auto probe = AIStateMapper::createModule(out.type);
    if (probe == nullptr)
        return at + "asks for unknown module type \"" + out.type + "\".";
    if (auto* module = dynamic_cast<ModuleBase*>(probe.get());
        module != nullptr && isMidiInstrumentType(module->getModuleType()))
        return at + "asks for \"" + out.type +
               "\", a MIDI instrument - the track's instrument is \"instrument\", and an insert must process audio.";
    if (probe->getTotalNumInputChannels() < 1 || probe->getTotalNumOutputChannels() < 1)
        return at + "asks for \"" + out.type + "\", which does not take audio in and send audio out.";

    if (!isOptionalInt(insertObj->getProperty("id")))
        return at + "has a non-integer \"id\".";

    out.params = insertObj->getProperty("params");
    if (!out.params.isVoid()) {
        auto* paramsObj = out.params.getDynamicObject();
        if (paramsObj == nullptr)
            return at + "has a \"params\" that is not an object.";
        if (const auto result = AIStateMapper::validateNodeParams(probe.get(), paramsObj); !result.ok)
            return at + "(" + out.type + "): " + result.message;
    }
    return {};
}

// Three decimals at most, trailing zeros trimmed ("0.2", "0", "1.5"): the preview line is pinned by tests.
juce::String formatNumber(double value) {
    juce::String text(value, 3);
    if (text.containsChar('.')) {
        text = text.trimCharactersAtEnd("0");
        text = text.trimCharactersAtEnd(".");
    }
    return text == "-0" ? juce::String("0") : text;
}

juce::String formatParamValue(const juce::var& value) {
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return static_cast<bool>(value) ? "on" : "off";
    return formatNumber(static_cast<double>(value));
}

/** The track's own envelope: a closed object `{ "id", "params" }` whose params pass the SAME untrusted
 *  per-node check an insert's do, against the ADSR module the build adds. */
juce::String readEnvelope(const juce::var& envelopeVar, juce::var& paramsOut) {
    auto* envelopeObj = envelopeVar.getDynamicObject();
    if (envelopeObj == nullptr)
        return "has an \"envelope\" that is not an object.";
    for (int i = 0; i < envelopeObj->getProperties().size(); ++i) {
        const juce::String key = envelopeObj->getProperties().getName(i).toString();
        if (key != "id" && key != "params")
            return "envelope has an unknown field \"" + key + "\". An envelope accepts only \"id\" and \"params\".";
    }
    if (!isOptionalInt(envelopeObj->getProperty("id")))
        return "envelope has a non-integer \"id\".";

    const juce::var params = envelopeObj->getProperty("params");
    if (params.isVoid())
        return {};
    auto* paramsObj = params.getDynamicObject();
    if (paramsObj == nullptr)
        return "envelope has a \"params\" that is not an object.";
    // The track's poly setting decides whether its envelope is per-voice; a "poly" here could only
    // break that pairing.
    if (paramsObj->hasProperty("poly"))
        return "envelope params include \"poly\", which the track's own \"poly\" decides. Leave it out.";
    auto probe = AIStateMapper::createModule("ADSR");
    if (probe == nullptr)
        return "envelope cannot be checked in this build.";
    if (const auto result = AIStateMapper::validateNodeParams(probe.get(), paramsObj); !result.ok)
        return "envelope (ADSR): " + result.message;
    paramsOut = params;
    return {};
}

// The "envelope" suffix of the preview: attack/decay/sustain/release in that order, then any other
// given param by name, listing only what the op set.
juce::String describeEnvelopeParams(const juce::var& params) {
    auto* paramsObj = params.getDynamicObject();
    if (paramsObj == nullptr || paramsObj->getProperties().size() == 0)
        return {};
    juce::StringArray parts;
    for (const char* key : {"attack", "decay", "sustain", "release"})
        if (paramsObj->hasProperty(key))
            parts.add(juce::String(key) + " " + formatParamValue(paramsObj->getProperty(key)));
    juce::StringArray others;
    for (int i = 0; i < paramsObj->getProperties().size(); ++i) {
        const juce::String key = paramsObj->getProperties().getName(i).toString();
        if (key != "attack" && key != "decay" && key != "sustain" && key != "release")
            others.add(key);
    }
    others.sort(false);
    for (const auto& key : others)
        parts.add(key + " " + formatParamValue(paramsObj->getProperty(key)));
    return parts.joinIntoString(", ");
}

} // namespace

juce::String readInstrumentTrackOpFields(juce::DynamicObject& op, InstrumentTrackOpFields& out) {
    const juce::var instrumentVar = op.getProperty("instrument");
    if (!instrumentVar.isString())
        return "needs a string \"instrument\", one of " + instrumentListText() + ".";
    out.instrument = instrumentVar.toString();
    if (!isAuthorableInstrument(out.instrument))
        return "asks for instrument \"" + out.instrument + "\". The instruments are " + instrumentListText() + ".";

    const juce::var polyVar = op.getProperty("poly");
    if (!polyVar.isVoid() && !polyVar.isBool())
        return "has a non-boolean \"poly\".";
    out.poly = polyVar.isBool() && static_cast<bool>(polyVar);
    // The menu offers no poly Sampler (it has no poly parameter), so the preview would describe a
    // track the build cannot make.
    if (out.poly && out.instrument == "Sampler")
        return "asks for a poly Sampler. Only \"Oscillator\" and \"Wavetable\" have a poly mode.";

    // An in-response node reference: inert here, resolved by AIIntegrationService's edit plan
    // (which also requires it, like an insert's "id", to be a non-negative id distinct from every
    // other id in the response).
    if (!isOptionalInt(op.getProperty("instrumentId")))
        return "has a non-integer \"instrumentId\".";

    // Checked before the early return below: an op with an envelope and no inserts still has one.
    if (const juce::var envelopeVar = op.getProperty("envelope"); !envelopeVar.isVoid()) {
        if (out.instrument == "Sampler")
            return "Sampler tracks have no envelope; leave \"envelope\" out.";
        if (const auto error = readEnvelope(envelopeVar, out.envelopeParams); error.isNotEmpty())
            return error;
    }

    const juce::var insertsVar = op.getProperty("inserts");
    if (insertsVar.isVoid())
        return {};
    if (!insertsVar.isArray())
        return "has an \"inserts\" that is not an array.";
    const auto& insertList = *insertsVar.getArray();
    if (insertList.size() > TimelineOps::kMaxInstrumentInserts)
        return "asks for " + juce::String(insertList.size()) + " inserts, exceeding the limit of " +
               juce::String(TimelineOps::kMaxInstrumentInserts) + ".";
    for (int i = 0; i < insertList.size(); ++i) {
        InstrumentTrackInsert insert;
        if (const auto error = readInsert(i, insertList.getReference(i), insert); error.isNotEmpty())
            return error;
        out.inserts.push_back(std::move(insert));
    }
    return {};
}

juce::String describeInstrumentTrack(const InstrumentTrackOpFields& fields) {
    // Oscillator and Wavetable have no envelope of their own, so the build adds ADSR + VCA (a
    // per-voice one when poly); Sampler plays through its own one-shot envelope.
    const bool addsEnvelope = fields.instrument != "Sampler";
    juce::String text = (fields.poly ? "poly " : "") + fields.instrument +
                        (addsEnvelope ? " with envelope and channel strip" : " with channel strip");
    if (!fields.inserts.empty()) {
        juce::StringArray types;
        for (const auto& insert : fields.inserts)
            types.add(insert.type);
        text << ", inserts: " << types.joinIntoString(", ");
    }
    if (const auto envelope = describeEnvelopeParams(fields.envelopeParams); envelope.isNotEmpty())
        text << ", envelope: " << envelope;
    return text;
}

} // namespace synth
