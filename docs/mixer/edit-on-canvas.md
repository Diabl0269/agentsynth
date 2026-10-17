# Edit on canvas

Every strip, bus and Master column header has an **Edit on canvas** button (`TrackShowModuleButton`, the same
control as the timeline row's target button, component id `mixerEditOnCanvas`) on the right of the name row. It is
there whatever the chain is, and "+ Insert" never turns into it. A click selects the channel's macro on the canvas
(or, for a branching chain, its first boxed module's macro) and scrolls the canvas to centre it
(`MixerPanelComponent::revealOnCanvas`), so the button always shows something. It is a Tab stop with the focus ring,
named "Edit <channel> on canvas", Return or Space press it, and its tooltip says what it does. The Direct column has
none.
