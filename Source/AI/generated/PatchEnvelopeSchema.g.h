#pragma once

// GENERATED FILE — DO NOT EDIT. Vendored from the synth-platform repo (P6-13); this client repo
// has no build-time dependency on pnpm/tsx, so this is a manual copy-and-commit step, not a build
// step — same discipline as this repo's other synth-platform-derived schema/type headers.
//
// Regenerate in synth-platform: pnpm --filter @platform/contracts codegen:envelope-schema
// Then copy packages/contracts/generated/PatchEnvelopeSchema.g.h here verbatim and commit both.
// Source of truth: synth-platform's packages/contracts/src/patch.ts (PatchSchema), rendered
// flat/inlined (no $ref/definitions — see synth-platform's emit-envelope-schema.ts for why).
//
// This is the ENVELOPE ONLY — node/connection/modulation shape, required/optional fields. It
// intentionally does NOT constrain "type" to an enum, or "params" values to per-parameter choice
// enums: those come from THIS repo's live module registry (AIStateMapper::moduleFactory, built by
// instantiating every registered AudioProcessor and reading AudioParameterChoice::choices), which
// synth-platform has no way to see. AIStateMapper::getPatchSchema() (AIStateMapper.cpp) parses
// this envelope and layers that enum injection on top of it.

namespace synth::generated {

// additionalProperties is always the JSON Schema boolean `true` here, never `{}` — Ollama's
// grammar-constrained decoder mangles an empty-object subschema into a garbage wrapped value
// instead of leaving it unconstrained (confirmed root cause, P6-13; see
// synth-platform/packages/inference/src/index.ts's rewriteEmptyAdditionalProperties).
inline constexpr const char* kPatchEnvelopeSchemaJson = R"PATCH_SCHEMA({
  "type": "object",
  "properties": {
    "nodes": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {
            "type": "integer"
          },
          "type": {
            "type": "string"
          },
          "params": {
            "type": "object",
            "additionalProperties": true
          }
        },
        "required": [
          "id",
          "type"
        ],
        "additionalProperties": false
      }
    },
    "connections": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "src": {
            "type": "integer"
          },
          "srcPort": {
            "type": "integer"
          },
          "dst": {
            "type": "integer"
          },
          "dstPort": {
            "type": "integer"
          }
        },
        "required": [
          "src",
          "srcPort",
          "dst",
          "dstPort"
        ],
        "additionalProperties": false
      }
    },
    "mode": {
      "type": "string",
      "enum": [
        "replace",
        "merge"
      ]
    },
    "remove": {
      "type": "array",
      "items": {
        "type": "integer"
      }
    },
    "modulations": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "source": {
            "type": "integer"
          },
          "sourcePort": {
            "type": "integer"
          },
          "dest": {
            "type": "integer"
          },
          "destPort": {
            "type": "integer"
          },
          "amount": {
            "type": "number"
          },
          "bypass": {
            "type": "boolean"
          }
        },
        "required": [
          "source",
          "dest",
          "destPort"
        ],
        "additionalProperties": false
      }
    },
    "removeModulations": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "source": {
            "type": "integer"
          },
          "dest": {
            "type": "integer"
          },
          "destPort": {
            "type": "integer"
          }
        },
        "required": [
          "source",
          "dest",
          "destPort"
        ],
        "additionalProperties": false
      }
    }
  },
  "required": [
    "nodes",
    "connections"
  ],
  "additionalProperties": false
}
)PATCH_SCHEMA";

} // namespace synth::generated
