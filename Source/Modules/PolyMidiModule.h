#pragma once

#include "ModuleBase.h"
#include <atomic>

class PolyMidiModule : public ModuleBase {
public:
    PolyMidiModule()
        : ModuleBase("Poly MIDI", 0, 16) {
        // Port 0: Pitch CV (8 channels)
        // Port 1: Gate CV (8 channels)
        enableVisualBuffer(true);
    }

    bool acceptsMidi() const override { return true; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        for (auto& v : voices) {
            v.note = -1;
            v.active = false;
            v.lastUsedTime = 0;
            v.currentFreq = 0.0f;
            v.smoothedGate.reset(sampleRate, 0.005); // 5ms smoothing for gates
            v.smoothedFreq.reset(sampleRate, 0.005); // 5ms smoothing for pitch
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        buffer.clear();

        int numSamples = buffer.getNumSamples();
        int currentSample = 0;

        int midiEventCount = 0;
        for (const auto metadata : midiMessages) {
            auto msg = metadata.getMessage();
            int triggerSample = msg.getTimeStamp();

            triggerSample = juce::jlimit(0, numSamples - 1, triggerSample);

            if (triggerSample > currentSample) {
                renderChunk(buffer, currentSample, triggerSample);
                currentSample = triggerSample;
            }

            if (msg.isNoteOn()) {
                handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
                ++midiEventCount;
            } else if (msg.isNoteOff()) {
                handleNoteOff(msg.getNoteNumber());
                ++midiEventCount;
            } else if (msg.isAllNotesOff()) {
                allNotesOff();
                ++midiEventCount;
            }
        }

        if (currentSample < numSamples) {
            renderChunk(buffer, currentSample, numSamples);
        }

        // Push to visual buffer (Pitch Channel 0)
        if (auto* vb = getVisualBuffer()) {
            auto* ch = buffer.getWritePointer(0); // Pitch Voice 0
            for (int i = 0; i < numSamples; ++i) {
                vb->pushSample(ch[i] > 20.0f ? ch[i] / 1000.0f : 0.0f); // Scale for viz
            }
        }

        // Update atomic voice mask for lock-free UI reads (§4.3)
        uint8_t mask = 0;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].active)
                mask |= static_cast<uint8_t>(1 << i);
        }
        voiceMaskAtomic_.store(mask, std::memory_order_relaxed);
    }

    // API for UI — lock-free atomic read
    uint8_t getActiveVoiceMask() const noexcept { return voiceMaskAtomic_.load(std::memory_order_relaxed); }
    ModuleType getModuleType() const override { return ModuleType::PolyMidi; }
    int getVisibleOutputPortCount() const override { return 1; }
    juce::String getOutputPortLabel(int) const override { return "Poly Out"; }
    int getVisibleInputPortCount() const override { return 0; }

    LogicalPort mapOutputChannel(int raw) const override {
        LogicalPort p;
        // Pitch fan: raw 0-7
        if (raw >= 0 && raw <= 7) {
            p.visibleJackIndex = 0;
            p.role = PortRole::Pitch;
            p.isPolyGroupHead = (raw == 0);
            p.polyVoiceSpan = (raw == 0) ? 8 : 1;
            return p;
        }
        // Gate fan: raw 8-15
        if (raw >= 8 && raw <= 15) {
            p.visibleJackIndex = 0;
            p.role = PortRole::Gate;
            p.isPolyGroupHead = (raw == 8);
            p.polyVoiceSpan = (raw == 8) ? 8 : 1;
            return p;
        }
        return ModuleBase::mapOutputChannel(raw);
    }

private:
    static constexpr int MAX_VOICES = 8;

    // Atomic voice mask written by the audio thread (processBlock), read by UI thread
    // (getActiveVoiceMask). No lock required — relaxed atomics suffice for a status display.
    std::atomic<uint8_t> voiceMaskAtomic_{0};

    struct Voice {
        int note = -1;
        bool active = false;
        juce::int64 lastUsedTime = 0;
        float currentFreq = 0.0f; // Store freq to hold it on NoteOff
        juce::SmoothedValue<float> smoothedGate;
        juce::SmoothedValue<float> smoothedFreq;
    };
    Voice voices[MAX_VOICES];

    // Helper to render state to buffer
    void renderChunk(juce::AudioBuffer<float>& buffer, int startSample, int endSample) {
        if (endSample <= startSample)
            return;

        for (int i = 0; i < MAX_VOICES; ++i) {
            float* pitchCh = buffer.getWritePointer(i);
            float* gateCh = buffer.getWritePointer(i + 8);

            voices[i].smoothedFreq.setTargetValue(voices[i].currentFreq);
            voices[i].smoothedGate.setTargetValue(voices[i].active ? 1.0f : 0.0f);

            for (int s = startSample; s < endSample; ++s) {
                pitchCh[s] = voices[i].smoothedFreq.getNextValue();
                gateCh[s] = voices[i].smoothedGate.getNextValue();
            }
        }
    }

    void handleNoteOn(int note, float velocity) {
        juce::ignoreUnused(velocity);
        juce::int64 now = juce::Time::getMillisecondCounter();
        float freq = juce::MidiMessage::getMidiNoteInHertz(note);

        // Try to find existing note to re-trigger
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].active && voices[i].note == note) {
                voices[i].lastUsedTime = now;
                voices[i].currentFreq = freq;
                return;
            }
        }

        // Find empty voice
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!voices[i].active) {
                voices[i].note = note;
                voices[i].active = true;
                voices[i].lastUsedTime = now;
                voices[i].currentFreq = freq;
                return;
            }
        }

        // Steal least recently used voice
        int oldestIdx = findOldestVoiceIndex();
        voices[oldestIdx].note = note;
        voices[oldestIdx].active = true;
        voices[oldestIdx].lastUsedTime = now;
        voices[oldestIdx].currentFreq = freq;
    }

    void handleNoteOff(int note) {
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].active && voices[i].note == note) {
                voices[i].active = false;
            }
        }
    }

    void allNotesOff() {
        for (int i = 0; i < MAX_VOICES; ++i) {
            voices[i].active = false;
            voices[i].note = -1;
        }
    }

    int findOldestVoiceIndex() const {
        int oldestIdx = 0;
        juce::int64 oldestTime = voices[0].lastUsedTime;

        for (int i = 1; i < MAX_VOICES; ++i) {
            if (voices[i].lastUsedTime < oldestTime) {
                oldestTime = voices[i].lastUsedTime;
                oldestIdx = i;
            }
        }

        return oldestIdx;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyMidiModule)
};
