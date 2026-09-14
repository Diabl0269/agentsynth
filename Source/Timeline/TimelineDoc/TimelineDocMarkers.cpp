// Concern: markers.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>

namespace synth {

using namespace detail;

// -------------------------------------------------------------------- markers --

MarkerId TimelineDoc::addMarker(double beat, const juce::String& text, juce::uint32 colourArgb) {
    if (!isFiniteAtOrAfterZero(beat) || !isValidMarkerText(text))
        return {};
    if (static_cast<int>(markers.size()) >= kMaxMarkers)
        return {};

    return applyMutation([&] {
        Marker marker;
        marker.id = MarkerId{nextMarkerId++};
        marker.beat = beat;
        marker.text = text;
        marker.colourArgb = colourArgb;
        const auto pos = std::lower_bound(markers.begin(), markers.end(), marker, markerLess);
        return markers.insert(pos, std::move(marker))->id;
    });
}

bool TimelineDoc::removeMarker(MarkerId id) {
    auto* marker = findMarker(id);
    if (marker == nullptr)
        return false;

    return applyMutation([&] {
        markers.erase(markers.begin() + (marker - markers.data()));
        return true;
    });
}

bool TimelineDoc::setMarkerText(MarkerId id, const juce::String& text) {
    auto* marker = findMarker(id);
    if (marker == nullptr || !isValidMarkerText(text))
        return false;
    if (marker->text == text)
        return true; // already there: no revision bump, no notification
    return applyMutation([&] {
        marker->text = text;
        return true;
    });
}

bool TimelineDoc::setMarkerColour(MarkerId id, juce::uint32 colourArgb) {
    auto* marker = findMarker(id);
    if (marker == nullptr)
        return false;
    if (marker->colourArgb == colourArgb)
        return true;
    return applyMutation([&] {
        marker->colourArgb = colourArgb;
        return true;
    });
}

bool TimelineDoc::moveMarker(MarkerId id, double newBeat) {
    auto* marker = findMarker(id);
    if (marker == nullptr || !isFiniteAtOrAfterZero(newBeat))
        return false;
    if (marker->beat == newBeat)
        return true;

    return applyMutation([&] {
        // Copy out, erase, re-insert at the sorted position — the same re-seat moveClip does, so
        // the (beat, id) order holds without re-sorting the whole vector.
        Marker moved = *marker;
        moved.beat = newBeat;
        markers.erase(markers.begin() + (marker - markers.data()));
        const auto pos = std::lower_bound(markers.begin(), markers.end(), moved, markerLess);
        markers.insert(pos, std::move(moved));
        return true;
    });
}

} // namespace synth
