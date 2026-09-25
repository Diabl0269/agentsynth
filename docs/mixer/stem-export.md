# Stem Export

One render pass writes one audio file per channel strip. Every strip is a natural tap point, which is
the "single render, parallel writers" shape stem export needs. What a channel is lives in
[`docs/mixer/mixer.md`](mixer.md).

---

## One render pass, one file per strip

`ChannelStripModule` carries an opt-in, **non-owning** stem tap (`setStemTapBuffer`): a
message-thread-armed pointer to a preallocated stereo buffer, **null outside an export**. When armed,
the strip copies its FINAL output — post gain, pan, mute AND solo, exactly what it hands Master — into
the tap at the end of every `processBlock` exit path. **No allocation and no locks**: an atomic
pointer swap, and when armed one `copyFrom` per leg.

`synth::StemExporter::exportStems` (`Source/Transport/StemExporter.{h,cpp}`) drives ONE render pass
through the exact same offline path `BounceExporter::bounce` uses — same `BounceOptions` (range, tail,
sample rate, bit depth, format), same suspend, reprepare and restore choreography, same progress and
cancel semantics — via a sibling session class `synth::StemSession` and a sibling chunked runner
`synth::StemRunner`, mirroring `BounceSession`/`BounceRunner` so the message thread stays responsive
exactly like Export Audio. The two pieces both paths would otherwise duplicate
(`synth::validateBounceOptions`, and the metronome and external-MIDI RAII guards) are factored into
`Source/Transport/BounceGuards.h`, so `BounceExporter`'s own behaviour and tests are unchanged.

**Strips are enumerated from the graph in ascending node-id order** — the "else node id" fallback,
whose ORDER has no dependency on the timeline or track model — and named `"NN - <name>.<ext>"`.

## Stem naming

`<name>` prefers, in order:

1. **The strip's own persisted name** (FRO225, `ChannelStripModule::getStripName()`) — set from the
   mixer column header's inline rename ([`docs/mixer/panel.md#renaming-a-channel`](panel.md#renaming-a-channel)).
   Empty (unset, and every strip created before FRO225) falls through to the rule below, unchanged.
2. **The ONE track** — `TimelineMidiSource` or `TimelineAudioSource`, "Track In" or "Track Audio" —
   whose signal feeds that strip, **not the strip's own graph-node instance name**, which is identical
   across every strip in a patch and therefore useless as a stem name once more than one channel
   exists.

`StemSession` walks the graph upstream from the strip, **along signal edges only**, mirroring
`synth::ChannelFlows`' `isSignalEdge` rule: **never through an `AttenuverterModule`, never through a
`PortRole::ModCV` input** — a modulation cable from an unrelated track must not make that track "feed"
the strip — transitively through the instrument and macro chain, **stopping at another
`ChannelStripModule`**, since that strip already terminates its own track's chain.

Exactly one track found this way contributes its `TimelineDoc` name ("Bass", "Audio 1"). **Zero or
several tracks, or no `TimelineDoc` at all (a headless caller), fall back to `"Channel N"`**, with `N`
matching the strip's own `NN` position so it agrees with the file's own number and is unique on its
own even with no `TimelineDoc` available. `StemSession`'s constructor takes an optional
`const TimelineDoc*` for this; null means every strip without its own persisted name falls back to
`"Channel N"`.

## Buses are stems too

A group or send bus is an ordinary `ChannelStripModule`, so `collectStemStrips` picks it up with **no
code change at all**.

**A source's stem stays pre-send** — the tap copies the main legs only and never a send leg — so
nothing is double-counted for a post-fader send, and a pre-fader send is not lost either: it appears
in the bus's stem and nowhere else. The sum identity survives exactly:
`sum(stems) = source main outs + bus outs = everything Master receives on Mix`.

A bus has no feeding track, so **its stem name falls back to `"Bus N"`** (`synth::busFallbackName`)
rather than the misleading `"Channel N"` — unless the bus has its own persisted name (FRO225), which
still wins ahead of this fallback, same as any other strip.

## What sums back to the mix

Two decisions, both load-bearing for "the stems sum back to the mix":

- **Every strip ALWAYS gets a file — muted or soloed-out included.** The tap sits AFTER the strip's
  own bypass, mute and solo logic, so a muted or non-soloed strip's stem is simply silent for exactly
  the blocks it was silent, never absent. A soloed strip during export therefore never changes
  *which* strips get written, only what most of them contain — silence sums to zero, so the mix
  identity holds in every solo and mute combination, and solo and mute state is left unchanged by the
  export.
- **Master's Direct input — cables that bypass every strip — is NOT a stem.** Direct is not a channel
  ([`docs/mixer/panel.md`](panel.md#what-the-mixer-shows)), so summing the stems reproduces the
  pre-Master MIX bus, not the whole signal Master receives. A patch that also uses Direct will not
  find it isolated in any stem, by design, the same way it is not a mixer column either.

**Both decisions assume every enumerated strip is actually routed into Master's Mix bus.** Strips are
enumerated by scanning the graph for `ChannelStripModule` nodes regardless of routing
(`collectStemStrips`), so an orphaned strip wired to nothing, or one wired somewhere other than
Master's Mix inputs, still gets a stem file — the mirror image of the Direct caveat: that stem is not
part of what sums back to the pre-Master mix, exactly because it never fed Master's Mix bus.

**No strips in the patch.** `StemExporter::hasChannelStrips` lets the UI show a clear message before
even opening the dialog, and `StemSession`'s own setup fails with the identical message as a
defence-in-depth backstop.

**"Export Stems..." sits immediately after "Export Audio..."** everywhere that action is offered,
opening `ExportAudioDialog` in a stems mode: the destination is a **FOLDER** (default
`"<project name> Stems"` inside the same `Exports/` base Export Audio uses), with the same range, tail,
format, rate and bit-depth controls and the same progress page, and **no destination-exists collision
prompt** — a stems folder is a re-exportable container, not a one-shot file. A cancelled or failed
export leaves no stem files behind, never touches a pre-existing file, and disarms every tap.

## Related

- [`docs/mixer/mixer.md`](mixer.md) — what a channel is, and the solo gate the tap sits after.
- [`docs/mixer/sends-and-buses.md`](sends-and-buses.md) — why a source's stem is pre-send.
- [`docs/mixer/panel.md`](panel.md#what-the-mixer-shows) — why Direct is not a channel.
- [`docs/architecture/audio-engine.md#bounceexport`](../architecture/audio-engine.md#bounceexport) — the offline bounce path this shares.
