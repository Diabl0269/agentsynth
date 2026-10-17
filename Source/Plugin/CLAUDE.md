# Plugin-layer invariants (Source/Plugin/)

Host-mode rules (`HostMode::Hosted` never touches hardware; the stream-format hook) are in `Source/CLAUDE.md`.

- **A plugin editor never calls `Desktop::setDefaultLookAndFeel`** (process-global in the host); scope with `setLookAndFeel(&processor.getLookAndFeel())`. A detached Timeline/Mixer window (`DetachedPanelWindow`) follows the same rule. → [plugin-layer](../../docs/architecture/plugin-layer.md#who-owns-what) · [panel](../../docs/mixer/panel.md)
- **JUCE is patched, and the patch is load-bearing** for the plugin window's right-click "Add to card" (`cmake/patches/juce-vst3-context-menu.patch`): change the JUCE tag only with a regenerated patch. → [juce-patches](../../docs/development/juce-patches.md) · [plugin-card-layout](../../docs/control/plugin-card-layout.md#add-to-card-from-the-plugin-window)
