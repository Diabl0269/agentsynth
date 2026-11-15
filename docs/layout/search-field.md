# Search field

Every search or filter box in the app is one class, `synth::ui::SearchPillField` (`Source/UI/Layout/SearchPillField.h`):
a 26 px pill with a magnifier, a placeholder, and, while the box is empty and wide enough, the key that focuses it as a
key cap on the right. How rows match and highlight is a separate rule ([Shared search](chrome.md#shared-search)).

## What the user sees

- **Pill.** 26 px tall, radius 13, fill `surface`, 1 px `border`. A 14 px magnifier (1.5 px stroke, `textMuted`) on
  the left; 12 px text; the placeholder in `textMuted`, shown focused or not.
- **Focus ring.** The border turns `accent` and the shared focus ring (`FocusRing.h`) sits just outside with a 2 px
  gap in `bg0`. It fades in over 100 ms on `ShownFade`. It paints outside the field's own bounds
  (`setPaintingIsUnclipped`), so a field needs 4 px of room around it; the parent repaints that margin.
- **Key cap.** While the field is empty, the key that focuses it, drawn by `AppLookAndFeel::drawShortcutKeyCap`
  (the same key cap as the Cmd-hold hints) at 18 px tall. The text comes from the shortcut settings
  (`setShortcutFromAction`, or `setShortcutHint` with `hint::formatKeyCapTextForPlatform`), so a rebound key shows its new
  key and macOS shows glyphs. A box with no rebindable action focusing it shows no cap: do not invent one.
- **Clear button.** While there is text, a 16 px round button (`surfaceHi`, a `textMuted` x) takes the cap's place.
  Clicking it (or Return on it) empties the field, keeps focus in the field and fires `onTextChange`.
- **Too narrow.** The cap needs the whole placeholder plus its own room beside the magnifier, so it is left out where
  the field is narrower than that. The text, the placeholder, the magnifier and the clear button never overlap it.

## Motion

The cap fades on `FadeAmount` and the clear button on `FadeVisibility` (160 ms in, 110 ms out, 80 ms under Reduce
Motion, none under Animations Off, [animation](animation.md#fading-things-in-and-out)); the cap also fades out when the
field narrows. No second animator.

## Adding a search box

Use `SearchPillField` and set `setTitle` (the screen-reader name), `setPlaceholder`, `setSearchTooltip` (the tooltip,
with the key when a cap is set) and the bounds (26 px, `SearchPillField::kHeight`). Never set the field's colours,
font, borders or indents: it takes them from the theme in `applyTheme()` (so all four themes match) and re-reads them in
`lookAndFeelChanged`. Keep the site's own Escape behaviour (`onEscapeKey`, `onNavigationKey`).
`SearchPillField::setText` also refreshes the cap and the clear button, so a silent `setText(x, dontSendNotification)`
is fine.

`SearchBoxGuardTests` fails by file name for a raw `juce::TextEditor` or `NavigationSearchField` named for a search or
filter, or given a "Search ..." placeholder of its own. The one `NavigationSearchField` left outside the pill is the
mod dot source row's numeric amount editor (`ModDotSourceRow`), which is not a search box.

## Where it is used

The library's Add list, the Sounds browser, the Cmd+K bar and every `ModMatrixPicker` (Add track, Add source, Add
target, the pickers), `KeyPicker`, the port panel's `PortTargetSearchPage`, the mod dot's `ModDotAddSourcePage`, the
card layout editor's control search and Add control panel, the Preferences and Keyboard Shortcuts filters, the MIDI
Remote action picker, the MIDI destination picker and the Mixer zones filter.

Keys on the cap today: Cmd+F (`focusLibrarySearch`) on the library's Add list and the Sounds browser, Cmd+K
(`openCommandPalette`) on the Cmd+K bar.
