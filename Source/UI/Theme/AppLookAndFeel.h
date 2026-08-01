#pragma once

#include "IconLibrary.h"
#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

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
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;

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
    void drawModulationRing(juce::Graphics&, juce::Point<float> centre, float radius, float baseNorm, float modNorm,
                            bool positive);

    // Fill a themed background (bg0 for windows/panels, bg1 for the graph canvas). When
    // `isCanvas` is true also stamps the dotted grid (matches the mockups' radial-dot grid).
    void fillThemedBackground(juce::Graphics&, juce::Rectangle<float> bounds, bool isCanvas);

    // 270° rotary sweep constants shared by knob + ring drawing (see constraint #7).
    static constexpr float kRotaryStart = -juce::MathConstants<float>::pi * 0.75f;
    static constexpr float kRotaryEnd = juce::MathConstants<float>::pi * 0.75f;

private:
    void refreshTypefaces(); // (re)load cached typefaces for theme.type.uiFamily/monoFamily

    // Themed-widget geometry constants (section 5).
    static constexpr int kScrollbarWidth = 6;      // slim scrollbar (was JUCE default 14)
    static constexpr int kTabBarDepth = 30;        // tab bar strip height
    static constexpr float kComboArrowSize = 5.0f; // combo chevron half-width

    Theme theme{}; // active theme copy

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
