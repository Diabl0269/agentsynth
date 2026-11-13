# Recolour Focused Item

`recolourFocusedItem` (General, **Alt+C** by default, rebindable) opens the shared colour picker
(`ColourPickerPopup`) for the item that holds keyboard focus, beside that item, so every "Colour..." menu item has a
key. It runs the way Open Context Menu does: `MainComponent` performs it (inactive while the launch overlay is up front),
the detached panel window and the shape editor window resolve the same bound key themselves, and the action walks up
from the focused component to the nearest `KeyboardColourProvider` (`Source/UI/Layout/KeyboardColourProvider.h`),
whose answer is final. The picker opens in the card call-out, so Cmd+Z and the other app shortcuts still reach the main
window from it.

| Focus is in | Picker opened |
|-------------|---------------|
| Timeline track row (or its name label / chip) | The track's colour swatch picker, beside the swatch |
| Mixer strip | The strip's colour dot picker (the bare **C** key does the same inside the mixer); nothing on Master or a strip with no track or macro |
| Graph canvas | The picker of the single selected macro (its card, or every member of an open one); nothing with none or several |
| Morph map dot | The dot's own colour: live on the dot, its chip and the key strip, one undo step on close |

Bare Alt+C is free (no other binding uses Alt with C); a text field takes the key first.

