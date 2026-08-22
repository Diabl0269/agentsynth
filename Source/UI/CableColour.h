#pragma once

#include "../Modules/ModuleBase.h"
#include "Theme/Theme.h"
#include <array>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>

// Cable colour resolution — the single source of truth for what colour a wire is drawn in.
//
// Everything here is a pure function over (mode, signal, category, theme, overrides): no GUI
// state, no JUCE message thread, no LookAndFeel. GraphEditor's paint loop and the Appearance
// settings tab both call into this, so the swatch a user sees in Settings is guaranteed to be
// the colour that actually reaches the canvas. Headless-testable by construction — see
// CableColourTests.cpp.
namespace synth::ui {

// The kind of signal a user-visible cable carries. Drives "By signal type" colouring and maps
// 1:1 onto the wire colour tokens in synth::theme::Colors.
//
// NOTE: Midi is a distinct entry even though MIDI cables historically rendered with the
// audioWire token — a mode that claims to colour "by signal type" while drawing MIDI and audio
// identically would be lying, so MIDI got its own token (theme.colors.midiWire).
enum class CableSignal { Audio, Midi, ModCV, PolyBus, Pitch, Gate };
inline constexpr int kCableSignalCount = 6;

// Coarse grouping of module types, mirroring the section headers in ModuleLibraryComponent.
// Drives "By source module" colouring.
//
// Deliberately eight buckets rather than one swatch per module type: 22 colour pickers is not a
// settings panel anyone will configure, and per-category means a newly added module inherits a
// sensible colour for free instead of rendering uncoloured until someone updates a table.
enum class ModuleCategory { Sources, Sequencing, EnvelopesControl, Filters, ModulationFX, TimeFX, Dynamics, Utility };
inline constexpr int kModuleCategoryCount = 8;

// How the engine picks a cable's base colour, before user overrides.
enum class CableColourMode {
    BySignalType,    // audio vs MIDI vs mod CV vs pitch vs gate — preserves signal semantics
    BySourceCategory // colour follows the module the cable leaves from
};

//==============================================================================
// Enum <-> stable id/label mapping
//==============================================================================

// Stable, persisted identifiers. These end up in the user's settings file as part of an
// override key, so they must never change once shipped — rename the display name instead.
inline const char* cableSignalId(CableSignal s) noexcept {
    switch (s) {
    case CableSignal::Audio:
        return "audio";
    case CableSignal::Midi:
        return "midi";
    case CableSignal::ModCV:
        return "modcv";
    case CableSignal::PolyBus:
        return "polybus";
    case CableSignal::Pitch:
        return "pitch";
    case CableSignal::Gate:
        return "gate";
    }
    return "audio";
}

inline const char* cableSignalLabel(CableSignal s) noexcept {
    switch (s) {
    case CableSignal::Audio:
        return "Audio";
    case CableSignal::Midi:
        return "MIDI";
    case CableSignal::ModCV:
        return "Mod CV";
    case CableSignal::PolyBus:
        return "Poly Bus";
    case CableSignal::Pitch:
        return "Pitch";
    case CableSignal::Gate:
        return "Gate";
    }
    return "Audio";
}

// Reads the shared table in Theme.h so the ids used in user theme JSON and in settings keys are
// literally the same strings.
inline const char* moduleCategoryId(ModuleCategory c) noexcept {
    static_assert(kModuleCategoryCount == synth::theme::kCableCategoryCount,
                  "ModuleCategory and Theme::cableCategory must stay index-aligned");
    return synth::theme::kCableCategoryIds[(size_t)c];
}

inline const char* moduleCategoryLabel(ModuleCategory c) noexcept {
    switch (c) {
    case ModuleCategory::Sources:
        return "Sources";
    case ModuleCategory::Sequencing:
        return "Sequencing";
    case ModuleCategory::EnvelopesControl:
        return "Envelopes & Control";
    case ModuleCategory::Filters:
        return "Filters";
    case ModuleCategory::ModulationFX:
        return "Modulation FX";
    case ModuleCategory::TimeFX:
        return "Time FX";
    case ModuleCategory::Dynamics:
        return "Dynamics";
    case ModuleCategory::Utility:
        return "Utility";
    }
    return "Utility";
}

//==============================================================================
// Module type -> category
//==============================================================================

// Total over ModuleType — every module lands in exactly one bucket. Groupings mirror the
// section headers in ModuleLibraryComponent so the canvas and the library agree.
inline ModuleCategory categoryFor(ModuleType t) noexcept {
    switch (t) {
    case ModuleType::Oscillator:
    case ModuleType::Wavetable:
    case ModuleType::Noise:
    case ModuleType::Sampler:
    case ModuleType::LFO:
    // Track Audio is bucketed with the Sampler rather than with its sibling Track In: in
    // ByModuleCategory mode the bucket colours the CABLE, and what leaves this node is audio read
    // off a file — exactly what leaves a Sampler. Track In stays under Sequencing because what
    // leaves it is notes.
    case ModuleType::TimelineAudioSource:
        return ModuleCategory::Sources;

    case ModuleType::Sequencer:
    case ModuleType::PolySequencer:
    case ModuleType::MidiKeyboard:
    case ModuleType::PolyMidi:
    case ModuleType::ExternalMidi:
    case ModuleType::TimelineMidiSource:
        return ModuleCategory::Sequencing;

    case ModuleType::ADSR:
    case ModuleType::EnvelopeFollower:
    case ModuleType::VCA:
        return ModuleCategory::EnvelopesControl;

    case ModuleType::Filter:
    case ModuleType::ParametricEQ:
        return ModuleCategory::Filters;

    case ModuleType::Chorus:
    case ModuleType::Phaser:
    case ModuleType::Flanger:
    case ModuleType::Distortion:
    case ModuleType::Bitcrusher:
    case ModuleType::PitchShifter:
    case ModuleType::RingModulator:
        return ModuleCategory::ModulationFX;

    case ModuleType::Delay:
    case ModuleType::Reverb:
        return ModuleCategory::TimeFX;

    case ModuleType::Compressor:
    case ModuleType::Limiter:
        return ModuleCategory::Dynamics;

    case ModuleType::MacroControl:
    case ModuleType::SampleHold:
    case ModuleType::Comparator:
    case ModuleType::VoiceMixer:
    case ModuleType::Math:
    case ModuleType::Attenuverter:
    // Audio Input sits under the library's "I/O" header, which has no cable-colour bucket of its
    // own (its partner, Audio Output, is not even a ModuleBase). Utility is the bucket that means
    // "plumbing", which is exactly what a device tap is.
    case ModuleType::AudioInput:
    // Rec Tap is plumbing in the purest sense: a transparent stereo pass-through that
    // exists only so a take has somewhere to be copied out of. Same bucket as the device tap.
    case ModuleType::RecordTap:
    // Hosted Plugin. Every other entry in this switch names a module with ONE DSP role;
    // this one names a host, and what it hosts is a third-party instrument or a third-party effect
    // of any kind — unknowable here, since categoryFor() sees a ModuleType and not the loaded
    // instance. Utility is the neutral bucket rather than a guess that would be wrong about half
    // the time (and the library has no section for it either — it is hidden for now). A future pass
    // can split it on the instance's isInstrument flag once the card can ask.
    case ModuleType::HostedPlugin:
        return ModuleCategory::Utility;
    }
    return ModuleCategory::Utility;
}

//==============================================================================
// User overrides
//==============================================================================

// A sparse override layer sitting on top of the active theme. An unset entry means "use the
// theme token" — that is what makes "Reset to theme" a delete rather than a re-write, and what
// lets a theme switch move any colour the user has not explicitly pinned.
//
// Overrides are stored globally rather than per-theme on purpose: if someone picks green
// cables, they want green cables, not green-until-the-theme-changes. Reset covers the clash.
struct CableColourOverrides {
    std::array<std::optional<juce::Colour>, kCableSignalCount> signal{};
    std::array<std::optional<juce::Colour>, kModuleCategoryCount> category{};

    void setSignal(CableSignal s, juce::Colour c) { signal[(size_t)s] = c; }
    void clearSignal(CableSignal s) { signal[(size_t)s].reset(); }
    void setCategory(ModuleCategory c, juce::Colour col) { category[(size_t)c] = col; }
    void clearCategory(ModuleCategory c) { category[(size_t)c].reset(); }

    bool hasAny() const noexcept {
        for (const auto& o : signal)
            if (o.has_value())
                return true;
        for (const auto& o : category)
            if (o.has_value())
                return true;
        return false;
    }

    void clearAll() {
        signal = {};
        category = {};
    }
};

// Settings keys. Prefixed and suffixed with the stable ids above so the persisted form is
// readable and survives display-name changes.
inline juce::String signalOverrideKey(CableSignal s) { return juce::String("cableColour.signal.") + cableSignalId(s); }
inline juce::String categoryOverrideKey(ModuleCategory c) {
    return juce::String("cableColour.category.") + moduleCategoryId(c);
}
inline const char* cableColourModeKey() noexcept { return "cableColour.mode"; }

// Mode <-> persisted string.
inline const char* cableColourModeId(CableColourMode m) noexcept {
    return m == CableColourMode::BySourceCategory ? "sourceCategory" : "signalType";
}
inline CableColourMode cableColourModeFromId(const juce::String& id) noexcept {
    return id == "sourceCategory" ? CableColourMode::BySourceCategory : CableColourMode::BySignalType;
}

//==============================================================================
// Resolution
//==============================================================================

// The theme's colour for a signal kind, ignoring overrides.
inline juce::Colour themeColourForSignal(const synth::theme::Colors& colors, CableSignal s) noexcept {
    switch (s) {
    case CableSignal::Audio:
        return colors.audioWire;
    case CableSignal::Midi:
        return colors.midiWire;
    case CableSignal::ModCV:
        return colors.modWire;
    case CableSignal::PolyBus:
        return colors.polyBusWire;
    case CableSignal::Pitch:
        return colors.pitchWire;
    case CableSignal::Gate:
        return colors.gateWire;
    }
    return colors.audioWire;
}

// The theme's colour for a module category, ignoring overrides.
inline juce::Colour themeColourForCategory(const synth::theme::Colors& colors, ModuleCategory c) noexcept {
    return colors.cableCategory[(size_t)c];
}

// Base colour for a cable under `mode`, with the user override layer applied but WITHOUT the
// bypass treatment. Settings swatches render this so they show exactly what the user pinned.
inline juce::Colour resolveCableBaseColour(CableColourMode mode, CableSignal signal, ModuleCategory category,
                                           const synth::theme::Colors& colors,
                                           const CableColourOverrides& overrides) noexcept {
    if (mode == CableColourMode::BySourceCategory) {
        if (const auto& o = overrides.category[(size_t)category])
            return *o;
        return themeColourForCategory(colors, category);
    }

    if (const auto& o = overrides.signal[(size_t)signal])
        return *o;
    return themeColourForSignal(colors, signal);
}

// Alpha applied to a bypassed modulation cable. Matches the long-standing GraphEditor value —
// changing it here changes it on the canvas, which is the point of routing through one function.
inline constexpr float kBypassedCableAlpha = 0.3f;

// Full resolution including the bypass treatment. This is what the canvas draws.
inline juce::Colour resolveCableColour(CableColourMode mode, CableSignal signal, ModuleCategory category,
                                       const synth::theme::Colors& colors, const CableColourOverrides& overrides,
                                       bool isBypassed) noexcept {
    auto c = resolveCableBaseColour(mode, signal, category, colors, overrides);
    return isBypassed ? c.withAlpha(kBypassedCableAlpha) : c;
}

// The activity/hover treatment a wire's core stroke applies ON TOP of the resolved colour.
// The idle-dim ramp is a dark-theme device (an idle wire blends toward the black canvas and
// brightens with signal); on a light canvas that same ramp darkens every hue toward black and
// destroys the wire's colour identity, so light themes keep the token colour at idle and darken
// slightly with activity instead. Lives here (not in the LookAndFeel) so it is headless-testable.
inline juce::Colour wireCoreColour(bool isDarkTheme, juce::Colour colour, float normalisedActivity,
                                   bool hovered) noexcept {
    if (hovered)
        return isDarkTheme ? colour.brighter(0.3f) : colour.darker(0.3f);
    return isDarkTheme ? colour.withMultipliedBrightness(0.5f + normalisedActivity * 0.5f)
                       : colour.darker(normalisedActivity * 0.2f);
}

//==============================================================================
// Persistence
//
// Lives here rather than in the settings tab so that MainComponent (which restores the config at
// launch) and AppearanceSettingsTab (which edits it) cannot disagree about the storage format.
//==============================================================================

inline CableColourMode loadCableColourMode(juce::PropertiesFile& props) {
    return cableColourModeFromId(
        props.getValue(cableColourModeKey(), cableColourModeId(CableColourMode::BySignalType)));
}

inline void saveCableColourMode(juce::PropertiesFile& props, CableColourMode mode) {
    props.setValue(cableColourModeKey(), cableColourModeId(mode));
    props.saveIfNeeded();
}

// An absent or empty key means "not overridden" — that is what makes "Reset to theme" a key
// removal rather than writing the theme's current colour in as a new pin.
inline CableColourOverrides loadCableColourOverrides(juce::PropertiesFile& props) {
    CableColourOverrides o;
    for (int i = 0; i < kCableSignalCount; ++i) {
        const auto v = props.getValue(signalOverrideKey((CableSignal)i), {});
        if (v.isNotEmpty())
            o.signal[(size_t)i] = juce::Colour::fromString(v);
    }
    for (int i = 0; i < kModuleCategoryCount; ++i) {
        const auto v = props.getValue(categoryOverrideKey((ModuleCategory)i), {});
        if (v.isNotEmpty())
            o.category[(size_t)i] = juce::Colour::fromString(v);
    }
    return o;
}

inline void saveCableColourOverrides(juce::PropertiesFile& props, const CableColourOverrides& o) {
    for (int i = 0; i < kCableSignalCount; ++i) {
        const auto key = signalOverrideKey((CableSignal)i);
        if (const auto& c = o.signal[(size_t)i])
            props.setValue(key, c->toString());
        else
            props.removeValue(key);
    }
    for (int i = 0; i < kModuleCategoryCount; ++i) {
        const auto key = categoryOverrideKey((ModuleCategory)i);
        if (const auto& c = o.category[(size_t)i])
            props.setValue(key, c->toString());
        else
            props.removeValue(key);
    }
    props.saveIfNeeded();
}

} // namespace synth::ui
