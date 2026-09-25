#pragma once

#include "UI/Mixer/MeterColourStops.h"
#include "UI/Theme/IconLibrary.h"
#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::theme {

// Single source of all theming-aware drawing. Holds a COPY of the active Theme (cheap,
// ~few hundred bytes) updated via applyTheme() on every theme change. Owned by Main.cpp /
// MainWindow and installed via juce::Desktop::setDefaultLookAndFeel().
//
// Two responsibilities:
//   (1) Re-skin all STOCK JUCE widgets by overriding LookAndFeel_V4 draw methods and by
//       setting JUCE ColourIds from theme tokens in applyTheme() (section 3).
//   (2) Provide PUBLIC helper draw methods that the bespoke painters (ModuleComponent,
//       GraphEditor) call so cards / wires / rings honor the active treatment from ONE place.
class AppLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AppLookAndFeel();
    ~AppLookAndFeel() override;

    // Store the theme, re-map every ColourId (section 3), refresh cached typefaces if the
    // family changed, and set the default sans/serif font. Does NOT repaint — the caller
    // (MainComponent::changeListenerCallback) issues the single repaint pass (section 6.5).
    void applyTheme(const Theme& theme);
    const Theme& getTheme() const noexcept { return theme; }

    // ---------- meter colour stops (FRO147) ----------
    // The ONE cache every meter painter (MixerMeter -- mixer columns, Master, a detached mixer
    // window, since they all resolve their LookAndFeel back to this same instance -- and
    // ChannelChipComponent) reads instead of rebuilding a MeterColourStops from scratch on every
    // paint. Recomputed here rather than in each painter so an edit in Settings > Appearance is a
    // single write followed by a repaint, never a per-tick rebuild.
    //
    // Absent override -> effective stops follow the active theme (MeterColourStops::fromTheme(),
    // recomputed on every applyTheme() so a theme switch moves them); a present override -> the
    // custom stops are used regardless of theme, until setMeterColourStopsOverride(nullopt)
    // ("Reset to Theme") clears it. Does NOT persist anything itself and does NOT repaint -- same
    // contract as applyTheme() above; the caller (AppearanceSettingsTab's persistence, and
    // MainComponent's startup/settings-reload paths) owns the properties file and the repaint.
    void setMeterColourStopsOverride(std::optional<synth::ui::MeterColourStops> override_);
    bool hasMeterColourStopsOverride() const noexcept { return meterColourStopsOverride.has_value(); }
    const synth::ui::MeterColourStops& getMeterColourStops() const noexcept { return meterColourStops; }

    // ---------- icon registry ----------
    // Re-tint every Icon from the active theme tokens. Called at the end of applyTheme()
    // (so a theme switch stays exactly ONE re-skin pass). Tints from the untinted originals,
    // so repeated switches are always correct (no accumulating tint).
    void retintIcons();
    // getIcon: a fresh clone of the (tinted) icon, or nullptr if assets are absent (headless).
    std::unique_ptr<juce::Drawable> getIcon(Icon id) const { return iconLibrary_.getDrawable(id); }
    // peekIcon: non-owning view into the tinted cache. Nullptr if absent.
    const juce::Drawable* peekIcon(Icon id) const noexcept { return iconLibrary_.peekDrawable(id); }

    // ---------- stock widget overrides ----------
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox&) override;
    void drawComboBoxTextWhenNothingSelected(juce::Graphics&, juce::ComboBox&, juce::Label&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                       int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override;
    void drawScrollbarButton(juce::Graphics&, juce::ScrollBar&, int width, int height, int buttonDirection,
                             bool isScrollbarVertical, bool isMouseOverButton, bool isButtonDown) override;
    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;
    // Hugs the pointer (centred, 14 px below, flipping above at the parent's edge) instead of the
    // stock 24-px sideways offset — sized with drawTooltip()'s own font so fitted text never clips.
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawDrawableButton(juce::Graphics&, juce::DrawableButton&, bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

    // Resolve a font's family name to an embedded typeface (cached). Falls back to the JUCE
    // default sans/mono if the family is unavailable (tests / missing BinaryData — section 8.4).
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    // ---------- public treatment helpers (called by bespoke painters) ----------
    // Draw a module card background honoring style/glow/shadow/blur/texture.
    //   bounds   : full card local bounds (the helper draws the header band itself at the top
    //              `headerHeight` px and the body below; ModuleComponent passes its current
    //              header height = 24).
    //   selected : accent glow border.
    //   bypassed : desaturate + dim fill + (caller still draws the "B" badge via its button).
    void drawModulePanel(juce::Graphics&, juce::Rectangle<float> bounds, int headerHeight, const juce::String& title,
                         bool selected, bool bypassed);

    // Draw a connection wire as casing + core (+ glow for Glass) along a path. The caller
    // builds the cubic-bezier path (so it can also animate dots). If `path` is empty the
    // helper builds a default cubic bezier between p1 and p2.
    //   colour      : already resolved by the caller from the theme token for the wire role.
    //   isModulation: dashes the core for mod wires (matches mockups' thinner dashed mod look).
    //   activity    : 0..1 brightness/width boost from signal peak (caller passes modSignalPeak).
    //   hovered     : highlight pass.
    void drawConnectionWire(juce::Graphics&, juce::Point<float> p1, juce::Point<float> p2, const juce::Path& path,
                            juce::Colour colour, bool isModulation, float activity, bool hovered);

    // Draw the outer Serum-style modulation ring around a knob (replaces the inline logic in
    // ModuleComponent.cpp:551-564). centre/radius in the SAME coordinate space the caller paints
    // in. baseNorm/modNorm are 0..1 parameter positions; positive determines ring color.
    // hovered (FRO288): the routing driving this ring is correlated with a hovered cable (or vice
    // versa, docs/modules/modulation.md#modulation-rings-on-knobs) -- widens the stroke by
    // Theme::Metrics::modRingHoverWidthBoost and brightens the colour, the same brighter(0.3)
    // treatment a hovered cable already gets (docs/layout/cables.md#hover).
    void drawModulationRing(juce::Graphics&, juce::Point<float> centre, float radius, float baseNorm, float modNorm,
                            bool positive, bool hovered = false);

    // Draw the reachable-range band under a modulation ring (FRO287): the arc between
    // [startNorm, endNorm] (already clamped to 0..1 by the caller -- see modDepthBandRange in
    // ModuleComponentModBand.h), same geometry as drawModulationRing, at theme.metrics
    // .modDepthBandAlpha. `colour` is the caller's already-resolved ring colour (modRingPositive
    // or modRingNegative) -- this helper does not pick it, so two bands on one knob (two
    // routings) can each carry their own colour.
    void drawModulationDepthBand(juce::Graphics&, juce::Point<float> centre, float radius, float startNorm,
                                 float endNorm, juce::Colour colour);

    // Fill a themed background (bg0 for windows/panels, bg1 for the graph canvas). When
    // `isCanvas` is true also stamps the dotted grid (matches the mockups' radial-dot grid).
    void fillThemedBackground(juce::Graphics&, juce::Rectangle<float> bounds, bool isCanvas);

    // 270° rotary sweep constants shared by knob + ring drawing (see constraint #7).
    static constexpr float kRotaryStart = -juce::MathConstants<float>::pi * 0.75f;
    static constexpr float kRotaryEnd = juce::MathConstants<float>::pi * 0.75f;

    // Shared angle mapping for drawModulationRing/drawModulationDepthBand -- a 0..1 norm to a point
    // on the same 270 degree rotary sweep, clamped. Keeping this ONE place is what keeps the ring,
    // the band it's drawn under, and (FRO288) a knob's mod-target ring-anchor point from ever
    // drifting apart geometrically. Public so ModuleComponent::getModTargetKnobAnchor can reuse it
    // (see UI/Graph/ModuleComponent/ModuleComponentInternal.h's modRingPointForNorm).
    static float modRingAngleForNorm(float norm) {
        return kRotaryStart + juce::jlimit(0.0f, 1.0f, norm) * (kRotaryEnd - kRotaryStart);
    }

private:
    void refreshTypefaces();          // (re)load cached typefaces for theme.type.uiFamily/monoFamily
    void recomputeMeterColourStops(); // override if set, else MeterColourStops::fromTheme(theme.colors)

    // Themed-widget geometry constants (section 5).
    static constexpr int kScrollbarWidth = 6;      // slim scrollbar (was JUCE default 14)
    static constexpr int kTabBarDepth = 30;        // tab bar strip height
    static constexpr float kComboArrowSize = 5.0f; // combo chevron half-width

    Theme theme{}; // active theme copy

    // FRO147: the user's pinned stop set (nullopt = follow the theme) and the effective, cached
    // result recomputeMeterColourStops() derives from it -- see the public accessors above.
    std::optional<synth::ui::MeterColourStops> meterColourStopsOverride;
    synth::ui::MeterColourStops meterColourStops;

    // SVG icon registry, re-tinted from theme tokens by retintIcons() inside applyTheme().
    IconLibrary iconLibrary_;

    // The default sans/mono typefaces for the active theme.
    juce::Typeface::Ptr uiTypeface;
    juce::Typeface::Ptr monoTypeface;

    // Per-instance typeface cache (family+weight -> Typeface), populated lazily by
    // getTypefaceForFont. Kept as an instance member (NOT a process-wide static) so the
    // cached Typeface::Ptrs are released when this LookAndFeel is destroyed — while JUCE's
    // font subsystem is still alive. A process-lifetime static would release them during
    // static teardown after JUCE's statics are gone, throwing "mutex lock failed" on exit.
    juce::HashMap<juce::String, juce::Typeface::Ptr> typefaceCache;
    juce::SpinLock typefaceCacheLock;

    // Hoisted gradients/paths for hot paths (drawModulePanel). Prevents per-paint allocation.
    // These are rebuilt on every call so they can share state across concurrent calls safely.
    std::optional<juce::Path> bodyPath;
    std::optional<juce::Path> headerPath;
    std::optional<juce::ColourGradient> glassTopHiGradient;
    std::optional<juce::ColourGradient> texturedGradient;
    std::optional<juce::ColourGradient> flatGradient;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppLookAndFeel)
};

} // namespace synth::theme
