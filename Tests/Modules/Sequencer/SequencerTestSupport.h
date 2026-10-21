#pragma once

#include "Modules/Sequencer/SequencerModuleBase.h"
#include "Transport/TransportService.h"
#include <gtest/gtest.h>
#include <vector>

namespace seqtest {

/** One MIDI event with its absolute sample position since the first rendered block. */
struct Event {
    juce::int64 sample = 0;
    bool isOn = false;
    bool isOff = false;
    bool isCc = false;
    int note = 0; // note number, or the controller value for a CC
};

inline void setNumber(juce::AudioProcessor& module, const char* id, float realValue) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(realValue));
}

inline void setChoice(juce::AudioProcessor& module, const char* id, const juce::String& name) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&module, id));
    ASSERT_NE(p, nullptr) << id;
    const int index = p->choices.indexOf(name);
    ASSERT_GE(index, 0) << name;
    *p = index;
}

inline void run(juce::AudioProcessor& module, bool on) { setNumber(module, "run", on ? 1.0f : 0.0f); }

inline void collect(juce::MidiBuffer& midi, juce::int64 blockStart, std::vector<Event>& out) {
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        Event e;
        e.sample = blockStart + meta.samplePosition;
        e.isOn = msg.isNoteOn();
        e.isOff = msg.isNoteOff();
        e.isCc = msg.isController() && msg.getControllerNumber() == 74;
        e.note = e.isCc ? msg.getControllerValue() : msg.getNoteNumber();
        if (e.isOn || e.isOff || e.isCc)
            out.push_back(e);
    }
}

/** Renders `totalSamples` free-running in blocks of `blockSize` (last block may be shorter). */
inline std::vector<Event> render(juce::AudioProcessor& module, int totalSamples, int blockSize = 512) {
    std::vector<Event> events;
    juce::int64 position = 0;
    while (position < totalSamples) {
        const int n = (int)std::min<juce::int64>(blockSize, totalSamples - position);
        juce::AudioBuffer<float> buffer(2, n);
        juce::MidiBuffer midi;
        module.processBlock(buffer, midi);
        collect(midi, position, events);
        position += n;
    }
    return events;
}

inline std::vector<Event> onsOnly(const std::vector<Event>& events) {
    std::vector<Event> out;
    for (const auto& e : events)
        if (e.isOn)
            out.push_back(e);
    return out;
}

inline std::vector<Event> offsOnly(const std::vector<Event>& events) {
    std::vector<Event> out;
    for (const auto& e : events)
        if (e.isOff)
            out.push_back(e);
    return out;
}

inline std::vector<Event> ccsOnly(const std::vector<Event>& events) {
    std::vector<Event> out;
    for (const auto& e : events)
        if (e.isCc)
            out.push_back(e);
    return out;
}

/** A pattern whose first `count` steps have the given pitches and every other field at its default. */
inline synth::SequencerPattern patternOf(synth::SequencerModuleBase& module, std::initializer_list<int> pitches) {
    auto pattern = module.getPattern();
    int i = 0;
    for (int pitch : pitches)
        pattern.steps[(size_t)i++].pitch = (std::uint8_t)pitch;
    return pattern;
}

} // namespace seqtest
