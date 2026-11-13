#pragma once

// ReachSweepRings.h -- the ring sources of the reach sweep (ReachSweepWalk.h): each thing that paints a ring over the
// stops of a focus region, read by the walker after every key. The mixer's are defined in KeyboardReachTests.cpp, which
// already builds against the mixer panel, so the sweep itself does not widen the mixer headers' include reach.
#include "UI/Layout/KeyRing.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <vector>

class MainComponent;

namespace synth::test::reach {

// One thing that paints a ring over the stops of a region.
struct RingSource {
    std::string name;
    juce::Component* owner = nullptr; // focus must be inside it while the ring shows
    std::function<bool()> active;
    std::function<std::vector<juce::Component*>()> stops;
    std::function<juce::Component*()> ringed; // the stop the ring is on, null when it is on none
};

namespace detail {

inline RingSource keyRingSource(const char* name, synth::ui::KeyRing& ring, juce::Component& owner) {
    RingSource s;
    s.name = name;
    s.owner = &owner;
    s.active = [&ring] { return ring.isActive(); };
    s.stops = [&ring] { return ring.getStops(); };
    s.ringed = [&ring]() -> juce::Component* { return ring.isActive() ? ring.getRingedStop() : nullptr; };
    return s;
}

} // namespace detail

/** The mixer panel's rings: its top bar KeyRing, the focused column's outline and the zones pane's cursor row. */
std::vector<RingSource> mixerRingSources(juce::Component& mixerPanel);
/** True when `c` sits in a mixer strip, Direct or Master column, whose controls are reached by the column's own keys.
 */
bool insideMixerColumn(juce::Component& c);
/** Opens or closes the mixer panel's side pane (the zones and visibility list). */
void setMixerSidePaneOpen(MainComponent& mc, bool open);

} // namespace synth::test::reach
