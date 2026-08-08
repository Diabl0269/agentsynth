#include "BuiltInThemes.h"

namespace synth::theme {

Theme makeObsidian() {
    Theme t;
    t.name = "Obsidian Studio";
    t.id = "obsidian";
    t.isUserTheme = false;

    // Colors
    t.colors.bg0 = juce::Colour(0xff0B0D10);
    t.colors.bg1 = juce::Colour(0xff13161B);
    t.colors.surface = juce::Colour(0xff1B1F26);
    t.colors.surfaceHi = juce::Colour(0xff232833);
    t.colors.border = juce::Colour(0xff2A2F38);
    t.colors.accent = juce::Colour(0xff00D1FF);
    t.colors.accent2 = juce::Colour(0xff00D1FF);
    t.colors.audioWire = juce::Colour(0xffE8EDF2);
    t.colors.midiWire = juce::Colour(0xffB48EF5);
    t.colors.modWire = juce::Colour(0xff00D1FF);
    t.colors.pitchWire = juce::Colour(0xffAAD4FF);
    t.colors.gateWire = juce::Colour(0xffFFA500);
    t.colors.polyBusWire = juce::Colour(0xff00E5FF);
    t.colors.cableCategory = {
        juce::Colour(0xffFFB454), // Sources
        juce::Colour(0xffC792EA), // Sequencing
        juce::Colour(0xff7FD962), // Envelopes & Control
        juce::Colour(0xff4FC1FF), // Filters
        juce::Colour(0xffFF7AB2), // Modulation FX
        juce::Colour(0xff56D4C0), // Time FX
        juce::Colour(0xffF07178), // Dynamics
        juce::Colour(0xffA0A8B4)  // Utility
    };
    t.colors.textPrimary = juce::Colour(0xffEAEEF3);
    t.colors.textMuted = juce::Colour(0xff8A93A0);
    t.colors.textDisabled = juce::Colour(0xff5C6470);
    t.colors.success = juce::Colour(0xff46C66B);
    t.colors.warning = juce::Colour(0xffE0A33D);
    t.colors.error = juce::Colour(0xffE5484D);
    t.colors.knobBody = juce::Colour(0xff13161B);
    t.colors.knobPointer = juce::Colour(0xffEAEEF3);
    t.colors.meterFill = juce::Colour(0xff00D1FF);
    t.colors.modRingPositive = juce::Colour(0xff00E5FF);
    t.colors.modRingNegative = juce::Colour(0xffFF6E00);

    // Metrics
    t.metrics.cornerRadius = 10.0f;
    t.metrics.windowRadius = 14.0f;
    t.metrics.pillRadius = 8.0f;
    t.metrics.padding = 14;
    t.metrics.spacingUnit = 6;
    t.metrics.knobTrackWidth = 4.0f;
    t.metrics.knobRingWidth = 3.5f;
    t.metrics.borderWidth = 1.0f;
    t.metrics.wireCoreWidth = 2.5f;
    t.metrics.wireCasingWidth = 5.0f;
    t.metrics.gridSize = 8;
    t.metrics.guideAlpha = 0.7f;
    t.metrics.guideLineWidth = 1.5f;
    t.metrics.cornerRadiusSmall = 4.0f;

    // Typography
    t.type.uiFamily = "Inter";
    t.type.monoFamily = "JetBrains Mono";
    t.type.h1 = 18.0f;
    t.type.h2 = 13.0f;
    t.type.label = 10.5f;
    t.type.value = 10.0f;
    t.type.micro = 8.5f;

    // Treatment
    t.treatment.style = ThemeStyle::Flat;
    t.treatment.glow = 0.0f;
    t.treatment.shadow = 0.65f;
    t.treatment.blur = 0.0f;
    t.treatment.texture = 0.0f;

    return t;
}

Theme makeNeon() {
    Theme t;
    t.name = "Neon Lab";
    t.id = "neon";
    t.isUserTheme = false;

    // Colors
    t.colors.bg0 = juce::Colour(0xff0A0612);
    t.colors.bg1 = juce::Colour(0xff120A22);
    t.colors.surface = juce::Colour(0x991E1238);
    t.colors.surfaceHi = juce::Colour(0xb3301C56);
    t.colors.border = juce::Colour(0xff34215C);
    t.colors.accent = juce::Colour(0xffFF2BD6);
    t.colors.accent2 = juce::Colour(0xff22E0FF);
    t.colors.audioWire = juce::Colour(0xffFFFFFF);
    t.colors.midiWire = juce::Colour(0xffC77DFF);
    t.colors.modWire = juce::Colour(0xff22E0FF);
    t.colors.pitchWire = juce::Colour(0xffFF2BD6);
    t.colors.gateWire = juce::Colour(0xff46E0A0);
    t.colors.polyBusWire = juce::Colour(0xff22E0FF);
    t.colors.cableCategory = {
        juce::Colour(0xffFFD166), // Sources
        juce::Colour(0xffC77DFF), // Sequencing
        juce::Colour(0xff46E0A0), // Envelopes & Control
        juce::Colour(0xff22E0FF), // Filters
        juce::Colour(0xffFF2BD6), // Modulation FX
        juce::Colour(0xff7B61FF), // Time FX
        juce::Colour(0xffFF5C7A), // Dynamics
        juce::Colour(0xff9C8FC0)  // Utility
    };
    t.colors.textPrimary = juce::Colour(0xffF4ECFF);
    t.colors.textMuted = juce::Colour(0xff9C8FC0);
    t.colors.textDisabled = juce::Colour(0xff6A5C8C);
    t.colors.success = juce::Colour(0xff46E0A0);
    t.colors.warning = juce::Colour(0xffFFC65C);
    t.colors.error = juce::Colour(0xffFF5C7A);
    t.colors.knobBody = juce::Colour(0xff160D2A);
    t.colors.knobPointer = juce::Colour(0xffF4ECFF);
    t.colors.meterFill = juce::Colour(0xff22E0FF);
    t.colors.modRingPositive = juce::Colour(0xff22E0FF);
    t.colors.modRingNegative = juce::Colour(0xffFF2BD6);

    // Metrics
    t.metrics.cornerRadius = 14.0f;
    t.metrics.windowRadius = 18.0f;
    t.metrics.pillRadius = 10.0f;
    t.metrics.padding = 14;
    t.metrics.spacingUnit = 6;
    t.metrics.knobTrackWidth = 4.0f;
    t.metrics.knobRingWidth = 3.5f;
    t.metrics.borderWidth = 1.0f;
    t.metrics.wireCoreWidth = 3.0f;
    t.metrics.wireCasingWidth = 5.5f;
    t.metrics.gridSize = 8;
    t.metrics.guideAlpha = 0.7f;
    t.metrics.guideLineWidth = 1.5f;
    t.metrics.cornerRadiusSmall = 4.0f;

    // Typography. All built-in themes share Inter + JetBrains Mono: switching the embedded
    // typeface family at runtime corrupts text rendering globally (JUCE 8 + CoreText), so the
    // themes differ by colour/treatment/glow, not font. (The data model still supports per-theme
    // fonts for user JSON themes chosen at launch; see docs/theming.md.)
    t.type.uiFamily = "Inter";
    t.type.monoFamily = "JetBrains Mono";
    t.type.h1 = 18.0f;
    t.type.h2 = 13.0f;
    t.type.label = 10.5f;
    t.type.value = 10.5f;
    t.type.micro = 8.5f;

    // Treatment
    t.treatment.style = ThemeStyle::Glass;
    t.treatment.glow = 0.85f;
    t.treatment.shadow = 0.5f;
    t.treatment.blur = 0.6f;
    t.treatment.texture = 0.0f;

    return t;
}

Theme makeWarm() {
    Theme t;
    t.name = "Warm Console";
    t.id = "warm";
    t.isUserTheme = false;

    // Colors
    t.colors.bg0 = juce::Colour(0xff161310);
    t.colors.bg1 = juce::Colour(0xff211C17);
    t.colors.surface = juce::Colour(0xff2C2620);
    t.colors.surfaceHi = juce::Colour(0xff3A332A);
    t.colors.border = juce::Colour(0xff3E352B);
    t.colors.accent = juce::Colour(0xffFF9E3D);
    t.colors.accent2 = juce::Colour(0xff9E5A1E);
    t.colors.audioWire = juce::Colour(0xffF2E8D5);
    t.colors.midiWire = juce::Colour(0xffCE93B8);
    t.colors.modWire = juce::Colour(0xffFFC65C);
    t.colors.pitchWire = juce::Colour(0xffFFB36B);
    t.colors.gateWire = juce::Colour(0xffE07A5F);
    t.colors.polyBusWire = juce::Colour(0xffFFC65C);
    t.colors.cableCategory = {
        juce::Colour(0xffF2A65A), // Sources
        juce::Colour(0xffC98FB0), // Sequencing
        juce::Colour(0xff9BBF6A), // Envelopes & Control
        juce::Colour(0xff6BB8C4), // Filters
        juce::Colour(0xffE07A5F), // Modulation FX
        juce::Colour(0xff7FBFA8), // Time FX
        juce::Colour(0xffD9636B), // Dynamics
        juce::Colour(0xffA89A85)  // Utility
    };
    t.colors.textPrimary = juce::Colour(0xffF2E8D5);
    t.colors.textMuted = juce::Colour(0xffA89A85);
    t.colors.textDisabled = juce::Colour(0xff7D715F);
    t.colors.success = juce::Colour(0xff9BBF6A);
    t.colors.warning = juce::Colour(0xffFF9E3D);
    t.colors.error = juce::Colour(0xffE07A5F);
    t.colors.knobBody = juce::Colour(0xff1C140E);
    t.colors.knobPointer = juce::Colour(0xffF2E8D5);
    t.colors.meterFill = juce::Colour(0xffFF9E3D);
    t.colors.modRingPositive = juce::Colour(0xffFFC65C);
    t.colors.modRingNegative = juce::Colour(0xffE07A5F);

    // Metrics
    t.metrics.cornerRadius = 16.0f;
    t.metrics.windowRadius = 18.0f;
    t.metrics.pillRadius = 9.0f;
    t.metrics.padding = 14;
    t.metrics.spacingUnit = 6;
    t.metrics.knobTrackWidth = 4.5f;
    t.metrics.knobRingWidth = 3.5f;
    t.metrics.borderWidth = 1.0f;
    t.metrics.wireCoreWidth = 2.5f;
    t.metrics.wireCasingWidth = 5.0f;
    t.metrics.gridSize = 8;
    t.metrics.guideAlpha = 0.7f;
    t.metrics.guideLineWidth = 1.5f;
    t.metrics.cornerRadiusSmall = 4.0f;

    // Typography. NOTE: IBM Plex Sans/Mono (the original Warm pick) garbles when swapped in at
    // runtime — its large multi-script glyph tables get mis-indexed by JUCE 8's shaper after a
    // live typeface change (Inter & Manrope, with simpler Latin tables, switch cleanly). Warm's
    // identity is its amber palette + brushed-metal treatment, so we use the known-good
    // Inter/JetBrains Mono here. (Tracked as a follow-up: find a warm humanist font that
    // survives runtime switching.)
    t.type.uiFamily = "Inter";
    t.type.monoFamily = "JetBrains Mono";
    t.type.h1 = 18.0f;
    t.type.h2 = 13.0f;
    t.type.label = 10.5f;
    t.type.value = 10.5f;
    t.type.micro = 8.5f;

    // Treatment
    t.treatment.style = ThemeStyle::Textured;
    t.treatment.glow = 0.3f;
    t.treatment.shadow = 0.55f;
    t.treatment.blur = 0.0f;
    t.treatment.texture = 0.55f;

    return t;
}

std::vector<Theme> builtInThemes() { return {makeObsidian(), makeNeon(), makeWarm()}; }

} // namespace synth::theme
