#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>

namespace synth::theme {

// The canonical icon set. White-filled SVG glyphs, tinted programmatically at
// theme-apply time; the toolbar's glyphs are multi-role instead (see IconRoleColours). Backed by Assets BinaryData (see
// CMakeLists.txt) when HAS_FONT_ASSETS is defined; otherwise every entry is a null fallback so headless tests (no asset
// library) still link and run.
//
// IMPORTANT: this is a juce::Drawable (SVG) registry, NOT an icon/glyph font. A runtime
// font-family swap corrupts text globally on JUCE 8 + CoreText, so chrome glyphs live here
// as Drawables instead of as a symbol font.
enum class Icon : int {
    TransportPlay = 0, // scaffolding only — no DrawableButton wired this phase
    TransportStop,     // used for master-mute in StatusBarComponent
    ActionUndo,
    ActionRedo,
    ActionSave,
    ActionLoad,
    ActionNew,
    ActionSettings,
    ActionAutoArrange,
    ActionFeedback,
    ToggleAI,
    ToggleMatrix,
    ToggleLibrary,
    ThemeToggle,
    ModuleBypass,
    ModuleMute,
    ModuleDelete,
    CatSources,
    CatSequencing,
    CatEnvelopes,
    CatFilters,
    CatModulationFX,
    CatTimeFX,
    CatDynamics,
    CatUtility,
    // Waveform glyphs — rendered in combo-box items for Oscillator waveform selection.
    WaveformSine,
    WaveformSaw,
    WaveformSquare,
    WaveformTriangle,
    // Toolbar toggle for the GraphEditor minimap overlay.
    ToggleMinimap,
    // Module header: split one Audio jack into Left/Right (Dual I/O).
    ModuleDualIO,
    // Timeline edit-tool strip (Cubase-style tools; see Source/UI/Timeline/EditTool.h). Index-order here
    // has no relationship to EditTool's enumerator order — this table is looked up by the UI
    // layer via a small tool->Icon mapping, not by casting one enum to the other.
    ToolSelect,
    ToolSplit,
    ToolGlue,
    ToolErase,
    ToolMute,
    ToolDraw,
    // Timeline track-header kind glyphs + the panel's follow-playhead toggle.
    TrackMidi,
    TrackAudio,
    TrackAutomation,
    FollowPlayhead,
    // I/O category icon (speaker glyph): the ModuleLibraryComponent "I/O" header (Audio Input /
    // Audio Output) and the Audio Output card's identity treatment in ModuleComponent — both
    // previously fell back to CatUtility, which had no way to say "this is where sound goes".
    // Appended here rather than grouped next to the other CatXxx entries so every existing
    // enum ordinal (and the IconLibraryTests.cpp spot-checks against them) stays unchanged.
    CatIO,
    // The icon-only "open in window" / "dock back" control DetachablePanelHost uses for
    // both the Timeline and Mixer panels. Appended immediately before kCount, same
    // append-only convention as CatIO above (see docs/mixer/panel.md).
    ActionDetachWindow,
    // EditTool::Range's glyph — appended, not grouped with the Tool* block (append-only, as CatIO).
    ToolRange,
    // The mixer column header's sources badge ("plays into this channel") -- appended, as above.
    MixerSources,
    // The toolbar's Hide/Show panel glyph -- appended, as above.
    TogglePanel,
    kCount
};

// The colours of a multi-role icon. Such an SVG paints each role in a fixed placeholder colour
// (kRoleHue, kRoleInk, kRolePaper; the soft role is kRoleHue at kRoleSoftAlpha), and a recolour
// maps each placeholder to its role colour, keeping any extra opacity the SVG gives a shape.
struct IconRoleColours {
    juce::Colour hue;   // the main shapes
    juce::Colour soft;  // back shapes (usually the hue at kRoleSoftAlpha)
    juce::Colour ink;   // dark details drawn on the colour
    juce::Colour paper; // light details
};
inline constexpr juce::uint32 kRoleHue = 0xffff00ff;
inline constexpr juce::uint32 kRoleInk = 0xff00ffff;
inline constexpr juce::uint32 kRolePaper = 0xffffff00;
inline constexpr float kRoleSoftAlpha = 0.45f;

class IconLibrary {
public:
    IconLibrary();

    // setTintColour: always starts from the untinted original, so repeated calls across
    // theme switches produce correct results (does NOT accumulate tint). No-op if the icon
    // is absent (headless / missing assets).
    void setTintColour(Icon id, juce::Colour c);

    // createRecoloured: a fresh clone of the UNTINTED original with its placeholder roles mapped to
    // `roles` (caller owns). Nullptr if the icon is absent.
    std::unique_ptr<juce::Drawable> createRecoloured(Icon id, const IconRoleColours& roles) const;

    // Maps the placeholder roles of `drawable` (fills and strokes, recursively) to `roles`, in one
    // pass: a colour is classified before it is written, so a role colour that happens to equal a
    // placeholder is never mapped twice.
    static void recolourRoles(juce::Drawable& drawable, const IconRoleColours& roles);

    // getDrawable: returns a fresh clone (caller owns). Returns nullptr if the icon is absent.
    std::unique_ptr<juce::Drawable> getDrawable(Icon id) const;

    // peekDrawable: non-owning view into the tinted cache. Nullptr if absent.
    const juce::Drawable* peekDrawable(Icon id) const noexcept;

private:
    // Two parallel arrays:
    //   originals_: loaded from BinaryData, never mutated (source for re-tinting)
    //   drawables_: tinted copies (updated by setTintColour)
    std::array<std::unique_ptr<juce::Drawable>, (size_t)Icon::kCount> originals_;
    std::array<std::unique_ptr<juce::Drawable>, (size_t)Icon::kCount> drawables_;

    static std::pair<const void*, int> binaryDataForIcon(Icon id); // #ifdef guarded
    static std::unique_ptr<juce::Drawable> loadSVG(const void* data, int size);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IconLibrary)
};

} // namespace synth::theme
