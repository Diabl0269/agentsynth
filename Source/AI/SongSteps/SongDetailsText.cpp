#include "SongDetailsText.h"

#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>

// Concern: the plain-language details of a built song, read from its ProjectEditEnvelope.
namespace synth::songsteps {

namespace {

constexpr int kBeatsPerBar = 4;
constexpr size_t kMaxMidiBytes = 8u * 1024u * 1024u;

struct TrackInfo {
    juce::String name;
    juce::StringArray modules;
    int notes = 0;
};

juce::String plural(int n, const char* one, const char* many) { return juce::String(n) + " " + (n == 1 ? one : many); }

int countMidiNotes(const juce::String& base64) {
    juce::MemoryOutputStream decoded;
    if (base64.isEmpty() || !juce::Base64::convertFromBase64(decoded, base64) || decoded.getDataSize() > kMaxMidiBytes)
        return 0;
    juce::MemoryInputStream in(decoded.getData(), decoded.getDataSize(), false);
    juce::MidiFile file;
    if (!file.readFrom(in))
        return 0;
    int count = 0;
    for (int t = 0; t < file.getNumTracks(); ++t)
        if (auto* sequence = file.getTrack(t))
            for (auto* event : *sequence)
                if (event->message.isNoteOn())
                    ++count;
    return count;
}

int countClipNotes(const juce::var& clips) {
    int count = 0;
    if (auto* list = clips.getArray())
        for (const auto& clip : *list)
            if (auto* notes = clip.getProperty("notes", {}).getArray())
                count += notes->size();
    return count;
}

void addModule(juce::StringArray& modules, const juce::String& type) {
    if (type.isNotEmpty())
        modules.addIfNotAlreadyThere(type);
}

std::vector<TrackInfo> readTracks(const juce::Array<juce::var>& ops) {
    std::vector<TrackInfo> tracks;
    std::map<juce::String, size_t> byName;
    for (const auto& op : ops) {
        const auto kind = op.getProperty("op", {}).toString();
        if (kind == "addInstrumentTrack" || kind == "addTrack") {
            TrackInfo track;
            track.name = op.getProperty("name", {}).toString();
            addModule(track.modules, op.getProperty("instrument", {}).toString());
            if (auto* inserts = op.getProperty("inserts", {}).getArray())
                for (const auto& insert : *inserts)
                    addModule(track.modules, insert.getProperty("type", {}).toString());
            byName[track.name] = tracks.size();
            tracks.push_back(std::move(track));
        }
    }
    for (const auto& op : ops) {
        const auto kind = op.getProperty("op", {}).toString();
        if (kind != "placeClips" && kind != "placeMidiClip")
            continue;
        const auto it = byName.find(op.getProperty("track", {}).toString());
        if (it == byName.end())
            continue;
        tracks[it->second].notes += kind == "placeClips" ? countClipNotes(op.getProperty("clips", {}))
                                                         : countMidiNotes(op.getProperty("midBase64", {}).toString());
    }
    return tracks;
}

juce::String trackLine(const TrackInfo& track) {
    juce::String line = track.name.isNotEmpty() ? track.name : juce::String("Track");
    line << ": ";
    line << (track.modules.isEmpty() ? juce::String("no sound modules") : track.modules.joinIntoString(", "));
    line << ", " << plural(track.notes, "note", "notes");
    return line;
}

juce::String sectionsLine(const std::vector<PlanSection>& sections) {
    if (sections.empty())
        return {};
    juce::StringArray parts;
    int total = 0;
    for (const auto& s : sections) {
        parts.add(s.name + " " + juce::String(s.bars));
        total += s.bars;
    }
    return plural((int)sections.size(), "section", "sections") + ": " + parts.joinIntoString(", ") + " (" +
           plural(total, "bar", "bars") + " in all)";
}

// The 1-based bar a beat falls in, and the section that bar is in ("" without sections).
juce::String sectionAtBeat(const std::vector<PlanSection>& sections, double beat) {
    double start = 0.0;
    for (const auto& s : sections) {
        const double end = start + s.bars * kBeatsPerBar;
        if (beat < end)
            return s.name;
        start = end;
    }
    return sections.empty() ? juce::String() : sections.back().name;
}

juce::String laneLine(const juce::var& op, const std::map<int, juce::String>& nodeTypes,
                      const std::vector<PlanSection>& sections) {
    double first = 0.0, last = 0.0;
    bool any = false;
    if (auto* points = op.getProperty("points", {}).getArray())
        for (const auto& point : *points) {
            const double beat = (double)point.getProperty("beat", 0.0);
            first = any ? std::min(first, beat) : beat;
            last = any ? std::max(last, beat) : beat;
            any = true;
        }
    juce::String line = op.getProperty("paramId", {}).toString();
    if (line.isEmpty())
        line = "Parameter";
    const auto nodeId = op.getProperty("nodeId", {});
    const auto type = nodeId.isVoid() ? nodeTypes.end() : nodeTypes.find((int)nodeId);
    if (type != nodeTypes.end())
        line << " on " << type->second;
    if (!any)
        return line;
    const int firstBar = (int)(first / kBeatsPerBar) + 1;
    const int lastBar = (int)(last / kBeatsPerBar) + 1;
    line << ", "
         << (firstBar == lastBar ? "bar " + juce::String(firstBar)
                                 : "bars " + juce::String(firstBar) + " to " + juce::String(lastBar));
    const auto from = sectionAtBeat(sections, first);
    const auto to = sectionAtBeat(sections, last);
    if (from.isNotEmpty())
        line << (from == to ? " (" + from + ")" : " (" + from + " to " + to + ")");
    return line;
}

} // namespace

juce::String buildSongDetailsText(const juce::var& envelope, const std::vector<PlanSection>& sections) {
    juce::Array<juce::var> ops;
    if (auto* list = envelope.getProperty("timelineOps", {}).getArray())
        ops = *list;
    std::map<int, juce::String> nodeTypes;
    if (auto* nodes = envelope.getProperty("nodes", {}).getArray())
        for (const auto& node : *nodes)
            nodeTypes[(int)node.getProperty("id", -1)] = node.getProperty("type", {}).toString();

    juce::StringArray lines;
    const auto tracks = readTracks(ops);
    for (size_t i = 0; i < tracks.size() && (int)i < kMaxDetailLines; ++i)
        lines.add(trackLine(tracks[i]));
    if (tracks.size() > (size_t)kMaxDetailLines)
        lines.add("... and " + plural((int)tracks.size() - kMaxDetailLines, "more track", "more tracks"));

    const auto sectionsText = sectionsLine(sections);
    if (sectionsText.isNotEmpty())
        lines.add(sectionsText);

    int lanes = 0;
    for (const auto& op : ops)
        if (op.getProperty("op", {}).toString() == "writeLane") {
            if (++lanes <= kMaxDetailLines)
                lines.add("Automation: " + laneLine(op, nodeTypes, sections));
        }
    if (lanes > kMaxDetailLines)
        lines.add("... and " + plural(lanes - kMaxDetailLines, "more automation lane", "more automation lanes"));
    return lines.joinIntoString("\n");
}

} // namespace synth::songsteps
