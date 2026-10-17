#include "Modules/Sampler/SfzReader.h"
#include "Modules/Sampler/SamplerNoteNames.h"
#include <algorithm>
#include <map>

namespace synth::sampler {

namespace {

constexpr int kSfzOctaveForMiddleC = 4;

using OpcodeMap = std::map<juce::String, juce::String>;

enum class Section { None, Control, Global, Master, Group, Region, Ignored };

// Opcodes this reader understands; anything else is reported once.
bool isKnownOpcode(const juce::String& name) {
    static const char* const known[] = {"sample",     "lokey",        "hikey",     "key",           "pitch_keycenter",
                                        "lovel",      "hivel",        "tune",      "transpose",     "volume",
                                        "loop_mode",  "loop_start",   "loopstart", "loop_end",      "loopend",
                                        "seq_length", "seq_position", "trigger",   "ampeg_release", "default_path"};
    for (const char* k : known)
        if (name == k)
            return true;
    return false;
}

// Removes /* */ blocks and // line tails, keeping line breaks so the line-based value rule still holds.
juce::String stripComments(const juce::String& text) {
    juce::String out;
    int i = 0;
    const int n = text.length();
    while (i < n) {
        if (text[i] == '/' && text[i + 1] == '*') {
            const int end = text.indexOf(i + 2, "*/");
            for (int j = i; j < (end < 0 ? n : end); ++j)
                if (text[j] == '\n')
                    out += '\n';
            i = end < 0 ? n : end + 2;
        } else if (text[i] == '/' && text[i + 1] == '/') {
            while (i < n && text[i] != '\n')
                ++i;
        } else {
            out += text[i++];
        }
    }
    return out;
}

bool isNameChar(juce::juce_wchar c) { return juce::CharacterFunctions::isLetterOrDigit(c) || c == '_' || c == '$'; }

// Splits "a=1 b=two words c=3" into opcodes: a value runs to the next " name=".
void splitOpcodes(const juce::String& chunk, std::vector<std::pair<juce::String, juce::String>>& out) {
    std::vector<std::pair<int, int>> starts; // name start, '=' position
    for (int eq = 0; eq < chunk.length(); ++eq) {
        if (chunk[eq] != '=')
            continue;
        int s = eq;
        while (s > 0 && isNameChar(chunk[s - 1]))
            --s;
        if (s < eq && (s == 0 || juce::CharacterFunctions::isWhitespace(chunk[s - 1])))
            starts.push_back({s, eq});
    }
    for (size_t k = 0; k < starts.size(); ++k) {
        const int valueEnd = k + 1 < starts.size() ? starts[k + 1].first : chunk.length();
        out.push_back({chunk.substring(starts[k].first, starts[k].second).toLowerCase(),
                       chunk.substring(starts[k].second + 1, valueEnd).trim()});
    }
}

struct Parser {
    juce::File directory;
    SfzParseResult result;
    Section section = Section::None;
    OpcodeMap control, global, master, group, region;
    bool regionOpen = false;
    juce::StringArray unknown, ignoredKinds;
    std::vector<std::pair<juce::String, juce::String>> defines;

    void run(const juce::String& text);
    void line(const juce::String& rawLine);
    void header(const juce::String& name);
    void opcode(const juce::String& name, const juce::String& value);
    void finishRegion();
    bool define(const juce::String& trimmedLine);
    juce::String substitute(juce::String value) const;
    void applyOpcodes(RegionSpec& spec, const OpcodeMap& m);
    void applyKeys(RegionSpec& spec, const OpcodeMap& m);
    int noteValue(const juce::String& name, const OpcodeMap& m, int fallback);
};

void Parser::run(const juce::String& text) {
    for (const auto& l : juce::StringArray::fromLines(stripComments(text)))
        line(l);
    finishRegion();
    if (unknown.size() > 0)
        result.warnings.add("Ignored unsupported opcodes: " + unknown.joinIntoString(", "));
}

bool Parser::define(const juce::String& t) {
    if (t.startsWith("#include")) {
        result.warnings.add("#include is not supported and was skipped");
        return true;
    }
    if (!t.startsWith("#define"))
        return false;
    const juce::String rest = t.substring(7).trim();
    const int space = rest.indexOfAnyOf(" \t");
    if (space > 0)
        defines.push_back({rest.substring(0, space), rest.substring(space).trim()});
    return true;
}

juce::String Parser::substitute(juce::String value) const {
    if (!value.contains("$"))
        return value;
    auto sorted = defines;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
    for (const auto& d : sorted)
        value = value.replace(d.first, d.second);
    return value;
}

void Parser::line(const juce::String& rawLine) {
    const juce::String t = rawLine.trim();
    if (t.isEmpty() || define(t))
        return;
    int pos = 0;
    while (pos < t.length()) {
        const int open = t.indexOfChar(pos, '<');
        const int close = open < 0 ? -1 : t.indexOfChar(open, '>');
        const int chunkEnd = (open >= 0 && close > open) ? open : t.length();
        std::vector<std::pair<juce::String, juce::String>> ops;
        splitOpcodes(t.substring(pos, chunkEnd), ops);
        for (const auto& op : ops)
            opcode(op.first, substitute(op.second));
        if (chunkEnd == t.length())
            break;
        header(t.substring(open + 1, close).trim().toLowerCase());
        pos = close + 1;
    }
}

void Parser::header(const juce::String& name) {
    finishRegion();
    if (name == "control") {
        section = Section::Control;
    } else if (name == "global") {
        global.clear();
        master.clear();
        group.clear();
        section = Section::Global;
    } else if (name == "master") {
        master.clear();
        group.clear();
        section = Section::Master;
    } else if (name == "group") {
        group.clear();
        section = Section::Group;
    } else if (name == "region") {
        region.clear();
        regionOpen = true;
        section = Section::Region;
    } else {
        section = Section::Ignored;
        if (!ignoredKinds.contains(name)) {
            ignoredKinds.add(name);
            result.warnings.add("Ignored <" + name + "> section");
        }
    }
}

void Parser::opcode(const juce::String& name, const juce::String& value) {
    if (section == Section::Ignored || section == Section::None)
        return;
    if (!isKnownOpcode(name)) {
        unknown.addIfNotAlreadyThere(name);
        return;
    }
    OpcodeMap* target = section == Section::Control  ? &control
                        : section == Section::Global ? &global
                        : section == Section::Master ? &master
                        : section == Section::Group  ? &group
                                                     : &region;
    (*target)[name] = value;
}

int Parser::noteValue(const juce::String& name, const OpcodeMap& m, int fallback) {
    const auto it = m.find(name);
    if (it == m.end())
        return fallback;
    if (auto n = parseNoteName(it->second, kSfzOctaveForMiddleC))
        return *n;
    result.warnings.add("Invalid " + name + " value '" + it->second + "'");
    return fallback;
}

void Parser::applyKeys(RegionSpec& spec, const OpcodeMap& m) {
    if (m.count("key") != 0) {
        spec.loKey = spec.hiKey = spec.rootKey = noteValue("key", m, 60);
    }
    spec.loKey = noteValue("lokey", m, spec.loKey);
    spec.hiKey = noteValue("hikey", m, spec.hiKey);
    spec.rootKey = noteValue("pitch_keycenter", m, spec.rootKey);
    if (m.count("lovel") != 0)
        spec.loVel = juce::jlimit(1, 127, m.at("lovel").getIntValue());
    if (m.count("hivel") != 0)
        spec.hiVel = juce::jlimit(1, 127, m.at("hivel").getIntValue());
}

void Parser::applyOpcodes(RegionSpec& spec, const OpcodeMap& m) {
    applyKeys(spec, m);
    auto number = [&](const char* key, double fallback) {
        const auto it = m.find(key);
        return it == m.end() ? fallback : it->second.getDoubleValue();
    };
    spec.tuneCents = (float)(number("tune", 0.0) + 100.0 * number("transpose", 0.0));
    spec.volumeDb = (float)number("volume", 0.0);
    spec.seqLength = juce::jmax(1, (int)number("seq_length", 1));
    spec.seqPosition = juce::jmax(1, (int)number("seq_position", 1));
    spec.ampReleaseSeconds = (float)number("ampeg_release", -1.0);
    spec.loopStart = (juce::int64)number("loop_start", number("loopstart", -1.0));
    spec.loopEnd = (juce::int64)number("loop_end", number("loopend", -1.0));
    if (m.count("trigger") != 0)
        spec.releaseTrigger = m.at("trigger") == "release";
    if (m.count("loop_mode") != 0) {
        const juce::String mode = m.at("loop_mode");
        spec.loop = mode == "one_shot"          ? RegionSpec::Loop::OneShot
                    : mode == "loop_continuous" ? RegionSpec::Loop::Continuous
                    : mode == "loop_sustain"    ? RegionSpec::Loop::Sustain
                                                : RegionSpec::Loop::None;
    }
}

void Parser::finishRegion() {
    if (!regionOpen)
        return;
    regionOpen = false;
    OpcodeMap merged;
    for (const OpcodeMap* level : {&global, &master, &group, &region})
        for (const auto& kv : *level)
            merged[kv.first] = kv.second;
    if (merged.count("sample") == 0 || merged["sample"].isEmpty()) {
        result.warnings.add("A region without a sample was dropped");
        return;
    }
    RegionSpec spec;
    applyOpcodes(spec, merged);
    juce::String relative = control.count("default_path") != 0 ? control["default_path"] : juce::String();
    relative = relative.replaceCharacter('\\', '/');
    if (relative.isNotEmpty() && !relative.endsWithChar('/'))
        relative += '/';
    relative += merged["sample"].replaceCharacter('\\', '/');
    spec.samplePath = directory.getChildFile(relative).getFullPathName();
    result.regions.push_back(spec);
}

} // namespace

SfzParseResult parseSfz(const juce::String& text, const juce::File& sfzDirectory) {
    Parser parser;
    parser.directory = sfzDirectory;
    parser.run(text);
    return std::move(parser.result);
}

Instrument::Ptr loadSfzInstrument(const juce::File& sfzFile, juce::StringArray& errors) {
    if (!sfzFile.existsAsFile()) {
        errors.add("Could not read " + sfzFile.getFullPathName());
        return nullptr;
    }
    const SfzParseResult parsed = parseSfz(sfzFile.loadFileAsString(), sfzFile.getParentDirectory());
    if (parsed.regions.empty()) {
        errors.add("The SFZ file has no usable regions.");
        return nullptr;
    }
    return buildInstrument(sfzFile.getFileNameWithoutExtension(), parsed.regions, errors);
}

} // namespace synth::sampler
