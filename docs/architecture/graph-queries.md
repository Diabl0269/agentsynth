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
  `TrackChannelLinkMap`.
- **The canvas's macro geometry** (`MacroGroupControllerInternal.h`): a pass over many hulls (layout units, port
  docking) measures them against one card map.

## What still grows with the project

The undo step itself: `AppUndoManager` snapshots the whole graph before and after an edit (`graphToJSON`) and compares
the two as JSON text, and an undo restores a whole snapshot and rebuilds every card. That is linear in the project by
design; a flat curve needs undo steps that record a diff rather than a whole snapshot. Two constant costs on top of it
are kept to once: a step is sized for the undo history from the lengths the change check already measured, and keeps
that size (`AppUndoManagerSnapshotSize.h`; the history re-sizes the oldest step it drops on every edit once full), and
`AppLookAndFeel::uiTextWidth` remembers each label's width (every rebuilt card measures its footer labels).
`SnippetManager::extractSnippet` serialises the whole graph to copy one track's modules, and every edit rebuilds the
mixer snapshot and the track-ownership rule once each: linear passes, not per-item ones.

## Measuring

`DISABLED_LoadTestProjectProfile` (`Tests/App/ManyTracksProfileTests.cpp`) duplicates the first track N times and
prints each duplicate's time, then times three undos:
`PROFILE_DUPLICATE=180 ./Tests --gtest_also_run_disabled_tests --gtest_filter='*LoadTestProjectProfile'`. Attach
`sample <pid> 20` past 120 tracks to see which pass grows.
