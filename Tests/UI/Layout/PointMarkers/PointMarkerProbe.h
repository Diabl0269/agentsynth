// PointMarkerProbe.h -- records every marker a component paints (each ellipse, rounded rectangle and rectangle, its
// children's included, in the component's own coordinates) so a test can ask whether everything drawn around a point
// sits on that point's drawn dot. Nothing opts in: a new ring, halo or focus stop painted near a point is checked the
// day it is added. Focus rings paint as if focused while it renders (synth::ui::setFocusRingsShownForTest).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <vector>

namespace point_marker_test {

struct Mark {
    enum class Shape { ellipse, roundedRect, rect };
    Shape shape = Shape::ellipse;
    bool filled = false;
    juce::Rectangle<float> area;
    float alpha = 1.0f;

    /** A round or rounded square no bigger than a fingertip: a dot, its halo, a ring or a focus stop. */
    bool isPointMarker() const;
    std::string describe() const;
};

/** Paints `c` (and its children) once and returns every visible mark it made, in `c`'s coordinates. */
std::vector<Mark> paintMarks(juce::Component& c);

/** What was painted around one point. */
struct PointReport {
    bool dotFound = false;
    juce::Point<float> dot;   // the centre of the filled ellipse nearest the expected place
    int onDot = 0;            // point markers centred on the dot (the dot itself included)
    int ringsOnDot = 0;       // stroked markers centred on the dot that are bigger than it (rings, focus stops)
    std::vector<Mark> strays; // point markers near the dot or the stale place but not centred on the dot
};

/** Finds the point's dot (a filled ellipse within 1.5 px of `expected`) and every point marker within reach of the dot
 *  or of `stale` (where the point rests, or was before the move). */
PointReport inspectPoint(const std::vector<Mark>& marks, juce::Point<float> expected, juce::Point<float> stale);

/** gtest checks: the dot is at `expected`, nothing near it or near `stale` is off it, and at least `minRings` rings
 *  (focus ring, selection ring, halo) sit on it. */
void expectMarkersOnDot(const std::vector<Mark>& marks, juce::Point<float> expected, juce::Point<float> stale,
                        int minRings, const std::string& what);

/** The biggest stroked rectangle (a selection box): centred on the bounds of `dots` within a pixel. */
void expectBoxAroundDots(const std::vector<Mark>& marks, const std::vector<juce::Point<float>>& dots,
                         const std::string& what);

} // namespace point_marker_test
