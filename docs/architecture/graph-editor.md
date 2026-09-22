# GraphEditor

The visual patching canvas: its per-concern translation units, the three collaborator classes reached through `GraphCanvasHost`, and the canvas behaviours themselves.

Part of the architecture docs — start at [`architecture.md`](architecture.md) for the
layer map, signal flow and the index of the other topic docs.

`Source/UI/Graph/GraphEditor/` — one class (declared in `GraphEditor.h`) split across per-concern translation units, none over 1,000 lines, plus a private `GraphEditorInternal.h` for helpers shared by two or more of them. Source layout:

- `GraphEditor.cpp` — constructor/destructor, core lifecycle
- `GraphEditorCables.cpp` — cable geometry/colour, `GraphContentComponent::paint`/`paintOverChildren`/`resized`
- `GraphEditorConnections.cpp` — poly-link resolution, connection drag begin/drag/end
- `GraphEditorSmartConnections.cpp` — the three `SmartConnectionEngine` forwarders (below) GraphEditor keeps because they satisfy `GraphCanvasHost` pure virtuals, plus `nodeHasCables`, `estimatePortCenter`
- `GraphEditorModuleTitles.cpp` — custom module title get/set/resolve, committing an open inline title editor
- `GraphEditorCanvas.cpp` — component lifecycle, paint/resized, zoom/pan/minimap, canvas mouse handling
- `GraphEditorSelection.cpp` — selection model, marquee, selection drag
- `GraphEditorChannels.cpp` — auto-create-channel-on-connect, "Make channel"/"Duplicate into this channel"
- `GraphEditorMacroApi.cpp` — FRO254: down to one method, `changeMacroPortColour`, the one deliberate exception left after every other macro forwarder onto `MacroGroupController` was migrated and deleted (its body does real work beyond a pass-through: it also repaints both port-colour paint surfaces and disarms any live preview)
- `GraphEditorMacroCards.cpp` — the macro-presentation pieces that need a genuine `GraphEditor&`/`juce::Component` identity and so stayed out of `MacroGroupController` (`syncMacroCards()`, `categoryPreviewColour()`)
- `GraphEditorMacroPrompts.cpp` — every macro dialog/popup/menu builder that constructs a `juce::Component::SafePointer<GraphEditor>` for an async callback, for the same reason
- `GraphEditorCommands.cpp` — snippets, copy/paste/duplicate, context menu, keyboard, delete/replace, timer
- `GraphEditorDragDrop.cpp` — estimated module size, drag-preview API, file/plugin drop, drop placement
- `GraphEditorStereoWiring.cpp` — dual-I/O wiring, stereo-pair completion, module-resize handling
- `GraphEditorPersistence.cpp` — auto-arrange, patch save/load/new-patch

`Source/UI/Graph/SmartConnectionEngine/` — `SmartConnectionEngine`, the first real
collaborator class extracted out of `GraphEditor`: owns smart-connection mode/suggestion state and
the proximity-suggestion algorithm (`refreshSmartSuggestions`/`applySmartSuggestions`), reaching its
canvas only through `Source/UI/Graph/GraphCanvasHost.h` — the narrow interface GraphEditor
implements privately. `GraphEditor` holds one instance (`smartConnections_`), reached externally via
`getSmartConnections()`. FRO254: its plain one-line forwarders are gone — only
`refreshSmartSuggestions`/`clearSmartSuggestions`/`seedInsertModifierSample` stay on `GraphEditor`,
because `GraphDragDropController` calls them polymorphically through the `GraphCanvasHost`
interface, which a call through `getSmartConnections()` can't satisfy.

`Source/UI/Graph/MacroGroupController/` — `MacroGroupController`, the second
collaborator: macro grouping/membership/collapse, geometry + card jacks, port CRUD, the
port-crossing-plan math, and the bypass/mute fan-out, through the same `GraphCanvasHost` seam.
`GraphEditor` holds `macroController_`, reached externally via `getMacroController()`. FRO254: its
plain one-line forwarders are gone — only `changeMacroPortColour` stays (see
`GraphEditorMacroApi.cpp` above); a handful of other methods needing a genuine `GraphEditor&` also
stay on `GraphEditor` — see `MacroGroupController.h`'s class comment.

`Source/UI/Graph/GraphDragDropController/` — `GraphDragDropController`, the third
collaborator: drag-preview state (grid + landing ghost), alignment guides, and the
`DragAndDropTarget`/`FileDragAndDropTarget` overrides, through the same seam (five new host
methods: `lookAndFeel`, `seedInsertModifierSample`, `canvasPositionOfLocalPoint`,
`estimateModuleSizeForType`, `resolveSnippetPayload`). `GraphEditor` holds `dragDropController_`,
reached externally via `getDragDropController()`. FRO254: its plain one-line forwarders
(beginDragPreview/updateDragPreview/endDragPreview/isDragPreviewActive/getDragPreviewGhost/
getAlignmentGuides/getDragPreviewSelfId/buildDragPreviewState) are gone; the
`DragAndDropTarget`/`FileDragAndDropTarget` overrides stay as forwarders unchanged — JUCE resolves
a drop target by Component identity, so the override itself can't move off `GraphEditor`.

The visual patching interface. Lives in the `AgentSynth` app target.

- **Zoom/pan** — `zoomLevel` + `panOffset`; `mouseWheelMove` / `mouseDrag` on the canvas.
- **Wire drawing** — poly-bus wires (collapsed N-voice `DirectCV` connections) rendered with an "xN" badge; wire endpoints anchored to visible jacks via `ModuleBase`'s logical-port API.
- **Drag-to-connect** — `beginConnectionDrag` / `dragConnection` / `endConnectionDrag`; resolves the dragged jacks through the logical-port API (`resolvePolyLink`) and creates one connection per voice when both ends front an equally-wide poly fan, so a single drag between two poly jacks wires the whole fan at once. `disconnectPort` removes every raw channel a jack owns, including all voices of a fan.
- **Poly toggle rewire** — `rewireForPolyChange` re-anchors a module's existing cables to its new channel layout when its `poly` parameter changes (mono <-> fan), driven by `ModuleComponent`'s `"poly"` parameter listener.
- **Module drag** — `finalizeModuleDrag` snaps the released module to the 8 px grid and resolves overlaps via spiral search. A live drag-preview system (`beginDragPreview` / `updateDragPreview` / `endDragPreview`) shows a themed grid-dot overlay plus a snapped landing ghost during drags.
- **Library drops** — `resolvePlacement` + `finalizeModuleDrag` run on the real component after `updateComponents()` so the final position anti-overlaps using true pixel dimensions.
- **Auto-arrange** — `autoArrange()` (triggered by Cmd+L or the toolbar button) topologically layers modules by signal-flow depth in a single undo step. See [`docs/layout/layout.md`](../layout/layout.md) for the full layout model.
- **Delete** — `requestDeleteModule(NodeID)` is the canonical deletion entry point; `ModuleComponent::deleteButton.onClick` delegates here.

See [`docs/layout/layout.md`](../layout/layout.md) for the grid model, anti-overlap algorithm, and `autoArrange` constants.

## New API goes on the collaborator, not GraphEditor

`GraphEditor` is a thin owner of these three collaborators, not a facade over them. A new method
for one of these concerns is declared on the collaborator only and reached through
`getMacroController()` / `getSmartConnections()` / `getDragDropController()` — all three accessors
exist (FRO254). Re-declaring it on `GraphEditor` would add it to a header that ~300 translation
units include, and make the same API reachable two ways. `GraphEditor.h` itself grows only for work
that genuinely needs the editor: canvas, component lifetime, and wiring between collaborators.

FRO254 migrated every call site off the legacy one-line forwarders this rule used to describe as
"stay until migrated" and deleted them, except a small, deliberate set that can't move:

- **`MacroGroupController::changeMacroPortColour`** — `GraphEditor::changeMacroPortColour`
  (`GraphEditorMacroApi.cpp`) stays: its body does real work beyond the pass-through (it also
  repaints both port-colour paint surfaces and disarms any live preview), so callers must keep
  going through `GraphEditor`, not `getMacroController().changeMacroPortColour(...)` directly.
- **`SmartConnectionEngine::refreshSmartSuggestions` / `clearSmartSuggestions` /
  `seedInsertModifierSample`** (`GraphEditorSmartConnections.cpp`) — stay because they satisfy
  `GraphCanvasHost` pure virtuals that `GraphDragDropController` calls polymorphically through the
  host interface; a caller reaching the engine directly through `getSmartConnections()` can't
  satisfy that contract.
- **`GraphDragDropController`'s `DragAndDropTarget`/`FileDragAndDropTarget` overrides**
  (`isInterestedInDragSource`/`itemDragEnter`/`itemDragMove`/`itemDragExit`/`itemDropped`/
  `isInterestedInFileDrag`/`filesDropped`, `GraphEditorDragDrop.cpp`) — stay because JUCE resolves
  a drop target by Component identity, so the override itself has to stay a `GraphEditor` member
  even though its body is one call into the controller.
