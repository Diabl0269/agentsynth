#include "SoundBrowser/SoundNameGuess.h"
#include <iterator>

namespace synth::soundbrowser {

namespace {

juce::StringArray tokensOf(const juce::String& text) {
    juce::StringArray tokens;
    juce::String current;
    for (auto c : text) {
        if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '#')
            current += c;
        else if (current.isNotEmpty()) {
            tokens.add(current);
            current = {};
        }
    }
    if (current.isNotEmpty())
        tokens.add(current);
    return tokens;
}

// Which tag a lower-case token names, or empty. Plurals are listed because a "Kicks" folder counts as Drums.
juce::String tagOfToken(const juce::String& t) {
    static const char* const drums[] = {"kick", "kicks", "snare", "snares", "hat",    "hats",    "hihat", "hihats",
                                        "clap", "claps", "tom",   "toms",   "cymbal", "cymbals", "perc",  "percussion",
                                        "drum", "drums", "crash", "ride",   "rim",    "shaker",  "conga", "bongo"};
    static const char* const bass[] = {"bass", "basses", "sub"};
    static const char* const keys[] = {"keys", "piano", "rhodes", "epiano", "organ", "wurli", "keyboard"};
    static const char* const vocals[] = {"vox", "vocal", "vocals", "voice", "choir"};
    static const char* const fx[] = {"fx", "sfx", "riser", "impact", "sweep", "whoosh"};
    static const char* const strings[] = {"string", "strings", "violin", "viola", "cello"};
    const auto in = [&t](const char* const* list, size_t n) {
        for (size_t i = 0; i < n; ++i)
            if (t == list[i])
                return true;
        return false;
    };
    if (in(drums, std::size(drums)))
        return "Drums";
    if (in(bass, std::size(bass)))
        return "Bass";
    if (in(keys, std::size(keys)))
        return "Keys";
    if (in(vocals, std::size(vocals)))
        return "Vocals";
    if (in(fx, std::size(fx)))
        return "FX";
    if (in(strings, std::size(strings)))
        return "Strings";
    return {};
}

bool isTempo(int v) { return v >= 60 && v <= 200; }

// "96bpm", "bpm96" inside one token.
int bpmInToken(const juce::String& token) {
    const auto lower = token.toLowerCase();
    const int at = lower.indexOf("bpm");
    if (at < 0)
        return 0;
    const auto before = lower.substring(0, at);
    const auto after = lower.substring(at + 3);
    if (before.isNotEmpty() && before.containsOnly("0123456789"))
        return before.getIntValue();
    if (after.isNotEmpty() && after.containsOnly("0123456789"))
        return after.getIntValue();
    return 0;
}

// An explicit "120 BPM" / "96bpm" wins; a bare number counts only between two other words ("dusty_120_Am"), so a
// trailing sample number ("kick_100") is never a tempo.
double tempoOf(const juce::StringArray& tokens) {
    for (int i = 0; i < tokens.size(); ++i) {
        const int inToken = bpmInToken(tokens[i]);
        if (isTempo(inToken))
            return inToken;
        if (tokens[i].equalsIgnoreCase("bpm") && i > 0 && tokens[i - 1].containsOnly("0123456789") &&
            isTempo(tokens[i - 1].getIntValue()))
            return tokens[i - 1].getIntValue();
    }
    for (int i = 1; i + 1 < tokens.size(); ++i)
        if (tokens[i].containsOnly("0123456789") && tokens[i].length() >= 2 && isTempo(tokens[i].getIntValue()))
            return tokens[i].getIntValue();
    return 0.0;
}

// "am", "c#min", "ebm", "fmaj" in one token; the mode word may also be the next token ("F major").
juce::String keyOfToken(const juce::String& token, const juce::String& next) {
    if (token.isEmpty() || token.length() > 8)
        return {};
    const auto lower = token.toLowerCase();
    const auto letter = lower[0];
    if (letter < 'a' || letter > 'g')
        return {};
    int pos = 1;
    juce::String accidental;
    if (pos < lower.length() && (lower[pos] == '#' || lower[pos] == 'b')) {
        accidental = lower[pos] == '#' ? "#" : "b";
        ++pos;
    }
    auto mode = lower.substring(pos);
    if (mode.isEmpty())
        mode = next.toLowerCase();
    const bool minor = mode == "m" || mode == "min" || mode == "minor";
    const bool major = mode == "maj" || mode == "major";
    if (!minor && !major)
        return {};
    return juce::String::charToString(juce::CharacterFunctions::toUpperCase(letter)) + accidental +
           (minor ? " minor" : " major");
}

juce::String keyOf(const juce::StringArray& tokens) {
    for (int i = 0; i < tokens.size(); ++i) {
        const auto key = keyOfToken(tokens[i], i + 1 < tokens.size() ? tokens[i + 1] : juce::String());
        if (key.isNotEmpty())
            return key;
    }
    return {};
}

} // namespace

juce::String noteNameOf(int midiNote) {
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[((midiNote % 12) + 12) % 12];
}

SoundGuess guessFromName(const juce::String& folder, const juce::String& fileName) {
    SoundGuess guess;
    const auto nameTokens = tokensOf(fileName);
    const auto folderTokens = tokensOf(folder);
    juce::StringArray all = folderTokens;
    all.addArray(nameTokens);

    for (const auto& token : all) {
        const auto tag = tagOfToken(token.toLowerCase());
        if (tag.isNotEmpty())
            guess.tags.addIfNotAlreadyThere(tag);
        const auto lower = token.toLowerCase();
        if (lower.startsWith("loop") || lower == "lp")
            guess.loopHint = true;
    }
    // The file name's own words win over the folder's.
    guess.tempo = tempoOf(nameTokens);
    if (guess.tempo == 0.0)
        guess.tempo = tempoOf(folderTokens);
    guess.key = keyOf(nameTokens);
    if (guess.key.isEmpty())
        guess.key = keyOf(folderTokens);
    return guess;
}

} // namespace synth::soundbrowser
