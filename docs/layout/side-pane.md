# Side pane

A bottom-panel tab can have its own **left side pane**: a column at the left of the tab's body that
holds context for that tab. Today only the Mixer has one (its Zones and visibility list, see
[the Mixer's side pane](../mixer/panel.md#side-pane-zones-and-visibility)); the Timeline's routing
pane will be the second. The container is reusable, so a new pane is a content class and a few
lines of wiring, never a second implementation of the open/close, width or persistence logic.

The pieces live in `Source/UI/Layout/SidePane/`:

| Class | Job |
|---|---|
| `SidePane` | The container: holds one `SidePaneContent`, tweens open and closed, owns the resize edge and persists open state and width per tab |
| `SidePaneContent` | The interface a tab implements: the component to show and its accessible title |
| `SidePaneToggleButton` | The button that shows and hides a pane; follows the pane's state and hides itself when the pane has no content |

## How a panel uses it

1. The panel owns a `SidePane` as a **child** and calls `setContent(&content)`. Because the pane is
   part of the panel component, it travels with the panel into a detached window and into the
   Mixer's Own-panel strip with no extra wiring.
2. The panel calls `setPersistence(settings, "<tab>")` once its settings store is known. Open
   state and width are then remembered **per tab, app-wide in the user settings** (like the dock
   height), under `sidePaneOpen.<tab>` and `sidePaneWidth.<tab>`. A tab with nothing stored opens
   at 200 px, so the pane is open the first time a tab is opened.
3. In `resized()` the panel gives the pane `getOccupiedWidth()` pixels at the left of its body and
   the rest to its own content. `onOccupiedWidthChanged` fires whenever that number changes, so the
   panel re-lays out (the panel is the layout owner; the pane never resizes its parent).
4. The panel's own top bar hosts a `SidePaneToggleButton` at its **left end**, bound with
   `bind(&pane)`. A pane with no content hides the button, so a tab without a pane shows none.

## Width and the resize edge

The default width is 200 px. Dragging the pane's right edge (a 6 px strip with a resize cursor and an
accent line on hover) sets the width, **clamped to 160-320 px**. The width is stored when the drag
ends, not on every step. The content keeps its full width while the pane tweens, so opening reveals it
instead of re-flowing it every frame.

## Motion

Follows the [Motion rules](animation.md#motion-rules): the pane opens in **160 ms `easeOutCubic`**
and closes in **110 ms `easeInCubic`**, driven by one `AnimationDriver` (VBlank, time-bounded), with no
`Timer`. A toggle mid-tween retargets from the CURRENT width and takes a share of the nominal duration
equal to the distance left. The pane stays visible for the whole tween (a hidden component loses its
VBlank callbacks) and hides only once it has closed. A pane whose panel is not on screen gets no frames,
so it lands at its target at once (this is also what keeps headless tests deterministic). Nothing
repaints once it has settled.

## The shortcut

`toggleSidePane` (default **Cmd+Shift+B**, rebindable, View menu item "Show/Hide Side Pane") toggles the
**active** tab's pane through `BottomDockComponent::toggleActiveSidePane()`. With the bottom panel
hidden it first shows the panel and then makes sure the pane is open (a remembered-open pane is not
closed as it appears). It is a no-op when the active tab has no pane. The toggle button carries the
Cmd-hold shortcut hint through `MainComponentShortcutHints`. See
[shortcuts](../control/shortcuts.md).

## Rules for a new pane

- Its content is a normal component; it must not take keyboard focus away from the panel's focus
  root except where a control genuinely needs typing (the Mixer's filter box hands focus back on
  Return and Esc).
- Anything it lists is a **view** of state the panel owns: it reports edits through callbacks and is
  refreshed with a new snapshot, and it never holds a pointer into the graph.
- Reorderable lists inside it use the shared `ReorderDragAnimator`.
