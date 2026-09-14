#pragma once

// Shared helpers used by more than one TimelineDoc<Concern>.cpp unit: ordering comparators and
// validation predicates that a mutation path and the fromVar() loader must agree on exactly.
// Everything here is an implementation detail of the split, never included from outside this
// directory — the class itself is declared in TimelineDoc.h.

#include "TimelineDoc.h"

#include <cmath>

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

} // namespace detail
} // namespace synth
