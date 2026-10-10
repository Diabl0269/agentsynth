#include "ArrangementContext.h"
#include "AudioEngine/ModuleTitle.h"
#include <cmath>
#include <unordered_map>

namespace synth {

namespace {

// Everything after the last '/' — assetRef is validated bundle-relative (no backslashes, no
// drive letters, no ".." — see TimelineDoc::isValidAssetRef), so a plain forward-slash search is
// enough; no juce::File round trip, so nothing here ever touches the filesystem or normalises
// against a working directory.
juce::String bareFileName(const juce::String& assetRef) { return assetRef.fromLastOccurrenceOf("/", false, false); }

// Trims a value to the shortest text that round-trips it for display: whole numbers print with no
// decimal point (bpm 120, not bpm 120.000), fractional ones keep up to 3 decimals with trailing
// zeros dropped (beat 0.5, not beat 0.500).
juce::String formatNumber(double value) {
    if (std::abs(value - std::round(value)) < 1e-9)
        return juce::String(static_cast<juce::int64>(std::llround(value)));

    juce::String s(value, 3);
    while (s.endsWithChar('0'))
        s = s.dropLastCharacters(1);
    if (s.endsWithChar('.'))
        s = s.dropLastCharacters(1);
    return s;
}

juce::String trackKindName(TrackKind kind) {
    switch (kind) {
    case TrackKind::Midi:
        return "Midi";
    case TrackKind::Audio:
        return "Audio";
    case TrackKind::Automation:
        return "Automation";
    }
    return "Unknown";
}

juce::String laneRecordModeName(int mode) {
    switch (static_cast<LaneRecordMode>(mode)) {
    case LaneRecordMode::Off:
        return "Off";
    case LaneRecordMode::Read:
        return "Read";
    case LaneRecordMode::Touch:
        return "Touch";
    case LaneRecordMode::Latch:
        return "Latch";
    case LaneRecordMode::Write:
        return "Write";
    }
    return "Unknown";
}

// uuid -> live node's display name. Built once per summarize() call so every binding/lane lookup
// is O(1) rather than a fresh scan of the graph, and so the resolution never depends on iteration
// order (determinism — see ArrangementContextTests.cpp's BudgetTruncatesAtTrackGranularity).
std::unordered_map<juce::String, juce::String> buildDisplayNameByUuid(const juce::AudioProcessorGraph& graph) {
    std::unordered_map<juce::String, juce::String> byUuid;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr || node->getProcessor() == nullptr)
            continue;
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            byUuid[uuid] = synth::moduleTitle(*node);
    }
    return byUuid;
}

// "unbound" (no uuid was ever set), "MISSING" (a uuid was set but no live node carries it — an
// orphaned binding, resolved fresh against `graph` rather than trusting the doc's own cached
// `orphaned` flag), or the bound node's display name. Never the raw uuid — see the security note
// on ArrangementContext::summarize().
juce::String resolveBinding(const juce::String& uuid, const std::unordered_map<juce::String, juce::String>& byUuid) {
    if (uuid.isEmpty())
        return "unbound";
    auto it = byUuid.find(uuid);
    return it != byUuid.end() ? it->second : juce::String("MISSING");
}

// One track's block of lines (its own header, clip summary and lane lines), joined so the caller
// can measure and place it as a single truncation unit.
juce::String buildTrackBlock(const Track& track, const std::unordered_map<juce::String, juce::String>& byUuid) {
    juce::StringArray lines;

    juce::StringArray flags;
    if (track.armed)
        flags.add("armed");
    if (track.muted)
        flags.add("muted");
    if (track.soloed)
        flags.add("soloed");

    juce::String header = "Track \"" + track.name + "\" (" + trackKindName(track.kind) + ")";
    if (!flags.isEmpty())
        header += " [" + flags.joinIntoString(", ") + "]";
    header += ": binding " + resolveBinding(track.bindingUuid, byUuid);
    lines.add(header);

    if (!track.clips.empty()) {
        if (track.kind == TrackKind::Audio) {
            // Audio clips are listed individually — name, beat window, bare file name only.
            for (const auto& clip : track.clips) {
                juce::String clipLine = "  Clip \"" + clip.name + "\" @ " + formatNumber(clip.startBeat) + "-" +
                                        formatNumber(clip.startBeat + clip.lengthBeats) + " beats";
                const juce::String fileName = bareFileName(clip.assetRef);
                if (fileName.isNotEmpty())
                    clipLine += ": " + fileName;
                lines.add(clipLine);
            }
        } else {
            // MIDI (and Automation-kind, which carries no clips today) tracks compress their
            // clip list to one line: beat windows plus a total note count, never a per-clip name.
            juce::StringArray windows;
            int totalNotes = 0;
            for (const auto& clip : track.clips) {
                windows.add(formatNumber(clip.startBeat) + "-" + formatNumber(clip.startBeat + clip.lengthBeats));
                totalNotes += static_cast<int>(clip.notes.size());
            }
            const juce::String clipWord = track.clips.size() == 1 ? " clip @ " : " clips @ ";
            lines.add("  " + juce::String(track.clips.size()) + clipWord + windows.joinIntoString(", ") + " beats; " +
                      juce::String(totalNotes) + " notes total");
        }
    }

    for (const auto& lane : track.lanes) {
        lines.add("  " + lane.paramId + " lane on " + resolveBinding(lane.nodeUuid, byUuid) + ": " +
                  juce::String(lane.points.size()) + (lane.points.size() == 1 ? " point, " : " points, ") +
                  laneRecordModeName(lane.recordMode));
    }

    return lines.joinIntoString("\n");
}

juce::String buildHeader(std::size_t trackCount, const TransportService::PositionSnapshot& transport) {
    juce::String loopPart =
        transport.looping ? "[" + formatNumber(transport.loopStartPpq) + ", " + formatNumber(transport.loopEndPpq) + ")"
                          : juce::String("off");

    return "Arrangement: " + juce::String(trackCount) + (trackCount == 1 ? " track, bpm " : " tracks, bpm ") +
           formatNumber(transport.bpm) + ", " + juce::String(transport.timeSigNumerator) + "/" +
           juce::String(transport.timeSigDenominator) + ", loop " + loopPart;
}

// The markers on one line, "Markers (beats): "Intro" @ 0, "Drop" @ 8", so a later edit can refer to a section by
// name. Whole markers only, up to half the budget, with a "+K more" tail, so a heavily marked project cannot push
// every track out. A name is user- or model-authored text: line breaks become spaces so it stays one line.
juce::String buildMarkersLine(const std::vector<Marker>& markers, int maxChars) {
    if (markers.empty())
        return {};
    const juce::String prefix = "Markers (beats): ";
    juce::String line = prefix;
    int included = 0;
    for (const auto& marker : markers) {
        const juce::String name = marker.text.replaceCharacters("\r\n", "  ");
        const juce::String item = "\"" + name + "\" @ " + formatNumber(marker.beat);
        const juce::String candidate = line + (included > 0 ? ", " : "") + item;
        if (included > 0 && candidate.length() > maxChars / 2)
            break;
        line = candidate;
        ++included;
    }
    const int remaining = static_cast<int>(markers.size()) - included;
    if (remaining > 0)
        line += ", +" + juce::String(remaining) + " more";
    return line;
}

} // namespace

juce::String ArrangementContext::summarize(const TimelineDoc& doc, const juce::AudioProcessorGraph& graph,
                                           const TransportService::PositionSnapshot& transport, int maxChars) {
    if (doc.isEmpty() && doc.getMarkers().empty())
        return {};

    const auto& tracks = doc.getTracks();
    const auto byUuid = buildDisplayNameByUuid(graph);

    juce::String result = buildHeader(tracks.size(), transport);
    if (const auto markers = buildMarkersLine(doc.getMarkers(), maxChars); markers.isNotEmpty())
        result += "\n" + markers;

    int includedTracks = 0;
    for (const auto& track : tracks) {
        const juce::String block = buildTrackBlock(track, byUuid);

        // Track-granularity budget check: a track is included whole or not at all, so the result
        // never gets cut mid-line. The tail marker below is deliberately exempt from this check —
        // it is what explains the truncation and must always appear once anything is dropped.
        const int prospectiveLength = result.length() + 1 /* newline */ + block.length();
        if (prospectiveLength > maxChars)
            break;

        result += "\n" + block;
        ++includedTracks;
    }

    const int remaining = static_cast<int>(tracks.size()) - includedTracks;
    if (remaining > 0)
        result += juce::String::fromUTF8("\n\xe2\x80\xa6 [+") + juce::String(remaining) +
                  " more tracks]"; // U+2026 HORIZONTAL ELLIPSIS, UTF-8 encoded

    return result;
}

} // namespace synth
