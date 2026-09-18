# AI Engine

How a prompt becomes a change to the synthesizer graph: the two classes that do it, the order they
run in, and the undo contract every applied patch obeys.

The wire format those classes exchange is the [patch format](patch-format.md); the gate an
untrusted patch passes through is [patch safety](patch-safety.md); the card the user approves is
the [patch preview](patch-preview.md).

## The pieces

- **`AIProvider`** (`Source/AI/AIProvider.h`) — the interface every AI backend implements: send a
  prompt, send a capability request, list models, cancel a request, receive an auth token, a
  conversation id and a request timeout. The concrete providers are
  [OllamaProvider](ollama-provider.md) and [RemoteProvider](remote-provider.md); the registry that
  builds them is in [providers](providers.md).

- **`AIIntegrationService`** (`Source/AI/AIIntegrationService/`) — the orchestrator. It owns the
  conversation history, builds the outgoing request, hands it to the installed provider, parses
  what comes back, and drives the apply. It holds non-owning, nullable pointers to the collaborators
  its host installs: an `AIProvider` (`setProvider`), an `AppUndoManager` (`setUndoManager`), and
  the live timeline (`setTimelineContext`). Constructed without them it still works, which is what
  the standalone service tests rely on.

- **`AIStateMapper`** (`Source/AI/AIStateMapper/`) — translates between the JSON patch format and
  `juce::AudioProcessorGraph`: `graphToJSON` for context and snapshots, `applyJSONToGraph` for
  applying one, `validatePatch` for the untrusted gate.

  | Unit | Concern |
  |------|---------|
  | `AIStateMapper.h` | Class declaration, shared across every unit below |
  | `AIStateMapper.cpp` | Module factory access, graph to JSON mapping (`graphToJSON`/`applyJSONToGraph`), `createModule` |
  | `AIStateMapperValidation.cpp` | `validatePatch`/`validateNodeParams` — the untrusted-input security boundary |
  | `AIStateMapperSnapshots.cpp` | Undo/redo snapshot restore (`applySnapshotPreservingNodes`) |
  | `AIStateMapperSchema.cpp` | AI-facing schema generation (`getPatchSchema`, `getPatchSchemaWithTimelineOps`, `getTimelineOpsEnvelopeSchema`) |
  | `AIStateMapperInternal.h` | Private helpers shared by two or more units above (the module factory map, `kNonAuthorableModuleTypes`, `mirrorUuidIntoProcessor`) — not part of the public API, never included outside this directory |

## Request flow

1. The user types a prompt in the [chat panel](chat-component.md) and sends it.
2. `AIIntegrationService` appends the turn to `chatHistory` and builds the outgoing content.
   `buildPatchAugmentedContent()` renders the current graph into the **last** message as
   `"Current patch state:\n\`\`\`json\n<JSON>\n\`\`\`\n\nUser request: <text>"` when the graph has
   nodes, and as `"Current patch is empty.\n\nUser request: <text>"` when it has none. The model
   always gets an explicit signal about whether a patch already exists.
3. When a timeline is installed, the request also carries an `## Arrangement` section
   ([arrangement context](arrangement-context.md)) and an `## Automation targets` section
   ([timeline ops](timeline-ops.md#the-local-path)).
4. The installed provider performs the request and delivers an `AIResponse`.
5. The service extracts the JSON payload from the response, and routes each half to its own gate:
   a patch through `validatePatch` and `applyJSONToGraph`, a `timelineOps` envelope through
   `TimelineOps::validate`. Each half gets its own card and its own Apply button.
6. Applying notifies listeners, which rebuild the editor's module components.

**Why the patch text is wrapped client-side even for the hosted provider.** The hosted
`patch.generate` capability performs the *exact same* wrapping server-side, from a separate
`currentPatch` + `userPrompt` pair, and only wraps when `currentPatch` is present. Sending the
client's already-wrapped text as `userPrompt` alone, with `currentPatch` omitted, produces
byte-identical model input to the structured split — without `RemoteProvider` ever parsing the
wrapper back apart. Do not turn this into a parser that splits `userPrompt` into `currentPatch` +
`userPrompt`: it would reimplement, and risk diverging from, wrapping the service already does.

## Undo and redo

`AIIntegrationService::applyPatch()` routes through `AppUndoManager::recordAIPatch()` whenever an
undo manager has been injected (`MainComponent::initialiseCommon()` injects one). Without one, the
service applies directly.

The recorded action is the snapshot-based `SnapshotAction`, not a fine-grained diff: `graphToJSON`
is captured before the patch and after it, undo restores the before-snapshot and redo re-applies
the after-snapshot. Merge mode needs no special handling, because the after-snapshot is the whole
merged graph. Transactions are named `"AI patch"` and `"AI merge"`.

Two invariants:

1. **Listener notifications fire on undo and redo, not just the initial apply.**
   `aiPatchAboutToApply` / `aiPatchApplied` are passed to the action as its `preRestore` /
   `postRestore` hooks. Undo and redo rebuild the graph exactly the way the original apply did, so
   skipping them leaves `GraphEditor` holding stale `ModuleComponent`s pointing at `VisualBuffer`s
   that `applyJSONToGraph` has already freed — a use-after-free, not just a stale render. Guarded by
   `AIUndoTest.ListenersFireOnUndo`.
2. **A rejected patch pushes nothing.** `applyPatch()` runs `validatePatch()` before touching any
   listener or the undo stack, and `recordAIPatch()` returns without pushing if the mutation reports
   failure, so an invalid patch leaves no no-op entry for `Cmd+Z` to consume. Guarded by
   `AIUndoTest.FailedPatchPushesNothing`.

Because the notifications are dispatched from the undoable action, they are wrapped in a
`juce::WeakReference<AIIntegrationService>`, which keeps the ordering safe if the service is ever
destroyed before the undo manager.

An applied `timelineOps` batch is one undo step of its own — see
[timeline ops](timeline-ops.md#trust-posture).

Tests: `Tests/AI/AIUndoTests.cpp`.
