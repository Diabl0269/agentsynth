# Mute Option Implementation Plan - Phase 2

## Goal
Implement `isMuted()` check in `processBlock` for all remaining audio modules:
- ADSRModule
- ExternalMidiModule
- FX Modules (Chorus, Compressor, Delay, Distortion, Flanger, Limiter, Phaser, Reverb)
- LFOModule
- MidiKeyboardModule
- PolyMidiModule
- PolySequencerModule
- SequencerModule
- VoiceMixerModule

## Strategy
1. **Iterate** over the module list.
2. **Inspect** each module's `processBlock` to identify existing bypass logic.
3. **Replace/Inject** `if (isBypassed() || isMuted())` at the start of `processBlock`.
4. **Run existing tests** to verify no regressions.

## Implementation Steps
- [ ] ADSRModule (Source/Modules/ADSRModule.h)
- [ ] ExternalMidiModule (Source/Modules/ExternalMidiModule.h)
- [ ] ChorusModule (Source/Modules/FX/ChorusModule.h)
- [ ] CompressorModule (Source/Modules/FX/CompressorModule.h)
- [ ] DelayModule (Source/Modules/FX/DelayModule.h)
- [ ] DistortionModule (Source/Modules/FX/DistortionModule.h)
- [ ] FlangerModule (Source/Modules/FX/FlangerModule.h)
- [ ] LimiterModule (Source/Modules/FX/LimiterModule.h)
- [ ] PhaserModule (Source/Modules/FX/PhaserModule.h)
- [ ] ReverbModule (Source/Modules/FX/ReverbModule.h)
- [ ] LFOModule (Source/Modules/LFOModule.h)
- [ ] MidiKeyboardModule (Source/Modules/MidiKeyboardModule.h)
- [ ] PolyMidiModule (Source/Modules/PolyMidiModule.h)
- [ ] PolySequencerModule (Source/Modules/PolySequencerModule.h)
- [ ] SequencerModule (Source/Modules/SequencerModule.h)
- [ ] VoiceMixerModule (Source/Modules/VoiceMixerModule.h)

## Verification
- Run `cmake --build build --target GravisynthTests`
- Run `./build/Tests/GravisynthTests`
- Verify existing bypass tests still pass with the new mute condition.

## Docs Updates
- Update `Module_Development_Guide.md` if necessary to reflect the new mute requirement.
