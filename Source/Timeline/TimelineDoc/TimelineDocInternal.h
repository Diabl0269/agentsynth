#pragma once

// Shared helpers used by more than one TimelineDoc<Concern>.cpp unit: ordering comparators and
// validation predicates that a mutation path and the fromVar() loader must agree on exactly.
// Everything here is an implementation detail of the split, never included from outside this
// directory — the class itself is declared in TimelineDoc.h.

#include "TimelineDoc.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace synth {
namespace detail {

// -- ordering invariants ------------------------------------------------------
// The two comparators the whole file sorts by. Kept here, in one place, because a mutation
// path and the loader that repairs a hand-edited file must agree on the order exactly.

inline bool clipLess(const Clip& a, const Clip& b) {
    if (a.startBeat != b.startBeat)
        return a.startBeat < b.startBeat;
    return a.id.value < b.id.value;
}

inline bool markerLess(const Marker& a, const Marker& b) {
    if (a.beat != b.beat)
        return a.beat < b.beat;
    return a.id.value < b.id.value;
}

inline bool noteLess(const MidiNote& a, const MidiNote& b) {
    if (a.startBeat != b.startBeat)
        return a.startBeat < b.startBeat;
    if (a.pitch != b.pitch)
        return a.pitch < b.pitch;
    return a.id.value < b.id.value;
}

// -- note partitioning --------------------------------------------------------
// THE rule for cutting a clip's notes at a clip-relative beat, shared by splitClip and every
// range edit (trimClipStart/trimClipEnd/clipToRange) so a cut can never mean two different things.
// Notes ending at or before `atBeat` go left untouched; notes starting at or after it go right,
// re-based so `atBeat` becomes beat 0; a straddling note is cut in two — the left half keeps its
// id and ends at the boundary, the right half takes `freshId(original)` and starts at 0 (a caller
// that discards one side passes a lambda returning original.id, so the survivor keeps it). Both
// outputs come back sorted by noteLess (a straddler's right half can tie a note at beat 0).
template <typename FreshId>
inline void partitionNotesAt(const std::vector<MidiNote>& notes, double atBeat, FreshId&& freshId,
                             std::vector<MidiNote>& left, std::vector<MidiNote>& right) {
    left.clear();
    right.clear();
    left.reserve(notes.size());
    right.reserve(notes.size());
    for (const auto& note : notes) {
        const double noteEnd = note.startBeat + note.lengthBeats;
        if (noteEnd <= atBeat) {
            left.push_back(note);
        } else if (note.startBeat >= atBeat) {
            MidiNote moved = note;
            moved.startBeat -= atBeat;
            right.push_back(moved);
        } else {
            MidiNote leftHalf = note;
            leftHalf.lengthBeats = atBeat - note.startBeat;
            left.push_back(leftHalf);

            MidiNote rightHalf = note;
            rightHalf.id = freshId(note);
            rightHalf.startBeat = 0.0;
            rightHalf.lengthBeats = noteEnd - atBeat;
            right.push_back(rightHalf);
        }
    }
    std::stable_sort(left.begin(), left.end(), noteLess);
    std::stable_sort(right.begin(), right.end(), noteLess);
}

// -- validation ---------------------------------------------------------------
// Non-finite values are rejected everywhere, not just because they're meaningless musically:
// a NaN beat makes every comparator above non-transitive, which is undefined behaviour for
// std::sort and std::lower_bound.

inline bool isFiniteAtOrAfterZero(double v) noexcept { return std::isfinite(v) && v >= 0.0; }
inline bool isFinitePositive(double v) noexcept { return std::isfinite(v) && v > 0.0; }

inline bool isValidNote(const MidiNote& note) noexcept {
    return isFiniteAtOrAfterZero(note.startBeat) && isFinitePositive(note.lengthBeats) && note.pitch >= 0 &&
           note.pitch <= 127 && note.velocity >= 1 && note.velocity <= 127 && note.channel >= 1 && note.channel <= 16;
}

// The asset-reference rule, in one place because setClipAsset and fromVar must agree
// EXACTLY — a path the mutation API refuses must not be loadable from a file, or a hand-edited
// bundle becomes the way around the check. See Clip::assetRef for the threat.
//
// Rejected: a leading '/' or '\' (absolute POSIX / UNC), a Windows drive letter ("C:..."), any
// segment that is exactly ".." (escapes the bundle root), and any embedded NUL. Everything else —
// including a plain file name with no directory — is accepted, because a bundle-relative path is
// resolved against the bundle root and nothing else.
inline bool isValidAssetRefString(const juce::String& ref) noexcept {
    if (ref.isEmpty())
        return true; // "no asset": what every MIDI clip carries

    if (ref.containsChar('\0'))
        return false;
    const juce::juce_wchar first = ref[0];
    if (first == '/' || first == '\\')
        return false;
    // "C:", "C:/", "C:\..." — a drive-relative path is absolute enough to escape the bundle.
    if (ref.length() >= 2 && ref[1] == ':')
        return false;

    // Both separators are checked: a bundle written on Windows and opened on macOS must be
    // rejected by the same rule, not merely mis-resolved.
    juce::StringArray segments;
    segments.addTokens(ref.replaceCharacter('\\', '/'), "/", {});
    for (const auto& segment : segments)
        if (segment == "..")
            return false;

    return true;
}

// A marker label is capped but never trimmed or rewritten — see TimelineDoc::addMarker.
inline bool isValidMarkerText(const juce::String& text) noexcept {
    return text.length() <= TimelineDoc::kMaxMarkerTextLength;
}

inline bool isValidRange(const AutomationLane::RangeSnapshot& range) noexcept {
    return std::isfinite(range.minValue) && std::isfinite(range.maxValue) && std::isfinite(range.defaultValue) &&
           range.minValue <= range.maxValue;
}

inline bool isValidCurve(int curve) noexcept {
    return curve >= static_cast<int>(BreakpointCurve::Hold) && curve <= static_cast<int>(BreakpointCurve::Bezier);
}

inline bool isValidRecordMode(int mode) noexcept {
    return mode >= static_cast<int>(LaneRecordMode::Off) && mode <= static_cast<int>(LaneRecordMode::Write);
}

inline AutomationLane::Breakpoint makeBreakpoint(const AutomationLane::RangeSnapshot& range, double beat, double value,
                                                 float tension, int curve) {
    AutomationLane::Breakpoint point;
    point.beat = beat;
    // Values are stored denormalised, so the lane's captured range is the only thing that can
    // bound them.
    point.value = juce::jlimit(static_cast<double>(range.minValue), static_cast<double>(range.maxValue), value);
    point.tension = juce::jlimit(-1.0f, 1.0f, tension);
    point.curve = curve;
    return point;
}

// -- controller (CC) lanes ------------------------------------------------------
// Shared by TimelineDocControllers.cpp (the mutation API) and the fromVar loader, which must agree
// exactly on what a legal CC lane is.

inline bool isValidCcNumber(int cc) noexcept { return cc >= 0 && cc <= 127; }

// Hold or Linear only. Bezier (2) is reserved for automation lanes and has no CC meaning, so a CC
// lane never stores it — refusing it here keeps the snapshot free of a curve nobody evaluates.
inline bool isValidControllerCurve(int curve) noexcept {
    return curve == static_cast<int>(BreakpointCurve::Hold) || curve == static_cast<int>(BreakpointCurve::Linear);
}

inline bool isValidControllerPoint(const ControllerPoint& point) noexcept {
    return isFiniteAtOrAfterZero(point.beat) && std::isfinite(point.value) && isValidControllerCurve(point.curve);
}

// Sorts by beat, collapses same-beat duplicates (the LAST one wins, the same rule a repeated
// addBreakpoint follows) and clamps every value into the 7-bit CC range. Callers validate first.
inline std::vector<ControllerPoint> normalisedControllerPoints(std::vector<ControllerPoint> points) {
    std::stable_sort(points.begin(), points.end(),
                     [](const ControllerPoint& a, const ControllerPoint& b) { return a.beat < b.beat; });
    std::vector<ControllerPoint> out;
    out.reserve(points.size());
    for (auto point : points) {
        point.value = juce::jlimit(0.0, 127.0, point.value);
        if (!out.empty() && out.back().beat == point.beat)
            out.back() = point;
        else
            out.push_back(point);
    }
    return out;
}

inline bool controllerLaneLess(const ClipControllerLane& a, const ClipControllerLane& b) noexcept {
    return a.ccNumber < b.ccNumber;
}

// The lane's value at a clip-relative beat, with the audio thread's semantics (AutomationKernel):
// flat outside the lane's own span, a segment shaped by its LEFT point (Hold keeps the left value,
// Linear lerps). `curveOut` receives the shaping curve in force at `beat`. Message thread only —
// the structural edits (split) use it to cut a lane without changing what it plays.
inline double controllerValueAt(const std::vector<ControllerPoint>& points, double beat, int& curveOut) noexcept {
    curveOut = static_cast<int>(BreakpointCurve::Linear);
    if (points.empty())
        return 0.0;
    if (beat <= points.front().beat) {
        curveOut = points.front().curve;
        return points.front().value;
    }
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (beat < b.beat) {
            curveOut = a.curve;
            if (a.curve == static_cast<int>(BreakpointCurve::Hold) || !(b.beat > a.beat))
                return a.value;
            return a.value + (b.value - a.value) * ((beat - a.beat) / (b.beat - a.beat));
        }
    }
    curveOut = points.back().curve;
    return points.back().value;
}

// Structural-edit halves for CC lanes, defined in TimelineDocControllers.cpp beside their contracts.
void splitControllerLanes(const std::vector<ClipControllerLane>& lanes, double atBeat,
                          std::vector<ClipControllerLane>& leftOut, std::vector<ClipControllerLane>& rightOut);
bool mergeControllerLanes(const std::vector<ClipControllerLane>& a, const std::vector<ClipControllerLane>& b,
                          double rebaseB, std::vector<ClipControllerLane>& out);

} // namespace detail
} // namespace synth
