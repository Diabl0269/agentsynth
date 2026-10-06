#pragma once

#include "UI/Mixer/MeterColourStops.h"
#include "UI/Theme/IconLibrary.h"
#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {
class IconButton;
class ColourSwatchButton;
class ToolbarButton;
} // namespace synth::ui

namespace synth::theme {

// Paints a synth::ui::IconButton (background for its style, glyph, focus ring) from `theme`.
// AppLookAndFeel::drawIconButton calls it with its own theme; IconButton calls it with a default
// Theme when it sits outside any AppLookAndFeel.
void paintIconButton(juce::Graphics& g, const synth::ui::IconButton& button, const Theme& theme, bool highlighted,
                     bool down);
// The colour the glyph is painted in: disabled, lit (the on-colour or accent), hot, then rest.
juce::Colour iconButtonGlyphColour(const Theme& theme, const synth::ui::IconButton& button, bool highlighted,
                                   bool down);

// Paints a synth::ui::ToolbarButton (ground, group chip, glyph, caption, focus ring) from `theme`.
void paintToolbarButton(juce::Graphics& g, const synth::ui::ToolbarButton& button, const Theme& theme);
// The chip a toolbar button's icon sits on, in the button's coordinates, before any press squash.
juce::Rectangle<float> toolbarChipBounds(const synth::ui::ToolbarButton& button);
// Maps the 24-unit icon grid onto the 19 px glyph centred on that chip, before any lift or squash.
juce::AffineTransform toolbarIconTransform(const synth::ui::ToolbarButton& button);

// The theme `comp` paints with: its AppLookAndFeel's, or a default-constructed Theme when it sits
// outside any (a bare test fixture). The shared controls below never fall back to colour literals.
const Theme& themeOf(const juce::Component& comp);

// What a chip is painted for. `raised` fills with surfaceHi instead of surface (a chip on a surface-
// coloured strip would vanish); `active` is the lit state (toolActive); `warning` outlines in the
// warning colour.
struct ChipState {
    bool hovered = false;
    bool down = false;
    bool warning = false;
    bool active = false;
    bool raised = false;
};

// The one chip: fill (brighter on hover and press), 1 px border, theme corner radius, and no content.
// Returns the fill it painted so a caller can pick a label colour that contrasts with it.
juce::Colour paintChip(juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme, const ChipState& state);

// The "v" of a closed combo, stroked in `colour` around `centre`; shared by combo boxes and chip combos.
void paintComboChevron(juce::Graphics& g, juce::Point<float> centre, juce::Colour colour);

// The one fold arrow: a filled triangle filling `area`, pointing right when `openness` is 0 (closed) and
// down at 1 (open), rotated in between so an animated fold turns it with the fold. A path, not a text
// glyph, so it never depends on a font's arrow coverage.
void paintDisclosureChevron(juce::Graphics& g, juce::Rectangle<float> area, float openness, juce::Colour colour);

// The theme's colour for a fold arrow: textMuted at rest, textPrimary while highlighted.
juce::Colour disclosureChevronColour(const Theme& theme, bool highlighted);

// paintDisclosureChevron in that theme colour: what every fold arrow in the app calls.
void paintDisclosureChevron(juce::Graphics& g, juce::Rectangle<float> area, float openness, const Theme& theme,
                            bool highlighted);

// A piano key drawn as a toggle (pianoKeyBlack/pianoKeyWhite base, accent when on, wash on hover/press).
void paintKeyToggle(juce::Graphics& g, juce::ToggleButton& button, const Theme& theme, bool isBlackKey,
                    bool highlighted, bool down);

// A filled colour swatch with a border, hover ring and focus ring.
void paintColourSwatch(juce::Graphics& g, const synth::ui::ColourSwatchButton& button, const Theme& theme,
                       bool highlighted, bool down);

// A text link: accent text, brighter and underlined on hover, dim when disabled.
void paintTextLink(juce::Graphics& g, juce::Button& button, const Theme& theme, juce::Justification justification,
                   bool highlighted);

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

    // ---------- Meter colour stops -------------------
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

    // ---------- Knob appearance ----------
    // The user's knob style and colour-by-family switch (Settings > Appearance > Controls). Same
    // contract as setMeterColourStopsOverride(): no persistence, no repaint -- the caller does both.
    void setKnobAppearance(synth::theme::KnobAppearance appearance) { knobAppearance = appearance; }
    const synth::theme::KnobAppearance& getKnobAppearance() const noexcept { return knobAppearance; }
    // Component property a card sets (int ModuleCategory) so its knobs take the family colour.
    static constexpr const char* kKnobFamilyProperty = synth::theme::kKnobFamilyProperty;
    // The value-arc colour for `knob`: its card's family hue when colour-by-family is on, else accent.
    juce::Colour knobValueColour(const juce::Component& knob) const;

    // ---------- icon registry ----------
    // Re-tint every Icon from the active theme tokens. Called at the end of applyTheme()
    // (so a theme switch stays exactly ONE re-skin pass). Tints from the untinted originals,
    // so repeated switches are always correct (no accumulating tint).
    void retintIcons();
    // getIcon: a fresh clone of the (tinted) icon, or nullptr if assets are absent (headless).
    std::unique_ptr<juce::Drawable> getIcon(Icon id) const { return iconLibrary_.getDrawable(id); }
    // peekIcon: non-owning view into the tinted cache. Nullptr if absent.
    const juce::Drawable* peekIcon(Icon id) const noexcept { return iconLibrary_.peekDrawable(id); }
    // A fresh multi-role icon with its roles in `roles` (IconLibrary::createRecoloured); nullptr if absent.
    std::unique_ptr<juce::Drawable> getRoleIcon(Icon id, const IconRoleColours& roles) const;

    // ---------- stock widget overrides ----------
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    // Cap-sized end inset so the fader cap is never clipped (AppLookAndFeelFader.cpp).
    int getSliderThumbRadius(juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox&) override;
    void drawComboBoxTextWhenNothingSelected(juce::Graphics&, juce::ComboBox&, juce::Label&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    // Narrowest width at which every item's text shows unclipped, for any look-and-feel.
    static int comboBoxWidthToFitItems(juce::ComboBox&);
    // Popup motion (AppLookAndFeelWindowMotion.cpp): every popup menu / dropdown window,
    // alert box and call-out is handed to synth::ui::PopupMotion as JUCE builds it, so each fades
    // and slides in and out. See docs/layout/animation.md, "Popup windows".
    void preparePopupMenuWindow(juce::Component&) override;
    juce::AlertWindow* createAlertWindow(const juce::String& title, const juce::String& message,
                                         const juce::String& button1, const juce::String& button2,
                                         const juce::String& button3, juce::MessageBoxIconType iconType, int numButtons,
                                         juce::Component* associatedComponent) override;
    int getCallOutBoxBorderSize(const juce::CallOutBox&) override;
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
    // drawToggleButton with the focus state given, so a headless test can paint the focused look.
    void paintToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                           bool keyboardFocused);
    // Hugs the pointer (centred, 14 px below, flipping above at the parent's edge) instead of the
    // stock 24-px sideways offset — sized with drawTooltip()'s own font so fitted text never clips.
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawDrawableButton(juce::Graphics&, juce::DrawableButton&, bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;
    // The one icon button (Source/UI/Layout/IconButton.h): not a JUCE override, called by IconButton::paintButton.
    void drawIconButton(juce::Graphics&, synth::ui::IconButton&, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown);
    // The top bar's button (Source/UI/Chrome/ToolbarButton/): not a JUCE override, called by
    // ToolbarButton::paintButton.
    void drawToolbarButton(juce::Graphics&, synth::ui::ToolbarButton&, bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown);
    // The shared chip, piano-key toggle, colour swatch, text link and fold arrow (AppLookAndFeelSharedControls.cpp):
    // not JUCE overrides, called by the controls that own the content.
    juce::Colour drawChip(juce::Graphics&, juce::Rectangle<float> bounds, const ChipState&);
    void drawKeyToggle(juce::Graphics&, juce::ToggleButton&, bool isBlackKey, bool shouldDrawButtonAsHighlighted,
                       bool shouldDrawButtonAsDown);
    void drawColourSwatch(juce::Graphics&, synth::ui::ColourSwatchButton&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown);
    void drawTextLink(juce::Graphics&, juce::Button&, juce::Justification, bool shouldDrawButtonAsHighlighted);
    void drawDisclosureChevron(juce::Graphics&, juce::Rectangle<float> area, float openness, bool highlighted);

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
    // hovered: the routing driving this ring is correlated with a hovered cable (or vice
    // versa, docs/modules/modulation.md#modulation-rings-on-knobs) -- widens the stroke by
    // Theme::Metrics::modRingHoverWidthBoost and brightens the colour, the same brighter(0.3)
    // treatment a hovered cable already gets (docs/layout/cables.md#hover).
    void drawModulationRing(juce::Graphics&, juce::Point<float> centre, float radius, float baseNorm, float modNorm,
                            bool positive, bool hovered = false);

    // Draw the reachable-range band under a modulation ring: the arc between
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
    // the band it's drawn under, and a knob's mod-target ring-anchor point from ever
    // drifting apart geometrically. Public so ModuleComponent::getModTargetKnobAnchor can reuse it
    // (see UI/Graph/ModuleComponent/ModuleComponentInternal.h's modRingPointForNorm).
    static float modRingAngleForNorm(float norm) {
        return kRotaryStart + juce::jlimit(0.0f, 1.0f, norm) * (kRotaryEnd - kRotaryStart);
    }

    // ---------- shortcut key cap ----------
    // The physical-key bubble used by the Cmd-hold shortcut hints: a rounded rect (Metrics::
    // cornerRadiusSmall) filled surfaceHi with a textDisabled outline (border is nearly the fill on
    // dark themes), a thicker bottom edge and a soft shadow, text in the medium mono face. `bounds`
    // is the whole cap (20 px tall, 15 in a dock tab, the `compact` flag); size it with
    // getShortcutKeyCapWidth().
    static constexpr int kKeyCapHeight = 20;
    static constexpr int kKeyCapCompactHeight = 15;
    static constexpr int kKeyCapMinWidth = 20;
    static constexpr int kKeyCapSidePadding = 6;
    static constexpr int kKeyCapBottomEdge = 2;
    static constexpr int kKeyCapShadowRadius = 4;
    juce::Font getShortcutKeyCapFont(bool compact = false) const;
    int getShortcutKeyCapWidth(const juce::String& text, bool compact = false) const;
    void drawShortcutKeyCap(juce::Graphics&, juce::Rectangle<int> bounds, const juce::String& text,
                            bool compact = false) const;

    // ---------- toggle pill and disabled controls ----------
    // A toggle whose properties hold kTogglePillProperty = true paints as a small pill (a card's
    // footer row); the same toggle otherwise paints as the tick box. A disabled knob, fader or toggle
    // paints at kDisabledControlAlpha, and so does a control marked kDimmedProperty (or a child of
    // one: a switch's segments, a stepper's buttons, a combo's text), which stays enabled.
    static constexpr const char* kTogglePillProperty = "togglePill";
    static constexpr const char* kDimmedProperty = "dimmed";
    // True when `component` paints greyed out: disabled, or it or its parent carries kDimmedProperty.
    static bool paintsDimmed(const juce::Component& component);
    static constexpr float kTogglePillFontHeight = 12.0f;
    static constexpr int kTogglePillHeight = 20;
    static constexpr float kDisabledControlAlpha = 0.45f;
    // The width a pill needs for `text`; needs no theme, so a size estimate can ask it too.
    static int togglePillWidth(const juce::String& text);
    // The embedded Inter regular at `height` (the default font only when font assets are absent), and the
    // width of `text` in it. Card and footer geometry measures with these, never the system typeface,
    // so a card lays out identically on every platform. The typeface is created per call, not cached in
    // a static (a static outlives JUCE shutdown and trips the leak detector).
    static juce::Font uiFont(float height);
    // The embedded Inter semi-bold at `height` (the default bold font when font assets are absent).
    static juce::Font uiSemiBoldFont(float height);
    static int uiTextWidth(const juce::String& text, float height);
    void paintTogglePill(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                         bool keyboardFocused);

private:
    void paintTickToggle(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted, bool keyboardFocused,
                         bool focusRingOnly = false);
    void refreshTypefaces();          // (re)load cached typefaces for theme.type.uiFamily/monoFamily
    void recomputeMeterColourStops(); // override if set, else MeterColourStops::fromTheme(theme.colors)

    // Themed-widget geometry constants (section 5).
    static constexpr int kScrollbarWidth = 6;       // slim scrollbar (was JUCE default 14)
    static constexpr int kTabBarDepth = 30;         // tab bar strip height
    static constexpr int kComboTextLeftInset = 8;   // closed combo: text label's left edge
    static constexpr int kComboTextRightInset = 22; // closed combo: room kept for the chevron

    Theme theme{}; // active theme copy

    // The user's pinned stop set (nullopt = follow the theme) and the effective, cached
    // result recomputeMeterColourStops() derives from it -- see the public accessors above.
    std::optional<synth::ui::MeterColourStops> meterColourStopsOverride;
    synth::ui::MeterColourStops meterColourStops;
    synth::theme::KnobAppearance knobAppearance;

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
