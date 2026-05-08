# Product Requirements

## User Stories
- As a sound designer, I want to create complex sound patches using a modular graph editor.
- As a musician, I want to quickly trigger and sequence patterns using the sequencer and MIDI keyboard modules.
- As a user, I want to save and load my graph setups to persist my sound designs.
- As an experimental user, I want to generate patches using AI.

## Functional Requirements
- **Node-based Graph Editor:** Support for connecting audio modules.
- **Audio Synthesis Engine:** Support for essential modules (Oscillator, Filter, VCA, Envelope, etc.).
- **Modulation Matrix:** Support for CV-based modulation routing.
- **Persistence:** Save/Load presets in JSON format.
- **AI Integration:** Ollama-based natural language patch generation.

## Non-Functional Requirements
- **Performance:** Real-time audio processing must be stable with minimal CPU usage.
- **Maintainability:** Modular architecture for easy addition of new modules.
- **Testability:** >85% test coverage for audio DSP and core logic.
