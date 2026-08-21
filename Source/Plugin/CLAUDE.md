# Plugin-layer invariants (Source/Plugin/)

Host-mode rules (`HostMode::Hosted` never touches hardware; the stream-format hook) live in `Source/CLAUDE.md`, which also loads for work here.

- **A plugin editor must never call `Desktop::setDefaultLookAndFeel`** — it's process-global inside the host and would re-skin the host's own windows and every sibling plugin. Scope with `setLookAndFeel(&processor.getLookAndFeel())` on the editor itself; `ThemeManager`/`AppLookAndFeel` belong to the processor, since hosts recreate the editor repeatedly. → [`docs/architecture.md`](../../docs/architecture.md)
