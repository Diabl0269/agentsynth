# Arrangement View Design Spec (Orchestrator Model)

## Objective
Add a dedicated "Arrangement View" to Gravisynth that provides a DAW-style workspace for sequencing.

## Orchestrator Architecture
- **ArrangementViewComponent:** The UI "Orchestrator" that manages the timeline, playhead, and tracks.
- **Track Model:** Each Track is bound to a `TimelineModule` node instance in the Graph Editor.
- **Data Flow:**
    1. **MIDI Patterns:** User places MIDI clips in the Arrangement View tracks. The Arrangement View pushes these patterns into the corresponding `TimelineModule`.
    2. **MIDI Routing:** The `TimelineModule` remains a patchable node. The user patches the MIDI output port of the `TimelineModule` to the desired target modules in the `GraphEditor`.
    3. **CC/Parameter Automation:** Automation lanes in the Arrangement View are bound directly to `AudioParameters` of target modules. During playback, the Orchestrator handles the parameter modulation (via `AudioParameter::setValueNotifyingHost`).

## Workflow
1. User toggles Arrangement View.
2. User binds an existing `TimelineModule` node from the Graph to a track in the Arrangement View.
3. User drags MIDI patterns into the track.
4. User patches the `TimelineModule` output to the synth in the `GraphEditor`.
5. User adds automation lanes to target parameters for parameter changes.

## Refined Data Structure
- `TimelineModule` (as before): Serves as the MIDI patch point.
- `ArrangementData`: Stored at the project level, mapping `TrackID` -> `TimelineModuleNodeID` and managing the automation lane assignments.

---

Does this architecture for the toggleable Arrangement View look correct before I draft the implementation plan?
