// DrumIdeas: starter patterns, variations and randomized patterns (see DrumIdeas.h). Patterns are written as text, one
// character per sixteenth: X an accent, x a hit, o a ghost, . nothing.

#include "DrumIdeas.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace synth::ui::drumideas {

namespace {

constexpr int kKick = 36, kRim = 37, kSnare = 38, kClap = 39, kClosedHat = 42, kTom = 45, kOpenHat = 46, kCymbal = 49,
              kCowbell = 56;
constexpr int kAccentVelocity = 120, kHitVelocity = 96, kGhostVelocity = 50;
constexpr int kSixteenths = 16;
constexpr double kEpsilon = 1.0e-6;
constexpr double kWildAmount = 0.95;

struct Row {
    int pitch;
    const char* pattern; ///< 16 characters
};

// A roll: extra hits between the sixteenths, at `step` sixteenths into the bar (fractions allowed).
struct Roll {
    double step;
    int pitch;
    int velocity;
};

struct StyleData {
    std::vector<Row> rows;
    std::vector<Roll> rolls;
    int stepsPerBar = 16;
};

const StyleData& dataFor(Style style) {
    static const std::map<Style, StyleData> table = {
        {Style::House,
         {{{kKick, "X...X...X...X..."},
           {kClap, "....X.......X..."},
           {kOpenHat, "..X...X...X...X."},
           {kClosedHat, "o.o...o.o.o...o."}},
          {}}},
        {Style::Techno,
         {{{kKick, "X...X...X...X..."},
           {kClap, "....x.......x..."},
           {kClosedHat, "oxoxoxoxoxoxoxox"},
           {kOpenHat, "..x.......x....."},
           {kRim, "......x.......x."}},
          {}}},
        {Style::HipHop,
         {{{kKick, "X.....x..x.x...."},
           {kSnare, "....X.......X..."},
           {kClosedHat, "x.x.x.x.x.x.x.x."},
           {kOpenHat, "..............x."}},
          {}}},
        {Style::Trap,
         {{{kKick, "X..x......x....."},
           {kSnare, "........X......."},
           {kClap, "........x......."},
           {kClosedHat, "x.x.x.x.x.x.x.o."},
           {kOpenHat, "......x........."}},
          {{14.0, kClosedHat, 80},
           {14.5, kClosedHat, 70},
           {15.0, kClosedHat, 90},
           {15.5, kClosedHat, 100},
           {6.5, kClosedHat, 70},
           {7.5, kClosedHat, 80},
           {11.5, kClosedHat, 70}},
          32}},
        {Style::DrumAndBass,
         {{{kKick, "X.........x....."},
           {kSnare, "....X..o.o..X..."},
           {kClosedHat, "x.x.x.x.x.x.x.x."},
           {kCymbal, "X..............."}},
          {}}},
        {Style::Breakbeat,
         {{{kKick, "X.x.......x....."},
           {kSnare, "....X..o.o..X..o"},
           {kClosedHat, "x.x.x.x.x.x.x.x."},
           {kCymbal, "X.......x......."}},
          {}}},
        {Style::Rock,
         {{{kKick, "X.....x.X......."},
           {kSnare, "....X.......X..."},
           {kClosedHat, "x.x.x.x.x.x.x.x."},
           {kCymbal, "X..............."}},
          {}}},
        {Style::Funk,
         {{{kKick, "X..x..x...x.x..."},
           {kSnare, "....X..o.o..X..o"},
           {kClosedHat, "xoxoxoxoxoxoxoxo"},
           {kOpenHat, "......x........."}},
          {}}},
        {Style::Disco,
         {{{kKick, "X...X...X...X..."},
           {kSnare, "....X.......X..."},
           {kClap, "....x.......x..."},
           {kClosedHat, "x.x.x.x.x.x.x.x."},
           {kOpenHat, "..X...X...X...X."}},
          {}}},
        {Style::Reggaeton,
         {{{kKick, "X...X...X...X..."},
           {kSnare, "...X..X....X..X."},
           {kClosedHat, "x.x.x.x.x.x.x.x."},
           {kRim, "....o.......o..."}},
          {}}},
        {Style::Afrobeat,
         {{{kKick, "X.......x.x....."},
           {kSnare, "....X.......X..."},
           {kClosedHat, "x.x.x.x.x.x.x.x."},
           {kCowbell, "X..X..X...X.X..."},
           {kTom, "......x.......x."}},
          {}}},
        {Style::LoFi,
         {{{kKick, "X.....o...x....."},
           {kSnare, "....x.......x..."},
           {kClosedHat, "o.x.o.x.o.x.o.x."},
           {kRim, "..............o."}},
          {}}},
    };
    return table.at(style);
}

int velocityOf(char c) {
    switch (c) {
    case 'X':
        return kAccentVelocity;
    case 'x':
        return kHitVelocity;
    case 'o':
        return kGhostVelocity;
    default:
        return 0;
    }
}

synth::MidiNote makeHit(double startBeat, double lengthBeats, int pitch, int velocity) {
    synth::MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = std::clamp(velocity, 1, 127);
    return note;
}

void sortNotes(std::vector<synth::MidiNote>& notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch < b.pitch;
    });
}

// The position inside its bar of a note, as a fraction in [0, 1).
double barFraction(double startBeat, const Grid& grid) {
    const double bar = std::max(kEpsilon, grid.beatsPerBar);
    const double position = std::fmod(startBeat, bar) / bar;
    return position < 0.0 ? position + 1.0 : position;
}

bool nearFraction(double fraction, double target) {
    return std::abs(fraction - target) < 1.0e-4 || std::abs(fraction - target - 1.0) < 1.0e-4;
}

bool isHat(int pitch) noexcept { return pitch == 42 || pitch == 44 || pitch == 46; }

// A kick on a bar's first beat, or a snare on a backbeat (the second and fourth quarter of the bar): the hits a groove
// stands on.
bool isProtected(const synth::MidiNote& note, const Grid& grid) {
    const double fraction = barFraction(note.startBeat, grid);
    if (isKick(note.pitch))
        return nearFraction(fraction, 0.0);
    return isSnare(note.pitch) && (nearFraction(fraction, 0.25) || nearFraction(fraction, 0.75));
}

// Whether any note of `pitchTest` starts within half a step of `startBeat`.
template <typename Pred>
bool occupied(const std::vector<synth::MidiNote>& notes, double startBeat, double stepBeats, Pred pitchTest) {
    return std::any_of(notes.begin(), notes.end(), [&](const auto& n) {
        return pitchTest(n.pitch) && std::abs(n.startBeat - startBeat) < stepBeats * 0.5;
    });
}

int cellOfNote(const synth::MidiNote& note, double stepBeats) {
    return static_cast<int>(std::floor(note.startBeat / stepBeats + 1.0e-6));
}

} // namespace

bool isKick(int pitch) noexcept { return pitch == 35 || pitch == 36; }
bool isSnare(int pitch) noexcept { return pitch == 38 || pitch == 40; }

int Grid::stepsPerBar() const noexcept {
    return std::max(1, static_cast<int>(std::lround(beatsPerBar / std::max(kEpsilon, stepBeats))));
}

juce::String styleName(Style style) {
    static const char* const names[] = {"House", "Techno", "Hip hop", "Trap",      "Drum and bass", "Breakbeat",
                                        "Rock",  "Funk",   "Disco",   "Reggaeton", "Afrobeat",      "Lo-fi"};
    return names[static_cast<int>(style)];
}

Style styleAt(int index) { return static_cast<Style>(std::clamp(index, 0, kStyleCount - 1)); }

int requiredStepsPerBar(Style style) noexcept { return dataFor(style).stepsPerBar; }

std::vector<synth::MidiNote> starter(Style style, const Grid& grid) {
    const auto& data = dataFor(style);
    const double sixteenth = grid.beatsPerBar / kSixteenths;
    std::vector<synth::MidiNote> notes;
    for (double barStart = 0.0; barStart < grid.lengthBeats - kEpsilon; barStart += grid.beatsPerBar) {
        const auto add = [&](double step, int pitch, int velocity, double length) {
            const double start = barStart + step * sixteenth;
            if (start < grid.lengthBeats - kEpsilon)
                notes.push_back(makeHit(start, length, pitch, velocity));
        };
        for (const auto& row : data.rows)
            for (int step = 0; step < kSixteenths; ++step)
                if (const int velocity = velocityOf(row.pattern[step]); velocity > 0)
                    add(step, row.pitch, velocity, sixteenth);
        for (const auto& roll : data.rolls)
            add(roll.step, roll.pitch, roll.velocity, sixteenth * 0.5);
    }
    sortNotes(notes);
    return notes;
}

std::vector<ChangedCell> changedCells(const std::vector<synth::MidiNote>& before,
                                      const std::vector<synth::MidiNote>& after, double stepBeats) {
    using Key = std::pair<int, int>;
    const auto index = [stepBeats](const std::vector<synth::MidiNote>& notes) {
        std::map<Key, std::multiset<int>> cells;
        for (const auto& note : notes)
            cells[{note.pitch, cellOfNote(note, stepBeats)}].insert(note.velocity);
        return cells;
    };
    const auto a = index(before);
    const auto b = index(after);
    std::set<Key> keys;
    for (const auto& [key, velocities] : a)
        keys.insert(key);
    for (const auto& [key, velocities] : b)
        keys.insert(key);
    std::vector<ChangedCell> changed;
    for (const auto& key : keys) {
        const auto ia = a.find(key);
        const auto ib = b.find(key);
        if (ia == a.end() || ib == b.end() || ia->second != ib->second)
            changed.push_back(key);
    }
    return changed;
}

//==============================================================================
namespace {

struct VaryState {
    std::vector<synth::MidiNote>& notes;
    const Grid& grid;
    juce::Random& rng;
    bool wild;

    int bars() const {
        return std::max(1, static_cast<int>(std::ceil(grid.lengthBeats / grid.beatsPerBar - kEpsilon)));
    }

    // A random step of the clip that is not a bar's first beat or a backbeat, as a start in beats; negative when none.
    double randomOffStep() {
        for (int attempt = 0; attempt < 12; ++attempt) {
            const int total = std::max(1, static_cast<int>(std::lround(grid.lengthBeats / grid.stepBeats)));
            const double start = rng.nextInt(total) * grid.stepBeats;
            const double fraction = barFraction(start, grid);
            if (!nearFraction(fraction, 0.0) && !nearFraction(fraction, 0.25) && !nearFraction(fraction, 0.75) &&
                start < grid.lengthBeats - kEpsilon)
                return start;
        }
        return -1.0;
    }

    void addGhost(int pitch, int lo, int hi) {
        const double start = randomOffStep();
        if (start >= 0.0 && !occupied(notes, start, grid.stepBeats, [pitch](int p) { return p == pitch; }))
            notes.push_back(makeHit(start, grid.stepBeats, pitch, lo + rng.nextInt(hi - lo + 1)));
    }

    std::vector<size_t> indicesWhere(const std::function<bool(const synth::MidiNote&)>& test) const {
        std::vector<size_t> out;
        for (size_t i = 0; i < notes.size(); ++i)
            if (test(notes[i]))
                out.push_back(i);
        return out;
    }

    size_t pick(const std::vector<size_t>& indices) { return indices[(size_t)rng.nextInt((int)indices.size())]; }

    bool mayChange(const synth::MidiNote& note) const { return wild || !isProtected(note, grid); }

    void removeHat() {
        const auto hats = indicesWhere([this](const auto& n) { return isHat(n.pitch) && mayChange(n); });
        if (!hats.empty())
            notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(pick(hats)));
    }

    void moveHat() {
        const auto hats = indicesWhere([this](const auto& n) { return isHat(n.pitch) && mayChange(n); });
        if (hats.empty())
            return;
        auto& note = notes[pick(hats)];
        const double target = note.startBeat + (rng.nextBool() ? grid.stepBeats : -grid.stepBeats);
        if (target >= 0.0 && target < grid.lengthBeats - kEpsilon &&
            !occupied(notes, target, grid.stepBeats, [&note](int p) { return p == note.pitch; }))
            note.startBeat = target;
    }

    void swapForOpenHat() {
        const auto closed = indicesWhere([this](const auto& n) {
            const double fraction = barFraction(n.startBeat, grid) * kSixteenths;
            return n.pitch == kClosedHat && std::abs(std::fmod(fraction, 4.0) - 2.0) < 1.0e-3 && mayChange(n);
        });
        if (closed.empty())
            return;
        auto& note = notes[pick(closed)];
        note.pitch = kOpenHat;
        note.velocity = std::max(note.velocity, kHitVelocity);
    }

    // Wild only: take a backbeat or a first-beat kick out, or shift a backbeat snare a step.
    void breakTheGroove() {
        const auto stands = indicesWhere([this](const auto& n) { return isProtected(n, grid); });
        if (stands.empty())
            return;
        const size_t i = pick(stands);
        if (rng.nextBool() || notes[i].startBeat + grid.stepBeats >= grid.lengthBeats - kEpsilon)
            notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(i));
        else
            notes[i].startBeat += grid.stepBeats;
    }

    void humanise(double amount) {
        const int count = static_cast<int>(std::lround(amount * 0.5 * static_cast<double>(notes.size())));
        const int spread = 2 + static_cast<int>(std::lround(amount * 12.0));
        for (int k = 0; k < count && !notes.empty(); ++k) {
            auto& note = notes[(size_t)rng.nextInt((int)notes.size())];
            const int floorVelocity = isProtected(note, grid) ? 60 : 1;
            note.velocity = std::clamp(note.velocity + rng.nextInt(2 * spread + 1) - spread, floorVelocity, 127);
        }
    }

    void applyOne() {
        const int ops = wild ? 8 : 7;
        switch (rng.nextInt(ops)) {
        case 0:
            addGhost(kSnare, 28, 50);
            break;
        case 1:
            addGhost(kClosedHat, 45, 80);
            break;
        case 2:
            removeHat();
            break;
        case 3:
            moveHat();
            break;
        case 4:
            addGhost(kKick, 70, 100);
            break;
        case 5:
            swapForOpenHat();
            break;
        case 6:
            addGhost(kSnare, 28, 50);
            break;
        default:
            breakTheGroove();
            break;
        }
    }
};

} // namespace

Variation vary(const std::vector<synth::MidiNote>& notes, const Grid& grid, double amount, int seed) {
    amount = std::clamp(amount, 0.0, 1.0);
    juce::Random rng(seed);
    Variation result;
    result.notes = notes;
    VaryState state{result.notes, grid, rng, amount >= kWildAmount};
    const int changes = 1 + static_cast<int>(std::lround(amount * 10.0));
    for (int i = 0; i < changes; ++i)
        state.applyOne();
    state.humanise(amount);
    sortNotes(result.notes);
    result.changed = changedCells(notes, result.notes, grid.stepBeats);
    return result;
}

//==============================================================================
namespace {

struct RowOdds {
    int pitch;
    double downbeat;  ///< chance at the start of a bar
    double beat;      ///< on the other quarter notes
    double eighth;    ///< on the off-beat eighths
    double sixteenth; ///< on the remaining sixteenths
    int velocity;
};

// Chances at density 0 and 1 for each drum; the odds between them are interpolated.
double lerp(double a, double b, double t) { return a + (b - a) * t; }

RowOdds oddsFor(int pitch, double d) {
    switch (pitch) {
    case kClap:
        return {pitch, lerp(0.0, 0.05, d), lerp(0.15, 0.35, d), lerp(0.0, 0.08, d), lerp(0.0, 0.05, d), kHitVelocity};
    case kClosedHat:
        return {pitch, lerp(0.3, 0.9, d), lerp(0.3, 0.9, d), lerp(0.25, 0.95, d), lerp(0.0, 0.7, d), 85};
    case kOpenHat:
        return {pitch, lerp(0.0, 0.05, d), lerp(0.0, 0.1, d), lerp(0.05, 0.3, d), lerp(0.0, 0.1, d), 90};
    case kTom:
        return {pitch, 0.0, lerp(0.0, 0.08, d), lerp(0.0, 0.12, d), lerp(0.0, 0.1, d), 90};
    case kCymbal:
        return {pitch, lerp(0.05, 0.5, d), 0.0, 0.0, 0.0, 100};
    default: // cowbell
        return {pitch, 0.0, lerp(0.0, 0.1, d), lerp(0.0, 0.1, d), lerp(0.0, 0.06, d), 90};
    }
}

RowOdds kickOdds(double d) { return {kKick, 1.0, lerp(0.5, 0.8, d), lerp(0.05, 0.3, d), lerp(0.0, 0.2, d), 105}; }
RowOdds snareOdds(double d) { return {kSnare, 0.0, 0.0, lerp(0.0, 0.1, d), lerp(0.0, 0.3, d), 40}; }

bool kept(int pitch) { return isKick(pitch) || isSnare(pitch); }

} // namespace

Variation randomize(const std::vector<synth::MidiNote>& notes, const Grid& grid, double density, bool keepKickAndSnare,
                    int seed) {
    density = std::clamp(density, 0.0, 1.0);
    juce::Random rng(seed);
    Variation result;
    if (keepKickAndSnare)
        for (const auto& note : notes)
            if (kept(note.pitch))
                result.notes.push_back(note);

    std::vector<RowOdds> rows;
    if (!keepKickAndSnare) {
        rows.push_back(kickOdds(density));
        rows.push_back(snareOdds(density));
    }
    for (const int pitch : {kClap, kClosedHat, kOpenHat, kTom, kCymbal, kCowbell})
        rows.push_back(oddsFor(pitch, density));

    const int total = std::max(1, static_cast<int>(std::lround(grid.lengthBeats / grid.stepBeats)));
    for (int step = 0; step < total; ++step) {
        const double start = step * grid.stepBeats;
        if (start >= grid.lengthBeats - kEpsilon)
            break;
        const double fraction = barFraction(start, grid) * kSixteenths; // sixteenths into the bar
        const bool downbeat = nearFraction(fraction / kSixteenths, 0.0);
        const bool backbeat = std::abs(fraction - 4.0) < 1.0e-3 || std::abs(fraction - 12.0) < 1.0e-3;
        const bool beat = std::abs(std::fmod(fraction, 4.0)) < 1.0e-3;
        const bool eighth = std::abs(std::fmod(fraction, 4.0) - 2.0) < 1.0e-3;
        for (const auto& row : rows) {
            double chance = downbeat ? row.downbeat : beat ? row.beat : eighth ? row.eighth : row.sixteenth;
            if (row.pitch == kSnare && backbeat)
                chance = 0.92;
            if (row.pitch == kClap && backbeat)
                chance = lerp(0.25, 0.5, density);
            if (rng.nextDouble() >= chance)
                continue;
            const bool ghost = row.pitch == kSnare && !backbeat;
            const int velocity = ghost ? row.velocity + rng.nextInt(16) : row.velocity + rng.nextInt(19) - 6;
            result.notes.push_back(makeHit(start, grid.stepBeats, row.pitch, velocity));
        }
    }
    sortNotes(result.notes);
    result.changed = changedCells(notes, result.notes, grid.stepBeats);
    return result;
}

} // namespace synth::ui::drumideas
