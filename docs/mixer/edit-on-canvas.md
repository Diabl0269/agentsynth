# Edit on canvas

Every strip, bus and Master column header has an **Edit on canvas** button (`TrackShowModuleButton`, the same
control as the timeline row's target button, component id `mixerEditOnCanvas`) on the right of the name row. It is
there whatever the chain is, and "+ Insert" never turns into it. A click selects the channel's macro on the canvas
(or, for a branching chain, its first boxed module's macro) and scrolls the canvas to centre it
(`MixerPanelComponent::revealOnCanvas`), so the button always shows something. It is a Tab stop with the focus ring,
named "Edit <channel> on canvas", Return or Space press it, and its tooltip says what it does. The Direct column has
none.

## Insert and send rows

Each insert row has a **Show on canvas** icon button (`TrackShowModuleButton` look, named "Show <module> on
canvas", with a tooltip) left of its bypass button, and a double-click on an insert row does the same. Both call
`MixerPanelComponent::revealModuleOnCanvas(uuid)` (via the `onFocusModule` callback): it unfolds any collapsed macro
that holds the module (waiting for the fold glide), selects the module itself, not its macro, and centres it. Split
and dry rows have no button.

A send row has no button: at the 140 px column width the controls already leave the name about 32 px. The single
click that opens the row's target menu has a **Show <bus> on canvas** item instead, which calls the same
`onFocusModule` path for the bus the send feeds. It is disabled when the send has no target.
