# EQ shortcuts

The EQ category of Settings > Shortcuts; the rest of the shortcut system is in [shortcuts.md](shortcuts.md).

## EQ

**From a band chip.** With focus on a band chip (Tab to the strip, Return selects it) every key below acts on that chip's band exactly as it does from the curve: Option+arrows (held, they glide as one undo step), Up / Down, S, T, + and Cmd+D. Plain Left / Right still walk the chips, Delete and Backspace delete the band and put focus on the next chip, Return selects. The + chip passes on only the add key.

The Parametric EQ curve's keys (`EQCurveComponent::keyPressed`), live while the curve has focus on the card or in the
pop-out window; Shift on a width key makes it a quarter step, and a band's fine move is Ctrl+Shift+arrows (the shared point-editor chord, a quarter of the Alt+arrows step, held it glides slower); Alt+Shift+arrows is not a move. Up/Down now change the band's width, not its gain
(gain is Alt+Up/Down), Backspace removes the band like Delete (the next band takes the selection, like the other point editors), and Return (fixed, with 1-4, Page Up/Down, Delete and
Escape) switches it on or off; Shift+Left /
Right extends the selection, Cmd+A selects every band, and the Option+arrows hold and glide
([point-editing.md](point-editing.md)). Details: [visualizers.md](../layout/visualizers.md#eqcurvecomponent).

| Shortcut (macOS) | Windows / Linux | Action |
|------------------|-----------------|--------|
| Left / Right | Left / Right | Select the previous / next band in the order the chips show (by frequency, off bands skipped); no wrap, and Right past the last band puts focus on the + chip, where Return adds a band and Left goes back to the last band (`eqSelectPrev`, `eqSelectNext`) |
| Option+Left / Right | Alt+Left / Right | Move the band down / up a semitone in frequency; held, it glides (`eqMoveLeft`, `eqMoveRight`) |
| Option+Up / Down | Alt+Up / Down | Raise / lower the band's gain by 1 dB (`eqMoveUp`, `eqMoveDown`) |
| Up / Down | Up / Down | Widen / narrow the band (`eqWidthUp`, `eqWidthDown`) |
| S | S | Solo the selected band, again to end it (`eqSolo`) |
| T | T | Open the band's type menu (`eqTypeMenu`) |
| + | + | Add a band in the widest gap (`eqAddBand`); also Shift+= and the number pad + while it is the default |
| Backspace | Backspace | Remove the band and select the next one, exactly as Delete does (`eqRemoveBand`); Return switches it off and on instead. Right-click on a dot has the same "Delete band" in its menu |
| Cmd+D | Ctrl+D | Duplicate the band two semitones up, into the lowest free slot (`eqDuplicateBand`) |
