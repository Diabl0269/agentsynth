# Implementation Plan: Rebrand AgentSynth to Agent Synth

## Objective
Rebrand the project from "AgentSynth" to "Agent Synth". This involves renaming classes, files, namespaces, and build targets systematically.

## Key Files & Context
- `Source/`: Contains core logic, namespaces, and JUCE application files.
- `CMakeLists.txt`: Defines build targets and module sources.
- `docs/`: All documentation files require updates.
- `scripts/`: Build/Test scripts reference old names.
- `Tests/`: Test files include old headers and class references.
- `.github/`: Workflow files.

## Implementation Steps
1. **Pass 1: Namespace & String Replacement**
   - Replace "AgentSynth" with "AgentSynth" across all source and text files, prioritizing namespacing and code identifiers.
2. **Pass 2: Filename Simplification**
   - Rename files (e.g., `UndoManager.h` -> `UndoManager.h`, `AgentSynthApplication.cpp` -> `Application.cpp` where applicable) to remove unnecessary brand prefixes.
3. **Pass 3: Build System & Scripts**
   - Update `CMakeLists.txt`, `scripts/`, and `.github/` to reflect new targets, file paths, and names.
4. **Pass 4: Documentation**
   - Update all files in `docs/`, `README.md`, `CONTRIBUTING.md`, `CLAUDE.md`, and `GEMINI.md`.

## Verification & Testing
- Build the project using `cmake`.
- Run existing test suites.
- Verify CI/CD workflows match new naming.

## Docs Updates
- All files in `docs/`, `README.md`, `CONTRIBUTING.md`, `CLAUDE.md`, `GEMINI.md`.
