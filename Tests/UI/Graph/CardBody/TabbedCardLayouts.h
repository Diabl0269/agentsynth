// TabbedCardLayouts.h -- the tabbed layouts the Pluck, Mallet and Wavetable cards had before they moved to lanes.
// No default card layout uses tabs any more; tests of tab behaviour (cables parking on a tab button, a tab's controls
// in the on-card editor) store one of these as the card's own layout, the way a saved tabbed card would carry it.
#pragma once

#include "Modules/CardLayout.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace cardbody_test {

inline synth::CardLayout tabbedLayout(const juce::String& type) {
    using namespace synth::cardlayout;
    synth::CardLayout layout;
    if (type == "Pluck")
        layout.sections = {
            section("output", std::nullopt, {param("level")}, 1),
            tab("string", "String", {param("decay"), param("release"), param("brightness"), param("stiffness")}, 4),
            tab("pick", "Pick", {param("pickPosition"), param("pickHardness"), param("velBright")}),
            tab("tune", "Tune", {param("tune"), param("fine"), param("body"), param("decayTrack")}, 4),
            footer({}),
        };
    else if (type == "Mallet")
        layout.sections = {
            section("material", std::nullopt, {param("material")}),
            section("output", std::nullopt, {param("level")}, 1),
            tab("bar", "Bar", {param("decay"), param("damping"), param("brightness"), param("release")}, 4),
            tab("mallet", "Mallet", {param("hardness"), param("velBright"), param("resonator"), param("tremolo")}, 4),
            tab("tune", "Tune", {param("tune"), param("fine")}),
            footer({}),
        };
    else
        layout.sections = {
            section("perform", std::nullopt, {param("warp"), param("position"), param("warpAmount")}, 1),
            tab("tune", "Tune", {param("octave"), param("coarse"), param("fine"), param("level"), param("pan")}),
            tab("unison", "Unison", {param("stack"), param("unison"), param("detune"), param("width"), param("blend")},
                2),
            tab("phase", "Phase", {param("syncMode"), param("phase"), param("randomPhase"), param("spread")}, 2),
            tab("sub", "Sub", {param("subOctave"), param("subShape"), param("subLevel")}, 2),
            tab("file", "File", {param("importMode"), param("interpolation")}),
            footer({}),
        };
    return layout;
}

/** True when `stored` is still exactly the tabbed layout the card was given: nothing the editor did is left. */
inline bool isUntouchedTabbed(const std::optional<synth::CardLayout>& stored, const juce::String& type) {
    if (!stored.has_value())
        return false;
    auto given = *stored;
    given.basedOn = {}; // saving stamps the revision the layout was drawn on; the editor must add nothing else
    return juce::JSON::toString(given.toVar()) == juce::JSON::toString(tabbedLayout(type).toVar());
}

} // namespace cardbody_test
