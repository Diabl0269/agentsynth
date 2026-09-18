# Plan: Fix Graph Editor Zoom Behavior

## Objective
Implement zooming centered on the mouse position in `GraphEditor` instead of zooming to the center of the component.

## Context
Current implementation in `Source/UI/GraphEditor.cpp` (lines 430-450) zooms around a fixed center (`xOffset, getHeight() / 2.0f`).

## Research Needed
1. Analyze how `AffineTransform` is used in `GraphEditor::updateTransform()`.
2. Determine the mathematical relationship between mouse position and pan/zoom offsets to correctly zoom around the mouse cursor.

## Implementation Steps
1. Modify `GraphEditor::mouseWheelMove` to capture the mouse position `e.position`.
2. Update `GraphEditor::updateTransform` to accept a center point or handle the mouse-relative zoom.
3. Calculate the new `panOffset` so that the point under the mouse cursor remains at the same relative position in the graph after the scale transformation changes.
4. Test zoom in/out with various mouse positions.

## Tests
1. Create a new test in `Tests/GraphEditorTests.cpp` (or verify existing if applicable) to simulate wheel events and assert that the graph content under the mouse stays stationary during zooming.

## Docs Updates
1. Update `CLAUDE.md` if the behavior change warrants it.
2. Document the new zooming behavior in `docs/architecture/architecture.md` if necessary.
