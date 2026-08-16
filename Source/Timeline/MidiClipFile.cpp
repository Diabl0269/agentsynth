#include "MidiClipFile.h"
#include <algorithm>
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

// Pairs note-on/note-off events in one SMF track into clip-relative-beat MidiNotes, FIFO per
// (pitch, channel). Returns std::nullopt the moment the track's note count would exceed
// TimelineDoc::kMaxNotesPerClip — the caller rejects the WHOLE import on that signal.
std::optional<MidiClipFile::ImportedTrack> pairTrack(const juce::MidiMessageSequence& sequence, double ticksPerBeat) {
    MidiClipFile::ImportedTrack imported;
    std::map<std::pair<int, int>, std::vector<OpenNoteOn>> open;

    const auto overCap = [&imported] { return imported.notes.size() > (std::size_t)TimelineDoc::kMaxNotesPerClip; };

    for (int i = 0; i < sequence.getNumEvents(); ++i) {
        const auto& message = sequence.getEventPointer(i)->message;

        if (imported.name.isEmpty() && message.isTrackNameEvent())
            imported.name = message.getTextFromTextMetaEvent();

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

bool MidiClipFile::importIntoTrack(TimelineDoc& doc, TrackId trackId, double startBeat, const ImportResult& result) {
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
    }

    return true;
}

bool MidiClipFile::exportClip(const TimelineDoc& doc, ClipId clipId, juce::OutputStream& stream) {
    const Clip* clip = doc.getClip(clipId);
    if (clip == nullptr)
        return false;

    juce::MidiMessageSequence sequence;
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
