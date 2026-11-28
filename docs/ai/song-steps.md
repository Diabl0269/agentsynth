# Song steps (hosted whole songs)

A whole song asked of the hosted service takes two requests instead of one `project.generate`: a **free, priced
plan**, then a **build streamed step by step** that ends in the same edit plan the chat already applies. The server
side (routes, token, prices) is the hosted service's own song steps doc; this page is the app's half.

## The flow

1. `AIIntegrationService::planSong` posts `/v1/song/plan` (`RemoteProvider::sendRouteRequest`): the prompt, the chosen
   Length (`short`/`medium`/`long`) and Detail (`light`/`full`/`rich`), and, when re-planning, `previousPlan` (the plan
   object kept verbatim from the earlier reply), plus `songKey` (the same `"D Dorian (D E F G A B C)"` text `project.generate` gets) when the project has a song key, plus `melody` when a melody clip is selected (see "A given melody" below). Both requests (plan and build) also carry `instruments`, the names in
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
   edit plan. Before applying, the plan's `key` ("E minor", "Bb major", "A dorian") is read by `parseKeyText` (`Music/SongKey.h`) and, when readable and the envelope has no `setSongKey` of its own, appended to the envelope as a `setSongKey` op, so the Key field changes inside the build's one undo step (an unreadable key leaves the project's key alone; a key equal to the current one adds nothing to the undo step). A refused envelope is reported in the chat and the plan card stays out of it. The chat history gets the
   user's words and a one-line answer, so later edits have the context.

## When the chat uses it

`AIChatComponent::shouldPlanSong` (pure, tested) decides. The song path is used when the provider is hosted, a
timeline is wired in, the service has not answered a plan with 404, **and** the message looks like a whole-song
request: a song word (song, tune, beat, instrumental, full or whole track, "from scratch", a "N-minute" length, a few non-English words such as canción or Lied) or a genre/style name with no
edit verb and no module type in it, or any message while a plan card is waiting to be built. Edits to an existing project
("add a filter to the lead", "make the bass wobble") never match and keep the single `project.generate` request.
Ollama never uses the song steps.

A song-looking message is always a **song turn** in the chat record (the export's `kind`), on every route: the hosted
plan, the 404 fallback and a provider with no song routes (one line says so, then the single request runs). As a
backstop, an edit answer that adds two or more instrument tracks is recorded as a song too.

## Fallback

A plan request answered with 404 (a service without the routes) sets `canPlanSongs()` false for that provider and the
chat sends the same text through `sendProjectMessage`, after one assistant line ("Song steps aren't available on this server, so I'm building it in one go."), and the turn stays a song so the missing steps are explained. The check costs one free request per session.

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
[animation](../layout/animation.md#song-steps). A song message has the build card as its **only** card (no edit-plan card): once built, the card adds the "Details" fold (`songsteps::buildSongDetailsText`, `SongDetailsText.h`: a line per track with its sound modules and note count, the sections with their bars, a line per automation lane; stored in `SongSession::detailsText`) and the thumbs feedback. Undo is the project's own (one step, as the card says). The edit-plan card's rows carry the same kind icons, still before Apply,
animated once in order on Apply.

## The song card

### A given melody

With exactly one MIDI clip selected on the timeline (for example the one "Convert to melody" made from a hum), a NEW
song plan request carries `melody`: `{name, bars, notes, bpm}`. `name` is the clip's name (cut at 60 characters), `bars`
is the clip's length in 4/4 bars rounded up and held to 1 to 8, `notes` is space-separated `start:length:pitch:velocity`
tokens in beats from the clip's start (at most 3 decimals, trailing zeros trimmed, sorted by start then pitch), and
`bpm` is the transport tempo. Muted notes are left out; only the first 8 bars go (a note starting later is dropped, one
running past is shortened); at most 2000 notes; a clip left with no note sends no melody. An audio clip, several selected
clips or none send nothing (`givenMelodyFromNotes`, `givenMelodyFromSelection`, `AIIntegrationService::setGivenMelodySource`).
A re-plan never carries it: the server keeps the first plan's melody.

When the server built around it, the plan has `melody: {track, name, bars, notes, bpm}` (`SongPlan::melodyTrack`,
read only when `track` names one of the plan's tracks) and the plan card adds a line under the key line, `The "Lead"
track plays your melody "Hum melody" (4 bars), note for note.` (`planMelodyText`). A server that ignores the field returns no
`plan.melody`; the card then says `This server can't build around a melody yet, so the song ignores "Hum melody".` The sent
melody's name is kept on `SongSession::sentMelodyName`, through re-plans.

Both cards start the same way: the **song's name**, then a line with its **length, tempo (BPM) and key** (`planMetaText`,
for example "3:20 · 174 BPM · A minor · 7 sections"), so the tempo and key are in the header from the plan on through the
build to the built song. When the plan has sections in a different key than the song's (a `key` on a section, read as
`PlanSection::key`), the plan card adds a line under it, "Key A minor, changes to C major at bar 17 (Chorus)"
(`planKeyChangesText`; none for a single key). Below that the build card shows its state, the step running now and a progress bar, then two
folds in the app's standard fold header (`FoldSectionHeader`, the Settings tabs' header, so the arrow, the focus ring and
the "expanded / collapsed" name are shared):

- **Steps**, with "2 of 9 done" as its summary: a sunken box (`StepBlockBox`, `StepBlocks.h`) with **one block per step**.
  A block is the step's kind art in the kind's own colour, with a small check once done; the running step has a highlight
  ring that glides from block to block. The blocks wrap onto further rows; past 200 px the box scrolls vertically, so 30
  steps never run off the side or clip. Each block is a Tab stop (arrows move between them) with a name and a tooltip
  (what the step is and what it did).
- **Details** (once built): the track-by-track text.

**Colour by kind.** `stepKindColour` (`StepIcons.h`) gives each of the eleven step kinds one colour from the theme's own
tokens (the cable category hues, the warning colour for Fix, the violet hue for Mix, the neutral ink for Plan), so it
holds in every theme including Daylight. Cards, blocks and the running icon all ask it; nothing is drawn in a colour of
its own. `StepKindColour.EveryKindHasItsOwnColourInEveryTheme` keeps the eleven apart.

**The sections strip** (plan card) has a caption, "Sections", a tooltip saying that a block's width is its length in bars,
and each block shows its name and bars (shortened, never one letter). Hovering a block says what goes into it: its
energy, the tracks that play in it and what the plan says it does.

**Thumbs position.** A small row of two icon-only thumbs, **right-aligned to the card content's right edge, under
the finished content, before the follow-ups**; it fades in when the song is built, after the Steps and Details folds.
The chat apps (ChatGPT, Claude) put the rating at the right end of the action row under an answer, so the thumbs sit
there too, and the edit-plan card's rating row is on the same side with the same gap (`ThumbButton::kGap`) so every
rating row agrees (`TheRatingRowIsRightAlignedOnTheSongCardAndTheEditPlanCard`). The thumbs are drawn with paths
(`ThumbButton`), not text glyphs; a picked one is filled (green like, red dislike), a second press on it takes the
pick back, and a comment row, full width, opens under the row once one is picked.

**Follow-ups.** Once the song is built, three suggestion chips sit under the thumbs (and their comment row):
`songsteps::songFollowUps(plan)` (`SongFollowUps.h`, pure) gives a change request, a key or arrangement change and a
question ("Make it darker", "Change the key", "Why did you pick this tempo?"). They are quiet outlined buttons that
wrap onto a second line when the card is narrow, fade in with the thumbs and count in the card's height. They are Tab
stops after the thumbs and the comment row (ids `songFollowUp0` to `songFollowUp2`, each named by its text, tooltip
"Put this in the chat box"). Pressing one calls `Actions::onFollowUp`; the chat puts the text in its input, moves
keyboard focus there with the caret at the end and sends nothing (an edit costs a use). While the latest message is a
built song the input's hint reads "Ask for a change to this song, or ask why". A message typed after a built song is an
ordinary one: on a hosted provider it asks for an edit plan through `project.generate`, and no suggestion contains a
song word, so it never starts a new song plan (`SongFollowUps.AreThreeShortPlainSuggestionsThatNeverReadAsANewSong`).

## Real instruments

Both requests carry `sounds: true`, which lets the service name an instrument from the core sound pack on a track: a
plan track may carry `coreSound`, and the build's `addInstrumentTrack` op `sound`. The value is one of the ids in `SoundPacks/CoreSounds.h`: nine in the `core` pack (`core/upright-piano`,
`string-section`, `trumpet`, `french-horn`, `flute`, `clarinet`, `double-bass-plucked`, `electric-guitar`,
`acoustic-kit`) and five one-shot animal sounds in the `animals` pack (`animals/whale`, `crow`, `owl`, `frog`, `wolf`,
all standing in for an Oscillator and played at their recorded pitch on every key). The table gives the stand-in synth
each one comes with (Mallet, Wavetable, Pluck, Oscillator or Drum Kit). The plan parser keeps a `coreSound` only when it is in the table, and
the op reader accepts a `sound` only when the op's `instrument` is its stand-in; otherwise the sound is ignored and the
track is built from the synth alone.

What a track gets depends on the pack (`MainComponentTimelineOpsHostSounds.cpp`):

- **Installed, melodic:** a Sampler in Instrument mode loaded with the pack instrument and the sound's level trim
  (`trimDb` in the table, see [sound pack downloads](../modules/sound-pack-downloads.md#levels)), with the track's name
  and notes unchanged. The track body is the stand-in's: a Wavetable stand-in's ADSR and VCA are built with the op's
  `envelope` params, so the envelope id the ops address exists (the Sampler is built mono, whatever `poly` said). The
  stand-in's instrument params are dropped except Level.
- **Installed, acoustic kit:** the usual Drum Kit with the core kit loaded.
- **Missing:** the stand-in exactly as built before, plus the module's `pendingSound` (`Modules/PendingSound.h`) (a Drum Kit instead names the core
  kit, which it plays as synth drums until the pack exists). The swap after the download is in
  [sound pack downloads](../modules/sound-pack-downloads.md#song-builds-and-the-core-instruments).

A patch written for the stand-in can name a part the Sampler does not have (a Wavetable's `detune`, `pan`, `position`). `runPlan` (`AIIntegrationServiceProjectEditRun.cpp`) drops each such modulation, with a plan note, before `validatePatch` sees the patch: only a modulation onto a node the app built as a pack Sampler (`InstrumentTrackBuildResult::substitutedSampler`) and whose `destParam` or `destPort` that Sampler has no jack for. `level`, `pitch`, `attack` and the rest stay, every other node is still validated as sent, and `validatePatch` itself is unchanged. The preview's `StandInHost` builds the same Sampler, so a preview never passes what the apply would refuse.

The song path with Ollama never uses any of this. The plan card offers the download first when a plan names sounds and
a pack it names is missing.
