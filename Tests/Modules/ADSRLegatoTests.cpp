// ADSR Legato: with MIDI note messages driving the envelope, an overlapping note-on keeps it running.

#include "Modules/ADSRModule.h"

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;

void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

struct Event {
    int block;
    int offset;
    int note;
    bool on;
};

struct Rig {
    ADSRModule adsr;
    std::vector<float> out;

    explicit Rig(bool legato) {
        setActual(adsr, "attack", 0.05f);
        setActual(adsr, "decay", 0.05f);
        setActual(adsr, "sustain", 0.5f);
        setActual(adsr, "release", 0.1f);
        setActual(adsr, "legato", legato ? 1.0f : 0.0f);
        adsr.prepareToPlay(kSampleRate, kBlock);
    }

    void render(int numBlocks, const std::vector<Event>& events) {
        for (int b = 0; b < numBlocks; ++b) {
            juce::AudioBuffer<float> buf(adsr.getTotalNumOutputChannels(), kBlock);
            buf.clear();
            juce::MidiBuffer midi;
            for (const auto& e : events)
                if (e.block == b)
                    midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)100)
                                       : juce::MidiMessage::noteOff(1, e.note),
                                  e.offset);
            adsr.processBlock(buf, midi);
            out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        }
    }

    float peak(int fromBlock, int toBlock) const {
        float p = 0.0f;
        for (size_t i = (size_t)(fromBlock * kBlock); i < (size_t)(toBlock * kBlock); ++i)
            p = std::max(p, out[i]);
        return p;
    }
};

} // namespace

TEST(ADSRLegato, DefaultsOff) {
    ADSRModule adsr;
    auto* p = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&adsr, "legato"));
    ASSERT_NE(p, nullptr);
    EXPECT_FALSE(p->get());
}

TEST(ADSRLegato, AnOverlappingNoteOnKeepsTheEnvelopeAtSustain) {
    Rig rig(true);
    rig.render(14, {{0, 0, 60, true}, {8, 100, 64, true}});
    EXPECT_NEAR(rig.out[(size_t)(8 * kBlock)], 0.5f, 0.02f);
    EXPECT_LT(rig.peak(8, 14), 0.52f) << "no re-attack";
    EXPECT_GT(rig.out.back(), 0.45f) << "and it holds";
}

TEST(ADSRLegato, WithoutLegatoAnOverlappingNoteOnReArticulates) {
    Rig rig(false);
    rig.render(14, {{0, 0, 60, true}, {8, 100, 64, true}});
    EXPECT_GT(rig.peak(8, 14), 0.9f);
}

TEST(ADSRLegato, ReleasesOnlyWhenTheLastHeldNoteGoesOff) {
    Rig rig(true);
    rig.render(24, {{0, 0, 60, true},
                    {8, 0, 64, true},
                    {10, 0, 60, false}, // one note still down
                    {14, 0, 64, false}});
    EXPECT_NEAR(rig.out[(size_t)(13 * kBlock + kBlock - 1)], 0.5f, 0.02f) << "still sustaining with one note held";
    EXPECT_LT(rig.out.back(), 0.01f) << "released after the last note off";
}

TEST(ADSRLegato, AGaplessNoteOffAndOnStillReArticulatesWithLegatoOff) {
    Rig rig(false);
    rig.render(14, {{0, 0, 60, true}, {8, 100, 60, false}, {8, 100, 64, true}});
    EXPECT_GT(rig.peak(8, 14), 0.9f);
}
