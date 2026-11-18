#include "AI/ProjectGenerate/SongLayout.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace synth::projectgenerate {

namespace {

struct Pattern {
    juce::String id;
    juce::var track;
    juce::String trackKey;
    int bars = 1;
    std::vector<SongNote> notes;
};

struct Play {
    const Pattern* main = nullptr;
    const Pattern* fill = nullptr;
    int transpose = 0;
    double velocityScale = 1.0;
};

struct TrackClips {
    juce::var track;
    juce::Array<juce::var> clips;
};

void warn(juce::StringArray* warnings, const juce::String& text) {
    if (warnings != nullptr)
        warnings->add("Song layout: " + text);
}

bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

bool isWhole(double d) { return d == std::floor(d); }

// The number grammar the hosted parser accepts: an optional minus, digits, an optional fraction.
bool isPlainNumber(const juce::String& s) {
    int i = 0;
    const int n = s.length();
    if (i < n && s[i] == '-')
        ++i;
    const int intStart = i;
    while (i < n && s[i] >= '0' && s[i] <= '9')
        ++i;
    if (i == intStart)
        return false;
    if (i == n)
        return true;
    if (s[i] != '.')
        return false;
    ++i;
    const int fracStart = i;
    while (i < n && s[i] >= '0' && s[i] <= '9')
        ++i;
    return i > fracStart && i == n;
}

juce::StringArray splitOnColon(const juce::String& token) {
    juce::StringArray parts;
    int from = 0;
    for (;;) {
        const int at = token.indexOfChar(from, ':');
        if (at < 0) {
            parts.add(token.substring(from));
            return parts;
        }
        parts.add(token.substring(from, at));
        from = at + 1;
    }
}

// Whitespace-separated tokens; runs of whitespace never produce an empty token.
juce::StringArray splitOnWhitespace(const juce::String& text) {
    juce::StringArray tokens;
    juce::String current;
    for (const juce::juce_wchar c : text) {
        if (juce::CharacterFunctions::isWhitespace(c)) {
            if (current.isNotEmpty())
                tokens.add(current);
            current = {};
        } else {
            current += juce::String::charToString(c);
        }
    }
    if (current.isNotEmpty())
        tokens.add(current);
    return tokens;
}

juce::var makeNote(double start, double length, int pitch, int velocity) {
    auto* note = new juce::DynamicObject();
    note->setProperty("startBeat", start);
    note->setProperty("lengthBeats", length);
    note->setProperty("pitch", pitch);
    note->setProperty("velocity", velocity);
    return juce::var(note);
}

bool validTrackSelector(const juce::var& track) {
    if (track.isString())
        return track.toString().isNotEmpty() && track.toString().length() <= kTimelineOpsMaxNameChars;
    if (!track.isObject() || !track.hasProperty("index"))
        return false;
    const juce::var index = track["index"];
    if (!isNumber(index))
        return false;
    const double d = static_cast<double>(index);
    return isWhole(d) && d >= 0 && d < kTimelineOpsMaxTracks;
}

// Reads one pattern, or returns false after saying why it is skipped.
bool readPattern(const juce::var& raw, int position, const std::map<juce::String, Pattern>& known, Pattern& out,
                 juce::StringArray* warnings) {
    const juce::String label = "pattern " + juce::String(position + 1);
    if (!raw.isObject()) {
        warn(warnings, label + " is not an object and was skipped.");
        return false;
    }
    out.id = raw["id"].toString();
    if (!raw["id"].isString() || out.id.isEmpty() || out.id.length() > kSongIdMaxChars) {
        warn(warnings, label + " has no usable id and was skipped.");
        return false;
    }
    if (known.count(out.id) > 0) {
        warn(warnings, "pattern id \"" + out.id + "\" is used more than once; the later one was skipped.");
        return false;
    }
    out.track = raw["track"];
    if (!validTrackSelector(out.track)) {
        warn(warnings, "pattern \"" + out.id + "\" has no usable track and was skipped.");
        return false;
    }
    out.trackKey = juce::JSON::toString(out.track, true);
    const juce::var bars = raw["bars"];
    const double barsValue = isNumber(bars) ? static_cast<double>(bars) : 0.0;
    if (!isWhole(barsValue) || barsValue < 1 || barsValue > kSongPatternMaxBars) {
        warn(warnings, "pattern \"" + out.id + "\" needs bars from 1 to " + juce::String(kSongPatternMaxBars) +
                           " and was skipped.");
        return false;
    }
    out.bars = static_cast<int>(barsValue);
    const juce::String text = raw["notes"].isString() ? raw["notes"].toString() : juce::String();
    if (!raw["notes"].isString() || text.length() > kSongNotesMaxChars) {
        warn(warnings, "pattern \"" + out.id + "\" has no usable notes string and was skipped.");
        return false;
    }
    juce::String error;
    if (!parseSongNotes(text, out.bars, out.id, out.notes, error)) {
        warn(warnings, error + " The pattern was skipped.");
        return false;
    }
    return true;
}

std::map<juce::String, Pattern> readPatterns(const juce::var& rawPatterns, juce::StringArray* warnings) {
    std::map<juce::String, Pattern> patterns;
    const juce::Array<juce::var>* list = rawPatterns.getArray();
    if (list == nullptr)
        return patterns;
    for (int i = 0; i < list->size(); ++i) {
        if (i >= kSongMaxPatterns) {
            warn(warnings, "more than " + juce::String(kSongMaxPatterns) + " patterns; the rest were skipped.");
            break;
        }
        Pattern pattern;
        if (readPattern((*list)[i], i, patterns, pattern, warnings))
            patterns[pattern.id] = pattern;
    }
    return patterns;
}

void readPlayModifiers(const juce::var& raw, const juce::String& where, Play& play, juce::StringArray* warnings) {
    if (raw.hasProperty("transpose")) {
        const juce::var v = raw["transpose"];
        const double d = isNumber(v) ? static_cast<double>(v) : 1e9;
        if (isWhole(d) && std::abs(d) <= kSongMaxTranspose)
            play.transpose = static_cast<int>(d);
        else
            warn(warnings, where + " has a transpose that is not a whole number within +-" +
                               juce::String(kSongMaxTranspose) + "; it was ignored.");
    }
    if (raw.hasProperty("velocity")) {
        const juce::var v = raw["velocity"];
        const double d = isNumber(v) ? static_cast<double>(v) : -1.0;
        if (d >= kSongMinVelocityScale && d <= kSongMaxVelocityScale)
            play.velocityScale = d;
        else
            warn(warnings, where + " has a velocity scale outside " + juce::String(kSongMinVelocityScale) + " to " +
                               juce::String(kSongMaxVelocityScale) + "; it was ignored.");
    }
}

// Reads one play entry; false when its pattern cannot be found (the entry is skipped).
bool readPlay(const juce::var& raw, const juce::String& sectionName, const std::map<juce::String, Pattern>& patterns,
              Play& play, juce::StringArray* warnings) {
    const juce::String where = "section \"" + sectionName + "\"";
    const juce::String mainId = raw["pattern"].toString();
    const auto main = patterns.find(mainId);
    if (!raw.isObject() || main == patterns.end()) {
        warn(warnings, where + " plays pattern \"" + mainId + "\", which is not defined in \"patterns\"; skipped.");
        return false;
    }
    play.main = &main->second;
    readPlayModifiers(raw, where + " play of \"" + mainId + "\"", play, warnings);
    if (!raw.hasProperty("fill"))
        return true;
    const juce::String fillId = raw["fill"].toString();
    const auto fill = patterns.find(fillId);
    if (fill == patterns.end())
        warn(warnings,
             where + " uses fill \"" + fillId + "\", which is not defined in \"patterns\"; played without it.");
    else if (fill->second.trackKey != main->second.trackKey)
        warn(warnings,
             "fill \"" + fillId + "\" is on a different track than pattern \"" + mainId + "\"; played without it.");
    else
        play.fill = &fill->second;
    return true;
}

// Tiles `pattern` from `from` up to but not including `to` (absolute beats), with the play's transpose and velocity.
void tile(const Pattern& pattern, double from, double to, const Play& play, std::vector<SongNote>& out) {
    const double period = pattern.bars * kBeatsPerBar;
    for (double origin = from; origin < to; origin += period) {
        for (const SongNote& note : pattern.notes) {
            const double start = origin + note.startBeat;
            if (start >= to)
                continue;
            SongNote tiled;
            tiled.startBeat = start;
            tiled.lengthBeats = std::min(note.lengthBeats, to - start);
            tiled.pitch = std::clamp(note.pitch + play.transpose, 0, 127);
            tiled.velocity = std::clamp(static_cast<int>(std::floor(note.velocity * play.velocityScale + 0.5)), 1, 127);
            out.push_back(tiled);
        }
    }
}

// One section's notes per track, in the order the tracks first appear.
struct SectionNotes {
    std::vector<std::pair<juce::var, std::vector<SongNote>>> perTrack;
    std::map<juce::String, size_t> index;
};

void playSection(const juce::var& rawPlays, const juce::String& name, double start, double end,
                 const std::map<juce::String, Pattern>& patterns, SectionNotes& out, juce::StringArray* warnings) {
    const juce::Array<juce::var>* plays = rawPlays.getArray();
    if (plays == nullptr)
        return;
    for (int i = 0; i < plays->size(); ++i) {
        if (i >= kSongMaxPlaysPerSection) {
            warn(warnings, "section \"" + name + "\" has more than " + juce::String(kSongMaxPlaysPerSection) +
                               " plays; the rest were skipped.");
            break;
        }
        Play play;
        if (!readPlay((*plays)[i], name, patterns, play, warnings))
            continue;
        const double fillStart = play.fill != nullptr ? std::max(start, end - play.fill->bars * kBeatsPerBar) : end;
        std::vector<SongNote> notes;
        tile(*play.main, start, fillStart, play, notes);
        if (play.fill != nullptr)
            tile(*play.fill, fillStart, end, play, notes);
        auto found = out.index.find(play.main->trackKey);
        if (found == out.index.end()) {
            found = out.index.emplace(play.main->trackKey, out.perTrack.size()).first;
            out.perTrack.push_back({play.main->track, {}});
        }
        auto& dest = out.perTrack[found->second].second;
        dest.insert(dest.end(), notes.begin(), notes.end());
    }
}

juce::var makeClip(const juce::String& name, double start, double lengthBeats, std::vector<SongNote>& notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const SongNote& a, const SongNote& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch < b.pitch;
    });
    juce::Array<juce::var> noteVars;
    for (const SongNote& n : notes)
        noteVars.add(makeNote(n.startBeat - start, n.lengthBeats, n.pitch, n.velocity));
    auto* clip = new juce::DynamicObject();
    clip->setProperty("startBeat", start);
    clip->setProperty("lengthBeats", lengthBeats);
    clip->setProperty("name", name);
    clip->setProperty("notes", noteVars);
    return juce::var(clip);
}

juce::var makeOp(const juce::String& op) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("op", op);
    return juce::var(obj);
}

// The song's sections walked in order: markers and per-track clips, stopping at the bar budget.
void walkSections(const juce::var& rawSections, const std::map<juce::String, Pattern>& patterns,
                  juce::Array<juce::var>& markers, std::vector<TrackClips>& tracks,
                  std::map<juce::String, size_t>& trackIndex, juce::StringArray* warnings) {
    const juce::Array<juce::var>* sections = rawSections.getArray();
    if (sections == nullptr || sections->isEmpty()) {
        warn(warnings, "the song has no sections.");
        return;
    }
    double sectionStart = 0.0;
    int totalBars = 0;
    for (int s = 0; s < sections->size(); ++s) {
        const juce::var& raw = (*sections)[s];
        if (s >= kSongMaxSections) {
            warn(warnings, "more than " + juce::String(kSongMaxSections) + " sections; the rest were skipped.");
            break;
        }
        const juce::String name = raw["name"].isString() && raw["name"].toString().isNotEmpty()
                                      ? raw["name"].toString()
                                      : "Section " + juce::String(s + 1);
        const double barsValue = isNumber(raw["bars"]) ? static_cast<double>(raw["bars"]) : 0.0;
        if (!isWhole(barsValue) || barsValue < 1 || barsValue > kSongSectionMaxBars) {
            warn(warnings, "section \"" + name + "\" needs bars from 1 to " + juce::String(kSongSectionMaxBars) +
                               " and was skipped.");
            continue;
        }
        const int bars = static_cast<int>(barsValue);
        if (totalBars + bars > kSongMaxTotalBars) {
            warn(warnings, "the sections add up to more than " + juce::String(kSongMaxTotalBars) + " bars; section \"" +
                               name + "\" and the rest were skipped.");
            break;
        }
        totalBars += bars;
        const double sectionEnd = sectionStart + bars * kBeatsPerBar;
        const juce::var marker = makeOp("addMarker");
        marker.getDynamicObject()->setProperty("beat", sectionStart);
        marker.getDynamicObject()->setProperty("name", name);
        markers.add(marker);

        SectionNotes notes;
        playSection(raw["play"], name, sectionStart, sectionEnd, patterns, notes, warnings);
        for (auto& [track, trackNotes] : notes.perTrack) {
            if (trackNotes.empty())
                continue;
            if (static_cast<int>(trackNotes.size()) > kTimelineOpsMaxNotesPerClip) {
                warn(warnings,
                     "section \"" + name + "\" expands to " + juce::String(static_cast<int>(trackNotes.size())) +
                         " notes on one track, over the limit of " + juce::String(kTimelineOpsMaxNotesPerClip) +
                         "; that track was skipped for the section.");
                continue;
            }
            const juce::String key = juce::JSON::toString(track, true);
            auto found = trackIndex.find(key);
            if (found == trackIndex.end()) {
                found = trackIndex.emplace(key, tracks.size()).first;
                tracks.push_back({track, {}});
            }
            tracks[found->second].clips.add(makeClip(name, sectionStart, bars * kBeatsPerBar, trackNotes));
        }
        sectionStart = sectionEnd;
    }
}

bool readBpm(const juce::var& song, int& bpm, juce::StringArray* warnings) {
    const juce::var raw = song["bpm"];
    if (isNumber(raw)) {
        const double rounded = std::floor(static_cast<double>(raw) + 0.5);
        if (rounded >= kSongMinBpm && rounded <= kSongMaxBpm) {
            bpm = static_cast<int>(rounded);
            return true;
        }
    }
    warn(warnings, "bpm must be a number from " + juce::String(kSongMinBpm) + " to " + juce::String(kSongMaxBpm) +
                       "; no tempo was set.");
    return false;
}

} // namespace

bool parseSongNotes(const juce::String& text, int bars, const juce::String& patternId, std::vector<SongNote>& notes,
                    juce::String& error) {
    const juce::StringArray tokens = splitOnWhitespace(text);
    const int limit = bars * kBeatsPerBar;
    for (int i = 0; i < tokens.size(); ++i) {
        const juce::String where =
            "Pattern \"" + patternId + "\" note " + juce::String(i + 1) + " \"" + tokens[i] + "\"";
        const juce::StringArray parts = splitOnColon(tokens[i]);
        bool numeric = parts.size() >= 3 && parts.size() <= 4;
        for (const juce::String& part : parts)
            numeric = numeric && isPlainNumber(part);
        if (!numeric) {
            error = where + " is not start:length:pitch[:velocity].";
            return false;
        }
        const double start = parts[0].getDoubleValue();
        const double length = parts[1].getDoubleValue();
        const double pitch = parts[2].getDoubleValue();
        const bool hasVelocity = parts.size() == 4;
        const double velocity = hasVelocity ? parts[3].getDoubleValue() : 100.0;
        if (start < 0 || start >= limit) {
            error = where + " starts at beat " + juce::String(start) + ", but the pattern is " + juce::String(bars) +
                    " bars (" + juce::String(limit) +
                    " beats) long. A note's start is relative to the pattern and "
                    "must be from 0 up to but not including " +
                    juce::String(limit) + ".";
            return false;
        }
        if (length <= 0) {
            error = where + " has length " + juce::String(length) + "; the length in beats must be above 0.";
            return false;
        }
        if (!isWhole(pitch) || pitch < 0 || pitch > 127) {
            error = where + " has pitch " + juce::String(pitch) +
                    "; the pitch must be a whole MIDI note number from 0 to 127.";
            return false;
        }
        if (hasVelocity && (!isWhole(velocity) || velocity < 1 || velocity > 127)) {
            error = where + " has velocity " + juce::String(velocity) +
                    "; the velocity must be a whole number from 1 to 127 (or left out for 100).";
            return false;
        }
        notes.push_back({start, length, static_cast<int>(pitch), static_cast<int>(velocity)});
    }
    if (static_cast<int>(notes.size()) > kTimelineOpsMaxNotesPerClip) {
        error = "Pattern \"" + patternId + "\" has " + juce::String(static_cast<int>(notes.size())) +
                " notes; the most one pattern may have is " + juce::String(kTimelineOpsMaxNotesPerClip) + ".";
        return false;
    }
    return true;
}

juce::var expandSong(const juce::var& envelope, juce::StringArray* warnings) {
    if (!envelope.isObject() || !envelope.hasProperty("song"))
        return envelope;

    auto* result = new juce::DynamicObject();
    for (const auto& property : envelope.getDynamicObject()->getProperties())
        if (property.name.toString() != "song")
            result->setProperty(property.name, property.value);
    const juce::var out(result);
    const juce::var song = envelope["song"];
    if (!song.isObject()) {
        warn(warnings, "\"song\" is not an object and was ignored.");
        return out;
    }

    const std::map<juce::String, Pattern> patterns = readPatterns(song["patterns"], warnings);
    juce::Array<juce::var> markers;
    std::vector<TrackClips> tracks;
    std::map<juce::String, size_t> trackIndex;
    walkSections(song["sections"], patterns, markers, tracks, trackIndex, warnings);

    juce::Array<juce::var> ops;
    if (const juce::Array<juce::var>* own = envelope["timelineOps"].getArray())
        ops.addArray(*own);
    int bpm = 0;
    if (readBpm(song, bpm, warnings)) {
        const juce::var tempo = makeOp("setTempo");
        tempo.getDynamicObject()->setProperty("bpm", bpm);
        ops.add(tempo);
    }
    ops.addArray(markers);
    for (const TrackClips& track : tracks) {
        if (track.clips.size() > kTimelineOpsMaxClipsPerOp) {
            warn(warnings, "a track expands to " + juce::String(track.clips.size()) + " clips, over the limit of " +
                               juce::String(kTimelineOpsMaxClipsPerOp) + "; it was skipped.");
            continue;
        }
        const juce::var place = makeOp("placeClips");
        place.getDynamicObject()->setProperty("track", track.track);
        place.getDynamicObject()->setProperty("clips", track.clips);
        ops.add(place);
    }
    if (ops.size() > kTimelineOpsMaxOps) {
        warn(warnings, "the song and the other timeline ops come to " + juce::String(ops.size()) +
                           " ops, over the limit of " + juce::String(kTimelineOpsMaxOps) +
                           "; the last ones were dropped.");
        ops.removeLast(ops.size() - kTimelineOpsMaxOps);
    }
    if (!ops.isEmpty())
        result->setProperty("timelineOps", ops);
    return out;
}

} // namespace synth::projectgenerate
