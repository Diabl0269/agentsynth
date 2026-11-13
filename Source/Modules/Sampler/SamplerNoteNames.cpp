#include "Modules/Sampler/SamplerNoteNames.h"
#include <algorithm>
#include <map>

namespace synth::sampler {

namespace {

constexpr int kAppOctaveForMiddleC = 3;

int semitoneOfLetter(juce::juce_wchar letter) {
    switch (juce::CharacterFunctions::toUpperCase(letter)) {
    case 'C':
        return 0;
    case 'D':
        return 2;
    case 'E':
        return 4;
    case 'F':
        return 5;
    case 'G':
        return 7;
    case 'A':
        return 9;
    case 'B':
        return 11;
    default:
        return -1;
    }
}

bool isNoteLetter(juce::juce_wchar c) { return semitoneOfLetter(c) >= 0; }

std::optional<int> parseInteger(const juce::String& s) {
    if (s.isEmpty() || !s.containsOnly("-0123456789") || s.substring(1).containsChar('-'))
        return std::nullopt;
    if (s == "-")
        return std::nullopt;
    return s.getIntValue();
}

std::optional<int> toMidi(int semitone, int octave, int octaveForMiddleC) {
    const int midi = 60 + 12 * (octave - octaveForMiddleC) + semitone;
    if (midi < 0 || midi > 127)
        return std::nullopt;
    return midi;
}

// Letter, optional # or b, then a mandatory octave number.
std::optional<int> parseNote(const juce::String& s, int octaveForMiddleC) {
    if (s.isEmpty() || !isNoteLetter(s[0]))
        return std::nullopt;
    int semitone = semitoneOfLetter(s[0]);
    int pos = 1;
    if (s[pos] == '#') {
        ++semitone;
        ++pos;
    } else if (s[pos] == 'b' && s.length() > pos + 1) {
        --semitone;
        ++pos;
    }
    const auto octave = parseInteger(s.substring(pos));
    if (!octave || s.substring(pos).isEmpty())
        return std::nullopt;
    return toMidi(semitone, *octave, octaveForMiddleC);
}

// "kalimbaC2": an upper-case note glued to the end of lower-case letters.
std::optional<int> parseGluedNote(const juce::String& token) {
    for (int len = 3; len >= 2; --len) {
        if (token.length() <= len)
            continue;
        const int start = token.length() - len;
        if (!juce::CharacterFunctions::isLowerCase(token[start - 1]) ||
            !juce::CharacterFunctions::isUpperCase(token[start]))
            continue;
        if (auto note = parseNote(token.substring(start), kAppOctaveForMiddleC))
            return note;
    }
    return std::nullopt;
}

int loudnessOfToken(const juce::String& lower) {
    static const char* const soft[] = {"soft", "pp", "p", "mp", "quiet", "piano", "v1", "vel1", "lo"};
    static const char* const loud[] = {"loud", "ff", "f", "mf", "forte", "hard", "hi"};
    for (const char* s : soft)
        if (lower == s)
            return -1;
    for (const char* s : loud)
        if (lower == s)
            return 1;
    for (const char* prefix : {"vel", "v"}) {
        if (lower.startsWith(prefix)) {
            const juce::String digits = lower.substring((int)std::strlen(prefix));
            if (digits.isNotEmpty() && digits.containsOnly("0123456789") && digits.getIntValue() >= 2)
                return 1;
        }
    }
    return 0;
}

} // namespace

std::optional<int> parseNoteName(const juce::String& text, int octaveForMiddleC) {
    const juce::String s = text.trim();
    if (auto number = parseInteger(s))
        return (*number >= 0 && *number <= 127) ? std::optional<int>(*number) : std::nullopt;
    return parseNote(s, octaveForMiddleC);
}

FileNameHints hintsFromFileName(const juce::String& fileNameWithoutExtension) {
    juce::StringArray tokens;
    tokens.addTokens(fileNameWithoutExtension, "_- .", "");
    tokens.removeEmptyStrings();

    FileNameHints hints;
    for (const auto& token : tokens) {
        // A bare "A" or "Take" is never a note: parseNote insists on an octave number.
        std::optional<int> note = parseNote(token, kAppOctaveForMiddleC);
        if (!note)
            note = parseGluedNote(token);
        if (note) {
            hints.note = note;
            continue;
        }
        if (const int layer = loudnessOfToken(token.toLowerCase()); layer != 0)
            hints.layer = layer;
    }
    return hints;
}

namespace {

struct Placed {
    int index;
    FileNameHints hints;
};

// Splits one root's files over the velocity range; two files use the soft/loud hints.
void assignVelocities(std::vector<Placed>& group, std::vector<RegionSpec>& specs) {
    const int n = (int)group.size();
    if (n == 2) {
        const int a = group[0].hints.layer, b = group[1].hints.layer;
        const bool secondSoft = (b == -1 && a != -1) || (a == 1 && b != 1);
        if (secondSoft)
            std::swap(group[0], group[1]);
    }
    for (int k = 0; k < n; ++k) {
        specs[(size_t)group[(size_t)k].index].loVel = 1 + k * 127 / n;
        specs[(size_t)group[(size_t)k].index].hiVel = (k + 1) * 127 / n;
    }
}

} // namespace

std::vector<RegionSpec> placeDroppedFiles(const juce::StringArray& sortedFilePaths) {
    std::vector<RegionSpec> specs;
    std::vector<FileNameHints> hints;
    for (const auto& path : sortedFilePaths) {
        RegionSpec spec;
        spec.samplePath = path;
        specs.push_back(spec);
        hints.push_back(hintsFromFileName(juce::File(path).getFileNameWithoutExtension()));
    }

    std::map<int, std::vector<Placed>> byRoot;
    for (size_t i = 0; i < specs.size(); ++i)
        if (hints[i].note)
            byRoot[*hints[i].note].push_back({(int)i, hints[i]});

    int previousRoot = -1;
    for (auto it = byRoot.begin(); it != byRoot.end(); ++it) {
        const int root = it->first;
        for (const auto& p : it->second) {
            specs[(size_t)p.index].rootKey = root;
            specs[(size_t)p.index].loKey = previousRoot < 0 ? 0 : (previousRoot + root) / 2 + 1;
            auto next = std::next(it);
            specs[(size_t)p.index].hiKey = next == byRoot.end() ? 127 : (root + next->first) / 2;
        }
        assignVelocities(it->second, specs);
        previousRoot = root;
    }

    int nextKey = byRoot.empty() ? 60 : byRoot.rbegin()->first + 1;
    const bool lone = specs.size() == 1 && byRoot.empty();
    for (size_t i = 0; i < specs.size(); ++i) {
        if (hints[i].note)
            continue;
        const int key = std::min(nextKey++, 127);
        specs[i].rootKey = lone ? 60 : key;
        specs[i].loKey = lone ? 0 : key;
        specs[i].hiKey = lone ? 127 : key;
    }

    return specs;
}

} // namespace synth::sampler
