// DefaultCardLayoutsSources.cpp -- the code-default card layouts of Oscillator, Noise, Sampler, Drum Kit, Pluck,
// Mallet, LFO and Wavetable. Each type registers with defaults.add(type, layout, revision, dimRules), built with the
// cardlayout:: helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// All are lane grids (docs/layout/module-card-layout.md#lane-presentation): a mode row (a switch or choice
// on the left; Poly and the card's Show Scope are pills at its right end), lanes by role (hero, pitch, shape, time,
// motion, voice) and the Out lane last (Pan, Level: a source has no Mix).
// No view is placed in any of these: the Sampler's waveform and load row, the Wavetable's display, Table
// selector and load row, the Drum Kit's pads and the LFO's custom-wave editor are card chrome (ModuleComponent)
// that already draws where the design puts them, so a registered copy would be a second, unbound panel.
// docs/layout/module-card-layout.md#default-layouts.
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

// Revision 2 is the lane grid for the Noise, Pluck, Mallet and Wavetable; the Oscillator (earlier: a Fixed rate in
// Glide's cell, Blend and Width), the Sampler (earlier: Instrument mode, then Glide and Mono) and the Drum Kit
// (earlier: group tabs, then the pads area) take the grid at 4. A stored layout based on an older revision is stale.
constexpr int kLaneRevision = 2;
constexpr int kOscillatorRevision = 4;
constexpr int kSamplerRevision = 4;
constexpr int kDrumKitRevision = 4;
constexpr int kLfoRevision = 2; // earlier: the old grid with a Free/Sync switch

CardLayout layoutOf(std::vector<CardSection> sections) {
    CardLayout layout;
    layout.sections = std::move(sections);
    return layout;
}

// A lane title that joins two roles, in the doc's role order.
juce::String joined(const char* first, const char* second) {
    return juce::String(first) + juce::String::fromUTF8(" \xc2\xb7 ") + second;
}

// Rate (Follow notes / Fixed) waits in the More row so the card keeps its size; a Fixed rate plays no notes to glide
// between, so the Fixed rate knob (captioned Fixed, to fit a knob's cell) takes Glide's cell while it is on. Phase,
// Rand, Glide Mode and Curve and Legato wait in the More row too.
CardLayout oscillatorLayout() {
    return layoutOf(
        {modeRow({param("waveform", CardWidget::Segmented), param("poly")}),
         lane("pitch", "Pitch",
              {param("octave"), param("coarse"), param("fine"), showWhen(param("glide"), "rateMode", {"Follow notes"}),
               showWhen(labelled(param("fixedRate"), "Fixed"), "rateMode", {"Fixed"})}),
         lane("shape-voice", joined("Shape", "Voice"),
              {labelled(dimUnless(param("pulseWidth"), "waveform", {"Square"}), "Pulse"), param("unison"),
               param("detune"), param("blend"), param("width")}),
         outLane({param("pan"), param("level")})});
}

// Detune, Blend and Width do nothing with one voice; the rule is numeric, so it lives in code, not in the stored
// layout.
CardDimRule dimAtOneVoice(const juce::String& paramId) {
    return CardDimRule{paramId, {"unison"}, [](juce::AudioProcessor& module) {
                           auto* unison = findParameterByID(&module, "unison");
                           return unison != nullptr && unison->getValue() <= 0.0f;
                       }};
}

CardLayout noiseLayout() {
    return layoutOf({modeRow({param("noiseType", CardWidget::Segmented), param("poly")}),
                     lane("shape", "Shape", {param("color")}), outLane({param("level")})});
}

// The Sampler's three modes share one card. The waveform (or the Instrument picker and key strip) and the load row
// are chrome drawn above the body. Each mode's controls fill the same cells, so a mode switch never resizes the card:
// Loop and Reverse swap with Release Sound and Mono in the mode row's right end (before Show Scope), Start and End
// swap with Attack and Release in the time lane, the three grain controls with Tone, Feel and Voices in the shape
// lane, and Root Note's cell holds Glide in Instrument mode. The grain controls stay on the card in Sample mode (their
// CV jacks are bound to them) and dim until the mode is Granular (a code rule: a swap member carries no second
// condition).
CardLayout samplerLayout() {
    const juce::StringArray sampled{"Sample", "Granular"};
    const juce::StringArray instrument{"Instrument"};
    return layoutOf(
        {modeRow({param("playMode", CardWidget::Segmented), showWhen(param("loop"), "playMode", sampled),
                  showWhen(param("releaseSound"), "playMode", instrument),
                  showWhen(param("reverse"), "playMode", sampled), showWhen(param("mono"), "playMode", instrument)}),
         lane("pitch", "Pitch",
              {param("pitch"), showWhen(param("rootNote"), "playMode", sampled),
               showWhen(param("glide"), "playMode", instrument), param("fine")}),
         lane("shape", "Shape",
              {showWhen(param("grainSize"), "playMode", sampled), showWhen(param("tone"), "playMode", instrument),
               showWhen(param("density"), "playMode", sampled), showWhen(param("feel"), "playMode", instrument),
               showWhen(param("spray"), "playMode", sampled), showWhen(param("voices"), "playMode", instrument)}),
         lane("time", "Time",
              {showWhen(param("start"), "playMode", sampled), showWhen(param("attack"), "playMode", instrument),
               showWhen(param("end"), "playMode", sampled), showWhen(param("release"), "playMode", instrument)}),
         outLane({param("level")})});
}

CardDimRule dimUnlessGranular(const juce::String& paramId) {
    return CardDimRule{paramId, {"playMode"}, [](juce::AudioProcessor& module) {
                           auto* mode =
                               dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&module, "playMode"));
                           return mode != nullptr && mode->getCurrentChoiceName() == "Sample";
                       }};
}

// Only the master Level is a body control: the pads area above it (DrumKitPadsSection) edits every pad, and the group
// parameters stay for automation and the AI. The mode row holds only the card's Show Scope pill.
CardLayout drumKitLayout() { return layoutOf({modeRow({}), outLane({param("level")})}); }

// The string (how long and how bright it rings), the pick (how it is struck) and the tuning with the body of the
// instrument, one lane each; the mode row holds only the card's Show Scope pill.
CardLayout pluckLayout() {
    return layoutOf({modeRow({}),
                     lane("pitch", "Pitch",
                          {param("tune"), param("fine"), param("body"), labelled(param("decayTrack"), "Decay Trk")}),
                     lane("shape-time", joined("Shape", "Time"),
                          {param("brightness"), param("stiffness"), param("decay"), param("release")}),
                     lane("voice", "Voice",
                          {labelled(param("pickPosition"), "Pick Pos"), labelled(param("pickHardness"), "Pick Hard"),
                           labelled(param("velBright"), "Vel Bright")}),
                     outLane({param("level")})});
}

// The material picks the bar (the mode row's switch); then the tuning, the bar (tone, decay, damping, release) and the
// mallet (contact, tube, tremolo), one lane each.
CardLayout malletLayout() {
    return layoutOf(
        {modeRow({param("material", CardWidget::Choice)}), lane("pitch", "Pitch", {param("tune"), param("fine")}),
         lane("shape-time", joined("Shape", "Time"),
              {param("brightness"), param("damping"), param("decay"), param("release")}),
         lane("voice", "Voice",
              {param("hardness"), labelled(param("velBright"), "Vel Bright"), param("resonator"), param("tremolo")}),
         outLane({param("level")})});
}

// The Custom wave's morph cursor: Morph X and Y show only while the shape is Custom (the Custom lane, between the
// roles and Out), so every other shape keeps the old card. Where the shapes' map and its Edit shapes button sit is
// decided separately.
CardSection morphLane() {
    auto morph = lane("custom", "Morph", {param("morphX"), param("morphY")});
    morph.visibleWhen = CardCondition{"shape", {"Custom"}, CardConditionEffect::Show};
    return morph;
}

// The Shape choice is the mode row's selector (six values do not fit one switch), with Bipolar and Retrig as pills at
// its right end. Rate is the one control the LFO is about, so it is the hero: its Sync button sits at the end of its
// caption (lit = the note division takes the dial's cell, off = Hz). Phase, Fade In and Smooth are the shape and time
// of the wave; Level is the Out lane. The custom-wave editor stays card chrome and opens under the body while the
// shape is Custom.
CardLayout lfoLayout() {
    return layoutOf(
        {modeRow({param("shape"), param("bipolar"), param("retrig")}),
         lane("hero", "Hero",
              {param("mode", CardWidget::Sync),
               showWhen(labelled(param("rateHz", CardWidget::KnobLarge), "Rate"), "mode", {"false"}),
               showWhen(labelled(param("rateSync"), "Rate"), "mode", {"true"})}),
         lane("shape-time", joined("Shape", "Time"),
              {param("phase"), labelled(param("fadeIn"), "Fade In"), dimUnless(param("glide"), "shape", {"S&H"})}),
         morphLane(), outLane({param("level")})});
}

// The Warp choice is the mode row's switch, with Sync In, Sub Oct and Sub Wave as inline choices at its right end
// (then Poly and Show Scope). Position is the hero; Warp Amount, Phase and Rand Phase are the shape; Octave, Coarse
// and Fine the pitch; Unison, Detune, Width, Blend, Spread and the Sub level the voice. Stack and the File settings
// (Import, Interp) wait in the More row. The scanned frame view, Table selector and load row stay card chrome.
CardLayout wavetableLayout() {
    return layoutOf(
        {modeRow({param("warp", CardWidget::Choice), param("syncMode"), param("subOctave"), param("subShape"),
                  param("poly")}),
         lane("hero", "Hero", {param("position", CardWidget::KnobLarge)}),
         lane("pitch", "Pitch", {param("octave"), param("coarse"), param("fine")}),
         lane("shape", "Shape", {param("warpAmount"), param("phase"), labelled(param("randomPhase"), "Rand")}),
         lane("voice", "Voice",
              {param("unison"), param("detune"), param("width"), param("blend"), param("spread"), param("subLevel")}),
         outLane({param("pan"), param("level")})});
}

} // namespace

void registerSourceCardLayouts(DefaultCardLayouts& defaults) {
    defaults.add("Oscillator", oscillatorLayout(), kOscillatorRevision,
                 {dimAtOneVoice("detune"), dimAtOneVoice("blend"), dimAtOneVoice("width")});
    defaults.add("Noise", noiseLayout(), kLaneRevision);
    defaults.add("Sampler", samplerLayout(), kSamplerRevision,
                 {dimUnlessGranular("grainSize"), dimUnlessGranular("density"), dimUnlessGranular("spray")});
    defaults.add("Drum Kit", drumKitLayout(), kDrumKitRevision);
    defaults.add("Pluck", pluckLayout(), kLaneRevision);
    defaults.add("Mallet", malletLayout(), kLaneRevision);
    defaults.add("LFO", lfoLayout(), kLfoRevision);
    defaults.add("Wavetable", wavetableLayout(), kLaneRevision);
}

} // namespace synth
