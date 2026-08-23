#pragma once

#include <array>
#include <juce_graphics/juce_graphics.h>

namespace synth::theme {

// Number of module-category cable colours. Mirrors synth::ui::kModuleCategoryCount — kept as a
// separate constant so Theme.h stays free of any UI-layer include.
inline constexpr int kCableCategoryCount = 8;

// Stable persisted ids for the cableCategory[] entries, index-aligned with
// synth::ui::ModuleCategory. Defined here (rather than in CableColour.h) so ThemeLoader can
// parse user JSON without pulling the module layer in — CableColour.h reads these same strings,
// so there is exactly one definition and the two cannot drift apart.
// These appear in user theme files and settings keys: never renumber or rename a shipped id.
inline constexpr std::array<const char*, kCableCategoryCount> kCableCategoryIds{
    "sources", "sequencing", "envelopes", "filters", "modfx", "timefx", "dynamics", "utility"};

// Surface treatment family. JSON string mapping in section 4.
enum class ThemeStyle {
    Flat,    // Obsidian: flat fill + soft drop shadow, restrained.
    Glass,   // Neon: translucent fill + top highlight + glow, glassmorphism.
    Textured // Warm: brushed-metal gradient + low-opacity noise/striations + warm glow.
};

// Semantic color tokens. Every color the UI needs is named here; nothing hardcoded
// at call sites. Defaults below match the Obsidian theme so a half-populated theme
// still renders sanely.
struct Colors {
    juce::Colour bg0{0xff0B0D10};             // deepest page / window background
    juce::Colour bg1{0xff13161B};             // canvas / graph editor background
    juce::Colour surface{0xff1B1F26};         // module cards / panels
    juce::Colour surfaceHi{0xff232833};       // raised surface / card top gradient stop
    juce::Colour border{0xff2A2F38};          // hairline borders
    juce::Colour accent{0xff00D1FF};          // primary accent (selection, value arc)
    juce::Colour accent2{0xff00D1FF};         // secondary accent (Neon cyan vs magenta etc.)
    juce::Colour audioWire{0xffE8EDF2};       // audio signal wires
    juce::Colour midiWire{0xffB48EF5};        // MIDI note/event wires
    juce::Colour modWire{0xff00D1FF};         // modulation CV wires (DirectCV / attenuverter)
    juce::Colour pitchWire{0xffAAD4FF};       // poly pitch fan wires (GraphEditor role==Pitch)
    juce::Colour gateWire{0xffFFA500};        // poly gate fan wires (GraphEditor role==Gate)
    juce::Colour polyBusWire{0xff00E5FF};     // poly ModCV bus wires (RoutingKind::PolyBus)
    juce::Colour textPrimary{0xffEAEEF3};     // primary text
    juce::Colour textMuted{0xff8A93A0};       // secondary/label text
    juce::Colour textDisabled{0xff5C6470};    // disabled / bypassed text
    juce::Colour success{0xff46C66B};         // activity LED / OK
    juce::Colour warning{0xffE0A33D};         // warning / mute-pending
    juce::Colour error{0xffE5484D};           // error / mute
    juce::Colour knobBody{0xff13161B};        // knob body gradient inner stop (outer = surfaceHi)
    juce::Colour knobPointer{0xffEAEEF3};     // knob pointer line
    juce::Colour meterFill{0xff00D1FF};       // output meter fill (top of gradient)
    juce::Colour modRingPositive{0xff00E5FF}; // mod ring, positive modulation
    juce::Colour modRingNegative{0xffFF6E00}; // mod ring, negative modulation

    // Piano-roll note colours (default here mirrors makeObsidian()'s explicit values, same
    // convention as every other token above — a theme JSON that omits these keys falls back to
    // this struct's default, and that default IS the Obsidian value).
    juce::Colour noteFill{0xffB48EF5};       // unselected note body (Obsidian starts from midiWire)
    juce::Colour noteBorder{0xff4A3B75};     // unselected note outline — distinct from noteFill, not a .darker() of it
    juce::Colour noteSelected{0xff00D1FF};   // selected-note border/highlight (defaults to accent)
    juce::Colour noteOutOfScale{0xffFF6B57}; // "outside the active scale" warning fill — red/orange family
    juce::Colour pianoKeyWhite{0xffEDEFF3};  // piano-roll keyboard gutter: white key fill
    juce::Colour pianoKeyBlack{0xff15171C};  // piano-roll keyboard gutter: black key fill

    // Timeline track-header M/S/R button active-state colours. Families are fixed across every
    // theme (mute=amber/orange, solo=yellow, arm/record=red); only the exact shade varies.
    juce::Colour trackMuteOn{0xffFFA033}; // mute button, active
    juce::Colour trackSoloOn{0xffFFD23D}; // solo button, active
    juce::Colour trackArmOn{0xffE5484D};  // arm/record button, active

    // Active-tool highlight in the timeline edit-tool strip (Select/Split/Glue/Erase/Mute/Draw
    // — see Source/UI/EditTool.h). Defaults to the same literal as `accent`'s Obsidian default:
    // there is no existing precedent in this struct for one token defaulting FROM another at
    // construction time (accent2 merely repeats accent's literal too), so this follows that same
    // static-literal convention rather than introducing a new "derives from" mechanism.
    juce::Colour toolActive{0xff00D1FF}; // active edit-tool button highlight

    // Cable colours for CableColourMode::BySourceCategory, indexed by synth::ui::ModuleCategory
    // (Sources, Sequencing, Envelopes & Control, Filters, Modulation FX, Time FX, Dynamics,
    // Utility). Eight distinguishable hues rather than one per module type — see CableColour.h.
    std::array<juce::Colour, kCableCategoryCount> cableCategory{
        juce::Colour(0xffFFB454), // Sources
        juce::Colour(0xffC792EA), // Sequencing
        juce::Colour(0xff7FD962), // Envelopes & Control
        juce::Colour(0xff4FC1FF), // Filters
        juce::Colour(0xffFF7AB2), // Modulation FX
        juce::Colour(0xff56D4C0), // Time FX
        juce::Colour(0xffF07178), // Dynamics
        juce::Colour(0xffA0A8B4)  // Utility
    };
};

// Geometry / spacing. Pixel units at zoom 1.0.
struct Metrics {
    float cornerRadius{10.0f};   // card / panel corner radius
    float windowRadius{14.0f};   // top-level window radius (cosmetic; native title bar in use)
    float pillRadius{8.0f};      // buttons / pills
    int padding{14};             // card body padding
    int spacingUnit{6};          // base grid spacing unit
    float knobTrackWidth{4.0f};  // rotary track + value arc stroke width
    float knobRingWidth{3.5f};   // outer modulation ring stroke width
    float borderWidth{1.0f};     // hairline border stroke
    float wireCoreWidth{2.5f};   // connection wire core stroke
    float wireCasingWidth{5.0f}; // connection wire casing (dark underlay) stroke

    // --- Visual treatment constants (not parsed from user JSON, code-only defaults) ---
    int gridSize{8};               // snap quantum (kGridSize in LayoutUtil)
    float guideAlpha{0.7f};        // alignment guide opacity
    float guideLineWidth{1.5f};    // alignment guide stroke width
    float cornerRadiusSmall{4.0f}; // pill / small element radius

    // --- Chrome layout constants (code-only; not parsed from user JSON) ---
    // 44 (was 36): the old height only left room for a 9px icon-band trim and ~7px label text
    // (LookAndFeel_V2::drawDrawableButton's fixed min(16, 25%-of-height) split) — a few extra
    // px buys a readable ~11px label and a consistent ~18px icon via DrawableButton::edgeIndent.
    int toolbarHeight{44};        // code-only; not parsed from user JSON
    int statusBarHeight{24};      // code-only; not parsed from user JSON
    int controlPadding{4};        // code-only; not parsed from user JSON
    int minWindowWidth{480};      // code-only; not parsed from user JSON
    int minWindowHeight{400};     // code-only; not parsed from user JSON
    int sidebarCollapsedWidth{0}; // code-only; not parsed from user JSON
    int librarySidebarWidth{200}; // code-only; not parsed from user JSON
    int aiPanelWidth{300};        // code-only; not parsed from user JSON
    int iconSize{16};             // code-only; not parsed from user JSON

    // Timeline panel (bottom-docked, toggled via the toolbar / Cmd+T).
    int timelinePanelHeight{220};       // code-only; not parsed from user JSON
    int timelineTrackHeaderWidth{160};  // code-only; not parsed from user JSON
    int timelineTransportBarHeight{28}; // code-only; not parsed from user JSON
    int timelineRulerHeight{24};        // code-only; not parsed from user JSON
    // The row height BOTH the track-header column and the clip-lane area lay their rows
    // out at — the single source that keeps header rows and clip rows aligned. Replaces what used
    // to be TimelineTrackHeaderComponent::kRowHeight's exclusive say in the matter; that constant
    // now only serves as the headless literal fallback and is kept equal to this default.
    int timelineTrackRowHeight{56}; // code-only; not parsed from user JSON

    // The automation strip docked at the BOTTOM of the panel's lanes region (header row of
    // tool buttons + lane/record-mode pickers, above the AutomationLaneEditor curve canvas). The
    // clip-lane area (and the piano roll) shrink by exactly this much while the strip is open.
    int timelineAutomationStripHeight{72}; // code-only; not parsed from user JSON
};

// Font family NAMES (resolved to embedded typefaces by the LnF) + a type scale (pt).
struct Typography {
    juce::String uiFamily{"Inter"};
    juce::String monoFamily{"JetBrains Mono"};
    float h1{18.0f};    // window / large headings
    float h2{13.0f};    // card titles (rendered uppercase + tracked at call sites)
    float label{10.5f}; // knob names, port labels, section heads
    float value{10.0f}; // mono value readouts
    float micro{8.5f};  // smallest captions
};

// Data-driven surface treatment. Intensities are clamped to [0,1] by the loader.
struct Treatment {
    ThemeStyle style{ThemeStyle::Flat};
    float glow{0.0f};    // accent glow strength: selection halos, neon wire bloom, jack glow
    float shadow{0.6f};  // drop-shadow strength under cards
    float blur{0.0f};    // glass "frost" highlight strength (NOT live blur; see section 6)
    float texture{0.0f}; // brushed-metal striation / noise overlay opacity multiplier
};

// A complete theme.
struct Theme {
    juce::String name{"Untitled"}; // human-facing display name (from JSON "name" or built-in)
    juce::String id{"untitled"};   // stable lookup id (slug; built-in or derived from filename)
    bool isUserTheme{false};       // true if loaded from the user themes folder
    bool isDark{true};             // true for dark themes, false for light themes
    Colors colors{};
    Metrics metrics{};
    Typography type{};
    Treatment treatment{};
};

} // namespace synth::theme
