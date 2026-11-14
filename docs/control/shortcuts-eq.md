# EQ shortcuts

The EQ category of Settings > Shortcuts; the rest of the shortcut system is in [shortcuts.md](shortcuts.md).

## EQ

The Parametric EQ curve's keys (`EQCurveComponent::keyPressed`), live while the curve has focus on the card or in the
pop-out window; Shift on a stepping key makes it a quarter step. Up/Down now change the band's width, not its gain
(gain is Alt+Up/Down), Backspace removes the band (the selection stays on it), and Return (fixed, with 1-4, Page Up/Down, Delete and
Escape) switches it on or off. Delete also removes, but selects the next band, like the other point editors; Shift+Left /
Right extends the selection, Cmd+A selects every band, and the Option+arrows hold and glide
([point-editing.md](point-editing.md)). Details: [visualizers.md](../layout/visualizers.md#eqcurvecomponent).

| Shortcut (macOS) | Windows / Linux | Action |
|------------------|-----------------|--------|
| Left / Right | Left / Right | Select the previous / next band (`eqSelectPrev`, `eqSelectNext`) |
| Option+Left / Right | Alt+Left / Right | Move the band down / up a semitone in frequency; held, it glides (`eqMoveLeft`, `eqMoveRight`) |
| Option+Up / Down | Alt+Up / Down | Raise / lower the band's gain by 1 dB (`eqMoveUp`, `eqMoveDown`) |
| Up / Down | Up / Down | Widen / narrow the band (`eqWidthUp`, `eqWidthDown`) |
| S | S | Solo the selected band, again to end it (`eqSolo`) |
| T | T | Open the band's type menu (`eqTypeMenu`) |
| + | + | Add a band in the widest gap (`eqAddBand`); also Shift+= and the number pad + while it is the default |
| Backspace | Backspace | Remove the band, selection stays on it (`eqRemoveBand`); Delete removes it and selects the next band |
| Cmd+D | Ctrl+D | Duplicate the band two semitones up, into the lowest free slot (`eqDuplicateBand`) |
