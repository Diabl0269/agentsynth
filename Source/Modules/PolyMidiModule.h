#pragma once

#include "ModuleBase.h"
#include <algorithm>
#include <atomic>

class PolyMidiModule : public ModuleBase {
public:
    /** Voice-steal strategy, in `voiceSteal` choice order. Index 0 is the historical behaviour,
     *  so a patch saved before this parameter existed loads with unchanged voice allocation. */
    enum class StealMode { Oldest = 0, RoundRobin, Random };

    PolyMidiModule()
        : ModuleBase("Poly MIDI", 0, 16) {
        // Port 0: Pitch CV (8 channels)
        // Port 1: Gate CV (8 channels)
        addParameter(voiceStealParam = new juce::AudioParameterChoice("voiceSteal", "Voice Steal",
                                                                      {"Oldest", "Round-Robin", "Random"}, 0));
        enableVisualBuffer(true);
    }

    bool acceptsMidi() const override { return true; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        sampleCounter_ = 0;
        lastStamp_ = 0;
        roundRobinCursor_ = 0;
        stealRandom_.setSeed(kStealSeed);
        for (auto& v : voices) {
            v.note = -1;
            v.active = false;
            v.lastUsedSample = 0;
            v.currentFreq = 0.0f;
            v.smoothedGate.reset(sampleRate, 0.005); // 5ms smoothing for gates
            v.smoothedFreq.reset(sampleRate, 0.005); // 5ms smoothing for pitch
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        int numSamples = buffer.getNumSamples();

        // Voice age is measured in samples elapsed since prepareToPlay, never in wall-clock time
        // (issue #198): a sample counter is deterministic across runs and finer-grained than the
        // block rate, so a chord delivered inside one block still steals in arrival order. Advanced
        // before the bypass check so ages stay ordered across a bypass toggle.
        const juce::int64 blockStartSample = sampleCounter_;
        sampleCounter_ += numSamples;

        if (isBypassed()) {
            buffer.clear();
            return;
        }

        buffer.clear();

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
                handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity(), blockStartSample + triggerSample);
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

    /** Current voice-steal strategy. Cheap enough to read per note-on from the audio thread. */
    StealMode getStealMode() const noexcept { return static_cast<StealMode>(voiceStealParam->getIndex()); }

private:
    static constexpr int MAX_VOICES = 8;

    // Fixed PRNG seed for StealMode::Random. Re-applied in prepareToPlay so every render of a
    // patch makes the same "random" choices — the whole point of issue #198.
    static constexpr juce::int64 kStealSeed = 0x5EED0B01; // arbitrary — only its stability matters

    // Atomic voice mask written by the audio thread (processBlock), read by UI thread
    // (getActiveVoiceMask). No lock required — relaxed atomics suffice for a status display.
    std::atomic<uint8_t> voiceMaskAtomic_{0};

    juce::AudioParameterChoice* voiceStealParam = nullptr;

    struct Voice {
        int note = -1;
        bool active = false;
        juce::int64 lastUsedSample = 0;
        float currentFreq = 0.0f; // Store freq to hold it on NoteOff
        juce::SmoothedValue<float> smoothedGate;
        juce::SmoothedValue<float> smoothedFreq;
    };
    Voice voices[MAX_VOICES];

    juce::int64 sampleCounter_ = 0; // samples elapsed since prepareToPlay
    juce::int64 lastStamp_ = 0;     // last stamp handed out — keeps ages strictly increasing
    int roundRobinCursor_ = 0;      // next voice StealMode::RoundRobin will take
    juce::Random stealRandom_{kStealSeed};

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

    void handleNoteOn(int note, float velocity, juce::int64 eventSample) {
        juce::ignoreUnused(velocity);
        const juce::int64 now = stampFor(eventSample);
        float freq = juce::MidiMessage::getMidiNoteInHertz(note);

        // Try to find existing note to re-trigger
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].active && voices[i].note == note) {
                voices[i].lastUsedSample = now;
                voices[i].currentFreq = freq;
                return;
            }
        }

        // Find empty voice
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!voices[i].active) {
                voices[i].note = note;
                voices[i].active = true;
                voices[i].lastUsedSample = now;
                voices[i].currentFreq = freq;
                return;
            }
        }

        // All eight voices are busy — the Voice Steal parameter decides which one loses its note.
        int stealIdx = selectVoiceToSteal();
        voices[stealIdx].note = note;
        voices[stealIdx].active = true;
        voices[stealIdx].lastUsedSample = now;
        voices[stealIdx].currentFreq = freq;
    }

    // Voice ages must be strictly increasing, not merely non-decreasing: a chord whose notes share
    // one sample offset would otherwise hand out equal stamps, and "least recently used" would
    // collapse to "voice 0" for every note in it — the degenerate case from issue #198. Clamping to
    // lastStamp_ + 1 keeps the ordering exact while staying deterministic.
    juce::int64 stampFor(juce::int64 eventSample) noexcept {
        lastStamp_ = std::max(eventSample, lastStamp_ + 1);
        return lastStamp_;
    }

    int selectVoiceToSteal() noexcept {
        switch (getStealMode()) {
        case StealMode::RoundRobin: {
            const int idx = roundRobinCursor_;
            roundRobinCursor_ = (roundRobinCursor_ + 1) % MAX_VOICES;
            return idx;
        }
        case StealMode::Random:
            // Module-owned, fixed-seed PRNG advanced only here: the choice sounds random, but two
            // renders of the same input steal identically.
            return stealRandom_.nextInt(MAX_VOICES);
        case StealMode::Oldest:
        default:
            return findOldestVoiceIndex();
        }
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
        juce::int64 oldestTime = voices[0].lastUsedSample;

        for (int i = 1; i < MAX_VOICES; ++i) {
            if (voices[i].lastUsedSample < oldestTime) {
                oldestTime = voices[i].lastUsedSample;
                oldestIdx = i;
            }
        }

        return oldestIdx;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyMidiModule)
};
