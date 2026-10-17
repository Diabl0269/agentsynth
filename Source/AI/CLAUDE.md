# AI & trust-boundary invariants (Source/AI/)

The root `CLAUDE.md` rules (never relax `validatePatch`; `trusted=true` is about parameter fidelity) govern everything here. One rule per bullet; detail in [`patch-safety.md`](../../docs/ai/patch-safety.md).

- **`applyJSONToGraph` merge mode auto-connects new nodes**; a caller reproducing an exact sub-graph (snippet insert, paste, duplicate) passes `autoConnectNewNodes=false`. → [snippets-clipboard](../../docs/layout/snippets-clipboard.md)
- **`applyJSONToGraph` rebuilds the audio graph once, on return**: topology ops use `UpdateKind::none`, rebuild via `GraphRebuildBatch::rebuild`. → [graph-queries](../../docs/architecture/graph-queries.md#one-audio-graph-rebuild-per-edit)
- **Reserved patch fields stay reserved**: `"timeline"`, `"macros"`, `"midiRemote"`, `"mixerPanLaw"`, `"mixerView"`, `"view"` are refused on the untrusted path; extend `synth::validateTimeline`, never weaken the refusal; `Track In` is non-authorable. → [patch-format](../../docs/ai/patch-format.md) · [timeline-ops](../../docs/ai/timeline-ops.md#addinstrumenttrack)
- **Conversation-history persistence is resolved server-side from the entitlement**, never from a client header. → [history](../../docs/ai/history.md#server-side-conversation-history)
- **Persist a rotated refresh token before using its access token** (`AccountService::completeSignIn` is the funnel). → [accounts](../../docs/ai/accounts.md#rotation-before-use)
- **After `setProvider()`, call `AIChatComponent::refreshModels()` again**, or `/api/chat` gets a 400. → [chat-component](../../docs/ai/chat-component.md#model-discovery-ordering-contract)
- **A modulation names its target by `destPort` or `destParam`, and every edit-plan id is distinct**; enforce in `validatePatch` and the plan validator, not the schema. → [patch-format](../../docs/ai/patch-format.md#a-modulations-destination-destport-or-destparam) · [timeline-ops](../../docs/ai/timeline-ops.md#one-edit-plan)
