#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>

namespace synth::theme {

// The canonical icon set. White-filled SVG glyphs, tinted programmatically at
// theme-apply time. Backed by Assets BinaryData (see CMakeLists.txt) when
// HAS_FONT_ASSETS is defined; otherwise every entry is a null fallback so
// headless tests (no asset library) still link and run.
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
    // Toolbar toggle for the GraphEditor minimap overlay (issue #159).
    ToggleMinimap,
    // Module header: split one Audio jack into Left/Right (Dual I/O).
    ModuleDualIO,
    // Timeline edit-tool strip (Cubase-style tools; see Source/UI/EditTool.h). Index-order here
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
    kCount
};

class IconLibrary {
public:
    IconLibrary();

    // setTintColour: always starts from the untinted original, so repeated calls across
    // theme switches produce correct results (does NOT accumulate tint). No-op if the icon
    // is absent (headless / missing assets).
    void setTintColour(Icon id, juce::Colour c);

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
