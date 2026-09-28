#include "MidiClipFile.h"
#include "AutomationKernel.h"
#include "AutomationRecorder.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace synth {

namespace {

// A note-on awaiting its matching note-off, FIFO per (pitch, channel) — identical convention to
// MidiRecorder::stopAndCommit.
struct OpenNoteOn {
    double startBeat;
    int velocity;
};

MidiNote makeImportedNote(double startBeat, double lengthBeats, int pitch, int velocity, int channel) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    // Structurally guaranteed by juce::MidiMessage's own encoding (7-bit note number/velocity,
    // 4-bit channel nibble) — clamped defensively anyway, since this is a future untrusted surface.
    note.pitch = juce::jlimit(0, 127, pitch);
    note.velocity = juce::jlimit(1, 127, velocity);
    note.channel = juce::jlimit(1, 16, channel);
    return note;
}

// ---- CC import ----------------------------------------------------------------------------------
// A file's CC stream becomes one Hold lane per controller number, all channels merged (a clip CC
// lane has no channel of its own; it plays on the clip's note channels). Controllers 120..127 are
// channel-mode messages (all notes off, reset...), not data, and are skipped. Unchanged values are
// dropped; a lane still over kMaxControllerPointsPerLane is thinned with the automation recorder's
// RDP helper at a growing tolerance rather than truncated or rejected — a dense controller sweep is
// ordinary data, not an attack, and the lane keeps its shape.
struct CcCollector {
    std::map<int, std::vector<ControllerPoint>> byCc;

    void add(const juce::MidiMessage& message, double beat) {
        const int cc = message.getControllerNumber();
        if (cc < 0 || cc > 119)
            return;
        auto& points = byCc[cc];
        const double value = juce::jlimit(0, 127, message.getControllerValue());
        if (!points.empty() && points.back().value == value)
            return;
        if (!points.empty() && points.back().beat == beat)
            points.back().value = value; // same tick: the later event wins
        else
            points.push_back({beat, value, static_cast<int>(BreakpointCurve::Hold)});
    }

    static std::vector<ControllerPoint> fitToCap(std::vector<ControllerPoint> points) {
        double epsilon = 0.5;
        while ((int)points.size() > TimelineDoc::kMaxControllerPointsPerLane && epsilon <= 128.0) {
            std::vector<AutomationRecorder::CapturedPoint> captured;
            captured.reserve(points.size());
            for (const auto& point : points)
                captured.push_back({point.beat, point.value});
            std::vector<ControllerPoint> thinned;
            for (const auto& point : AutomationRecorder::thinPoints(captured, epsilon))
                thinned.push_back({point.beat, point.value, static_cast<int>(BreakpointCurve::Hold)});
            points = std::move(thinned);
            epsilon *= 2.0;
        }
        if ((int)points.size() > TimelineDoc::kMaxControllerPointsPerLane)
            points.resize((std::size_t)TimelineDoc::kMaxControllerPointsPerLane);
        return points;
    }

    std::vector<ClipControllerLane> lanes() const {
        std::vector<ClipControllerLane> out;
        for (const auto& [cc, points] : byCc) // std::map: already sorted by ccNumber
            out.push_back({cc, fitToCap(points)});
        return out;
    }
};

// Pairs note-on/note-off events in one SMF track into clip-relative-beat MidiNotes, FIFO per
// (pitch, channel). Returns std::nullopt the moment the track's note count would exceed
// TimelineDoc::kMaxNotesPerClip — the caller rejects the WHOLE import on that signal.
std::optional<MidiClipFile::ImportedTrack> pairTrack(const juce::MidiMessageSequence& sequence, double ticksPerBeat) {
    MidiClipFile::ImportedTrack imported;
    std::map<std::pair<int, int>, std::vector<OpenNoteOn>> open;
    CcCollector ccs;

    const auto overCap = [&imported] { return imported.notes.size() > (std::size_t)TimelineDoc::kMaxNotesPerClip; };

    for (int i = 0; i < sequence.getNumEvents(); ++i) {
        const auto& message = sequence.getEventPointer(i)->message;

        if (imported.name.isEmpty() && message.isTrackNameEvent())
            imported.name = message.getTextFromTextMetaEvent();
        if (message.isController())
            ccs.add(message, message.getTimeStamp() / ticksPerBeat);

        // isNoteOn()/isNoteOff() default arguments already implement the SMF convention that a
        // note-on with velocity 0 is a note-off — see juce_MidiMessage.h.
        if (message.isNoteOn()) {
            const auto key = std::make_pair(message.getNoteNumber(), message.getChannel());
            open[key].push_back({message.getTimeStamp() / ticksPerBeat, (int)message.getVelocity()});
        } else if (message.isNoteOff()) {
            const auto key = std::make_pair(message.getNoteNumber(), message.getChannel());
            auto it = open.find(key);
            if (it == open.end() || it->second.empty())
                continue; // a stray off with no matching on is ignored, same as MidiRecorder

            const OpenNoteOn on = it->second.front();
            it->second.erase(it->second.begin());
            const double offBeat = message.getTimeStamp() / ticksPerBeat;
            double length = offBeat - on.startBeat;
            if (!(length > 0.0))
                length = MidiClipFile::kMinNoteLengthBeats; // on/off landed on the same tick

            imported.notes.push_back(makeImportedNote(on.startBeat, length, key.first, on.velocity, key.second));
            if (overCap())
                return std::nullopt;
        }
    }

    // Anything still open never got a note-off anywhere later in the track: dangling, floored to
    // the minimum length rather than dropped.
    for (const auto& [key, opens] : open) {
        for (const auto& on : opens) {
            imported.notes.push_back(
                makeImportedNote(on.startBeat, MidiClipFile::kMinNoteLengthBeats, key.first, on.velocity, key.second));
            if (overCap())
                return std::nullopt;
        }
    }

    std::sort(imported.notes.begin(), imported.notes.end(), [](const MidiNote& a, const MidiNote& b) {
        if (a.startBeat != b.startBeat)
            return a.startBeat < b.startBeat;
        return a.pitch < b.pitch;
    });
    imported.controllers = ccs.lanes();

    return imported;
}

} // namespace

MidiClipFile::ImportResult MidiClipFile::importFromStream(juce::InputStream& stream) {
    ImportResult result;

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(stream, false, nullptr)) {
        result.message = "Not a readable Standard MIDI File";
        return result;
    }

    // Positive time-format values are ticks-per-quarter-note (PPQ); negative values pack an SMPTE
    // frame rate — see juce::MidiFile::getTimeFormat(). Zero is not a valid PPQ either.
    const short timeFormat = midiFile.getTimeFormat();
    if (timeFormat <= 0) {
        result.message = "SMPTE time format is not supported; only PPQ (ticks-per-quarter-note) files can be imported";
        return result;
    }
    const double ticksPerBeat = (double)timeFormat;

    for (int t = 0; t < midiFile.getNumTracks(); ++t) {
        const auto* sequence = midiFile.getTrack(t);
        if (sequence == nullptr)
            continue;

        auto imported = pairTrack(*sequence, ticksPerBeat);
        if (!imported.has_value()) {
            ImportResult rejected;
            rejected.message = "Track exceeds the maximum of " + juce::String(TimelineDoc::kMaxNotesPerClip) +
                               " notes per clip; import rejected";
            return rejected;
        }
        if (!imported->notes.empty())
            result.tracks.push_back(std::move(*imported));
    }

    result.ok = true;
    return result;
}

MidiClipFile::ImportResult MidiClipFile::importFromFile(const juce::File& file) {
    auto stream = file.createInputStream();
    if (stream == nullptr) {
        ImportResult result;
        result.message = "Could not open file: " + file.getFullPathName();
        return result;
    }
    return importFromStream(*stream);
}

bool MidiClipFile::ImportResult::hasControllerData() const noexcept {
    for (const auto& track : tracks)
        if (!track.controllers.empty())
            return true;
    return false;
}

namespace {
// Writes one imported track's CC lanes into `clip` through the mutation API (its validation and
// normalisation). A lane the clip already has for that CC is REPLACED: an import states what the
// controller does, and interleaving two unrelated streams would describe neither.
void applyImportedControllers(TimelineDoc& doc, ClipId clip, const MidiClipFile::ImportedTrack& imported) {
    for (const auto& lane : imported.controllers)
        doc.setControllerLanePoints(clip, lane.ccNumber, lane.points);
}
} // namespace

bool MidiClipFile::importIntoTrack(TimelineDoc& doc, TrackId trackId, double startBeat, const ImportResult& result,
                                   bool withControllers) {
    if (!result.ok)
        return false;

    const Track* track = doc.getTrack(trackId);
    if (track == nullptr)
        return false;

    std::vector<const ImportedTrack*> nonEmpty;
    for (const auto& imported : result.tracks)
        if (!imported.notes.empty())
            nonEmpty.push_back(&imported);

    if (nonEmpty.empty())
        return false;

    // All-or-nothing against the doc's own cap: reject before mutating anything rather than
    // adding some clips and then discovering the track has no room for the rest.
    if (track->clips.size() + nonEmpty.size() > (std::size_t)TimelineDoc::kMaxClipsPerTrack)
        return false;

    for (const auto* imported : nonEmpty) {
        double lastEnd = 0.0;
        for (const auto& note : imported->notes)
            lastEnd = std::max(lastEnd, note.startBeat + note.lengthBeats);
        const double clipLength = std::max(std::ceil(lastEnd), 1.0);

        const juce::String clipName = imported->name.isNotEmpty() ? imported->name : juce::String("Imported");
        const ClipId clip = doc.addClip(trackId, startBeat, clipLength, clipName);
        if (!clip.isValid())
            return false; // shouldn't happen given the pre-check above, but never claim success on a partial import

        for (const auto& note : imported->notes)
            doc.addNote(clip, note);
        if (withControllers)
            applyImportedControllers(doc, clip, *imported);
    }

    return true;
}

// "Import into THIS clip" (the piano roll's MIDI menu): every imported track's notes land in the
// one clip at their file beats (the file's beat 0 is the clip's start), the clip grows to fit the
// last note end — never shrinks — and, when asked, each CC lane is written through
// setControllerLanePoints. Rejected with no mutation when the clip is gone, nothing would be added,
// or the merged note count would pass kMaxNotesPerClip.
bool MidiClipFile::importIntoClip(TimelineDoc& doc, ClipId clipId, const ImportResult& result, bool withControllers) {
    const Clip* clip = doc.getClip(clipId);
    if (!result.ok || clip == nullptr)
        return false;
    std::size_t incoming = 0;
    double lastEnd = clip->lengthBeats;
    for (const auto& imported : result.tracks) {
        incoming += imported.notes.size();
        for (const auto& note : imported.notes)
            lastEnd = std::max(lastEnd, note.startBeat + note.lengthBeats);
    }
    if (incoming == 0 && !(withControllers && result.hasControllerData()))
        return false;
    if (clip->notes.size() + incoming > (std::size_t)TimelineDoc::kMaxNotesPerClip)
        return false;

    if (lastEnd > clip->lengthBeats)
        doc.resizeClip(clipId, std::ceil(lastEnd));
    for (const auto& imported : result.tracks) {
        for (const auto& note : imported.notes)
            doc.addNote(clipId, note);
        if (withControllers)
            applyImportedControllers(doc, clipId, imported);
    }
    return true;
}

namespace {

// ---- CC export -----------------------------------------------------------------------------------
// Each CC lane becomes controller events inside the clip window [0, lengthBeats) — exactly what
// playback sends: the lane's value at beat 0 (flat before its first point), then each Hold step at
// its point, and a Linear segment sampled every kExportCcStepBeats (a 1/32-beat step, ~16 ms at
// 120 BPM: finer than a receiver's own smoothing, coarse enough to keep files small). Values are
// evaluated with AutomationKernel — the audio thread's evaluator — and an event is written only
// when the rounded value CHANGES. Channels follow the playback rule: every channel the clip's
// notes use, channel 1 for a clip without notes.
void appendControllerEvents(const Clip& clip, juce::MidiMessageSequence& sequence) {
    std::uint16_t mask = 0;
    for (const auto& note : clip.notes)
        if (note.channel >= 1 && note.channel <= 16)
            mask = (std::uint16_t)(mask | (1u << (note.channel - 1)));
    if (mask == 0)
        mask = 1;

    for (const auto& lane : clip.controllers) {
        if (lane.points.empty())
            continue;
        std::vector<TimelineSnapshot::Point> points;
        points.reserve(lane.points.size());
        for (const auto& point : lane.points)
            points.push_back({point.beat, point.value, 0.0f, point.curve});

        // Sample beats: 0, every point beat, and the Linear sub-steps, all inside the window.
        std::vector<double> beats = {0.0};
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (points[i].beat < clip.lengthBeats)
                beats.push_back(points[i].beat);
            if (i + 1 < points.size() && points[i].curve != static_cast<int>(BreakpointCurve::Hold))
                for (double b = points[i].beat + MidiClipFile::kExportCcStepBeats;
                     b < points[i + 1].beat && b < clip.lengthBeats; b += MidiClipFile::kExportCcStepBeats)
                    beats.push_back(b);
        }
        std::sort(beats.begin(), beats.end());

        AutomationCursor cursor{};
        int last = -1;
        for (const double beat : beats) {
            const double value =
                AutomationKernel::evaluate(points.data(), (int)points.size(), beat, points[0].value, cursor);
            const int v7 = juce::jlimit(0, 127, (int)std::lround(value));
            if (v7 == last)
                continue;
            last = v7;
            for (int channel = 1; channel <= 16; ++channel) {
                if ((mask & (1u << (channel - 1))) == 0)
                    continue;
                auto message = juce::MidiMessage::controllerEvent(channel, lane.ccNumber, v7);
                message.setTimeStamp(std::round(beat * (double)MidiClipFile::kExportPpq));
                sequence.addEvent(message);
            }
        }
    }
}

} // namespace

bool MidiClipFile::exportClip(const TimelineDoc& doc, ClipId clipId, juce::OutputStream& stream) {
    const Clip* clip = doc.getClip(clipId);
    if (clip == nullptr)
        return false;

    juce::MidiMessageSequence sequence;
    // CCs first: at an equal tick the sequence keeps insertion order, so a controller lands before
    // the note it shapes — the same order playback emits them in.
    appendControllerEvents(*clip, sequence);
    for (const auto& note : clip->notes) {
        const double onTick = note.startBeat * (double)kExportPpq;
        const double offTick = (note.startBeat + note.lengthBeats) * (double)kExportPpq;

        auto onMessage =
            juce::MidiMessage::noteOn(note.channel, note.pitch, (juce::uint8)juce::jlimit(1, 127, note.velocity));
        onMessage.setTimeStamp(onTick);
        sequence.addEvent(onMessage);

        auto offMessage = juce::MidiMessage::noteOff(note.channel, note.pitch);
        offMessage.setTimeStamp(offTick);
        sequence.addEvent(offMessage);
    }

    // Explicit end-of-track at the clip's own length (or the last event, whichever is later) so
    // the file's declared duration reflects the clip even when notes end early, and so it never
    // lands earlier than an overhanging note's own events (addEvent keeps the sequence timestamp-
    // sorted, so anything placed before the last event would land in the middle, not the end).
    const double endTick = std::max(clip->lengthBeats * (double)kExportPpq, sequence.getEndTime());
    auto endOfTrack = juce::MidiMessage::endOfTrack();
    endOfTrack.setTimeStamp(endTick);
    sequence.addEvent(endOfTrack);

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(kExportPpq);
    midiFile.addTrack(sequence);
    return midiFile.writeTo(stream, 1);
}

bool MidiClipFile::exportClipToFile(const TimelineDoc& doc, ClipId clipId, const juce::File& file) {
    if (doc.getClip(clipId) == nullptr)
        return false;

    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    return exportClip(doc, clipId, *stream);
}

} // namespace synth
