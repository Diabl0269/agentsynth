// DefaultCardLayoutsSources.cpp -- the code-default card layouts of Oscillator, Noise, Sampler and LFO.
// Each type registers with defaults.add(type, layout, revision, dimRules), built with the cardlayout::
// helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// The Wavetable card is bespoke (cardBodyLayoutIsDataDriven is false for it) and has no entry here: its
// tab strip is drawn from `tab` sections in a later change.
// No view is placed in any of these: the Sampler's waveform and load row and the LFO's custom-wave editor
// are card chrome (ModuleComponent) that already draws where the design puts them, so a registered copy
// would be a second, unbound panel.
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

} // namespace

void registerSourceCardLayouts(DefaultCardLayouts& defaults) {
    defaults.add("Oscillator", oscillatorLayout(), kRevision, {detuneAtOneVoice()});
    defaults.add("Noise", noiseLayout(), kRevision);
    defaults.add("Sampler", samplerLayout(), kRevision);
    defaults.add("LFO", lfoLayout(), kRevision);
}

} // namespace synth
