// DefaultCardLayoutsSources.cpp -- the code-default card layouts of Oscillator, Noise, Sampler, Drum Kit, Pluck,
// Mallet, LFO and Wavetable. Each type registers with defaults.add(type, layout, revision, dimRules), built with the
// cardlayout:: helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// No view is placed in any of these: the Sampler's waveform and load row, the Wavetable's display, Table
// selector and load row, and the LFO's custom-wave editor are card chrome (ModuleComponent) that already
// draws where the design puts them, so a registered copy would be a second, unbound panel.
// docs/layout/module-card-layout.md#default-layouts.
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

constexpr int kRevision = 1;

// The Sampler layout changed (Instrument mode, then Glide and Mono): a user layout based on an older revision is stale.
constexpr int kSamplerRevision = 3;

// The Oscillator layout gained the Fixed rate in Glide's cell, with Rate in the More row (earlier: the Unison
// Blend and Width; Phase and Rand sit in the More controls row): a stored layout based on an older revision is
// stale and picks the new controls up.
constexpr int kOscillatorRevision = 3;

// The Drum Kit layout gained the Recorded switches (a recorded kit).
constexpr int kDrumKitRevision = 2;

// A row of four knobs (Oscillator Pitch, LFO shaping) on one line.
constexpr int kQuadColumns = 4;

CardLayout oscillatorLayout() {
    CardLayout layout;
    layout.sections = {
        section("waveform", std::nullopt, {param("waveform", CardWidget::Segmented)}),
        // Rate (Follow notes / Fixed) waits in the More row so the card keeps its size; a Fixed rate plays no notes to
        // glide between, so the Fixed rate knob takes Glide's cell while it is on.
        section("pitch", juce::String("Pitch"),
                {param("octave"), param("coarse"), param("fine"),
                 showWhen(param("glide"), "rateMode", {"Follow notes"}),
                 showWhen(param("fixedRate"), "rateMode", {"Fixed"})},
                kQuadColumns),
        section("unison", juce::String("Unison"),
                {param("unison"), param("detune"), param("blend"), param("width"),
                 dimUnless(param("pulseWidth"), "waveform", {"Square"})},
                kQuadColumns),
        section("output", juce::String("Output"), {param("level"), param("pan")}),
        footer({}),
    };
    return layout;
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
    CardLayout layout;
    layout.sections = {
        section("type", std::nullopt, {param("noiseType", CardWidget::Segmented)}),
        section("output", std::nullopt, {param("color"), param("level")}),
        footer({}),
    };
    return layout;
}

// The Sampler's three modes share one card. The waveform (or the Instrument picker and key strip) and the load row
// are chrome drawn above the body. Each mode's controls fill the same cells, so a mode switch never resizes the card:
// Start and End swap for Attack and Release, the Grains section for the Sound section, and Loop and Reverse for
// Release Sound and Mono. Root Note's cell holds Glide in Instrument mode. The grain controls stay on the
// card in Sample mode (their CV jacks are bound to them) and dim until the mode is Granular.
CardLayout samplerLayout() {
    const juce::StringArray sampled{"Sample", "Granular"};
    const juce::StringArray instrument{"Instrument"};
    auto grains = section("grains", juce::String("Grains"),
                          {dimUnless(param("grainSize"), "playMode", {"Granular"}),
                           dimUnless(param("density"), "playMode", {"Granular"}),
                           dimUnless(param("spray"), "playMode", {"Granular"})});
    grains.visibleWhen = CardCondition{"playMode", sampled, CardConditionEffect::Show};
    auto sound = section("sound", juce::String("Sound"), {param("tone"), param("feel"), param("voices")});
    sound.visibleWhen = CardCondition{"playMode", instrument, CardConditionEffect::Show};

    CardLayout layout;
    layout.sections = {
        section("mode", std::nullopt, {param("playMode", CardWidget::Segmented)}),
        section("region", std::nullopt,
                {showWhen(param("start"), "playMode", sampled), showWhen(param("attack"), "playMode", instrument),
                 showWhen(param("end"), "playMode", sampled), showWhen(param("release"), "playMode", instrument),
                 param("level")}),
        section("pitch", juce::String("Pitch"),
                {param("pitch"), showWhen(param("rootNote"), "playMode", sampled),
                 showWhen(param("glide"), "playMode", instrument), param("fine")}),
        grains,
        sound,
        footer({showWhen(param("loop"), "playMode", sampled), showWhen(param("releaseSound"), "playMode", instrument),
                showWhen(param("reverse"), "playMode", sampled), showWhen(param("mono"), "playMode", instrument)}),
    };
    return layout;
}

// The master Level stays above a strip of seven tabs, one per drum group (level, tune, decay; the hats add
// their two decays, each with the group's Recorded switch under the knobs), so the card is no taller than a Noise
// card plus two rows of knobs.
CardLayout drumKitLayout() {
    CardLayout layout;
    layout.sections = {
        section("output", std::nullopt, {param("level")}, 1),
        tab("kick", "Kick", {param("kickLevel"), param("kickTune"), param("kickDecay"), param("kickRecorded")}),
        tab("snare", "Snare", {param("snareLevel"), param("snareTune"), param("snareDecay"), param("snareRecorded")}),
        tab("clap", "Clap", {param("clapLevel"), param("clapTune"), param("clapDecay"), param("clapRecorded")}),
        tab("hats", "Hats",
            {param("hatLevel"), param("hatTune"), param("closedDecay"), param("openDecay"), param("hatRecorded")},
            kQuadColumns),
        tab("toms", "Toms", {param("tomLevel"), param("tomTune"), param("tomDecay"), param("tomRecorded")}),
        tab("cymbals", "Cym",
            {param("cymbalLevel"), param("cymbalTune"), param("cymbalDecay"), param("cymbalRecorded")}),
        tab("cowbell", "Bell",
            {param("cowbellLevel"), param("cowbellTune"), param("cowbellDecay"), param("cowbellRecorded")}),
        footer({}),
    };
    return layout;
}

// A Decay knob greys out while every note it shapes plays a recording; a Recorded switch is on the card only while a
// kit is loaded (the card re-reads the rules when the kit changes).
std::vector<CardDimRule> drumKitRules() {
    std::vector<CardDimRule> rules;
    const auto decayRule = [&rules](const char* decayId, const char* recordedId) {
        const juce::String id(decayId);
        rules.push_back({id, {recordedId}, [id](juce::AudioProcessor& module) {
                             auto* kit = dynamic_cast<DrumKitModule*>(&module);
                             return kit != nullptr && kit->decayIsMoot(id);
                         }});
    };
    decayRule("kickDecay", "kickRecorded");
    decayRule("snareDecay", "snareRecorded");
    decayRule("clapDecay", "clapRecorded");
    decayRule("closedDecay", "hatRecorded");
    decayRule("openDecay", "hatRecorded");
    decayRule("tomDecay", "tomRecorded");
    decayRule("cymbalDecay", "cymbalRecorded");
    decayRule("cowbellDecay", "cowbellRecorded");
    for (const char* id : {"kickRecorded", "snareRecorded", "clapRecorded", "hatRecorded", "tomRecorded",
                           "cymbalRecorded", "cowbellRecorded"})
        rules.push_back({id,
                         {},
                         [](juce::AudioProcessor& module) {
                             auto* kit = dynamic_cast<DrumKitModule*>(&module);
                             return kit != nullptr && !kit->hasRecordedKit();
                         },
                         true});
    return rules;
}

// Level stays above three tabs: the string (how long and how bright it rings), the pick (how it is
// struck), and the tuning with the body of the instrument.
CardLayout pluckLayout() {
    CardLayout layout;
    layout.sections = {
        section("output", std::nullopt, {param("level")}, 1),
        tab("string", "String", {param("decay"), param("release"), param("brightness"), param("stiffness")},
            kQuadColumns),
        tab("pick", "Pick", {param("pickPosition"), param("pickHardness"), param("velBright")}),
        tab("tune", "Tune", {param("tune"), param("fine"), param("body"), param("decayTrack")}, kQuadColumns),
        footer({}),
    };
    return layout;
}

// The material picks the bar; Level stays above three tabs: the bar (decay, damping, tone), the mallet
// (contact, tube, tremolo), and the tuning.
CardLayout malletLayout() {
    CardLayout layout;
    layout.sections = {
        section("material", std::nullopt, {param("material")}),
        section("output", std::nullopt, {param("level")}, 1),
        tab("bar", "Bar", {param("decay"), param("damping"), param("brightness"), param("release")}, kQuadColumns),
        tab("mallet", "Mallet", {param("hardness"), param("velBright"), param("resonator"), param("tremolo")},
            kQuadColumns),
        tab("tune", "Tune", {param("tune"), param("fine")}),
        footer({}),
    };
    return layout;
}

// The Custom wave's morph cursor: Morph X and Y show only while the shape is Custom, so every other shape keeps
// the old card. Where the shapes' map and its Edit shapes button sit is decided separately.
CardSection morphSection() {
    auto morph = section("morph", "Morph", {param("morphX"), param("morphY")});
    morph.visibleWhen = CardCondition{"shape", {"Custom"}, CardConditionEffect::Show};
    return morph;
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
        morphSection(),
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
    defaults.add("Oscillator", oscillatorLayout(), kOscillatorRevision,
                 {dimAtOneVoice("detune"), dimAtOneVoice("blend"), dimAtOneVoice("width")});
    defaults.add("Noise", noiseLayout(), kRevision);
    defaults.add("Sampler", samplerLayout(), kSamplerRevision);
    defaults.add("Drum Kit", drumKitLayout(), kDrumKitRevision, drumKitRules());
    defaults.add("Pluck", pluckLayout(), kRevision);
    defaults.add("Mallet", malletLayout(), kRevision);
    defaults.add("LFO", lfoLayout(), kRevision);
    defaults.add("Wavetable", wavetableLayout(), kRevision);
}

} // namespace synth
