// AIStateMapper — AI-facing schema generation.
//
// getPatchSchema/getPatchSchemaWithTimelineOps/getTimelineOpsEnvelopeSchema build the structured-
// output contracts providers are constrained to, generated from the module factory itself so the
// schema can't silently drift from what createModule actually accepts. The class itself is
// declared in AIStateMapper.h.

#include "AIStateMapper.h"

#include "AIStateMapperInternal.h"

#include "../generated/PatchEnvelopeSchema.g.h"

#include <map>
#include <set>

namespace synth {

namespace {

// The module types the model may emit, taken from the factory itself rather than a hand-kept
// list. The previous literal had silently drifted: "Voice Mixer" existed in the factory but was
// absent from the schema, so a constrained decoder could never produce one.
juce::Array<juce::var> authorableModuleTypeEnum() {
    juce::Array<juce::var> types;
    for (const auto& name : AIStateMapper::authorableModuleTypes())
        types.add(name);
    return types;
}

// Choice parameters, keyed by parameter id, gathered across every authorable module.
//
// This is what closes the gap that dominates rejections on smaller models: `params` used to be
// an unconstrained {"type":"object"}, so nothing stopped a model writing "waveform":"White
// Noise". Emitting the real enum makes the decoder itself unable to produce an illegal choice.
//
// Ids are shared across modules only when they mean the same thing; if two modules ever declare
// the same id with *different* option lists, constraining it globally would forbid values that
// are legal for one of them, so such an id is deliberately left unconstrained (and
// SchemaChoiceParamIdsAreUnambiguous fails, to make the collision a decision rather than a
// silent loss of enforcement).
juce::var choiceParamProperties() {
    std::map<juce::String, juce::StringArray> byParamId;
    std::set<juce::String> ambiguous;

    for (const auto& entry : detail::moduleFactory()) {
        if (detail::isInternalOnlyModule(entry.first))
            continue;
        auto processor = entry.second();
        if (!processor)
            continue;

        for (auto* param : processor->getParameters()) {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                auto existing = byParamId.find(choice->paramID);
                if (existing == byParamId.end())
                    byParamId[choice->paramID] = choice->choices;
                else if (existing->second != choice->choices)
                    ambiguous.insert(choice->paramID);
            }
        }
    }

    juce::DynamicObject::Ptr properties = new juce::DynamicObject();
    for (const auto& [paramId, choices] : byParamId) {
        if (ambiguous.count(paramId) > 0)
            continue;

        juce::Array<juce::var> options;
        for (const auto& option : choices)
            options.add(option);

        juce::DynamicObject::Ptr definition = new juce::DynamicObject();
        definition->setProperty("type", "string");
        definition->setProperty("enum", juce::var(options));
        properties->setProperty(paramId, juce::var(definition.get()));
    }
    return juce::var(properties.get());
}

} // namespace

juce::var AIStateMapper::getPatchSchema() {
    // Reserved fields are DELIBERATELY absent from this schema: "schemaVersion", node "uuid" and
    // the root "timeline" key. This document is the output contract handed to the provider as
    // `format` — every property in it is an invitation to emit that property, and all three are
    // ours to write, never the model's (uuid is identity, timeline is refused outright by
    // validatePatch). Pinned by AIStateMapperTest.SchemaOmitsReservedFields.
    //
    // The envelope (property names, required/optional shape) is generated from synth-platform's
    // Zod source (packages/contracts/src/patch.ts's PatchSchema) — see
    // Source/AI/generated/PatchEnvelopeSchema.g.h's header comment for the regen/vendor flow
    // (P6-13). Two things it can't carry, layered on here instead: the "type" enum and the
    // per-choice-parameter enums inside "params", both sourced from THIS build's live module
    // registry (moduleFactory), which synth-platform has no way to see.
    juce::var schema = juce::JSON::parse(juce::String(synth::generated::kPatchEnvelopeSchemaJson));
    jassert(!schema.isVoid());

    auto* properties = schema.getDynamicObject()->getProperty("properties").getDynamicObject();
    jassert(properties != nullptr);
    auto* nodeItems = properties->getProperty("nodes").getDynamicObject()->getProperty("items").getDynamicObject();
    jassert(nodeItems != nullptr);
    auto* nodeProperties = nodeItems->getProperty("properties").getDynamicObject();
    jassert(nodeProperties != nullptr);

    auto* typeDef = nodeProperties->getProperty("type").getDynamicObject();
    jassert(typeDef != nullptr);
    typeDef->setProperty("enum", juce::var(authorableModuleTypeEnum()));

    // `additionalProperties` stays open (true, from the generated envelope) on purpose: only
    // choice parameters can be enumerated, and numeric ones (cutoff, rateHz, …) must still be
    // expressible.
    auto* paramsDef = nodeProperties->getProperty("params").getDynamicObject();
    jassert(paramsDef != nullptr);
    paramsDef->setProperty("properties", choiceParamProperties());

    // Note: there is deliberately no "parameterChoices" property here. It used to be carried in
    // this schema "for AI reference", but this schema is the *output* contract handed to the
    // provider as `format` — every property in it is something the model is invited to emit, not
    // documentation it can read. The choice lists now do their real work as enums inside
    // node.params (above), and remain human-readable in the system prompt via getModuleSchema().
    return schema;
}

namespace {
// One permissive op shape, shared by BOTH structured-output contracts that can carry ops
// (getPatchSchemaWithTimelineOps and getTimelineOpsEnvelopeSchema) so the grammar cannot drift
// between them — this is a grammar, not a validator; TimelineOps::validate is still the real
// gate. Field names/types mirror TimelineOps.cpp's readers exactly. "track" is `{"type":
// "string"}`, not `{}` ("anything goes"): an empty-schema subschema is a confirmed Ollama
// grammar-compiler bug (P6-13) that mangles output into garbage instead of passing the value
// through unconstrained (same defect class documented in synth-platform's
// packages/inference/src/index.ts for the sibling `params` shape; `params` in getPatchSchema()
// above never hit this because its `additionalProperties: true` is a JSON Schema *boolean*, not
// `{}`). The natural fix would be `"oneOf": [string, {"index": integer}]` to match
// resolveTrack() (TimelineOps.cpp:210-256) exactly, but llama.cpp's grammar compiler (which
// Ollama's structured output sits on) handles anyOf/oneOf poorly, so this narrows to
// `"type": "string"` only — the common case of addressing a track by its exact name. TRADEOFF,
// documented rather than silently picked: TimelineOps::resolveTrack()'s `{"index": N}`
// disambiguation path for two tracks sharing a name is not expressible through this grammar — a
// duplicate-name op gets TimelineOps::validate's rejection message instead of succeeding, which
// the model can act on (e.g. rename) but not resolve via index. Still strictly better than `{}`,
// which was mangled on essentially every emission, string or object alike.
juce::var timelineOpsArraySchema() {
    const juce::String opsSchemaJson = R"json({
        "type": "array",
        "items": {
            "type": "object",
            "properties": {
                "op": {"type": "string", "enum": ["addTrack", "placeClips", "writeLane", "placeMidiClip"]},
                "kind": {"type": "string", "enum": ["midi", "automation"]},
                "name": {"type": "string"},
                "track": {"type": "string"},
                "clips": {"type": "array", "items": {"type": "object", "properties": {
                    "startBeat": {"type": "number"}, "lengthBeats": {"type": "number"},
                    "name": {"type": "string"},
                    "notes": {"type": "array", "items": {"type": "object", "properties": {
                        "startBeat": {"type": "number"}, "lengthBeats": {"type": "number"},
                        "pitch": {"type": "integer"}, "velocity": {"type": "integer"},
                        "channel": {"type": "integer"}},
                        "required": ["startBeat", "lengthBeats", "pitch"]}}},
                    "required": ["startBeat", "lengthBeats", "notes"]}},
                "nodeUuid": {"type": "string"},
                "paramId": {"type": "string"},
                "points": {"type": "array", "items": {"type": "object", "properties": {
                    "beat": {"type": "number"}, "value": {"type": "number"},
                    "tension": {"type": "number"}, "curve": {"type": "integer"}},
                    "required": ["beat", "value"]}},
                "startBeat": {"type": "number"},
                "midBase64": {"type": "string"}
            },
            "required": ["op"]
        }
    })json";
    return juce::JSON::parse(opsSchemaJson);
}
} // namespace

juce::var AIStateMapper::getPatchSchemaWithTimelineOps() {
    juce::var schema = getPatchSchema();
    auto* schemaObj = schema.getDynamicObject();
    jassert(schemaObj != nullptr);
    auto* properties = schemaObj->getProperty("properties").getDynamicObject();
    jassert(properties != nullptr);

    properties->setProperty("timelineOps", timelineOpsArraySchema());
    // Deliberately NOT added to "required": a patch-only response stays exactly as valid as it
    // was under getPatchSchema(), and the prompt tells the model when the key is warranted.
    return schema;
}

juce::var AIStateMapper::getTimelineOpsEnvelopeSchema() {
    juce::DynamicObject::Ptr properties = new juce::DynamicObject();
    properties->setProperty("timelineOps", timelineOpsArraySchema());

    juce::DynamicObject::Ptr schema = new juce::DynamicObject();
    schema->setProperty("type", "object");
    schema->setProperty("properties", juce::var(properties.get()));
    // Required here, unlike getPatchSchemaWithTimelineOps: an arrange-mode answer that carries
    // no ops is not an answer, and the grammar refusing it beats a prose apology the extraction
    // step would drop.
    schema->setProperty("required", juce::Array<juce::var>({"timelineOps"}));
    return juce::var(schema.get());
}

} // namespace synth
