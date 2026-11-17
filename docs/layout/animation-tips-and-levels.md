# Animation: long tips and Cmd+K level changes

Split from [animation.md](animation.md) to keep it under the file cap.

**Long tips.** The one tooltip box wraps its text: a line wider than `kTooltipMaxTextWidth` (280 px, `AppLookAndFeel.h`) is broken at spaces by `wrapTooltipLines` into the fewest lines and then evened out (the narrowest width that still needs that many lines), so a long tip is a short, even block, never one line across the window. `getTooltipBounds` and `drawTooltip` both call it, so the box and the text always agree, and the ghost, the dialog and popover tips share it. Line breaks written into a tip (`\n`) are kept, so a tip that lists things gives each its own line; write a long tip as a few short lines of one idea each (the Cmd+K bar's "i", the Preferences toggles). Tests: `TooltipBoundsTests.cpp`.

**Level change in the Cmd+K bar.** Entering a dropdown setting's choices, and stepping back out with Escape, swap the picker's list through `ModMatrixPicker::reloadRows(query, slideFromX)`: the new rows fade in over the shared 160 ms and glide 28 px home from the right (going in) or the left (going back); Reduce Motion drops the slide and keeps the fade. Closing the bar fades it out as before. Tests: `PickerFilterFadeTests.cpp`.
