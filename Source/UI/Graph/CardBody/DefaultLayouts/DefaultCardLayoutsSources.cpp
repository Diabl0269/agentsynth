// DefaultCardLayoutsSources.cpp -- the code-default card layouts of Oscillator, Noise, Sampler, Drum Kit, LFO
// and Wavetable. Each type registers with defaults.add(type, layout, revision, dimRules), built with the
// cardlayout:: helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// No view is placed in any of these: the Sampler's waveform and load row, the Wavetable's display, Table
// selector and load row, and the LFO's custom-wave editor are card chrome (ModuleComponent) that already
// draws where the design puts them, so a registered copy would be a second, unbound panel.
// docs/layout/module-card-layout.md#default-layouts.
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

constexpr int kRevision = 1;

// A row of four knobs (Oscillator Pitch, LFO shaping) on one line.
constexpr int kQuadColumns = 4;

CardLayout oscillatorLayout() {
    CardLayout layout;
    layout.sections = {
        section("waveform", std::nullopt, {param("waveform", CardWidget::Segmented)}),
        section("pitch", juce::String("Pitch"), {param("octave"), param("coarse"), param("fine"), param("glide")},
                kQuadColumns),
        section("unison", juce::String("Unison"),
                {param("unison"), param("detune"), dimUnless(param("pulseWidth"), "waveform", {"Square"})}),
        section("output", juce::String("Output"), {param("level"), param("pan")}),
        footer({}),
    };
    return layout;
}

// Detune does nothing with one voice; the rule is numeric, so it lives in code, not in the stored layout.
CardDimRule detuneAtOneVoice() {
    return CardDimRule{"detune", {"unison"}, [](juce::AudioProcessor& module) {
                           auto* unison = findParameterByID(&module, "unison");
                           return unison != nullptr && unison->getValue() <= 0.0f;
                       }};
}

CardLayout noiseLayout() {
    CardLayout layout;
    layout.sections = {
        section("type", std::nullopt, {param("noiseType", CardWidget::Segmented)}),
        section("output", std::nullopt, {param("color"), param("level")}),
        footer({}),
    };
    return layout;
}

// The waveform view and the load row are the card's chrome, drawn above the body. The grain controls
// stay on the card (their CV jacks are bound to them) and dim until the mode is Granular.
CardLayout samplerLayout() {
    CardLayout layout;
    layout.sections = {
        section("mode", std::nullopt, {param("playMode", CardWidget::Segmented)}),
        section("region", std::nullopt, {param("start"), param("end"), param("level")}),
        section("pitch", juce::String("Pitch"), {param("pitch"), param("rootNote"), param("fine")}),
        section("grains", juce::String("Grains"),
                {dimUnless(param("grainSize"), "playMode", {"Granular"}),
                 dimUnless(param("density"), "playMode", {"Granular"}),
                 dimUnless(param("spray"), "playMode", {"Granular"})}),
        footer({param("loop"), param("reverse")}),
    };
    return layout;
}

// The master Level stays above a strip of seven tabs, one per drum group (level, tune, decay; the hats add
// their two decays), so the card is no taller than a Noise card plus one row of knobs.
CardLayout drumKitLayout() {
    CardLayout layout;
    layout.sections = {
        section("output", std::nullopt, {param("level")}, 1),
        tab("kick", "Kick", {param("kickLevel"), param("kickTune"), param("kickDecay")}),
        tab("snare", "Snare", {param("snareLevel"), param("snareTune"), param("snareDecay")}),
        tab("clap", "Clap", {param("clapLevel"), param("clapTune"), param("clapDecay")}),
        tab("hats", "Hats", {param("hatLevel"), param("hatTune"), param("closedDecay"), param("openDecay")},
            kQuadColumns),
        tab("toms", "Toms", {param("tomLevel"), param("tomTune"), param("tomDecay")}),
        tab("cymbals", "Cymbals", {param("cymbalLevel"), param("cymbalTune"), param("cymbalDecay")}),
        tab("cowbell", "Bell", {param("cowbellLevel"), param("cowbellTune"), param("cowbellDecay")}),
        footer({}),
    };
    return layout;
}

// Shape stays a combo: six values (Sawtooth, Triangle) do not fit one 256 px switch.
// Rate is the one control the LFO is about: Hz and the tempo division share its cell, swapped by Sync.
// The custom-wave editor stays card chrome and opens under the body while the shape is Custom.
CardLayout lfoLayout() {
    CardLayout layout;
    layout.sections = {
        section("shape", std::nullopt, {param("shape")}),
        // One column, so the large Rate dial (or the division combo that replaces it) sits alone on its row.
        section("rate", std::nullopt,
                {param("mode", CardWidget::Segmented),
                 showWhen(param("rateHz", CardWidget::KnobLarge), "mode", {"false"}),
                 showWhen(param("rateSync"), "mode", {"true"})},
                1),
        section("shaping", std::nullopt,
                {param("phase"), param("fadeIn"), param("level"), dimUnless(param("glide"), "shape", {"S&H"})},
                kQuadColumns),
        footer({param("bipolar"), param("retrig")}),
    };
    return layout;
}

// Position and Warp are what you perform with, so they stay above the tabs; every other control is on
// one of five tabs. A tab's controls stack as every card's do (combos above knobs), and the strip is as
// tall as its tallest tab (Unison, Phase and Sub: a combo row over a knob row), so a tab switch never
// resizes the card. Knob cells are as wide as a tab's knob count allows.
CardLayout wavetableLayout() {
    CardLayout layout;
    layout.sections = {
        section("perform", std::nullopt, {param("warp"), param("position"), param("warpAmount")}, 1),
        tab("tune", "Tune", {param("octave"), param("coarse"), param("fine"), param("level"), param("pan")}),
        tab("unison", "Unison", {param("stack"), param("unison"), param("detune"), param("width"), param("blend")}, 2),
        tab("phase", "Phase", {param("syncMode"), param("phase"), param("randomPhase"), param("spread")}, 2),
        tab("sub", "Sub", {param("subOctave"), param("subShape"), param("subLevel")}, 2),
        tab("file", "File", {param("importMode"), param("interpolation")}),
        footer({}),
    };
    return layout;
}

} // namespace

void registerSourceCardLayouts(DefaultCardLayouts& defaults) {
    defaults.add("Oscillator", oscillatorLayout(), kRevision, {detuneAtOneVoice()});
    defaults.add("Noise", noiseLayout(), kRevision);
    defaults.add("Sampler", samplerLayout(), kRevision);
    defaults.add("Drum Kit", drumKitLayout(), kRevision);
    defaults.add("LFO", lfoLayout(), kRevision);
    defaults.add("Wavetable", wavetableLayout(), kRevision);
}

} // namespace synth
