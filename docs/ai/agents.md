# AI Assistant Overview

Agent Synth ships an AI sound-design assistant: the user describes a sound or an arrangement in
plain language, and the assistant answers with a proposed patch, a proposed set of timeline
changes, or both. Nothing it proposes reaches the document until the user presses Apply.

## Providers

Two providers are registered, both selectable in Settings → AI:

- **Ollama (local)**, id `ollama` — talks to an Ollama server the user runs. Nothing leaves the
  machine.
- **Remote (hosted)**, id `remote` — talks to the hosted inference service over HTTPS. The prompt,
  the current patch and (in arrange mode) a summary of the arrangement are sent to that service.

A brand-new install defaults to `remote`; an install that has launched before keeps `ollama` even
if it has never opened AI settings. An unknown or corrupt persisted provider id falls back to
`ollama`.

## What the assistant can author

- **Patches** — modules, connections, modulations and parameter values, as JSON in the
  [patch format](patch-format.md), gated by [`validatePatch`](patch-safety.md).
- **Timeline changes** — MIDI tracks, clips, notes and automation lanes, as a `timelineOps`
  envelope gated by [`TimelineOps::validate`](timeline-ops.md).

It authors nothing else. Audio assets, file paths, plugin state and record arming have no
authorable form at all — see [the agentic security model](timeline-safety.md#the-agentic-security-model).
