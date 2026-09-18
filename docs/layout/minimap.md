# Minimap Overlay

`Source/UI/Graph/MinimapComponent.h/.cpp` (`synth::ui::MinimapComponent`) — a small always-current
overview of the graph.

It is an untransformed sibling overlay on `GraphEditor`, the same pattern as the mod-matrix panel
(see [chrome](chrome.md#mod-matrix-panel)): it does not live inside the panned and zoomed
`GraphContentComponent`, so its own bounds are plain screen space. Everything it draws is expressed
in **canvas coordinates** — the space module cards and cables already live in — and mapped down to
the small map area with `computeWorldToMap()`.

## Placement, sizing and auto-hide

Positioned **bottom-left** with a 12 px margin, sized `min(220, w/4) x min(150, h/4)`.

**Bottom-left is deliberate**: the mod-matrix panel occupies the right-hand 600 px.

Below a 480x360 editor the minimap auto-hides. `GraphEditor::resized()` recomputes
`minimapVisible && fits` on every layout pass against **absolute floors** — a fraction-of-self test
such as `w/4 * 2 <= w` is always true, so it would never actually hide anything. This never clobbers
the user's preference: `minimapVisible` still records what they asked for, and the map reappears the
moment the window grows back past the floor.

## What it draws

It renders from a `MinimapModel` snapshot — nodes, cables and the current viewport rect, all in
canvas coordinates:

- **Nodes** — filled rounded rects in the module's per-category theme colour
  (`themeColourForCategory`), clamped to a `kMinNodeSize` (2 px) floor so zoomed-out modules stay
  visible; selected nodes get an additional `accent` stroke.
- **Cables** — thin straight lines in the cable's colour at reduced alpha, not the bezier the canvas
  draws. This is a thumbnail, not a second connection view.
- **Viewport** — the area outside the currently visible canvas rect is dimmed with a translucent
  `bg0` wash (an even-odd path fill punches a hole over the viewport rect rather than drawing four
  separate bars); the viewport itself is stroked in `accent`.

`computeWorldBounds()` derives what the map actually shows: the union of every node's bounds and the
viewport, inflated by an 80 px margin (`kWorldMargin`) and never narrower or shorter than 1200 px
(`kMinWorldSpan`) per axis, so a single module does not blow up to fill the map. All three drawing
and hit-testing helpers — `computeWorldBounds`, `computeWorldToMap`, `mapToWorld` — are pure static
functions with no `Component` state, unit-tested directly in
`Tests/UI/Graph/MinimapComponentTests.cpp`.

## Interaction

A click or drag on the map converts the event position to a canvas point (`mapToWorld`) and fires
`onNavigate`, which `GraphEditor` wires to `centreViewOn()` — it pans so that point is centred, and
zoom is unchanged. The scroll wheel fires `onZoom` with the wheel's `deltaY`, wired to
`zoomAroundCentre()`.

Both the minimap's wheel-zoom and the canvas's own (`GraphEditor::mouseWheelMove`) share one private
helper, `applyZoomAt(wheelDelta, screenAnchor)`: the canvas anchors on the mouse position, the
minimap on the visible area's centre, but the zoom curve and the `[0.1, 2.0]` clamp are identical.

Hovering the map shows a tooltip that leads with the show/hide shortcut, the same way the toolbar
buttons read: `"Hide Minimap  (Cmd+K)  - click or drag to navigate, scroll to zoom"`. **The binding
is rebindable, so `MinimapComponent` does not depend on `ShortcutManager`**: it exposes
`setShortcutHint(displayString)` and `MainComponent::applyToolbarIcons()` resolves the current
binding and pushes it down alongside the toolbar tooltips. Because tooltips embed the resolved
keypress, `MainComponent::updateCommandShortcuts()` — the `ShortcutManager::onBindingsChanged` hook
— re-runs `applyToolbarIcons()`; without that, every toolbar hint *and* the minimap's keeps
advertising the pre-rebind key until some unrelated toggle happens to refresh it.

## Repaint discipline

Follows [rendering](rendering.md). `setModel()` and `setViewport()` only `repaint()` when the
incoming data actually differs from the current model (`MinimapModel::operator==`, a field-wise
comparison over nodes, cables and viewport).

`GraphEditor::timerCallback()` — the existing 30 Hz tick that already drives the connection-flow
animation — pushes a freshly built model via `buildMinimapModel()` **only while the minimap is
visible**, so a hidden minimap costs nothing: no graph walk, no model diffing.

`updateTransform()`, called on every pan and zoom frame, pushes **only the viewport rect** via
`setViewport()`, because panning and zooming change what is visible, not where modules or cables
are. Rebuilding the full model on every drag frame would re-walk every graph edge for nothing.

## GraphEditor API

| Member | Purpose |
|---|---|
| `setMinimapVisible(bool)` | Sets the user preference and re-runs `resized()`'s fits check; also seeds a full model immediately so the map is not blank until the next 30 Hz tick |
| `toggleMinimapVisibility()` | `setMinimapVisible(!minimapVisible)` |
| `isMinimapVisible()` | The user preference, not the fits-adjusted effective visibility |
| `getMinimap()` | Direct access to the `MinimapComponent` |
| `getVisibleCanvasRect()` | The canvas rect currently visible — the inverse of the content transform applied to `getLocalBounds()` |
| `centreViewOn(canvasPoint)` | Pans so `canvasPoint` is centred; zoom unchanged |
| `zoomAroundCentre(wheelDelta)` | `applyZoomAt` anchored on the visible area's centre |
| `buildMinimapModel()` | Walks every module, and reuses the `buildVisibleCables()` memo (no separate enumeration walk) for the cable list, into a `MinimapModel` snapshot |

## Toolbar, shortcut and persistence

A toolbar toggle (`ToggleMinimap`, in the right-hand group before `ToggleModMatrix`) and the
**Cmd+K** shortcut (action id `toggleMinimap`, see [shortcuts](../shortcuts.md)) both call
`GraphEditor::toggleMinimapVisibility()`. Visibility persists under the `minimapVisible` key in
`juce::ApplicationProperties`, defaulting to `true`.
