# Timeline Concept Brainstorming

## Objective
Implement a timeline-based orchestration system in Gravisynth.

## Approaches

### 1. Global Timeline (DAW-style)
- **Concept:** A separate, persistent UI component (like the GraphEditor) that sits above the modular patch.
- **Pros:** Familiar DAW workflow, clear master control (play/pause/loop/locators), easy to see global arrangement.
- **Cons:** Less flexible than the modular approach; doesn't benefit from the existing "patch everything" philosophy; might feel "tacked on" rather than integrated.
- **Data Flow:** Master clock -> Timeline UI -> Global Event Bus -> Individual modules.

### 2. Timeline Module (Node-based)
- **Concept:** A dedicated module (e.g., `TimelineModule`) that appears in the `ModuleLibraryComponent`.
- **Pros:** Fits perfectly into the modular architecture; flexible routing (can have multiple timelines for different polyphonic groups); no "special cases" in the UI.
- **Cons:** Harder to manage a "global" play/pause state across multiple modules; potential UI complexity if the editor needs to show 8 timelines at once.
- **Data Flow:** Module-based clock/trigger -> Internal patterns -> MIDI output port -> Patchable connections to other modules.

## Proposed Strategy
I recommend a hybrid: **Timeline Module** as the primary engine for maximum flexibility, combined with a **Master Playback HUD** (a lightweight global control bar at the top of the main window) that broadcasts play/pause/sync state to all Timeline modules.
