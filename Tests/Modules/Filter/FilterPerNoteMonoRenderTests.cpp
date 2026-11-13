// Offline renders through FilterModule proving the Custom type's per-note modes in mono: a mono Filter on By pitch
// filters a low, a middle and a high note with the magnitude response of that band's shape, the same as poly voice 0
// and the same as the shared cursor parked on that shape, and a shape change mid-note does not click.
//
// Raw channels are literals on purpose (the saved-patch contract): Audio L is ch 0, Audio R ch 11, Pitch ch 19.
#include "CustomFilterTestHelpers.h"
#include "Modules/FilterModule.h"
#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::test;
using Mode = CustomFilterPerNote::Mode;

namespace {

constexpr int kBlock = 512;
constexpr int kChannels = 32;
constexpr int kLeft = 0, kRight = 11, kPitch = 19;
constexpr int kSettleBlocks = 8;   // the note is spotted and its kernel designed and faded in well before this
constexpr int kImpulseBlocks = 16; // 8192 samples: the 2048-tap kernel and then some

void setValue(FilterModule& m, const juce::String& id, float plain) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id) {
            r->setValueNotifyingHost(r->convertTo0to1(plain));
            return;
        }
    FAIL() << id;
}

float hzOf(int note) { return 440.0f * std::pow(2.0f, (float)(note - 69) / 12.0f); }

/** Three shapes far apart in sound, left to right on the map: a low pass, a band around 1 kHz and a high pass. */
CustomFilterShapes threeShapes() {
    CustomFilterShapes s;
    s.shapes = {
        makeShape("low pass", {{20.0f, 0.0f}, {400.0f, 0.0f}, {4000.0f, -30.0f}, {20000.0f, -36.0f}}, {0.1f, 0.5f}),
        makeShape("band", {{20.0f, -30.0f}, {300.0f, -24.0f}, {1000.0f, 0.0f}, {3000.0f, -24.0f}, {20000.0f, -30.0f}},
                  {0.5f, 0.5f}),
        makeShape("high pass", {{20.0f, -36.0f}, {300.0f, -30.0f}, {3000.0f, 0.0f}, {20000.0f, 0.0f}}, {0.9f, 0.5f})};
    return s;
}

struct Band {
    const char* name;
    int note;
    int shape; // index in threeShapes()
};
const Band kBands[] = {{"low", 36, 0}, {"middle", 66, 1}, {"high", 96, 2}};

/** A Custom Filter on threeShapes(), mono or Poly, with `mode` at Smoothness 0. */
std::unique_ptr<FilterModule> makeFilter(bool poly, Mode mode, float morphX = 0.5f, float morphY = 0.5f) {
    auto m = std::make_unique<FilterModule>();
    setValue(*m, "filterType", (float)FilterModule::kCustomTypeIndex);
    setValue(*m, "poly", poly ? 1.0f : 0.0f);
    setValue(*m, "morphX", morphX);
    setValue(*m, "morphY", morphY);
    m->setCustomShapes(threeShapes());
    CustomFilterPerNote p;
    p.mode = mode;
    p.smoothness = 0.0f;
    m->setCustomPerNote(p);
    m->prepareToPlay(kTestSampleRate, kBlock);
    return m;
}

/** Renders `notes` (one per block, -1 for no pitch) on voice 0: a sine of `amp` at `sineHz` on both legs, plus a unit
    impulse at the first sample of block `impulseBlock` (none when < 0). Returns Audio L. */
std::vector<float> render(FilterModule& m, const std::vector<int>& notes, int impulseBlock, float sineHz = 220.0f,
                          float amp = 0.3f) {
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out;
    const double w = 2.0 * juce::MathConstants<double>::pi * sineHz / kTestSampleRate;
    for (int b = 0; b < (int)notes.size(); ++b) {
        buf.clear();
        for (int i = 0; i < kBlock; ++i) {
            const float x = amp * (float)std::sin(w * (double)(b * kBlock + i)) + (b == impulseBlock && i == 0);
            buf.setSample(kLeft, i, x);
            buf.setSample(kRight, i, x);
            buf.setSample(kPitch, i, notes[(size_t)b] >= 0 ? hzOf(notes[(size_t)b]) : 0.0f);
        }
        m.processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(kLeft), buf.getReadPointer(kLeft) + kBlock);
    }
    return out;
}

/** The filter's impulse response while `note` is held: two identical renders, one with an impulse added once the
    note has settled; the convolution is linear and the kernels are the same in both, so the difference is the IR. */
std::vector<float> impulseResponse(bool poly, Mode mode, int note, float morphX = 0.5f, float morphY = 0.5f) {
    const std::vector<int> notes((size_t)(kSettleBlocks + kImpulseBlocks), note);
    auto plain = makeFilter(poly, mode, morphX, morphY);
    auto kicked = makeFilter(poly, mode, morphX, morphY);
    const auto a = render(*plain, notes, -1);
    const auto b = render(*kicked, notes, kSettleBlocks);
    std::vector<float> ir((size_t)(kImpulseBlocks * kBlock));
    for (size_t i = 0; i < ir.size(); ++i)
        ir[i] = b[(size_t)(kSettleBlocks * kBlock) + i] - a[(size_t)(kSettleBlocks * kBlock) + i];
    return ir;
}

/** Largest |y[n] - y[n-1]| over samples [from, to). */
float maxJump(const std::vector<float>& y, int from, int to) {
    float m = 0.0f;
    for (int n = std::max(1, from); n < to; ++n)
        m = std::max(m, std::abs(y[(size_t)n] - y[(size_t)n - 1]));
    return m;
}

/** Largest |y[n] - 2 y[n-1] + y[n-2]| over samples [from, to): a click is broadband, a sine's is tiny. */
float maxSecondDiff(const std::vector<float>& y, int from, int to) {
    float m = 0.0f;
    for (int n = std::max(2, from); n < to; ++n)
        m = std::max(m, std::abs(y[(size_t)n] - 2.0f * y[(size_t)n - 1] + y[(size_t)n - 2]));
    return m;
}

} // namespace

// Regression test for FRO1097: a mono Filter on By pitch kept the shared cursor's blend for every note.
TEST(FilterPerNoteMonoRender, ByPitchInMonoFiltersEachBandWithItsShapeExactlyAsPoly) {
    const auto freqs = logFrequencies(100.0f, 10000.0f, 31);
    const auto shapes = threeShapes();
    std::vector<std::vector<float>> monoDb;
    for (const auto& band : kBands) {
        const auto mono = impulseResponse(false, Mode::ByPitch, band.note);
        const auto poly = impulseResponse(true, Mode::ByPitch, band.note);
        const auto& pos = shapes.shapes[(size_t)band.shape].position;
        const auto parked = impulseResponse(false, Mode::Off, band.note, pos[0], pos[1]);
        float monoVsPoly = 0.0f, monoVsParked = 0.0f, monoVsDrawn = 0.0f;
        std::vector<float> db;
        for (float f : freqs) {
            const float m = magnitudeDb(mono, f);
            db.push_back(m);
            monoVsPoly = std::max(monoVsPoly, std::abs(m - magnitudeDb(poly, f)));
            monoVsParked = std::max(monoVsParked, std::abs(m - magnitudeDb(parked, f)));
            if (f >= 200.0f && f <= 8000.0f)
                monoVsDrawn = std::max(monoVsDrawn, std::abs(m - drawnDb(shapes.shapes[(size_t)band.shape], f)));
        }
        monoDb.push_back(db);
        std::printf("[render] %-6s band (note %d, %s): max |mono - poly| %.4f dB, |mono - cursor on shape| %.4f dB, "
                    "|mono - drawn| (200 Hz-8 kHz) %.2f dB\n",
                    band.name, band.note, shapes.shapes[(size_t)band.shape].name.toRawUTF8(), monoVsPoly, monoVsParked,
                    monoVsDrawn);
        EXPECT_LT(monoVsPoly, 0.01f) << band.name;
        EXPECT_LT(monoVsParked, 0.05f) << band.name;
        EXPECT_LT(monoVsDrawn, 3.0f) << band.name;
    }
    // The three bands really are three different filters: 20 dB or more apart somewhere.
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b) {
            float spread = 0.0f;
            for (size_t i = 0; i < freqs.size(); ++i)
                spread = std::max(spread, std::abs(monoDb[(size_t)a][i] - monoDb[(size_t)b][i]));
            std::printf("[render] %s vs %s band: largest difference %.1f dB\n", kBands[a].name, kBands[b].name, spread);
            EXPECT_GT(spread, 20.0f);
        }
}

// A hard (Smoothness 0) shape change under a held tone crossfades its kernel over one block: no sample step and no
// broadband spike beyond what the steady tone on either side already has.
TEST(FilterPerNoteMonoRender, AMonoShapeChangeDoesNotClick) {
    std::vector<int> notes;
    for (int note : {36, 96, 66, 36})
        notes.insert(notes.end(), 20, note);
    auto mono = makeFilter(false, Mode::ByPitch);
    auto poly = makeFilter(true, Mode::ByPitch);
    const auto y = render(*mono, notes, -1, 1000.0f, 0.5f); // 1 kHz: heard through every shape
    const auto p = render(*poly, notes, -1, 1000.0f, 0.5f);
    float monoVsPoly = 0.0f;
    for (size_t i = 0; i < y.size(); ++i)
        monoVsPoly = std::max(monoVsPoly, std::abs(y[i] - p[i]));
    EXPECT_LT(monoVsPoly, 1e-6f);
    for (int change = 1; change < 4; ++change) {
        const int at = change * 20 * kBlock;
        // Steady: the 8 blocks before the change and the 8 blocks from 4 blocks after it; changing: the change block
        // and the next one (the kernel crossfades over the block that spots the note).
        const int before0 = at - 8 * kBlock, after0 = at + 4 * kBlock, after1 = at + 12 * kBlock;
        const float steadyJump = std::max(maxJump(y, before0, at), maxJump(y, after0, after1));
        const float steadyD2 = std::max(maxSecondDiff(y, before0, at), maxSecondDiff(y, after0, after1));
        const float jump = maxJump(y, at, at + 2 * kBlock);
        const float d2 = maxSecondDiff(y, at, at + 2 * kBlock);
        std::printf("[render] change %d (note %d -> %d): peak |dy| %.5f vs steady %.5f (x%.3f); peak |d2y| %.6f vs "
                    "steady %.6f (x%.3f)\n",
                    change, notes[(size_t)(at / kBlock) - 1], notes[(size_t)(at / kBlock)], jump, steadyJump,
                    jump / steadyJump, d2, steadyD2, d2 / steadyD2);
        EXPECT_LE(jump, 1.1f * steadyJump) << "change " << change;
        EXPECT_LE(d2, 1.25f * steadyD2) << "change " << change;
    }
}
