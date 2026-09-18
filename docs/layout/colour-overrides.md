# Colour Overrides and the Picker

Two semantic colour sets — piano-roll notes and mixer meters — resolve from theme tokens and can be
replaced by a sparse user override layer, plus the one colour picker every swatch in the app opens.
Cables follow the same pattern and are documented with the wires they colour, in
[cables](cables.md#user-overrides); the tokens all three read are in
[theming](theming.md#colours).

## Note colours

Piano-roll note bodies are coloured through a single resolver, `synth::ui::resolveNoteColour()` in
`Source/UI/PianoRoll/NoteColour.h` — the note-colour analogue of `resolveCableColour()`. Nothing in
`PianoRollComponent::paint()` picks a note fill or border directly. The resolver is a pure function
of (theme colours, pitch, velocity, selected, muted, outOfScale, per-pitch-class overrides) with no
GUI state, so it is headless-testable on its own
(`Tests/UI/PianoRoll/NoteColourTests.cpp`).

**Precedence**, highest first:

1. An out-of-scale note — flagged by Scale Assist's pitch-visibility or quantize context — always
   forces `noteOutOfScale`. A warning wins over any colour choice, including a per-pitch-class
   override.
2. Otherwise a per-pitch-class override wins over the theme's `noteFill`.
3. Otherwise `noteFill`.

Velocity brightness and the selected and muted treatments then apply on top of whichever fill won.

**Per-pitch-class overrides are a sparse layer.** `synth::ui::NoteColourOverrides` holds up to 12
entries, indexed by `pitch % 12` (0 is C, 11 is B) rather than by MIDI note number, so one override
recolours a pitch class across every octave. An unset entry means "follow the theme's `noteFill`",
so a theme switch still moves any pitch class the user has not explicitly pinned.

They persist under the single properties key `"pianoRollNoteColourOverrides"` — 12 comma-separated
slots, each empty or a `juce::Colour::toString()` value — via `loadNoteColourOverrides` and
`saveNoteColourOverrides`. **A malformed slot count is treated as "no overrides at all" rather than
partially applied**, so one corrupted slot can never take the other eleven down with it.

**The UI** is the Appearance tab's "Piano Roll Notes" swatch row (`AppearanceSettingsTab`, one
12-cell row keyed C to B): left-click opens a `synth::ui::ColourPickerPopup`, right-click resets that
pitch class's override, and "Reset all" clears every pitch class in one action.

Every swatch — overridden or not — previews the colour through
`AppearanceSettingsTab::getNoteSwatchPreviewColour()`, which calls the exact same
`resolveNoteColour()` the roll paints notes with (a representative unselected, in-scale note at a
fixed velocity chosen so its brightness multiplier is about 1.0), composited over the panel
background so the resolver's deliberate fill alpha cannot read as a washed-out or darker chip than
the real note. Drawing the un-overridden swatch at a flat, hand-rolled low alpha instead of going
through the resolver is exactly what made the preview read noticeably darker than the piano roll's
actual notes.

"Not set" versus "pinned" is told apart by the swatch's **ring**, not the fill — a brighter ring
marks a pinned pitch class, the same affordance as the cable-colour swatches.

## Meter colours

The mixer meter — both bars in `MixerMeter` and the track header's `ChannelChipComponent` — has a
level-to-colour zone model, `synth::ui::MeterColourStops`
(`Source/UI/Mixer/MeterColourStops.h`; the mechanism is in [mixer](../mixer.md)'s Meters
subsection): below -18 dBFS is `meterFill` (low), -18 to -6 is `meterMid`, -6 to 0 is `meterHigh`,
and above 0 is `meterClip`. Four theme tokens, one per zone.

Settings -> Appearance's "Meter Colours" section
(`Source/UI/Settings/MeterColourStopsEditor.h`) lets those four theme tokens be replaced by an
arbitrary set of 1 to 8 user-positioned stops.

**Tokens versus the user override — the same "unset means follow the theme" relationship cables and
notes use, with one difference.** `MeterColourStops::fromTheme()` reads the active theme's four zone
tokens; a user override (`meterColourStops` in `ApplicationProperties`, `{db, ARGB}` pairs, with the
persistence format in `Source/UI/Mixer/MeterColourStops.h`) replaces them **wholesale, not
per-zone** — there is no partial pin the way a single cable signal or category can be pinned while
the rest follow the theme. An absent key means the active theme's four tokens, and a theme switch
moves them live; a present key means those exact stops, regardless of which theme is active, until
"Reset to Theme" removes it. Malformed stored data — one bad token, a bad count — is treated as
absent, never a partial apply and never zero stops, the same corrupted-means-theme-defaults rule
`pianoRollNoteColourOverrides` follows.

**One cache every painter reads.** Unlike cable and note colours, each resolved fresh at paint time
from a cheap pure function, a `MeterColourStops` is not cheap to hand-roll per pixel. So the
EFFECTIVE stops — the override if pinned, else the current theme's own four — are cached once, on
`synth::theme::AppLookAndFeel` itself (`getMeterColourStops()` / `setMeterColourStopsOverride()`,
recomputed in `applyTheme()` and on every override change). That is the same single
per-app/per-plugin-instance object every mixer column, Master, a detached mixer window and the track
header chip already reach via `getLookAndFeel()` for every other themed colour. Editing the section
in Settings writes the override, then the cache is refreshed and every visible meter repainted
ONCE — never rebuilt per paint tick.

**The live-apply push goes through the settings file's own `ChangeBroadcaster`**
(`MainComponent::changeListenerCallback`), not a direct pointer from the tab: `SettingsWindow` is its
own `juce::DialogWindow`, so `getLookAndFeel()` called from inside the tab is not guaranteed to
resolve back to the app's real `AppLookAndFeel`, and the plugin build never calls
`Desktop::setDefaultLookAndFeel` at all. See
[testing_mixer_meters.md](../testing_mixer_meters.md) for the full test list.

## Colour picker popup

`synth::ui::ColourPickerPopup` (`Source/UI/Chrome/ColourPickerPopup.h`) is the one full colour
picker in the app — a `juce::ColourSelector` plus a favourites shelf — shared by the timeline track
header's colour swatch, the timeline ruler's marker menu, the macro Change Colour item (see
[macro-cards](macro-cards.md#the-macro-menu)) and the Appearance tab's note-colour swatches, rather
than each rolling its own `juce::CallOutBox` plus `ColourSelector`.

Favourites persist under the single properties key `"favouriteColoursArgb"` — comma-separated
`AARRGGBB` hex, where a malformed token is dropped rather than corrupting the whole list — via a
caller-owned `juce::PropertiesFile*`. **`nullptr` is legal and means "in-memory only for this popup
instance"**, which is what keeps the class usable from a headless test with no
`ApplicationProperties` at all. A first run with no persisted favourites seeds the shelf from the
existing track palette, so it is never empty on a fresh install.

The popup exposes two callbacks: a **live-preview** one, fired on every drag or favourite click,
writing straight into the caller's target with no undo step; and a **commit-once** one, fired when
the popup closes, deciding the final colour.

The track-header colour swatch is the reference caller for the commit-once half's undo semantics.
Closing with no net change restores the original colour and records nothing; closing on a different
colour first silently restores the original, outside any undo-recorded mutation, and then performs
the real edit as the ONE undo step whose undo target is the original colour. Dragging through a
dozen preview colours before landing on a choice therefore costs exactly one Cmd+Z, not a dozen.

**A caller may fan the preview out to more than one target.** A timeline track that is LINKED to a
mixer channel builds its picker through `TrackChannelLinkController` instead, whose preview callback
writes the track colour AND the channel macro's colour on every drag frame, whose no-net-change
close restores both, and whose commit restores both and then performs ONE compound undo step
covering them. **The popup's own contract is unchanged** — fanning out is entirely the caller's
business.

**Committing always reconciles with `juce::ColourSelector::getCurrentColour()`** rather than trusting
whatever the last dispatched preview happened to be. A real slider drag or hex-field edit updates the
selector's own displayed state — the header swatch and the R/G/B/hex fields — synchronously, but only
reaches this popup's preview callback once `ColourSelector`'s change broadcast is actually
*dispatched*, which is asynchronous. If the popup is torn down between the user finishing an edit
and that broadcast arriving — the same click that commits a hex-field edit via focus-loss can also
be the click that dismisses the `juce::CallOutBox` — a commit that only replayed the last preview
would apply a stale, one-edit-old colour while the header and fields already showed the new one.
`commitOnce()` re-previews from the selector's live colour immediately before firing the commit
callback, closing that gap for every consumer.
