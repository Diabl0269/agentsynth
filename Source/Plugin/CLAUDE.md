# Plugin-layer invariants (Source/Plugin/)

Host-mode rules (`HostMode::Hosted` never touches hardware; the stream-format hook) live in `Source/CLAUDE.md`, which also loads for work here.

- **A plugin editor must never call `Desktop::setDefaultLookAndFeel`** — it's process-global inside the host and would re-skin the host's own windows and every sibling plugin. Scope with `setLookAndFeel(&processor.getLookAndFeel())` on the editor itself; `ThemeManager`/`AppLookAndFeel` belong to the processor, since hosts recreate the editor repeatedly. → [`docs/architecture/plugin-layer.md#who-owns-what`](../../docs/architecture/plugin-layer.md#who-owns-what)
- **Same rule for a DETACHED Timeline/Mixer window** — `DetachedPanelWindow` (FRO12, P9-6) is a second top-level window a plugin editor can open; it scopes its own `setLookAndFeel()` the identical way, never touching the process-global default. → [`docs/mixer.md §5.9`](../../docs/mixer.md)
