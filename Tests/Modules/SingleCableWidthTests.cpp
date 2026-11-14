// Width from a single cable: the Oscillator and Wavetable unison stack, and the Reverb, must spread when only one
// Audio jack is wired, as they do when Left and Right are split. Bug kinds audio-unheard (the knob did nothing the
// ear could hear) and jack-raw-mismatch (the joined jack showed one cable but wired one leg).
//
// Every number is a render, not a formula: the two legs as the cable carries them, their correlation, and their sum
// to mono against the narrow stack (docs/modules/oscillator-voice.md#width-on-a-single-cable).

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kBlocks = 48;
constexpr int kSkipBlocks = 16; // the note's start and the first Width ramp are not measured

struct Legs {
    std::vector<float> left, right;
};

void setParam(juce::AudioProcessor& m, const char* id, float value) {
    auto* p = findParameterByID(&m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

/** A source module playing `note`, collapsed or split, as the cable sees its two legs. */
Legs renderSource(const char* type, bool dual, int unison, float width, int note, int blocks = kBlocks) {
    auto proc = synth::AIStateMapper::createModule(type);
    auto* m = dynamic_cast<ModuleBase*>(proc.get());
    Legs out;
    if (m == nullptr)
        return out;
    setParam(*m, "dualIO", dual ? 1.0f : 0.0f);
    setParam(*m, "unison", (float)unison);
    setParam(*m, "width", width);
    m->setPlayConfigDetails(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels(), kRate, kBlock);
    m->prepareToPlay(kRate, kBlock);
    const int right = m->rightAudioLegChannel();
    juce::AudioBuffer<float> buf(std::max(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels()), kBlock);
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        m->processBlock(buf, midi);
        if (b < kSkipBlocks)
            continue;
        for (int i = 0; i < kBlock; ++i) {
            out.left.push_back(buf.getSample(0, i));
            out.right.push_back(buf.getSample(right, i));
        }
    }
    return out;
}

double correlation(const Legs& l) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < l.left.size(); ++i) {
        ab += (double)l.left[i] * l.right[i];
        aa += (double)l.left[i] * l.left[i];
        bb += (double)l.right[i] * l.right[i];
    }
    return ab / std::sqrt(aa * bb + 1e-30);
}

double rms(const std::vector<float>& v) {
    double s = 0;
    for (float x : v)
        s += (double)x * x;
    return std::sqrt(s / (double)std::max<size_t>(1, v.size()));
}

bool identical(const std::vector<float>& a, const std::vector<float>& b) { return a == b; }

/** With SINGLE_CABLE_WIDTH_WAV set to a file path: 3 s of a joined Oscillator's Unison 3 stack at Width 0, then 3 s at
    Width 100%, as the one cable carries it, to hear the spread on headphones. */
void writeListeningWav() {
    const char* path = std::getenv("SINGLE_CABLE_WIDTH_WAV");
    if (path == nullptr)
        return;
    const int blocks = (int)(3.0 * kRate / kBlock) + kSkipBlocks;
    const Legs narrow = renderSource("Oscillator", /*dual*/ false, 3, 0.0f, 45, blocks);
    const Legs full = renderSource("Oscillator", false, 3, 1.0f, 45, blocks);
    juce::AudioBuffer<float> out(2, (int)(narrow.left.size() + full.left.size()));
    for (size_t i = 0; i < narrow.left.size(); ++i) {
        out.setSample(0, (int)i, narrow.left[i]);
        out.setSample(1, (int)i, narrow.right[i]);
    }
    for (size_t i = 0; i < full.left.size(); ++i) {
        out.setSample(0, (int)(narrow.left.size() + i), full.left[i]);
        out.setSample(1, (int)(narrow.left.size() + i), full.right[i]);
    }
    juce::File file(juce::String::fromUTF8(path));
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    ASSERT_NE(stream, nullptr) << path;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), kRate, 2, 24, {}, 0));
    ASSERT_NE(writer, nullptr);
    stream.release(); // the writer owns it now
    writer->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
}

const char* const kSources[] = {"Oscillator", "Wavetable"};
// 110, 220 and 880 Hz: a low note whose fundamental beats slowly against the companion, and a high one.
const int kNotes[] = {45, 57, 81};

} // namespace

TEST(SingleCableWidth, AOneVoiceSourceOnAJoinedJackIsCentredAtAnyWidth) {
    // Width spreads a unison stack; one voice has nothing to spread (the card greys Width at Unison 1), so the two legs
    // of the joined jack are the same samples whatever Width says, and the left leg is what it was before.
    for (const char* type : kSources) {
        for (const int note : kNotes) {
            const std::string n = std::string(type) + " note " + std::to_string(note);
            const Legs flat = renderSource(type, /*dual*/ false, 1, 0.0f, note);
            const Legs full = renderSource(type, false, 1, 1.0f, note);
            ASSERT_GT(rms(flat.left), 0.01) << n << ": the source must be audible";
            EXPECT_TRUE(identical(full.left, full.right)) << n;
            EXPECT_TRUE(identical(full.left, flat.left)) << n << ": Width moves nothing at one voice";
        }
    }
}

TEST(SingleCableWidth, AStackOnAJoinedJackWidensWithWidthAndKeepsItsMonoSum) {
    // The jack-raw-mismatch and audio-unheard proof: the legs one cable carries move apart as Width goes up, and their
    // average (a mono mix downstream) loses no more than 3 dB against the narrow stack: the stack's own law, the same
    // as from split jacks (measured -2.2 dB at Width 100%), with no comb or cancellation.
    for (const char* type : kSources) {
        for (const int note : kNotes) {
            const std::string n = std::string(type) + " note " + std::to_string(note);
            const Legs narrow = renderSource(type, /*dual*/ false, 3, 0.0f, note);
            const Legs half = renderSource(type, false, 3, 0.5f, note);
            const Legs full = renderSource(type, false, 3, 1.0f, note);
            EXPECT_GT(correlation(narrow), 0.999) << n << ": Width 0 keeps the legs alike";
            EXPECT_LT(correlation(full), correlation(half)) << n << ": more Width, less alike";
            EXPECT_LT(correlation(full), 0.8) << n << ": Width 100% spreads the stack on one cable";
            std::vector<float> monoNarrow(narrow.left.size()), monoFull(full.left.size());
            for (size_t i = 0; i < monoNarrow.size(); ++i) {
                monoNarrow[i] = 0.5f * (narrow.left[i] + narrow.right[i]);
                monoFull[i] = 0.5f * (full.left[i] + full.right[i]);
            }
            const double dB = 20.0 * std::log10(rms(monoFull) / rms(monoNarrow));
            std::printf("[measured] %s: correlation %.3f / %.3f / %.3f at Width 0 / 50 / 100 %%, mono sum %+.2f dB\n",
                        n.c_str(), correlation(narrow), correlation(half), correlation(full), dB);
            EXPECT_GT(dB, -3.0) << n << ": the mono sum keeps its level";
            EXPECT_LT(dB, 0.5) << n;
        }
    }
    writeListeningWav();
}

TEST(SingleCableWidth, ASplitOneVoiceSourceKeepsItsSoundAtAnyWidth) {
    for (const char* type : kSources) {
        const Legs flat = renderSource(type, /*dual*/ true, 1, 0.0f, 57);
        const Legs full = renderSource(type, true, 1, 1.0f, 57);
        EXPECT_TRUE(identical(full.left, flat.left)) << type << ": a single voice has nothing to pan when split";
        EXPECT_TRUE(identical(full.right, flat.right)) << type;
        EXPECT_GT(correlation(full), 0.999) << type;
    }
}

TEST(SingleCableWidth, AUnisonStackSpreadsFromACollapsedJackExactlyAsFromASplitOne) {
    for (const char* type : kSources) {
        for (const int unison : {2, 3, 5}) {
            const std::string n = std::string(type) + " unison " + std::to_string(unison);
            const Legs collapsed = renderSource(type, false, unison, 1.0f, 57);
            const Legs split = renderSource(type, true, unison, 1.0f, 57);
            EXPECT_TRUE(identical(collapsed.left, split.left)) << n;
            EXPECT_TRUE(identical(collapsed.right, split.right)) << n;
            EXPECT_LT(correlation(collapsed), 0.95) << n << ": the one cable carries the spread stack";
        }
    }
}

TEST(SingleCableWidth, ACollapsedMonoJackOwnsBothLegsAndAPolyOneKeepsTheLeftOnly) {
    for (const char* type : kSources) {
        auto proc = synth::AIStateMapper::createModule(type);
        auto* m = dynamic_cast<ModuleBase*>(proc.get());
        ASSERT_NE(m, nullptr) << type;
        const int right = m->rightAudioLegChannel();
        setParam(*m, "dualIO", 0.0f);
        auto head = m->mapOutputChannel(0);
        EXPECT_EQ(head.polyVoiceSpan, 2) << type << ": one jack, both legs";
        EXPECT_EQ(head.voiceStride, right) << type << ": the legs are kRightBase apart";
        EXPECT_EQ(m->getJackTargets(0, /*isInput*/ false).front().rawStride, right) << type;
        EXPECT_FALSE(m->mapOutputChannel(right).isPolyGroupHead) << type << ": no jack of its own for the right block";

        setParam(*m, "dualIO", 1.0f);
        EXPECT_EQ(m->mapOutputChannel(0).polyVoiceSpan, 1) << type << ": split: Left is one leg";
        EXPECT_TRUE(m->mapOutputChannel(right).isPolyGroupHead) << type;

        setParam(*m, "dualIO", 0.0f);
        setParam(*m, "poly", 1.0f);
        EXPECT_EQ(m->mapOutputChannel(0).polyVoiceSpan, 8) << type << ": a collapsed poly jack still fans 8 voices";
        EXPECT_EQ(m->mapOutputChannel(0).voiceStride, 1) << type;
    }
}

TEST(SingleCableWidth, AMonoSourceFedToACollapsedReverbWidensWithWidth) {
    // The Reverb's collapsed jack already owns both legs, so a mono feed reaches both. Measure that Width moves the
    // wet legs apart from that feed (it is the third module the ticket names).
    auto corrAt = [](float width) {
        auto proc = synth::AIStateMapper::createModule("Reverb");
        auto* m = dynamic_cast<ModuleBase*>(proc.get());
        setParam(*m, "dualIO", 0.0f);
        setParam(*m, "width", width);
        setParam(*m, "wet", 1.0f);
        setParam(*m, "dry", 0.0f);
        m->setPlayConfigDetails(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels(), kRate, kBlock);
        m->prepareToPlay(kRate, kBlock);
        const int channels = std::max(m->getTotalNumInputChannels(), m->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf(channels, kBlock);
        Legs out;
        double phase = 0.0;
        for (int b = 0; b < kBlocks; ++b) {
            buf.clear();
            for (int i = 0; i < kBlock; ++i) {
                const float v = (b < 8) ? 0.4f * (float)(2.0 * std::fmod(phase, 1.0) - 1.0) : 0.0f;
                phase += 220.0 / kRate;
                buf.setSample(0, i, v);
                buf.setSample(1, i, v); // the collapsed jack fans one cable onto both legs
            }
            juce::MidiBuffer midi;
            m->processBlock(buf, midi);
            if (b >= 12)
                for (int i = 0; i < kBlock; ++i) {
                    out.left.push_back(buf.getSample(0, i));
                    out.right.push_back(buf.getSample(1, i));
                }
        }
        EXPECT_GT(rms(out.left), 1.0e-4) << "the reverb tail must be audible";
        return correlation(out);
    };
    const double narrow = corrAt(0.0f), wide = corrAt(1.0f);
    EXPECT_GT(narrow, 0.999) << "Width 0: both wet legs match";
    EXPECT_LT(wide, 0.5) << "Width 100%: the wet legs of a mono feed are well apart";
}
