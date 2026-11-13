#pragma once

// Shared fixture for the section tests (TimelineDocSection*Tests.cpp). Header-only.

#include "TimelineDocTestHelpers.h"

#include <map>

using synth::PlacementId;
using synth::SectionDropMode;
using synth::SectionId;

constexpr double kSpb = 0.5;

// A two-track song: an 8-beat verse on A (two clips) and B (one clip), a lane on A with a point in each half, a
// "Tail" clip after it, a marker and a lane point past the verse.
struct SectionSong {
    TrackId a;
    TrackId b;
    ClipId c0;
    ClipId c1;
    ClipId bass;
    ClipId tail;
    LaneId lane;
    MarkerId tailMarker;
};

inline SectionSong buildSectionSong(TimelineDoc& doc) {
    SectionSong s;
    s.a = doc.addTrack(TrackKind::Midi, "A");
    s.b = doc.addTrack(TrackKind::Midi, "B");
    s.c0 = doc.addClip(s.a, 0.0, 4.0, "c0");
    doc.addNote(s.c0, makeNote(0.0, 60));
    doc.addNote(s.c0, makeNote(2.0, 64));
    s.c1 = doc.addClip(s.a, 4.0, 4.0, "c1");
    doc.addNote(s.c1, makeNote(1.0, 67));
    s.bass = doc.addClip(s.b, 0.0, 8.0, "bass");
    doc.addNote(s.bass, makeNote(0.0, 36, 2.0));
    s.tail = doc.addClip(s.a, 8.0, 4.0, "tail");
    s.lane = doc.addLane(s.a, "uuid", "cut", makeRange(0.0f, 1000.0f, 0.0f));
    doc.addBreakpoint(s.lane, 1.0, 100.0);
    doc.addBreakpoint(s.lane, 5.0, 200.0);
    doc.addBreakpoint(s.lane, 9.0, 300.0);
    s.tailMarker = doc.addMarker(8.0, "Tail", 0xff000000);
    return s;
}

inline std::vector<const Clip*> clipsOn(const TimelineDoc& doc, TrackId track, double from, double to) {
    std::vector<const Clip*> out;
    if (const auto* t = doc.getTrack(track))
        for (const auto& clip : t->clips)
            if (clip.startBeat >= from && clip.startBeat < to)
                out.push_back(&clip);
    return out;
}

// Clip start beats on a track, in order.
inline std::vector<double> startsOn(const TimelineDoc& doc, TrackId track) {
    std::vector<double> out;
    if (const auto* t = doc.getTrack(track))
        for (const auto& clip : t->clips)
            out.push_back(clip.startBeat);
    return out;
}

inline std::vector<double> pointBeats(const TimelineDoc& doc, LaneId lane) {
    std::vector<double> out;
    if (const auto* l = doc.getLane(lane))
        for (const auto& p : l->points)
            out.push_back(p.beat);
    return out;
}

// True when every placement of the section holds the same music (ids ignored).
inline bool placementsAgree(const TimelineDoc& doc, SectionId section) {
    const auto ids = doc.placementsOf(section);
    const double length = doc.getSection(section)->lengthBeats;
    const auto contentOf = [&](double start) {
        synth::PlacementContent content;
        for (const auto& track : doc.getTracks()) {
            for (const auto& clip : track.clips)
                if (clip.startBeat >= start && clip.startBeat < start + length) {
                    Clip copy = clip;
                    copy.startBeat -= start;
                    content.clips[track.id.value].push_back(copy);
                }
            for (const auto& lane : track.lanes)
                for (auto point : lane.points)
                    if (point.beat >= start && point.beat < start + length) {
                        point.beat -= start;
                        content.points[lane.id.value].push_back(point);
                    }
        }
        return content;
    };
    const auto first = contentOf(doc.getPlacement(ids.front())->startBeat);
    for (const auto id : ids)
        if (!synth::samePlacementContent(first, contentOf(doc.getPlacement(id)->startBeat)))
            return false;
    return true;
}

// Verse [0,8) repeated three times (placements at 0, 8, 16), the tail pushed to 24.
struct ThreeVerses {
    SectionSong song;
    SectionId verse;
    PlacementId p1, p2, p3;
};

inline ThreeVerses buildThreeVerses(TimelineDoc& doc) {
    ThreeVerses t;
    t.song = buildSectionSong(doc);
    t.verse = doc.makeSection(0.0, 8.0, "Verse", 0xff112233, kSpb);
    t.p2 = doc.placeSection(t.verse, 8.0, SectionDropMode::MakeRoom, kSpb);
    t.p3 = doc.placeSection(t.verse, 16.0, SectionDropMode::MakeRoom, kSpb);
    t.p1 = doc.placementsOf(t.verse).front();
    return t;
}
