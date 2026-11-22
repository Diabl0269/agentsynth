# Save a plan's patch as a preset

A plan whose patch stands on its own gets an enabled **Save as preset** button on its edit-plan card. Any other plan that has an Apply (a merge whose modulation points at a module the plan's own track ops build, a plan that edits modules already in the project) shows the same button disabled, with a tooltip saying why ("Save as preset needs a plan that is a complete patch on its own. This one changes modules already in the project or builds tracks, so it can only be applied."); a disabled button is not a Tab stop, and a click that reaches it does nothing. A refused plan has no Apply and no button. It keeps a sound you
like without applying it: the project is never touched, no undo step is recorded, and Apply still works afterwards.

## What is saved

Only the patch part of the plan: its `nodes`, `connections` and `modulations`. Timeline ops, `mode` and `remove` belong
to the plan and are left out. The file is an ordinary patch preset (`.json`, the same dialect `GraphEditor::savePreset`
writes), so Open Patch loads it.

## The trust boundary

The card holds untrusted model output, so `synth::planpreset` (`Source/AI/PlanPreset.cpp`) never writes the plan's JSON
as it came:

1. The patch part is checked with `AIStateMapper::validatePatch(trusted=false)` as a replace patch against an empty
   graph, the same gate the live Apply runs. The button is offered only when that passes and the patch has at least one
   node. A merge that edits modules already in the project, or a patch that points at a module its own track ops build,
   is not a patch of its own and gets no button.
2. The patch is built on a scratch graph through the same untrusted `applyJSONToGraph` the live Apply uses, so a node's
   `state` is applied only where the untrusted path allows it (an LFO curve, a filter's shapes) and every other
   key is dropped, as in [patch-format.md](patch-format.md#per-node-state).
3. The scratch graph is read back with `graphToJSON`, and the node `uuid`s are removed: a load mints its own.
4. Modules the plan gave no position are laid out in columns by signal depth (rows 700 px apart, columns 340 px), so
   a loaded preset opens without cards stacked; Auto-arrange tidies it further.

## Name and file

The card asks for the name in the app's Save preset window (the one the card-layout editors use: Enter saves, Escape
cancels, an empty name does nothing). The name goes through `SnippetManager::sanitiseName`, the rule track presets and
snippets share (legal file name, no leading dot, 64 characters), and an existing preset is never overwritten: a taken
name becomes "Name 2", "Name 3". The folder is the one the Open Patch dialog starts in (the Patch save location
preference); nothing is listed in a separate browser, so Open Patch is where the saved preset shows up.

## On the card

The button has its own row under Show details and Apply (the narrowest chat has no room for a third button beside them), is a Tab stop right after Apply, and is named "Save patch as preset".
After a save it reads a disabled "Saved" (name "Preset saved"), the status bar says "Saved preset: <name>", and a
redraw of the conversation keeps that state for the session. A failed save adds an assistant message saying why.
Tests: `Tests/UI/Assistant/AIChatComponent/AIChatComponentEditPlanTests.cpp` (Save as preset, `PlanPresetTest`).
