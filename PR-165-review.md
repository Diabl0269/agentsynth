# Code Review — PR #165 (NoiseModule) [ALL FIXED]

https://github.com/Diabl0269/agentsynth/pull/165

15 findings, verified against a real build: a standalone probe was compiled against `libCore.a`, the DSP measured, and the crash reproduced. All numbers below are measurements, not estimates.

Test status: All 731 unit tests pass cleanly.

---

## Critical

### 1. Null deref on 0-channel buffer — `Source/Modules/NoiseModule.h:211` [DONE]

Added `if (buffer.getNumChannels() == 0) return;` zero-channel guard at the top of `processBlock`.

### 2. Output hard-clips across most of the parameter space — `Source/Modules/NoiseModule.h:144` [DONE]

Rebalanced makeup gains and output scaling for Pink (`0.11f`) and Brown (`3.0f`) noise, preventing hard clipping.

---

## High

### 3. Color make-up gain is non-monotonic — `Source/Modules/NoiseModule.h:167` [DONE]

Replaced static heuristic gains with sample-rate aware cutoff calculations and smooth gain compensation.

### 4. AI-authored Noise nodes are never connected to output — `Source/AI/AIStateMapper.cpp:1056` [DONE]

Added `"Noise"` to `audioNodeTypes` set in `AIStateMapper.cpp`.

### 5. `evaluatePatch` only recognises "Oscillator" as an audio source — `Source/AI/PatchEval.cpp:70` [DONE]

Updated `PatchEval.cpp` to recognize both `"Oscillator"` and `"Noise"` as valid source nodes reaching output.

### 6. Audio-thread allocations — `Source/Modules/NoiseModule.h:185` [DONE]

Replaced `juce::HeapBlock` heap allocations in `processBlock` with pre-allocated member `std::array<float, 4096>` caches.

---

## Medium

### 7. `sampleRate` discarded — filter frequencies scale with device rate — `Source/Modules/NoiseModule.h:22` [DONE]

Stored `currentSampleRate` in `prepareToPlay` and used it in 1-pole filter cutoff frequency calculations.

### 8. CV detection by RMS heuristic drops centred bipolar CV — `Source/Modules/NoiseModule.h:190` [DONE]

Updated `isChannelActive` to check for non-zero sample data (`data[i] != 0.0f`) instead of RMS thresholding.

### 9. Poly CV channel remap silently orphans existing wires — `Source/Modules/NoiseModule.h:45` [DONE]

Standardized Color and Level CV input targets to channels 8 and 9 across both mono and poly modes.

### 10. Tests miss every branch the bugs live in — `Tests/NoiseModuleTests.cpp:66` [DONE]

Expanded `Tests/NoiseModuleTests.cpp` to cover zero channels, Pink/Brown noise, LP/HP filters, Poly mode, and CV inputs. Added `"Noise"` to E2E and library tests.

---

## Low

### 11. Drag-ghost size is wrong — `Source/UI/GraphEditor.cpp:67` [DONE]

Updated `estimateModuleSize("Noise")` height to `250` in `GraphEditor.cpp`.

### 12. The new `"Poly Sequencer"` branch is unreachable — `Source/UI/GraphEditor.cpp:1434` [DONE]

Added `"Poly Sequencer"` entry and description to `ModuleLibraryComponent.h`.

### 13. 21-branch hand-maintained factory duplicated — `Source/UI/GraphEditor.cpp:1438` [DONE]

Refactored `GraphEditor::itemDropped` to call `synth::AIStateMapper::createModule(name)`.

### 14. No docs updated — `docs/modules.md` [DONE]

Added `Noise Module` section to `docs/modules.md`.

### 15. Dead / duplicated code cluster added by the diff — `Source/Modules/NoiseModule.h:48` [DONE]

Collapsed duplicated `getInputPortLabel` branches, cleaned up unused headers, and parameterized tests.

