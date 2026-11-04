# macOS keys the JUCE peer drops

Control turns Ctrl+Shift+2 (Ctrl+@) and Ctrl+Space into
the NUL code, so `[ev characters]` is `"\0"`, which JUCE reads as an empty string. The peer calls `handleKeyPress`
once per character of that string, so for these chords it never runs and no key press exists to match (the other
seven grid chords carry text, so they work). `MacPeerDroppedKeyRelay` (macOS, installed by the app) is a local
`NSEvent` keyDown monitor that rebuilds the key press the peer would have built (`recoverKeyPressDroppedByMacPeer`:
upper-cased `charactersIgnoringModifiers`, the event's modifiers, no text) and gives it to the focused window's peer.
Tests model the peer in `Tests/App/ShortcutManager/MacPeerKeyModel.h` with the values AppKit reports.
Test mode's `key` command hands a ready-made key press to the peer (it starts after this step), so it cannot show this
bug.

Back to [shortcuts](shortcuts.md).
