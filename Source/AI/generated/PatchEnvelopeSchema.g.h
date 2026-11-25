#pragma once

// GENERATED FILE — DO NOT EDIT. Vendored from the synth-platform repo; this client repo
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
// synth-platform has no way to see. AIStateMapper::getPatchSchema() (AIStateMapperSchema.cpp) parses
// this envelope and layers that enum injection on top of it.

namespace synth::generated {

// additionalProperties is always the JSON Schema boolean `true` here, never `{}` — see
// rewriteEmptyAdditionalProperties above for why that distinction matters to Ollama's
// grammar-constrained decoder.
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
          },
          "state": {
            "anyOf": [
              {
                "type": "object",
                "properties": {
                  "version": {
                    "type": "number",
                    "const": 1
                  },
                  "seed": {
                    "type": "integer",
                    "minimum": 0,
                    "maximum": 4294967295
                  },
                  "customScale": {
                    "type": "object",
                    "properties": {
                      "name": {
                        "type": "string",
                        "maxLength": 64
                      },
                      "mask": {
                        "type": "integer",
                        "minimum": 0,
                        "maximum": 4095
                      }
                    },
                    "required": [
                      "mask"
                    ],
                    "additionalProperties": false
                  },
                  "steps": {
                    "type": "array",
                    "items": {
                      "type": "object",
                      "properties": {
                        "pitch": {
                          "type": "integer",
                          "minimum": 0,
                          "maximum": 127
                        },
                        "on": {
                          "type": "boolean"
                        },
                        "gate": {
                          "type": "number",
                          "minimum": 0.05,
                          "maximum": 1
                        },
                        "velocity": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 127
                        },
                        "chance": {
                          "type": "integer",
                          "minimum": 0,
                          "maximum": 100
                        },
                        "repeat": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 8
                        },
                        "slide": {
                          "type": "boolean"
                        },
                        "nudge": {
                          "type": "number",
                          "minimum": -0.5,
                          "maximum": 0.5
                        },
                        "condition": {
                          "type": "string",
                          "pattern": "^[1-8]\\/[1-8]$"
                        },
                        "filter": {
                          "type": "number",
                          "minimum": 0,
                          "maximum": 1
                        },
                        "chord": {
                          "type": "string",
                          "enum": [
                            "Unison",
                            "Major",
                            "Minor",
                            "Maj7",
                            "Min7",
                            "5ths",
                            "Octs",
                            "Random",
                            "Scale Triad",
                            "Scale Seventh",
                            "Scale Sus2",
                            "Scale Sus4",
                            "Scale Add9",
                            "Scale Sixth",
                            "Dom7",
                            "Dim",
                            "Aug",
                            "Sus2",
                            "Sus4",
                            "Sixth",
                            "Min6",
                            "Add9",
                            "Dim7",
                            "HalfDim7",
                            "MinMaj7"
                          ]
                        }
                      },
                      "additionalProperties": false
                    },
                    "minItems": 1,
                    "maxItems": 128
                  }
                },
                "required": [
                  "steps"
                ],
                "additionalProperties": false
              },
              {
                "type": "object",
                "properties": {
                  "version": {
                    "type": "number",
                    "const": 1
                  },
                  "points": {
                    "type": "array",
                    "items": {
                      "type": "object",
                      "properties": {
                        "x": {
                          "type": "number",
                          "minimum": 0,
                          "maximum": 1
                        },
                        "y": {
                          "type": "number",
                          "minimum": 0,
                          "maximum": 1
                        },
                        "bend": {
                          "type": "number",
                          "minimum": -1,
                          "maximum": 1
                        }
                      },
                      "required": [
                        "x",
                        "y",
                        "bend"
                      ],
                      "additionalProperties": false
                    },
                    "minItems": 2,
                    "maxItems": 64
                  }
                },
                "required": [
                  "points"
                ],
                "additionalProperties": false
              }
            ]
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
