# Song steps (hosted whole songs)

A whole song asked of the hosted service takes two requests instead of one `project.generate`: a **free, priced
plan**, then a **build streamed step by step** that ends in the same edit plan the chat already applies. The server
side (routes, token, prices) is the hosted service's own song steps doc; this page is the app's half.

## The flow

1. `AIIntegrationService::planSong` posts `/v1/song/plan` (`RemoteProvider::sendRouteRequest`): the prompt, the chosen
   Length (`short`/`medium`/`long`) and Detail (`light`/`full`/`rich`), and, when re-planning, `previousPlan` (the plan
   object kept verbatim from the earlier reply). Both requests (plan and build) also carry `instruments`, the names in
   `TimelineOps::kAuthorableInstrumentTypes` (so Noise, Pluck and Mallet are offered only to apps that can build them;
   an older app sends nothing and the service keeps its old list). The reply is a plan, its price in uses and a signed token. Nothing is spent.
2. The chat shows a **song plan card** (`SongPlanCard`): title, `3:20 · 174 BPM · 7 sections`, a section strip (a
   focusable, tooltipped block per section, wide in proportion to its bars), instrument chips, the special line, Length
   and Detail choices, `Build song · N uses`, uses left, Cancel and a Details disclosure. Changing Length or Detail
   re-plans (still free) and the price updates. Typing a message while a plan is pending re-plans with that text as
   the change request.
3. **Build** calls `AIIntegrationService::buildSong`: `/v1/song/build` streams NDJSON lines (`steps`, `step`, `ping`,
   `done`, `error`). `RemoteProvider::streamRouteRequest` reads them on its worker thread, cuts them with
   `songsteps::LineSplitter` (bytes, so a read that splits a line or a character is harmless) and posts each to the
   message thread in order. A malformed or unknown line is skipped, never fatal.
4. `songsteps::BuildModel` turns the events into what the card shows; the **build card** (`BuildStepsCard`) is a view of it.
5. On `done` the service runs `previewProjectEdit` and `applyProjectEdit` on the envelope: one undo step, exactly like an
   edit plan. A refused envelope is reported in the chat and the plan card stays out of it. The chat history gets the
   user's words and a one-line answer, so later edits have the context.

## When the chat uses it

`AIChatComponent::shouldPlanSong` (pure, tested) decides. The song path is used when the provider is hosted, a
timeline is wired in, the service has not answered a plan with 404, **and** the message looks like a whole-song
request: a song word (song, tune, beat, instrumental, full or whole track, "from scratch") or a genre/style name with no
edit verb and no module type in it, or any message while a plan card is waiting to be built. Edits to an existing project
("add a filter to the lead", "make the bass wobble") never match and keep the single `project.generate` request.
Ollama never uses the song steps.

## Fallback

A plan request answered with 404 (a service without the routes) sets `canPlanSongs()` false for that provider and the
chat sends the same text through `sendProjectMessage`, after one assistant line ("Song steps aren't available on this server, so I'm building it in one go.") so the missing steps are explained. The check costs one free request per session.

## Errors and cancelling

- `PLAN_EXPIRED` (the token lasts 30 minutes): the chat plans again from the same plan, nothing was charged, the card
  says so and the user presses Build again.
- `QUOTA_EXCEEDED` (429): the existing quota bubble with the Upgrade button.
- An `error` line, or a stream that ends without `done`: the card shows what failed and that the uses came back (the
  server refunds a failed or dropped build).
- Cancel (the card's button, the input row's Cancel, Escape) closes the stream; the server aborts the remaining calls
  and refunds. The card says so.
- A streamed build uses its own 11 minute ceiling rather than the one-answer timeout (`requestTimeoutMs`), and holds the
  provider's single worker thread for the whole build; the chat input is read-only meanwhile.

## The cards

Both are standalone components in `Source/UI/Assistant/AIChatComponent/` (design system: `SongPlanCard`,
`BuildStepsCard`, `StepIcons`). The state they show lives in the message (`MessageData::song`), never in the card, because
the chat rebuilds its bubbles on a redraw. Step icons (`StepIcons.h`) move only while their step runs; see
[animation](../layout/animation.md#song-steps). A song message has the build card as its **only** card (no edit-plan card): once built, the card adds "Show details" (`songsteps::buildSongDetailsText`, `SongDetailsText.h`: a line per track with its sound modules and note count, the sections with their bars, a line per automation lane; stored in `SongSession::detailsText`) and the thumbs feedback. Undo is the project's own (one step, as the card says). The edit-plan card's rows carry the same kind icons, still before Apply,
animated once in order on Apply.

## Real instruments

Both requests carry `sounds: true`, which lets the service name an instrument from the core sound pack on a track: a
plan track may carry `coreSound`, and the build's `addInstrumentTrack` op `sound`. The value is one of nine ids
(`core/upright-piano`, `string-section`, `trumpet`, `french-horn`, `flute`, `clarinet`, `double-bass-plucked`,
`electric-guitar`, `acoustic-kit`); `SoundPacks/CoreSounds.h` is the table, with the stand-in synth each one comes
with (Mallet, Wavetable, Pluck or Drum Kit). The plan parser keeps a `coreSound` only when it is one of the nine, and
the op reader accepts a `sound` only when the op's `instrument` is its stand-in; otherwise the sound is ignored and the
track is built from the synth alone.

What a track gets depends on the pack (`MainComponentTimelineOpsHostSounds.cpp`):

- **Installed, melodic:** a Sampler in Instrument mode loaded with the pack instrument, with the track's name and
  notes unchanged. The stand-in's params are dropped except Level, and so are its envelope and poly flag (the Sampler
  has neither).
- **Installed, acoustic kit:** the usual Drum Kit with the core kit loaded.
- **Missing:** the stand-in exactly as built before, plus the module's `pendingSound` (`Modules/PendingSound.h`) (a Drum Kit instead names the core
  kit, which it plays as synth drums until the pack exists). The swap after the download is in
  [sound pack downloads](../modules/sound-pack-downloads.md#song-builds-and-the-core-instruments).

The song path with Ollama never uses any of this. The plan card offers the download first when a plan names sounds and
the pack is missing.
