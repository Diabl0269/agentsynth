# Module Library Sidebar

`Source/UI/Library/ModuleLibraryComponent/` — one class, declared in `ModuleLibraryComponent.h` and
split across per-concern translation units, none over 1,000 lines:

- `ModuleLibraryComponent.cpp` — construction and destruction, the snippet and plugin data setters,
  `activateRow`, `isEntryEnabled`
- `ModuleLibrarySearch.cpp` — query matching and highlighting, live filtering of `buildRows()`, the
  search field's theme colours
- `ModuleLibraryRows.cpp` — `rebuildEntries()`, `buildRows()`, per-row classification, row lookup by
  position, tooltip and description text
- `ModuleLibraryCollapse.cpp` — section collapse/expand state and its accordion animation
- `ModuleLibraryLayout.cpp` — `resized` / `lookAndFeelChanged` / `parentHierarchyChanged`, the
  scroll offset and its `juce::ScrollBar`
- `ModuleLibraryPainting.cpp` — `paint()` and the highlight, chevron and category-icon helpers it
  calls
- `ModuleLibraryInput.cpp` — mouse events, keyboard navigation, starting a drag-and-drop session
- `ModuleLibraryHelpPopover.cpp` — the "?" help popover's pin/float versus `CallOutBox` hosting

## One layout pass

`buildRows()` returns `{entryIndex, y, height}` for the currently visible rows and is used by
*both* `paint()` and `getEntryIndexAt()`. These previously duplicated the y-advance arithmetic in
two places — a standing invitation for paint and hit-testing to disagree. Guarded by
`ModuleLibraryStructure.HitTestingAgreesWithTheRowLayout`.

Rows carry a `RowKind` (`Header` / `SubHeader` / `Module` / `Snippet` / `Plugin` / `Action` /
`EmptyHint`) and their owning `section`, so collapsing is just "skip rows whose section is
collapsed"; headers stay visible.

Each category section-header row draws a 16x16 category icon at `x=10` using `lf->peekIcon(catIcon)`
and shifts the header text to `x=30`. This is null-guarded: when the `AppLookAndFeel` cast returns
null (headless tests, or assets absent) no icon is drawn and the header text falls back to `x=10`.

The Plugins section sub-groups its rows by format (`VST3`, `AudioUnit`, ...) behind a `SubHeader`
row per format — sorted alphabetically by format, name-sorted within a group, and shown even for a
single format — so a scan with more than one plugin format does not read as one undifferentiated
list. Collapsing the section hides its sub-labels along with everything else.

**`SubHeader` rows are independently collapsible**, keyed as `<Section> :: <SubHeader>` composite
strings via `ModuleLibraryComponent::subsectionKey(section, subHeader)`, a public static helper.
They are kept separate from the section's own `Header` key, so folding a format group never touches
(or is touched by) the section header's fold. A `SubHeader`'s fold rides the same single
`AnimationDriver` as header folds and persists through the same opaque `collapsedSections`
`StringArray` — no schema change. `setAllSectionsCollapsed()` (COLLAPSE ALL / EXPAND ALL) starts
from the current `collapsedSections` set and only adds or removes top-level `Header` keys, so a
subsection fold survives a Collapse All or Expand All untouched.

**The Snippets section stays visible when empty**, showing a "No snippets yet" hint, so the feature
is discoverable before the first snippet exists.

**Chevrons are `juce::Path` triangles, not glyphs.** Coverage of the triangle characters is not
guaranteed across the embedded typefaces (see the font limitation in
[theming](theming.md#typography)). The triangle is drawn once pointing down and rotated by
`-90 degrees x progress`, so it turns with the fold; for a square box the two endpoints are exactly
the shapes the old two-state version switched between.

Rows also carry a hover highlight, a grab / dragging-hand cursor when draggable, per-module
descriptions surfaced as `setTooltip()` via `descriptionFor(name)`, and the search-query substring
highlight described below.

## Search

A `juce::TextEditor` is pinned at the top of the library (`kSearchHeight` = 32), above the COLLAPSE
ALL strip. The two together are `kPinnedChromeHeight`; the scrollbar, the row clip and hit-testing
all start below that band, so the field never scrolls away and a scrolled row cannot steal a click
from it.

Typing a query — trimmed, case-insensitive substring — filters `buildRows()`:

- Module, snippet and empty-hint rows that do not contain the query are hidden.
- A section stays visible when its header matches **or** any of its children match, and a header
  match reveals every child in that section (searching `"Time"` shows Delay and Reverb).
- Matching sections lay out fully open regardless of `collapsedSections`. The stored fold is not
  rewritten and `onCollapseStateChanged` does not fire, so clearing the field restores exactly the
  collapse state the user had.
- Matching runs in the visible label are highlighted with the theme accent (fill plus accent text).
  `highlightSpansFor` is the pure helper `paint()` uses, and tests cover it directly.
- A query that matches nothing leaves `buildRows()` empty and the body draws "No matching modules".
- Filtering is layout-only. `getDraggableModuleNames()` is unfiltered, because callers that
  instantiate through the factory must not see a search-shrunk catalogue.

Escape clears the field.

**The search field's colours are re-applied in `parentHierarchyChanged()` as well as
`lookAndFeelChanged()`.** `MainComponent`'s constructor applies the persisted theme in its ctor
*body*, after `moduleLibrary` — a plain member — was already default-constructed against whatever
theme was active before that, so without this the field showed stale colours until the next theme
switch. `parentHierarchyChanged()` fires when `MainComponent::initialiseCommon()` calls
`addAndMakeVisible(moduleLibrary)`, which always runs after that ctor-body `applyTheme()` call.

## Collapsible sections

With a Snippets section on top of eight module categories the sidebar overflows its height. Every
section header is a disclosure toggle, plus a **COLLAPSE ALL / EXPAND ALL** strip in the top 24 px
(`kTopStripHeight`).

Collapsing and expanding tween over `kCollapseAnimMs` (150 ms, `easeInOutCubic`) via
`AnimationDriver` — no free-running repaints, per the
[time-bounded animation rule](animation.md#the-time-bounded-animation-rule).

- **`sectionProgress` is purely visual** (0 = open, 1 = shut). The logical state stays in
  `collapsedSections` and flips *instantly*, so `isSectionCollapsed()`, `areAllSectionsCollapsed()`,
  persistence and `onCollapseStateChanged` never lag a frame behind what the user clicked.
- **One driver for all sections**, so COLLAPSE ALL folds them together instead of racing nine
  animators. Retargeting mid-flight eases on from the current value rather than snapping back.
- **Rows are truncated, not squashed.** `buildRows()` gives each section a band of
  `naturalHeight x (1 - progress)`; rows keep their natural spacing inside it and are clipped at the
  band's bottom edge (`row.height < kItemHeight` marks a partly clipped row; rows past the band are
  dropped, so they stop hit-testing). `juce::Graphics::drawText` does not clip on its own, hence the
  explicit `reduceClipRegion` in `paint()`.
- **It snaps when not `isShowing()`.** There is no VBlank off screen, so a hidden component would
  otherwise freeze mid-fold. This is also what keeps the headless tests deterministic.
  `setCollapsedSections()`, the launch-time restore, always snaps — animating there would look like
  the sidebar folding itself up on startup.
- **Collapse state persists** as newline-joined section names under `libraryCollapsedSections` in
  `juce::ApplicationProperties`. `setCollapsedSections()` skips blank entries, because an unset
  preference arrives from `StringArray::fromLines("")` as a single empty string, and
  `onCollapseStateChanged` deliberately does *not* fire from it — that is the restore path, and
  re-notifying would write back what was just read.

## Scrolling

With every section expanded the rows exceed any realistic panel height, so the sidebar scrolls.

- **No `juce::Viewport`.** The library is a single painted component: rows come from one
  `buildRows()` pass, and it is also the tooltip client and the drag source. A viewport would split
  all three across an outer wrapper and an inner content component — and, because
  `findParentDragContainerFor()` walks to the *nearest* container ancestor, an inner component would
  bind drags to the sidebar instead of `MainComponent`, breaking drops onto the canvas. Instead a
  `juce::ScrollBar` drives a `scrollOffset` that `paint()` and hit-testing both apply.
- **The COLLAPSE ALL strip stays pinned** in the top `kTopStripHeight` px — the one control that
  shortens an overflowing list must never scroll out of reach. `paint()` therefore clips the rows to
  below the strip and `setOrigin(0, -scrollOffset)`s them, then draws the strip last over its own
  background fill.
- **Two coordinate spaces.** `buildRows()`, `getRowCentreY()` and `getEntryIndexAt()` are all
  *content*-space; mouse handlers go through `getEntryIndexAtComponentY()`, which rejects the pinned
  strip and then adds `scrollOffset`. Mixing them up is the failure mode this split exists to
  prevent — guarded by `ModuleLibraryScroll.HitTestingFollowsTheScrollOffset`.
- **`updateScrollBar()` runs after anything that changes content height** — resize, collapse,
  snippet refresh, theme change (the bar's width is the `kScrollbarWidth` token). It shows or hides
  the bar and re-clamps the offset, so a shrinking list can never leave the view scrolled past its
  end.
- **Rows lose the bar's width** (`getRowContentWidth()`) while it is visible, so row text and the
  snippet count never run under the thumb.

## Help popover

A small themed "?" button sits on the collapse-all strip, left of the COLLAPSE ALL / EXPAND ALL
label. `ModuleLibraryComponent::getHelpButtonBounds()` is the one rect `paint()` and the mouse
handlers (`mouseMove` / `mouseDown` / `mouseExit`) all read, so the drawn button and the clickable
one can never drift apart — the same "one enumeration" rule `buildRows()` follows for the rows. Its
hover state (`helpButtonHovered`) is tracked independently of the strip's own hover flag, so the
tooltip can name the button specifically ("Open a quick guide...") rather than reusing the
collapse-all strip's tooltip.

Clicking it shows `synth::ui::ModuleLibraryHelpPopup`
(`Source/UI/Library/ModuleLibraryHelpPopup.h`) — a self-painted opaque panel, the same pattern
`MidiDestinationPicker` documents, because a parentless `CallOutBox` does not necessarily inherit
`synth::theme::AppLookAndFeel`, and neither does a floating window. The popup holds three plain
disclosure sections — **Using modules**, **Your first patch** and **Key shortcuts** — open by
default, each collapsible via its own header row. There is no accordion animation: a one-shot guide
read once and dismissed does not need the sidebar's animated fold. It scrolls via a
`juce::Viewport` when the expanded content overflows `kMaxHeight`, and collapsing a section re-sizes
the popup itself, the same rebuild-then-resize pattern `MidiDestinationPicker::refreshRows()` uses.

**Content is data first.** `usingModulesLines()` / `firstPatchSteps()` / `shortcutLines()` are pure
static helpers, so a test can assert on the guide's text without ever constructing a
`juce::Component` — the same idiom `ModuleLibraryComponent::descriptionFor` already uses. The "Key
shortcuts" section resolves BOTH halves of each line live: the key via `shortcutHintFor` and the
label via `ShortcutManager::getActionDescription`, so it can never drift from the Settings tab's own
wording. `ModuleLibraryComponent::setShortcutManager()` is how an owner wires the live manager in —
read-only, the sidebar never rebinds anything; unset (every headless test) falls back to each
curated shortcut's shipped default via `shortcutHintFor`'s own null-manager contract. The "Your
first patch" steps are the minimal audible patch, verified against `Source/PresetManager.cpp`'s
Default preset and `VCAModule`'s own CV handling (see
[modules.md](../modules/modules.md#vca-amplifier-module)): Poly MIDI to Oscillator to VCA to Audio Output,
with an ADSR into the VCA's CV input — not optional shaping, since an unpatched VCA CV input reads
as silence rather than an implicit fully-open value.

### Pin or float it over the canvas

The popover's header carries a pin icon (default **off**) and a close (X). Unpinned it behaves as
above: `ModuleLibraryComponent` wraps it in a `juce::CallOutBox`, dismissed on outside click or Esc.
Pinning re-hosts the SAME popup instance as a plain, non-modal child of an ancestor component
instead — `addAndMakeVisible`, never a new desktop window — so it stays up while the user builds the
"Your first patch" chain on the canvas, closed only by the header's X.

**Why the popup is owned manually rather than launched asynchronously.**
`juce::CallOutBox::launchAsynchronously()`'s content is owned by an opaque, private
`ModalComponentManager::Callback` that deletes the box AND its content together the instant either
is dismissed — there is no safe way to detach a `launchAsynchronously`'d component and keep using
it. So `ModuleLibraryComponent` never calls it. It owns ONE persistent `ModuleLibraryHelpPopup`
(`helpPopup_`, created lazily, never rebuilt) and, when unpinned, wraps it in a *manually*
constructed `CallOutBox(Component&, area, parent)` — the plain, non-owning constructor — entering
modal state itself with `deleteWhenDismissed = false`. That reproduces the exact
dismiss-on-outside-click/Esc behaviour (`CallOutBox::inputAttemptWhenModal()` only ever calls
`exitModalState()` plus `setVisible(false)`, never `delete`) while keeping full manual ownership, so
a pin click can safely reparent the same object into `floatingHelpHostFor()` — the root of whatever
ancestor chain the sidebar is currently in, walked generically rather than naming `MainComponent`,
so the popup can float over the canvas without this file depending on that class. Un-pinning
reverses the transplant by constructing a fresh callout around the same object;
`juce::Component::addAndMakeVisible` auto-detaches a component from wherever it was parented before,
so neither direction needs an explicit remove-from-old-parent step.

**A persistent instance means the shortcut text must be regenerated per open.**
`refreshShortcutSection()` re-generates it against the live `ShortcutManager` and
`ModuleLibraryComponent::showHelpPopover()` calls it on every open — otherwise a rebind made in
Settings while the popup object is alive would freeze at whatever it said when the popup was first
built, staling exactly the thing "resolved live" is supposed to guarantee never happens.

Dragging the floating popup by its header is a two-line `juce::ComponentDragger` use in the header
bar's `mouseDown` / `mouseDrag`, gated off entirely while unpinned (`setDraggable(pinned)`) so it
never fights a `CallOutBox`'s own self-repositioning. Pin state is session-only and is not persisted
across app restarts.

**The floating position is always computed and always clamped.** The help button anchor sits in the
sidebar's topmost strip, only a few px below the top of the window, so there is almost no room above
it: placing the floating popup at "wherever the `CallOutBox` happened to be on screen", translated
verbatim into the floating host's local space, landed it at a slightly negative Y on any small
mismatch (a border inset, display-scaling rounding, a host whose top-left is not literally screen
0,0). Since the header is drawn at the popup's own local `(kOuterPadding, kOuterPadding)`, right at
its top, a negative Y pushes exactly the header above the host's visible area while the scrollable
body content, lower in local space, stays on screen — leaving no title, pin or X to click.

Two helpers on `ModuleLibraryComponent` are used everywhere a floating position is set, and are what
keep that from recurring: `defaultFloatingPosition()` computes a sensible spot — just right of the
sidebar, level with its top, clear of any toolbar above it — instead of trusting the callout's
screen position at all; `clampToHost()` then constrains that (or any other candidate) so the
popup's entire rect stays inside the host's local bounds, falling back to the host's top-left corner
— never negative — when the popup is larger than the host in either axis.
`ModuleLibraryComponent::resized()` also calls the clamp (`reclampFloatingHelpPopover()`) whenever
the sidebar reflows, since a window resize is the other way this class of bug resurfaces.

The header itself is structurally safe either way: `TopBar` is a plain sibling of `viewport_`, laid
out first in `resized()`, so it can never be clipped into the scrollable area.

The pin icon has a hover state (an accent-tinted highlight behind the glyph, mirroring the sidebar's
own "?" button treatment) and a tooltip ("Pin - keep open while you work") via
`juce::SettableTooltipClient`, resolved dynamically per hovered icon the same way
`ModuleLibraryComponent::mouseMove` resolves its per-row tooltip. The close (X) gets the same hover
treatment and a plain "Close" tooltip. The header's title reads simply "Help".

**Opening the popover is split across two `protected virtual` leaves**, the same seam idiom
[cables](cables.md) and `TimelineRulerComponent::openMarkerContextMenu` use for any real popup or
menu window — a `juce::CallOutBox` launched in a display-less test runner is the exact SIGSEGV trap
documented in [ruler](../timeline/ruler.md#opening-a-menu-is-a-protected-virtual).
`showHelpPopover()` is the pin-aware dispatcher (ensure the popup exists, refresh its shortcuts,
decide float versus callout), and `launchHelpCallOutBox()` is JUST the real `CallOutBox`
construction — split apart so a test can override only the second, never creating a real window,
while the first still runs its real logic. That is what lets pinning, closing and the survives-an-
outside-click guarantee all be exercised headlessly.
`ModuleLibraryComponent::createHelpPopupForTest()` is the separate seam for the popup's CONTENT: it
returns the SAME persistent object the real button shows, creating it on first call, never a fresh
lookalike, mirroring `PreferencesSettingsTab::createDualIOPerModuleDefaultsPopupForTest`.

## The Keyboard Shortcuts settings tab mirrors this pattern

`Source/UI/Settings/ShortcutsSettingsTab.h/.cpp` — the Settings "Keyboard Shortcuts" tab — grew the
same collapsible-section idiom once its row count passed 49 (see [shortcuts](../shortcuts.md)): one
collapsible section per `ShortcutCategory`, a search field above them, and a top strip whose label
flips between "COLLAPSE ALL" and "EXPAND ALL", lifted from `ModuleLibraryComponent` so the app's two
collapsible lists behave identically — clickable header rows with a chevron, a collapsed set keyed
by the header's identity, the same strip idiom.

Two things it deliberately does NOT copy, both because the two components live in different
contexts:

- **No fold animation.** The library's accordion is a VBlank-driven `AnimationDriver` over a
  hand-laid-out row list; here the rows are real child components inside a `juce::Viewport`, so
  animating a fold would mean animating child bounds every frame for no benefit inside a modal
  settings dialog. Collapsing is instant.
- **No persistence of the collapse state.** This tab is constructed fresh every time the Settings
  window opens and nothing has asked for the folds to survive that, so keeping the set in memory
  costs no new settings key to migrate.

**Search matches BOTH the action's description and its current binding text** — "cmd" finds every
Cmd shortcut, "transpose" finds the piano-roll block. Searching only the description would make the
list useless for the commonest question, "what is on Shift+Q?". An active filter FORCES a matching
section open without touching its collapse flag (`sectionIsExpanded`), so a match can never be
trapped inside a fold, and clearing the query restores exactly the folds the user had; a section
with no surviving row is dropped entirely rather than left as a lone header over empty space
(`sectionIsVisible`) — the same rule `ModuleLibraryComponent::buildRows` applies.

**Row indexing is a contract, sections or no sections:** row `i` is always
`ShortcutManager::getActionIds()[i]`. The section headers are separate widgets, never entries in the
row vectors, and `ShortcutManager`'s action table keeps each category's ids CONTIGUOUS so a section
is always one unbroken run (`ShortcutsSettingsTabTests` pins row `i` to `ids[i]`). One layout pass,
`rebuildLayout()`, paints the rows, hit-tests the header clicks and positions the child bounds, so
the three can never disagree about where a row is.

Group-separator hairlines use `kDividerAlpha = 0.10` here, drawn from the text colour at low alpha
rather than a theme token, so they read on both light and dark themes with no token of their own.
`PreferencesSettingsTab` keeps its own separate constant (`0.12`, softened from an earlier `0.18`
that read as table borders and boxed each preference in) in step with this one by comment rather
than by a shared header, since a one-line float is not worth a dependency between two settings tabs.
