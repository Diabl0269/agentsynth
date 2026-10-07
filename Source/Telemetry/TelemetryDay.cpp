#include "TelemetryDay.h"

// Concern: the module name mapping table and the active-minutes buckets.

namespace synth::telemetry {

const std::array<ModuleEntry, kModuleCount>& moduleTable() {
    static const std::array<ModuleEntry, kModuleCount> table = {{
        {"Audio Input", "audioInput", &platform::contracts::TelemetryModuleCounters::audioInput},
        {"Audio Output", "audioOutput", &platform::contracts::TelemetryModuleCounters::audioOutput},
        {"Midi Input", "midiInput", &platform::contracts::TelemetryModuleCounters::midiInput},
        {"Oscillator", "oscillator", &platform::contracts::TelemetryModuleCounters::oscillator},
        {"Filter", "filter", &platform::contracts::TelemetryModuleCounters::filter},
        {"VCA", "vca", &platform::contracts::TelemetryModuleCounters::vca},
        {"ADSR", "adsr", &platform::contracts::TelemetryModuleCounters::adsr},
        {"Sequencer", "sequencer", &platform::contracts::TelemetryModuleCounters::sequencer},
        {"LFO", "lfo", &platform::contracts::TelemetryModuleCounters::lfo},
        {"Distortion", "distortion", &platform::contracts::TelemetryModuleCounters::distortion},
        {"Delay", "delay", &platform::contracts::TelemetryModuleCounters::delay},
        {"Reverb", "reverb", &platform::contracts::TelemetryModuleCounters::reverb},
        {"MIDI Keyboard", "midiKeyboard", &platform::contracts::TelemetryModuleCounters::midiKeyboard},
        {"Amp Env", "ampEnv", &platform::contracts::TelemetryModuleCounters::ampEnv},
        {"Filter Env", "filterEnv", &platform::contracts::TelemetryModuleCounters::filterEnv},
        {"Poly MIDI", "polyMidi", &platform::contracts::TelemetryModuleCounters::polyMidi},
        {"Poly Sequencer", "polySequencer", &platform::contracts::TelemetryModuleCounters::polySequencer},
        {"Attenuverter", "attenuverter", &platform::contracts::TelemetryModuleCounters::attenuverter},
        {"Mod Slot", "modSlot", &platform::contracts::TelemetryModuleCounters::modSlot},
        {"Chorus", "chorus", &platform::contracts::TelemetryModuleCounters::chorus},
        {"Phaser", "phaser", &platform::contracts::TelemetryModuleCounters::phaser},
        {"Compressor", "compressor", &platform::contracts::TelemetryModuleCounters::compressor},
        {"Flanger", "flanger", &platform::contracts::TelemetryModuleCounters::flanger},
        {"Limiter", "limiter", &platform::contracts::TelemetryModuleCounters::limiter},
        {"Parametric EQ", "parametricEq", &platform::contracts::TelemetryModuleCounters::parametricEq},
        {"Voice Mixer", "voiceMixer", &platform::contracts::TelemetryModuleCounters::voiceMixer},
        {"Bitcrusher", "bitcrusher", &platform::contracts::TelemetryModuleCounters::bitcrusher},
        {"Pitch Shifter", "pitchShifter", &platform::contracts::TelemetryModuleCounters::pitchShifter},
        {"Ring Modulator", "ringModulator", &platform::contracts::TelemetryModuleCounters::ringModulator},
        {"Noise", "noise", &platform::contracts::TelemetryModuleCounters::noise},
        {"Envelope Follower", "envelopeFollower", &platform::contracts::TelemetryModuleCounters::envelopeFollower},
        {"Math", "math", &platform::contracts::TelemetryModuleCounters::math},
        {"Macros", "macros", &platform::contracts::TelemetryModuleCounters::macros},
        {"Sample & Hold", "sampleHold", &platform::contracts::TelemetryModuleCounters::sampleHold},
        {"Comparator", "comparator", &platform::contracts::TelemetryModuleCounters::comparator},
        {"Sampler", "sampler", &platform::contracts::TelemetryModuleCounters::sampler},
        {"Wavetable", "wavetable", &platform::contracts::TelemetryModuleCounters::wavetable},
        {"External MIDI", "externalMidi", &platform::contracts::TelemetryModuleCounters::externalMidi},
        {"Hosted Plugin", "hostedPlugin", &platform::contracts::TelemetryModuleCounters::hostedPlugin},
        {"Track In", "trackIn", &platform::contracts::TelemetryModuleCounters::trackIn},
        {"Rec Tap", "recTap", &platform::contracts::TelemetryModuleCounters::recTap},
        {"Track Audio", "trackAudio", &platform::contracts::TelemetryModuleCounters::trackAudio},
        {"Macro In", "macroIn", &platform::contracts::TelemetryModuleCounters::macroIn},
        {"Macro Out", "macroOut", &platform::contracts::TelemetryModuleCounters::macroOut},
        {"Macro MIDI In", "macroMidiIn", &platform::contracts::TelemetryModuleCounters::macroMidiIn},
        {"Macro MIDI Out", "macroMidiOut", &platform::contracts::TelemetryModuleCounters::macroMidiOut},
        {"Channel Strip", "channelStrip", &platform::contracts::TelemetryModuleCounters::channelStrip},
        {"Master", "master", &platform::contracts::TelemetryModuleCounters::master},
    }};
    return table;
}

int moduleIndexForFactoryName(const juce::String& factoryTypeName) {
    const auto& table = moduleTable();
    for (std::size_t i = 0; i < table.size(); ++i)
        if (factoryTypeName == table[i].factoryName)
            return static_cast<int>(i);
    return -1;
}

juce::String activeMinutesBucket(int minutes) {
    if (minutes < 15)
        return "<15";
    if (minutes < 60)
        return "15-60";
    if (minutes < 180)
        return "60-180";
    return "180+";
}

} // namespace synth::telemetry
