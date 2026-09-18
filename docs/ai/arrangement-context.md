# Arrangement Context

`synth::ArrangementContext::summarize` (`Source/Timeline/ArrangementContext.h/.cpp`) is the timeline
sibling of the patch-context injection: a compact, token-bounded, read-only text summary of the
arrangement — tracks, clip windows, note counts and automation lanes — folded into the same outgoing
AI request the current patch JSON rides on.

It is the **read** half of the timeline seam. The write half is
[timeline ops](timeline-ops.md); the gate those ops pass is
[timeline safety](timeline-safety.md).

## Where it is built in

`AIIntegrationService::buildPatchAugmentedContent` — the function `sendMessage` calls to build the
structured-output request — is the one seam patch context reaches the model through
(`"Current patch state:\n\`\`\`json...\`\`\`"`). The arrangement is a second, independent section
beside it, under its own `## Arrangement` delimiter, included only when `summarize` returns non-empty
text. An empty or absent timeline adds nothing, following the same "say nothing rather than say
empty" rule the patch section follows.

`AIIntegrationService` owns no `TimelineDoc` or `TransportService` itself.
`MainComponent::initialiseCommon` installs non-owning pointers to its own app-lifetime instances via
`setTimelineContext()`, mirroring `setProvider()` and `setUndoManager()`.

## Security model, read path only

This is a **read-only summary that never round-trips**: nothing it emits can be replayed back into
the timeline. It inherits the same two boundaries
[`validateTimeline`](timeline-safety.md) enforces on the write path, applied to a text summary
instead of a JSON payload.

- **Never a file path.** An audio clip's `assetRef` is bundle-relative (`Clip::assetRef`); the
  summary emits only the bare file name, everything after the last `/`, and drops the directory
  component outright. It is never a stored-then-redacted path — it was never anything but the bare
  file name to begin with.
- **Never a plugin or implementation identifier.** A bound track or lane is named by the bound
  node's display name (`juce::AudioProcessor::getName()`, the same string its title bar shows),
  never a node id, factory type key or raw uuid. `summarize()` resolves every binding against the
  **live graph** passed in, not the doc's own cached `orphaned` flags, which may be stale, and an
  unresolvable binding reports `"MISSING"` rather than leaking the uuid it failed to resolve.

Node uuids do appear in the separate `## Automation targets` section, on purpose and under a
different argument — see [timeline ops](timeline-ops.md#the-local-path).

## Format and budget

One line per item, in `TimelineDoc`'s own stable order: a header
(`"Arrangement: N tracks, bpm B, T/S, loop [a, b)"`, or `"loop off"`), then per track its kind,
name, `armed`/`muted`/`soloed` flags shown only when set, and its binding; a compressed clip line
for MIDI tracks (`"3 clips @ 0-8, 8-12, 16-20 beats; 42 notes total"`); one line per clip for audio
tracks (name, beat window, bare file name); then one line per automation lane
(`"cutoff lane on Filter: 12 points, Read"`).

`maxChars` (default 2000) is enforced at **track granularity only** — a track is included whole or
not at all, so the result is never cut mid-line — and a dropped tail is marked deterministically
with `"... [+K more tracks]"`.

Tests: `Tests/Timeline/ArrangementContextTests.cpp` — tracks, clips and lanes rendered across bound,
unbound and orphaned states; track-granularity truncation; the empty-doc case; the file-path-leak
pin; plus a seam-level test that the injected request gains an `## Arrangement` section exactly when
`buildPatchAugmentedContent` should add one.
