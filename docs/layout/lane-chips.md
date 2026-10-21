# Lane chips

A row of toggle chips under a stepped editor, one chip per lane. Each chip shows or hides one lane (a row of per-step
values), and several lanes can be on at once. The first user is the sequencer card's step grid
(`Source/UI/ModuleViews/StepGrid/`); the drum machine's pad lanes will use the same widget.

- **Widget:** `synth::ui::LaneChipRow` (`Source/UI/Layout/LaneChips/LaneChipRow.h`). `setLanes(names)` builds the chips,
  `setShown(index, shown)` sets one without notifying, `onToggle(index, shown)` fires for a user toggle (click, Space or
  a screen-reader press). Each chip is a pill-styled toggle button, as wide as its text plus padding.
- **What the owner does:** it decides what a chip means. The step grid folds the lane in or out (160 ms, the card's
  height follows each frame, the neighbours make room once per direction), and saves the shown lanes.
- **Saved with the node:** the step grid keeps its lanes in `CardViewState::stepGridLanes`, saved as the node's
  `"cardView": {"lanes": ["pitch", "velocity", ...]}`. A node showing the default lanes (Pitch and Velocity) saves
  nothing. Lane ids are `pitch`, `gate`, `velocity`, `chance`, `repeat`, `slide`, `nudge`, `condition`, `filter`;
  unknown ids are ignored on load and lanes the module cannot show (Slide on the Poly Sequencer) are dropped.
- **Accessibility:** the row is one Tab stop; Left and Right move the focused chip (the accent focus ring follows it),
  Space toggles it. Every chip is also a toggle button titled "<Lane> lane" with the tooltip "Show the <Lane> lane", so
  a screen reader and the mouse reach each one directly. The row itself is a group.
- **Motion:** the chip's pill eases like every toggle pill; the lane it controls folds with the owner's motion and
  follows Reduce Motion and Animations Off ([animation.md](animation.md#motion-rules)).
