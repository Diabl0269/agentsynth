// AIStateMapper -- the machine-readable module catalog (getModuleCatalogJson).
//
// The JSON twin of getModuleSchema()'s Markdown table, read from the same module factory, so the
// hosted AI server can be handed the exact parameter ids, kinds, ranges and choice lists instead
// of inventing them. The class itself is declared in AIStateMapper.h.

#include "AIStateMapper.h"

#include "AIStateMapperInternal.h"

#include "../../Timeline/TimelineOps.h"

#include <cmath>

namespace synth {

namespace {

// Floats go out rounded to six decimals so a denormalised default reads 0.3, not 0.30000001.
double tidy(double value) { return std::round(value * 1.0e6) / 1.0e6; }

juce::var stringArrayVar(const juce::StringArray& strings) {
    juce::Array<juce::var> out;
    for (const auto& s : strings)
        out.add(s);
    return juce::var(out);
}

// `module` is null for a processor that is not a ModuleBase (no modulation destinations).
juce::var parameterEntry(juce::RangedAudioParameter& p, ModuleBase* module) {
    juce::DynamicObject::Ptr entry = new juce::DynamicObject();
    entry->setProperty("id", p.paramID);
    const auto range = p.getNormalisableRange();
    const float defaultValue = range.convertFrom0to1(p.getDefaultValue());

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&p)) {
        entry->setProperty("kind", "choice");
        entry->setProperty("choices", stringArrayVar(choice->choices));
        const int index = juce::jlimit(0, juce::jmax(0, choice->choices.size() - 1), (int)std::lround(defaultValue));
        entry->setProperty("default", choice->choices[index]);
    } else if (dynamic_cast<juce::AudioParameterBool*>(&p) != nullptr) {
        entry->setProperty("kind", "bool");
        entry->setProperty("default", defaultValue >= 0.5f);
    } else if (dynamic_cast<juce::AudioParameterInt*>(&p) != nullptr) {
        entry->setProperty("kind", "int");
        entry->setProperty("min", (int)std::lround(range.start));
        entry->setProperty("max", (int)std::lround(range.end));
        entry->setProperty("default", (int)std::lround(defaultValue));
    } else {
        entry->setProperty("kind", "float");
        entry->setProperty("min", tidy(range.start));
        entry->setProperty("max", tidy(range.end));
        entry->setProperty("default", tidy(defaultValue));
    }
    // The rule validatePatch applies to a modulation's destParam.
    if (module != nullptr && module->modulationChannelForParam(p.paramID) >= 0)
        entry->setProperty("modulatable", true);
    return juce::var(entry.get());
}

juce::var parameterList(juce::AudioProcessor& processor) {
    juce::Array<juce::var> params;
    for (auto* param : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            params.add(parameterEntry(*ranged, dynamic_cast<ModuleBase*>(&processor)));
    return juce::var(params);
}

const char* categoryName(ModulationCategory category) {
    switch (category) {
    case ModulationCategory::Envelope:
        return "Envelope";
    case ModulationCategory::LFO:
        return "LFO";
    case ModulationCategory::Oscillator:
        return "Oscillator";
    case ModulationCategory::Sequencer:
        return "Sequencer";
    case ModulationCategory::Filter:
        return "Filter";
    case ModulationCategory::FX:
        return "FX";
    case ModulationCategory::Other:
        break;
    }
    return nullptr;
}

bool hasParameter(juce::AudioProcessor& processor, const juce::String& id) {
    for (auto* param : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param); ranged != nullptr && ranged->paramID == id)
            return true;
    return false;
}

juce::var moduleEntry(juce::AudioProcessor& processor) {
    juce::DynamicObject::Ptr entry = new juce::DynamicObject();
    if (auto* mb = dynamic_cast<ModuleBase*>(&processor))
        if (const char* category = categoryName(mb->getModulationCategory()))
            entry->setProperty("category", category);
    // "audio" here means any signal channel the module exposes (a CV jack counts), as the graph sees it.
    entry->setProperty("audioIn", processor.getTotalNumInputChannels() > 0);
    entry->setProperty("audioOut", processor.getTotalNumOutputChannels() > 0);
    entry->setProperty("params", parameterList(processor));
    return juce::var(entry.get());
}

// What an addInstrumentTrack op accepts: the instrument types, whether each is polyphonic and whether the
// track builds an envelope for it, plus the envelope's own parameters (the op's "envelope.params").
juce::var instrumentTracksEntry() {
    juce::DynamicObject::Ptr instruments = new juce::DynamicObject();
    for (const char* type : TimelineOps::kAuthorableInstrumentTypes) {
        auto processor = AIStateMapper::createModule(type);
        if (!processor)
            continue;
        juce::DynamicObject::Ptr entry = new juce::DynamicObject();
        entry->setProperty("module", juce::String(type));
        entry->setProperty("poly", hasParameter(*processor, "poly"));
        entry->setProperty("hasEnvelope", !TimelineOps::instrumentOwnsItsEnvelope(type));
        instruments->setProperty(type, juce::var(entry.get()));
    }

    juce::DynamicObject::Ptr envelope = new juce::DynamicObject();
    envelope->setProperty("module", "ADSR");
    if (auto adsr = AIStateMapper::createModule("ADSR"))
        envelope->setProperty("params", parameterList(*adsr));

    juce::DynamicObject::Ptr out = new juce::DynamicObject();
    out->setProperty("instruments", juce::var(instruments.get()));
    out->setProperty("envelope", juce::var(envelope.get()));
    return juce::var(out.get());
}

} // namespace

// Version 1 layout: modules (sorted by type name; audioIn, audioOut, optional category, params) and
// instrumentTracks (the addInstrumentTrack instruments plus the ADSR envelope's params).
juce::String AIStateMapper::getModuleCatalogJson() {
    juce::DynamicObject::Ptr modules = new juce::DynamicObject();
    for (const auto& name : authorableModuleTypes()) { // sorted, so the document is byte-stable
        auto processor = createModule(name);
        if (processor)
            modules->setProperty(juce::Identifier(name), moduleEntry(*processor));
    }

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("modules", juce::var(modules.get()));
    root->setProperty("instrumentTracks", instrumentTracksEntry());
    return juce::JSON::toString(juce::var(root.get()), false);
}

} // namespace synth
