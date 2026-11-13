// PitchGrid.cpp -- the lines each Filter grid choice draws and snaps to, on the curve's log-frequency axis.
#include "PitchGrid.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "Music/SongKey.h"
#include <cmath>

namespace synth::ui::pitchgrid {

namespace {
using synth::CustomFilterShapes;
using Line = CurveEditorComponent::GridLine;

// The board's harmonic labels stop here; above it the lines are too close for a label each.
constexpr int kLabelledHarmonics = 16;
constexpr float kLegacyLevelDb = 6.0f;

// U+00B7 MIDDLE DOT between a line's name and its frequency.
const juce::String& dot() {
    static const juce::String text = juce::String::fromUTF8(" \xc2\xb7 ");
    return text;
}

double xOf(double hz) { return (double)CustomFilterShapes::hzToX((float)hz); }

bool inRange(double hz) { return hz >= CustomFilterShapes::kMinHz - 1e-6 && hz <= CustomFilterShapes::kMaxHz + 1e-6; }

std::vector<Line> octaveLines(int steps) {
    std::vector<Line> lines;
    steps = juce::jmax(1, steps);
    const int first = (int)std::ceil(std::log2(CustomFilterShapes::kMinHz / 1000.0) * steps - 1e-9);
    const int last = (int)std::floor(std::log2(CustomFilterShapes::kMaxHz / 1000.0) * steps + 1e-9);
    for (int k = first; k <= last; ++k) {
        const double hz = 1000.0 * std::pow(2.0, (double)k / steps);
        lines.push_back({xOf(hz), Line::Tier::snap, Line::Kind::neutral, formatHz(hz), {}});
    }
    return lines;
}

// Every semitone in range; the C lines are the landmarks and carry the labels, a note outside the key is dotted and
// does not snap. A key with no notes (or none at all) counts every note in.
std::vector<Line> noteLines(const std::optional<MusicalScale>& key) {
    std::vector<Line> lines;
    const bool keyed = key.has_value() && key->mask != 0 && !key->isChromatic();
    const int first = (int)std::ceil(midiOf(CustomFilterShapes::kMinHz) - 1e-9);
    const int last = (int)std::floor(midiOf(CustomFilterShapes::kMaxHz) + 1e-9);
    for (int m = first; m <= last; ++m) {
        const double hz = noteHz(m);
        const bool isC = ((m % 12) + 12) % 12 == 0;
        const bool inKey = !keyed || key->contains(m);
        const auto tier = !inKey ? Line::Tier::outOfKey : isC ? Line::Tier::landmark : Line::Tier::snap;
        lines.push_back(
            {xOf(hz), tier, Line::Kind::note, noteName(m) + dot() + formatHz(hz), isC ? noteName(m) : juce::String()});
    }
    return lines;
}

// The root and its multiples up to the top of the range; the root and its octaves (x1, x2, x4 ...) are the landmarks.
std::vector<Line> harmonicLines(double rootHz) {
    std::vector<Line> lines;
    if (rootHz <= 0.0)
        return lines;
    for (int n = 1; rootHz * n <= CustomFilterShapes::kMaxHz + 1e-6; ++n) {
        const double hz = rootHz * n;
        if (!inRange(hz))
            continue;
        const bool octave = (n & (n - 1)) == 0;
        const juce::String multiple = "x" + juce::String(n);
        lines.push_back({xOf(hz), octave ? Line::Tier::landmark : Line::Tier::snap, Line::Kind::harmonic,
                         multiple + dot() + formatHz(hz), n <= kLabelledHarmonics ? multiple : juce::String()});
    }
    return lines;
}
} // namespace

const juce::StringArray& kindNames() {
    static const juce::StringArray names{"Off", "Octaves", "Notes", "Harmonics"};
    return names;
}

const juce::StringArray& stepNames() {
    static const juce::StringArray names{"1 oct", "1/2", "1/3", "1/4", "1/6", "1/12"};
    return names;
}

const std::vector<int>& stepCounts() {
    static const std::vector<int> counts{1, 2, 3, 4, 6, 12};
    return counts;
}

const juce::StringArray& levelNames() {
    static const juce::StringArray names{"Off", "12", "6", "3", "1.5"};
    return names;
}

const std::vector<float>& levelSteps() {
    static const std::vector<float> steps{0.0f, 12.0f, 6.0f, 3.0f, 1.5f};
    return steps;
}

std::optional<Choice> fromLegacyName(const juce::String& name) {
    const int steps = name == "1 oct" ? 1 : name == "1/2 oct" ? 2 : name == "1/3 oct" ? 3 : 0;
    if (steps == 0)
        return std::nullopt;
    Choice choice;
    choice.kind = Kind::octaves;
    choice.stepsPerOctave = steps;
    choice.levelDb = kLegacyLevelDb;
    return choice;
}

juce::String nameOf(const Choice& choice) {
    switch (choice.kind) {
    case Kind::octaves:
        return choice.stepsPerOctave == 1 ? juce::String("1 oct") : "1/" + juce::String(choice.stepsPerOctave) + " oct";
    case Kind::notes:
        return "Notes";
    case Kind::harmonics:
        return "Harmonics";
    case Kind::off:
        break;
    }
    return "Off";
}

double noteHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

double midiOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

juce::String noteName(int midi) {
    const int pitchClass = ((midi % 12) + 12) % 12;
    const int octave = (midi - pitchClass) / 12 - 1;
    return synth::keyRootName(pitchClass) + juce::String(octave);
}

juce::String formatHz(double hz) {
    if (hz >= 1000.0)
        return juce::String(hz / 1000.0, hz >= 10000.0 ? 1 : 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".") +
               " kHz";
    return juce::String(hz, hz >= 100.0 ? 0 : 1) + " Hz";
}

juce::String formatDb(float level) {
    const float db = CustomFilterShapes::yToDb(level);
    const float rounded = std::round(db * 10.0f) / 10.0f;
    const juce::String number = rounded == std::floor(rounded) ? juce::String((int)rounded) : juce::String(rounded, 1);
    return (rounded > 0.0f ? "+" : "") + number + " dB";
}

// The level lines split -48..+12 dB into equal steps, so 0 dB is always a line, and it is the landmark among them.
std::optional<CurveEditorComponent::CurveGrid> gridFor(const Choice& choice, const Context& context) {
    if (choice.kind == Kind::off && choice.levelDb <= 0.0f)
        return std::nullopt;
    CurveEditorComponent::CurveGrid grid;
    grid.xDivisions = 0;
    grid.yDivisions = 0;
    if (choice.levelDb > 0.0f)
        grid.yDivisions = juce::roundToInt((CustomFilterShapes::kMaxDb - CustomFilterShapes::kMinDb) / choice.levelDb);
    switch (choice.kind) {
    case Kind::octaves:
        grid.xLines = octaveLines(choice.stepsPerOctave);
        break;
    case Kind::notes:
        grid.xLines = noteLines(context.key);
        break;
    case Kind::harmonics:
        grid.xLines = harmonicLines(context.rootHz);
        break;
    case Kind::off:
        break;
    }
    grid.snapX = choice.snapAcross;
    grid.snapY = choice.snapLevel;
    grid.yLandmark = CustomFilterShapes::dbToY(0.0f);
    grid.xName = [](double x) { return formatHz(CustomFilterShapes::xToHz((float)x)); };
    grid.yName = &formatDb;
    return grid;
}

} // namespace synth::ui::pitchgrid
