// CustomFilterVowelTests.cpp -- the Custom filter's vowel shapes measured through the real engine: the kernel's
// magnitude response has each vowel's formants at the Csound tenor table's frequencies and filter-style bandwidths
// (wider than the table's, which are for pitch-independent synthesis), with deep valleys between them, and the five
// vowels are about equally loud. Eh and ih are in the Start from list.
#include "../../UI/ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "CustomFilterTestHelpers.h"
#include "Modules/FilterModule.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::test;

namespace {

struct Formant {
    float hz, bwHz;
};

struct VowelCase {
    CustomFilterShapes::Preset preset;
    const char* name;
    Formant f1, f2, f3; // frequencies from the table; bandwidths are the filter-style ones drawn
    bool checkF2F3Valley;
};

// Csound manual tenor table (a, e, i, u); ih (as in "bit") is Peterson and Barney's male F1-F3.
const VowelCase kVowels[] = {
    {CustomFilterShapes::Preset::Ah, "ah", {650, 155}, {1080, 185}, {2650, 120}, false},
    {CustomFilterShapes::Preset::Eh, "eh", {400, 130}, {1700, 180}, {2600, 100}, false},
    {CustomFilterShapes::Preset::Ee, "ee", {290, 110}, {1870, 150}, {2800, 100}, true},
    {CustomFilterShapes::Preset::Ih, "ih", {390, 110}, {1990, 160}, {2550, 110}, true},
    {CustomFilterShapes::Preset::Oo, "oo", {350, 120}, {600, 100}, {2700, 100}, false},
};

struct Response {
    std::vector<float> hz, db;
};

/** The vowel stamped on a single-shape Filter, measured on the engine's impulse response (Cutoff at its reference,
    no shift, Depth 100%, no resonance), 150 Hz to 6 kHz on a fine log grid. */
Response measure(CustomFilterShapes::Preset preset) {
    CustomFilterShapes shapes;
    CustomFilterShape shape;
    shape.name = "v";
    shape.curve = CustomFilterShapes::presetCurve(preset);
    shape.position = {0.5f, 0.5f};
    shapes.shapes = {shape};
    CustomFilterEngine engine;
    engine.setShapes(shapes);
    const auto ir = renderImpulse(engine, {}, 4096);
    Response r;
    r.hz = logFrequencies(150.0f, 6000.0f, 1800);
    for (float f : r.hz)
        r.db.push_back(magnitudeDb(ir, f));
    return r;
}

/** Local maxima at least 3 dB above the lowest point reached before a higher value on either side. */
std::vector<size_t> peaks(const Response& r) {
    std::vector<size_t> out;
    const size_t n = r.db.size();
    for (size_t i = 1; i + 1 < n; ++i) {
        if (!(r.db[i] >= r.db[i - 1] && r.db[i] > r.db[i + 1]))
            continue;
        float lowL = r.db[i], lowR = r.db[i];
        for (size_t j = i; j-- > 0 && r.db[j] <= r.db[i];)
            lowL = std::min(lowL, r.db[j]);
        for (size_t j = i + 1; j < n && r.db[j] <= r.db[i]; ++j)
            lowR = std::min(lowR, r.db[j]);
        if (r.db[i] - std::max(lowL, lowR) >= 3.0f)
            out.push_back(i);
    }
    return out;
}

float widthHz(const Response& r, size_t peak) {
    const float level = r.db[peak] - 3.0f;
    size_t l = peak, h = peak;
    while (l > 0 && r.db[l] > level)
        --l;
    while (h + 1 < r.db.size() && r.db[h] > level)
        ++h;
    return r.hz[h] - r.hz[l];
}

float valleyDb(const Response& r, size_t a, size_t b) {
    return *std::min_element(r.db.begin() + (long)a, r.db.begin() + (long)b);
}

} // namespace

TEST(CustomFilterVowels, EachVowelHasItsFirstThreeFormantsAtTheTableFrequencies) {
    for (const auto& v : kVowels) {
        const auto r = measure(v.preset);
        const auto p = peaks(r);
        ASSERT_GE(p.size(), 3u) << v.name;
        EXPECT_NEAR(r.hz[p[0]], v.f1.hz, 0.08f * v.f1.hz) << v.name << " F1";
        EXPECT_NEAR(r.hz[p[1]], v.f2.hz, 0.08f * v.f2.hz) << v.name << " F2";
        EXPECT_NEAR(r.hz[p[2]], v.f3.hz, 0.08f * v.f3.hz) << v.name << " F3";
        printf("[ vowel ] %s peaks %.0f / %.0f / %.0f Hz, F1 width %.0f, F2 width %.0f, F1-F2 dip %.1f dB\n", v.name,
               r.hz[p[0]], r.hz[p[1]], r.hz[p[2]], widthHz(r, p[0]), widthHz(r, p[1]),
               r.db[p[0]] - valleyDb(r, p[0], p[1]));
    }
}

TEST(CustomFilterVowels, TheFirstTwoFormantsHaveTheDrawnFilterStyleWidths) {
    for (const auto& v : kVowels) {
        const auto r = measure(v.preset);
        const auto p = peaks(r);
        ASSERT_GE(p.size(), 2u) << v.name;
        for (const auto& [index, f] : {std::pair{0, v.f1}, std::pair{1, v.f2}}) {
            const float w = widthHz(r, p[(size_t)index]);
            EXPECT_GE(w, 0.8f * f.bwHz) << v.name << " F" << index + 1 << " width " << w;
            EXPECT_LE(w, 1.3f * f.bwHz) << v.name << " F" << index + 1 << " width " << w;
            EXPECT_LT(w, 0.5f * f.hz) << v.name << " F" << index + 1 << " stays well under half an octave";
        }
    }
}

TEST(CustomFilterVowels, FormantsSitOnDeepValleys) {
    for (const auto& v : kVowels) {
        const auto r = measure(v.preset);
        const auto p = peaks(r);
        ASSERT_GE(p.size(), 3u) << v.name;
        EXPECT_GE(r.db[p[0]] - valleyDb(r, p[0], p[1]), 20.0f) << v.name << " valley between F1 and F2";
        if (v.checkF2F3Valley)
            EXPECT_GE(std::min(r.db[p[1]], r.db[p[2]]) - valleyDb(r, p[1], p[2]), 10.0f)
                << v.name << " valley between F2 and F3";
    }
}

TEST(CustomFilterVowels, TheLoudestPeakStaysUnderTheGainAuditCeiling) {
    for (const auto& v : kVowels) {
        const auto r = measure(v.preset);
        EXPECT_LE(*std::max_element(r.db.begin(), r.db.end()), 6.0f) << v.name << ": the gain audit ceiling";
    }
}

namespace {

double sawRmsDb(CustomFilterShapes::Preset preset, double f0) {
    FilterModule m;
    CustomFilterShapes shapes;
    CustomFilterShape shape;
    shape.name = "v";
    shape.curve = CustomFilterShapes::presetCurve(preset);
    shape.position = {0.5f, 0.5f};
    shapes.shapes = {shape};
    m.setCustomShapes(shapes);
    auto set = [&](const char* id, float plain) {
        for (auto* p : m.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
                r->setValueNotifyingHost(r->convertTo0to1(plain));
    };
    set("filterType", (float)FilterModule::kCustomTypeIndex);
    set("cutoff", 440.0f);
    set("resonance", 0.0f);
    constexpr int kBlock = 512;
    m.prepareToPlay(kTestSampleRate, kBlock);
    juce::AudioBuffer<float> buf(FilterModule::kNumChannels, kBlock);
    juce::MidiBuffer midi;
    const int harmonics = (int)(22000.0 / f0);
    double phase = 0.0, sum = 0.0;
    long count = 0;
    for (int b = 0; b < 100; ++b) {
        buf.clear();
        for (int i = 0; i < kBlock; ++i) {
            double v = 0.0;
            for (int h = 1; h <= harmonics; ++h)
                v += std::sin(h * phase) / h;
            phase += 2.0 * juce::MathConstants<double>::pi * f0 / kTestSampleRate;
            buf.setSample(0, i, (float)(0.3 * v));
        }
        m.processBlock(buf, midi);
        if (b >= 20)
            for (int i = 0; i < kBlock; ++i, ++count)
                sum += (double)buf.getSample(0, i) * buf.getSample(0, i);
    }
    return 10.0 * std::log10(sum / (double)count);
}

} // namespace

TEST(CustomFilterVowels, AllFiveVowelsAreWithinThreeDecibelsOfEachOtherOnABandLimitedSaw) {
    double lo = 1.0e9, hi = -1.0e9;
    for (const auto& v : kVowels) {
        const double db = sawRmsDb(v.preset, 110.0);
        printf("[ vowel ] %s 110 Hz saw RMS %.1f dB\n", v.name, db);
        lo = std::min(lo, db);
        hi = std::max(hi, db);
    }
    EXPECT_LE(hi - lo, 3.0) << "swapping or morphing vowels does not jump in level";
}

TEST(CustomFilterVowels, EhAndIhAreInTheStartFromList) {
    synth::ui::test::ShapeRig rig;
    std::vector<std::string> names;
    for (const auto& item : rig.adapter->getStartFrom())
        names.push_back(item.name.toStdString());
    const std::vector<std::string> vowels{"ah", "ee", "oo", "eh", "ih"};
    ASSERT_GE(names.size(), vowels.size());
    EXPECT_TRUE(std::equal(vowels.begin(), vowels.end(), names.begin()));
    EXPECT_STREQ(CustomFilterShapes::presetName(CustomFilterShapes::Preset::Eh), "eh");
    EXPECT_STREQ(CustomFilterShapes::presetName(CustomFilterShapes::Preset::Ih), "ih");
}
