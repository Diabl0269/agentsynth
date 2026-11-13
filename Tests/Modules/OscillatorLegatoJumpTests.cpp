// Oscillator mono Legato: a note that only touches the one before it jumps to its pitch at its own sample,
// the same way for every block alignment and at Glide 0 as well as with a long Glide; an overlapping
// note (or any note with Legato off) slides.

#include "Modules/OscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kStep = 6000; // a sixteenth note at 120 bpm
constexpr int kNotesPerPass = 16;

struct Hit {
    int on;
    int off;
    int note;
};

enum class Order { OffThenOn, OnThenOff };

struct Settings {
    bool legato = true;
    float glideMs = 0.0f;
    int block = 512;
    Order order = Order::OffThenOn;
};

void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

float hzOfNote(int note) { return 440.0f * std::pow(2.0f, ((float)note - 69.0f) / 12.0f); }

// Pitches that always move by at least a fourth, so a jump and a slide are easy to tell apart.
int pitchOfNote(int index) {
    static const int pitches[kNotesPerPass] = {48, 55, 50, 60, 52, 57, 47, 59, 53, 62, 49, 56, 51, 58, 54, 61};
    return pitches[index % kNotesPerPass];
}

// `passes` loops of 16 touching sixteenths, the first note starting at `lead`; each ends where the next begins.
std::vector<Hit> touchingNotes(int lead, int passes, int gap = 0) {
    std::vector<Hit> hits;
    for (int i = 0; i < kNotesPerPass * passes; ++i)
        hits.push_back({lead + i * kStep, lead + (i + 1) * kStep - gap, pitchOfNote(i)});
    return hits;
}

std::vector<float> render(const std::vector<Hit>& hits, int total, const Settings& cfg) {
    OscillatorModule osc;
    setActual(osc, "randomPhase", 0.0f); // passes are compared sample for sample
    setActual(osc, "glide", cfg.glideMs);
    setActual(osc, "legato", cfg.legato ? 1.0f : 0.0f);
    osc.prepareToPlay(kSampleRate, cfg.block);
    std::vector<float> out;
    for (int pos = 0; pos < total; pos += cfg.block) {
        juce::MidiBuffer midi;
        auto addOffs = [&] {
            for (const auto& h : hits)
                if (h.off >= pos && h.off < pos + cfg.block)
                    midi.addEvent(juce::MidiMessage::noteOff(1, h.note), h.off - pos);
        };
        auto addOns = [&] {
            for (const auto& h : hits)
                if (h.on >= pos && h.on < pos + cfg.block)
                    midi.addEvent(juce::MidiMessage::noteOn(1, h.note, (juce::uint8)100), h.on - pos);
        };
        if (cfg.order == Order::OffThenOn) {
            addOffs();
            addOns();
        } else {
            addOns();
            addOffs();
        }
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, cfg.block);
        buf.clear();
        osc.processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + cfg.block);
    }
    return out;
}

std::vector<double> upwardCrossings(const std::vector<float>& x) {
    std::vector<double> c;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            c.push_back((double)(i - 1) + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]));
    return c;
}

// Largest relative error of the frequency against `expectedHz` over the whole periods lying inside [from, to).
// 1000 when no whole period lies there, so a missing sound fails too.
double worstError(const std::vector<double>& crossings, double from, double to, double expectedHz) {
    double worst = 0.0;
    bool any = false;
    for (size_t i = 0; i + 1 < crossings.size(); ++i) {
        if (crossings[i] < from || crossings[i + 1] >= to)
            continue;
        any = true;
        worst = std::max(worst, std::abs(kSampleRate / (crossings[i + 1] - crossings[i]) / expectedHz - 1.0));
    }
    return any ? worst : 1000.0;
}

// The pitch before a note start is still the old one, and from just after the note-on's own sample it is the
// new one, with no slide in between. Returns the worst error over all boundaries.
double worstJump(const std::vector<Hit>& hits, const std::vector<float>& out) {
    const auto crossings = upwardCrossings(out);
    double worst = 0.0;
    for (size_t k = 1; k < hits.size(); ++k) {
        const double s = hits[k].on;
        worst = std::max(worst, worstError(crossings, s - 900.0, s - 2.0, hzOfNote(hits[k - 1].note)));
        worst = std::max(worst, worstError(crossings, s + 128.0, s + 1800.0, hzOfNote(hits[k].note)));
    }
    return worst;
}

const int kLeads[] = {0, 1, 37, 255, 311, 400, 511};

} // namespace

TEST(OscillatorLegatoJump, TouchingNotesJumpAtGlideZeroAndWithALongGlideForEveryAlignment) {
    for (const float glide : {0.0f, 200.0f}) {
        for (const int lead : kLeads) {
            for (const Order order : {Order::OffThenOn, Order::OnThenOff}) {
                Settings cfg;
                cfg.glideMs = glide;
                cfg.order = order;
                const auto hits = touchingNotes(lead, 1);
                const auto out = render(hits, lead + (kNotesPerPass + 1) * kStep, cfg);
                EXPECT_LT(worstJump(hits, out), 0.015)
                    << "glide " << glide << " lead " << lead << " order " << (int)order;
            }
        }
    }
}

TEST(OscillatorLegatoJump, ANoteAfterAGapOfOneBlockJumpsToo) {
    for (const float glide : {0.0f, 200.0f}) {
        for (const int lead : kLeads) {
            Settings cfg;
            cfg.glideMs = glide;
            const auto hits = touchingNotes(lead, 1, /*gap*/ cfg.block);
            const auto out = render(hits, lead + (kNotesPerPass + 1) * kStep, cfg);
            EXPECT_LT(worstJump(hits, out), 0.015) << "glide " << glide << " lead " << lead;
        }
    }
}

TEST(OscillatorLegatoJump, EveryPassOfALoopSoundsTheSameWhateverTheBlockAlignment) {
    for (const float glide : {0.0f, 200.0f}) {
        Settings cfg;
        cfg.glideMs = glide;
        const auto hits = touchingNotes(100, 2);
        const auto out = render(hits, 100 + (2 * kNotesPerPass + 1) * kStep, cfg);
        double worst = 0.0;
        for (int k = 1; k < kNotesPerPass; ++k) {
            const int a = hits[(size_t)k].on;
            const int b = hits[(size_t)(k + kNotesPerPass)].on;
            for (int i = 128; i < 1800; ++i)
                worst = std::max(worst, (double)std::abs(out[(size_t)(a + i)] - out[(size_t)(b + i)]));
        }
        EXPECT_LT(worst, 0.02) << "glide " << glide << ": a pass differs from the one before it";
    }
}

TEST(OscillatorLegatoJump, AnOverlappingNoteSlidesAndSoDoesEveryNoteWithLegatoOff) {
    for (const int lead : kLeads) {
        // Legato on: note 2 starts while note 1 is still held.
        std::vector<Hit> overlap = {{lead, lead + kStep + 400, 48}, {lead + kStep, lead + 2 * kStep, 60}};
        Settings on;
        on.glideMs = 200.0f;
        const auto slid = upwardCrossings(render(overlap, lead + 3 * kStep, on));
        const double from = lead + kStep + 2800.0;
        const double to = lead + kStep + 3600.0;
        const double hzNow = [&] {
            for (size_t i = 0; i + 1 < slid.size(); ++i)
                if (slid[i] >= from && slid[i + 1] < to)
                    return kSampleRate / (slid[i + 1] - slid[i]);
            return 0.0;
        }();
        EXPECT_GT(hzNow, hzOfNote(48) * 1.05) << "overlap, lead " << lead;
        EXPECT_LT(hzNow, hzOfNote(60) * 0.95) << "overlap, lead " << lead << ": still sliding 60 ms in";

        // Legato off: the same touching notes all slide.
        Settings off;
        off.legato = false;
        off.glideMs = 200.0f;
        const auto touching = touchingNotes(lead, 1);
        const auto out = render(touching, lead + (kNotesPerPass + 1) * kStep, off);
        const auto crossings = upwardCrossings(out);
        for (size_t k = 1; k < touching.size(); ++k) {
            const double s = touching[k].on;
            EXPECT_GT(worstError(crossings, s + 1500.0, s + 2500.0, hzOfNote(touching[k].note)), 0.03)
                << "legato off, note " << k << " lead " << lead;
        }
    }
}
