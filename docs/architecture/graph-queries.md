# Graph queries per edit

An edit (a track duplicate, a paste, an undo) runs several whole-project passes before it returns: the undo
snapshot, the track-ownership rule, the mixer snapshot, the timeline's modulator rows, the canvas's macro layout.
Each pass asks the graph about every strip, column, routing or macro member. Those passes must cost about the same
per item at 20 tracks and at 200, so **a pass asks the graph through lookups built once for that pass, never through a
whole-graph call per item.**

## Why

`juce::AudioProcessorGraph::getConnections()` copies and sorts every cable on each call, and finding a node by uuid
means scanning every node. Called once per item inside a pass, either makes the pass O(items x project), so every
edit grew with the project: on the Load test project (docs/layout/rendering.md#the-load-test-project) one track
duplicate took about 50 ms at 20 tracks, 300 ms at 80 and 1.6 s at 200, and undoing one took 3 s at 200.

## The shared forms

- **`synth::ConnectionIndex`** (`Source/AudioEngine/ConnectionIndex.h`): every cable taken once, listed by the node it
  enters and the node it leaves, each list in `getConnections()` order, so a walk that used to scan the whole list for
  the first match takes the same first match from the node's own list. A snapshot: build it after the pass's edits,
  or keep your own record of what the pass adds (`applyModulationEntries` keeps a set of routings and adds to it).
  The canvas's `synth::ui::ConnectionIndex` is the same index, counted in the paint work counters.
- **Index overloads of the mixer queries**: `isSignalEdge`, `resolveThroughPorts` (`ChannelFlows.h`),
  `findStripsFeedingStrip`, `resolveSendTarget` (`MixerSends.h`) each take a `ConnectionIndex` beside their
  one-shot form, which builds one and forwards, so other callers are unchanged.
- **`synth::MixerGraphView`** (`Source/Mixer/MixerModel/MixerModelInternal.h`): what `buildMixerSnapshot` hands
  every column builder: the index, a `TrackChannelLinkMap` (uuid lookups and the track/strip reach, memoised),
  a `ChannelMacroIndex` (`nearestChannelMacro` from maps built once) and per-strip bus answers.
- **`synth::NodeUuidCache`** (`Source/AudioEngine/NodeUuidCache.h`): uuid -> node lookups that stay cheap while the
  graph changes. It never answers a node that no longer carries the uuid; anything it cannot confirm costs one fresh
  scan. `MainComponent::findNodeByUuid` and the macro controller's member lookups use it.
- **`synth::MacroOwnerIndex`** (`Source/MacroOwnerIndex.h`): `MacroSet::findByMember` and
  `outermostCollapsedAncestorOf` from one member map, for a pass over every card or node (the canvas's layout units,
  macro-card sync, the modulator rows' port test). `MacroSet::findByMember` searches every macro's members per call.
- **`ChannelLinkBatch`** (`TrackChannelLinkSurface.h`): a run of per-track link questions answered from one
  `TrackChannelLinkMap`, and each channel's macro from one `MacroOwnerIndex`. `reconcileTimelineAfterGraphChange` holds
  one batch for the whole reconcile (linked tracks, every header), so the map is built once per edit.
- **`graph_editor_paint::MacroOwnerScope`** (`GraphEditorPaintMemo.h`): the cable rebuild asks which macro owns each
  macro port card's jack; inside the scope `MacroGroupController::macroPortOwnerFor` reads one `MacroOwnerIndex`.
- **The canvas's macro geometry** (`MacroGroupControllerInternal.h`): a pass over many hulls (layout units, port
  docking) measures them against one card map; the undo step's border snapshot and glide open a
  `graph_editor_paint::CardMapScope` (`GraphEditorPaintMemo.h`) so `MacroGroupController::macroHullBounds` reads one
  map for every border. `GraphEditor::updateComponents` matches cards to processors through sets built once.

## One audio-graph rebuild per edit

`juce::AudioProcessorGraph` rebuilds its whole render sequence after every node or cable change made with the default
`UpdateKind::sync`, so an edit that makes many such changes costs (changes x graph size). An edit that changes the
topology more than once therefore issues every change with `UpdateKind::none` inside one `synth::GraphRebuildScope`
(`Source/AI/AIStateMapper/GraphRebuildBatch.h`), which rebuilds once when it ends, or once at the end of the outermost
scope or `GraphRebuildBatch` around it. The macro port splicing a membership change does (a module dragged into or out
of a macro, grouped, ungrouped; `MacroGroupControllerPortSplice.cpp`, `MacroGroupControllerPorts.cpp`,
`MacroGroupControllerProgrammaticRouting.cpp`) and `GraphEditor::removeNodesNow` (a module, macro or track deleted)
work this way. Before, dragging a module into a macro on the Load test project took about 7 s, and deleting a track or a
macro about 0.3 s. `MacroDragMembership.CmdDragMemberOutAndBackInRebuildsTheRenderSequenceExactlyOncePerGesture` holds
it as a rebuild count.

## What still grows with the project

The undo step no longer does. `AppUndoManager` captures through `synth::GraphSnapshotCache`, which re-writes only the
nodes an edit changed and shares the rest with the previous snapshot; snapshots are compared with `synth::sameJson` and
sized by counting, never written out as text
([module-base.md](module-base.md#a-snapshot-costs-what-the-edit-changed-not-the-project)). A restore that frees nodes
tears down only their cards and unbinds only the mixer columns bound to them, and the UI refresh after an edit rebuilds
only what changed: the mixer keeps every column whose column is unchanged
([panel.md](../mixer/panel.md#a-rebuild-keeps-the-columns-that-did-not-change)), the timeline keeps the header row of
every track still there ([tracks.md](../timeline/tracks.md)), and inside an undo step or a duplicate each whole-project
pass runs once, in the reconcile after it ([app-wiring.md](app-wiring.md)). `SnippetManager::extractSnippet` writes only
the copied nodes. What is left per edit is a set of whole-project passes, each linear and none per item, with the share
each takes on the Load test project at 200 tracks (a duplicate is about 105 ms, its undo about 110 ms, a knob undo about
95 ms; the 30 Hz canvas work is apart):

- the visible-cable rebuild an undo diffs for its retract animation (about a quarter of a knob undo, most of it each
  cable end's `ModuleComponent::getPortCenter`), and the rest of the canvas: `updateComponents`' walk (output-dock
  reflow, canvas frame, macro cards, port docking, together about a sixth) and the macro border glide;
- the restore re-applying every node's parameters (about a tenth);
- the mixer snapshot (`buildMixerSnapshot`, about a tenth; the columns themselves are kept);
- the reconcile's timeline passes: linked tracks and macro colours, every header's `refreshFromDoc`, the modulator rows'
  `deriveRoutings` (once per knob undo; twice for a duplicate and for its undo, the doc change's own sync and the
  reconcile), the republish;
- for a duplicate, the ownership rule (`resolveAutomationOwners`) and the undo capture; for its undo and redo, the
  timeline and macro documents' own `toVar` / `fromVar`.

Two constant costs are still kept to once: a step keeps the size it was first given (`AppUndoManagerSnapshotSize.h`) and
`AppLookAndFeel::uiTextWidth` remembers each label's width.

## Measuring

`DISABLED_LoadTestProjectProfile` (`Tests/App/ManyTracksProfileTests.cpp`) duplicates the first track N times and
prints each duplicate's time, then times three undos:
`PROFILE_DUPLICATE=180 ./Tests --gtest_also_run_disabled_tests --gtest_filter='*LoadTestProjectProfile'`. At each size
in `PROFILE_CHECKPOINTS` (default `22,80,200` tracks) it prints a `[checkpoint]` line: the duplicate, its undo and
redo, and a knob edit recorded the way a card knob records it and its undo. `PROFILE_SAVE=<bundle>` keeps the grown
project, so `PROFILE_PROJECT=<bundle> PROFILE_DUPLICATE=1 PROFILE_CHECKPOINTS=1` measures one size quickly, and
`PROFILE_SPIN_CHECKPOINT=<s>` loops undo/redo, then knob edit/undo, for that long at each checkpoint to attach
`sample <pid> 10` to.

`Tests/App/MacroEditProfileTests.cpp` measures the other edits on a temporary copy of the same project:
`DISABLED_LoadTestMacroDragProfile` drags a macro's busiest member out of it and back in through the real mouse
callbacks (each drag tick, the drop, and the longest message-loop stall after it), and `DISABLED_LoadTestEditProfile`
times adding and deleting a module, deleting a macro, folding and unfolding every macro, resizing the window, and
duplicating and deleting a track, each with its undo. `PROFILE_ON_SCREEN=1 PROFILE_AUDIO=1` runs them in a real window
with the audio device running, as in the app.
