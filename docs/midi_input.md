# MIDI Input Support

Gravisynth now supports external MIDI input devices, allowing you to connect hardware controllers for more expressive playability, including polyphonic support.

## Usage
1. Open the application.
2. Go to **Settings** (Cmd+,).
3. In the **Audio** tab, you will find the audio device settings.
4. The **MIDI Input** dropdown allows you to select and enable your connected MIDI hardware controllers.
5. Once selected, MIDI messages from the hardware will be automatically routed to the active audio processor graph.

## Implementation Details
- `AudioEngine` now implements `juce::MidiInputCallback` to listen to external MIDI messages.
- Incoming MIDI messages are collected via `juce::MidiMessageCollector` and injected into the audio processing block.
- All available system MIDI input devices are automatically detected and started on application initialization.
- The `AudioDeviceSelectorComponent` in the Settings window has been updated to display MIDI input options, providing users with the ability to manage connections.
