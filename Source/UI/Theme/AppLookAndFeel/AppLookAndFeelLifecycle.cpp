#include "AppLookAndFeel.h"
#include "AppLookAndFeelInternal.h"

namespace synth::theme {

// Concern: construction, theme apply (ColourId mapping), and icon retinting.

AppLookAndFeel::AppLookAndFeel() {
    // Pre-create EVERY built-in family's typefaces now, at construction (the process is fresh
    // and no text has been rendered yet). Creating an embedded typeface for the first time at
    // RUNTIME — after the app has already rendered with another font — globally corrupts text
    // (JUCE 8 + CoreText). By pre-loading all typefaces up front and having getTypefaceForFont
    // only ever RETURN these cached instances, a live theme switch never triggers a runtime
    // typeface creation, so font switching stays clean.
    static const char* const kFamilies[] = {"Inter",      "JetBrains Mono", "Manrope",
                                            "Space Mono", "IBM Plex Sans",  "IBM Plex Mono"};
    for (auto* fam : kFamilies) {
        for (bool bold : {false, true}) {
            if (auto face = loadEmbeddedTypeface(fam, bold, false)) {
                const juce::String key = juce::String(fam) + (bold ? "|b" : "|r");
                typefaceCache.set(key, face);
            }
        }
    }
    // Initialize hoisted gradients/paths for hot paths (drawModulePanel).
    // std::optional is default-constructed (empty); they will be populated on first use.
    applyTheme(theme);
}

AppLookAndFeel::~AppLookAndFeel() = default;

void AppLookAndFeel::applyTheme(const Theme& newTheme) {
    const bool familyChanged =
        (theme.type.uiFamily != newTheme.type.uiFamily) || (theme.type.monoFamily != newTheme.type.monoFamily);

    theme = newTheme;

    const auto& c = theme.colors;

    // ---- Section 3: map theme tokens onto standard JUCE ColourIds ----
    setColour(juce::ResizableWindow::backgroundColourId, c.bg0);
    setColour(juce::DocumentWindow::textColourId, c.textPrimary);

    setColour(juce::Slider::rotarySliderFillColourId, c.accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, c.border);
    setColour(juce::Slider::thumbColourId, c.knobPointer);
    setColour(juce::Slider::textBoxTextColourId, c.textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, c.bg0);
    setColour(juce::Slider::textBoxOutlineColourId, c.border);
    setColour(juce::Slider::trackColourId, c.accent);
    setColour(juce::Slider::backgroundColourId, c.surface);

    setColour(juce::Label::textColourId, c.textPrimary);
    // An EDITABLE label (setEditable) opens a juce::TextEditor whose colours do NOT come from the
    // TextEditor ids below. juce::Label::createEditorComponent copies these three across
    // afterwards, and only when the id "isColourSpecified" — which LookAndFeel_V4 does for
    // textWhenEditingColourId, from its own default-scheme white. So the app's themed
    // TextEditor::textColourId was set correctly and then CLOBBERED by V4's white on the way into
    // the editor: the timeline transport bar's BPM and time-signature fields typed white-on-white
    // on every light theme. Overriding all three here is what makes an inline label editor themed;
    // outlineWhenEditing maps to TextEditor::focusedOutlineColourId, hence the accent.
    setColour(juce::Label::textWhenEditingColourId, c.textPrimary);
    setColour(juce::Label::backgroundWhenEditingColourId, c.bg0);
    setColour(juce::Label::outlineWhenEditingColourId, c.accent);
    // The caret is its own component with its own id, and V4 leaves it at plain black — invisible
    // against the dark bg0 the editor above sits on.
    setColour(juce::CaretComponent::caretColourId, c.accent);

    setColour(juce::TextButton::buttonColourId, c.surface);
    setColour(juce::TextButton::buttonOnColourId, c.accent);
    setColour(juce::TextButton::textColourOffId, c.textPrimary);
    setColour(juce::TextButton::textColourOnId, c.bg0);

    // Unused by our custom drawDrawableButton (which computes its own rest/hover/press/toggled
    // fill + label colour from theme tokens directly — see drawDrawableButton below) but still
    // set so any stock JUCE draw path that bypasses this LookAndFeel falls back to something
    // sane rather than an undefined default.
    setColour(juce::DrawableButton::textColourId, c.textMuted);
    setColour(juce::DrawableButton::textColourOnId, c.accent);
    setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::DrawableButton::backgroundOnColourId, c.accent.withAlpha(0.13f));

    setColour(juce::ToggleButton::textColourId, c.textPrimary);
    setColour(juce::ToggleButton::tickColourId, c.accent);
    setColour(juce::ToggleButton::tickDisabledColourId, c.border);

    setColour(juce::ComboBox::backgroundColourId, c.surface);
    setColour(juce::ComboBox::textColourId, c.textPrimary);
    setColour(juce::ComboBox::outlineColourId, c.border);
    setColour(juce::ComboBox::arrowColourId, c.textMuted);
    setColour(juce::ComboBox::buttonColourId, c.surfaceHi);

    setColour(juce::PopupMenu::backgroundColourId, c.surface);
    setColour(juce::PopupMenu::textColourId, c.textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, c.accent.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, c.textPrimary);

    setColour(juce::TextEditor::backgroundColourId, c.bg0);
    setColour(juce::TextEditor::textColourId, c.textPrimary);
    setColour(juce::TextEditor::outlineColourId, c.border);
    setColour(juce::TextEditor::highlightColourId, c.accent.withAlpha(0.30f));

    setColour(juce::ScrollBar::thumbColourId, c.border.brighter(0.3f));

    setColour(juce::TooltipWindow::backgroundColourId, c.surfaceHi);
    setColour(juce::TooltipWindow::textColourId, c.textPrimary);

    setColour(juce::ListBox::backgroundColourId, c.bg0);
    setColour(juce::ListBox::textColourId, c.textPrimary);
    setColour(juce::ListBox::outlineColourId, c.border);

    setColour(juce::TabbedComponent::backgroundColourId, c.bg0);
    setColour(juce::TabbedButtonBar::tabTextColourId, c.textMuted);
    setColour(juce::TabbedButtonBar::frontTextColourId, c.textPrimary);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, c.border);

    // MidiKeyboardComponent ColourIds live in juce_audio_utils, which Core does
    // not link; ModuleComponent::applyKeyboardThemeColours themes the on-screen
    // keyboard from the active Theme instead (create + lookAndFeelChanged).

    // ---- typefaces + default font ----
    if (familyChanged || uiTypeface == nullptr)
        refreshTypefaces();

    if (uiTypeface != nullptr)
        setDefaultSansSerifTypeface(uiTypeface);

    // When the font family changes at runtime, JUCE's global typeface cache still holds
    // glyph layouts computed for the PREVIOUS typeface keyed on the unchanged default-font
    // name ("<Sans-Serif>"). Reusing them against the new typeface renders garbled glyphs
    // (wrong character->glyph mapping). Clearing forces every Font to re-resolve cleanly.
    if (familyChanged)
        juce::Typeface::clearTypefaceCache();

    // Re-tint the SVG icon set from the new theme tokens. Part of the SAME re-skin pass — no
    // extra timer / repaint is scheduled here (the caller issues the single repaint).
    retintIcons();
}

void AppLookAndFeel::retintIcons() {
    const auto& c = theme.colors;

    // Module header glyphs carry semantic intent.
    iconLibrary_.setTintColour(Icon::ModuleBypass, c.textMuted);
    iconLibrary_.setTintColour(Icon::ModuleMute, c.warning);
    iconLibrary_.setTintColour(Icon::ModuleDelete, c.error);
    iconLibrary_.setTintColour(Icon::ModuleDualIO, c.textMuted);

    // Toolbar action + panel-toggle glyphs are tinted MUTED here — this is the rest-state base
    // that MainComponent::applyToolbarIcons() clones and re-tints (via Drawable::replaceColour)
    // into the hover (textPrimary) and toggled-on (accent) variants it hands to
    // DrawableButton::setImages(). Do not tint these textPrimary here: applyToolbarIcons()'s
    // replaceColour(textMuted, ...) calls assume this exact starting colour.
    iconLibrary_.setTintColour(Icon::ActionNew, c.textMuted);
    iconLibrary_.setTintColour(Icon::ActionUndo, c.textMuted);
    iconLibrary_.setTintColour(Icon::ActionRedo, c.textMuted);
    iconLibrary_.setTintColour(Icon::ActionSave, c.textMuted);
    iconLibrary_.setTintColour(Icon::ActionLoad, c.textMuted);
    iconLibrary_.setTintColour(Icon::ActionSettings, c.textMuted);
    iconLibrary_.setTintColour(Icon::ActionAutoArrange, c.textMuted);
    iconLibrary_.setTintColour(Icon::ActionFeedback, c.textMuted);
    iconLibrary_.setTintColour(Icon::ToggleAI, c.textMuted);
    iconLibrary_.setTintColour(Icon::ToggleMatrix, c.textMuted);
    iconLibrary_.setTintColour(Icon::ToggleLibrary, c.textMuted);
    iconLibrary_.setTintColour(Icon::ThemeToggle, c.textMuted);
    iconLibrary_.setTintColour(Icon::ToggleMinimap, c.textMuted);

    // TransportStop is status-bar-only chrome (StatusBarComponent's master-mute button, which
    // sets only a single "normal" image — no hover/toggled-on variants) — kept at textPrimary,
    // unlike the toolbar set above.
    iconLibrary_.setTintColour(Icon::TransportStop, c.textPrimary);

    // TransportPlay is scaffolding (no dedicated glyph yet — see IconLibrary.h) reused as the
    // ToggleTimeline toolbar button's icon, so it follows the same muted-base convention as the
    // rest of the toolbar set above.
    iconLibrary_.setTintColour(Icon::TransportPlay, c.textMuted);

    // Library category headers are quieter than the action chrome.
    iconLibrary_.setTintColour(Icon::CatSources, c.textMuted);
    iconLibrary_.setTintColour(Icon::CatSequencing, c.textMuted);
    iconLibrary_.setTintColour(Icon::CatEnvelopes, c.textMuted);
    iconLibrary_.setTintColour(Icon::CatFilters, c.textMuted);
    iconLibrary_.setTintColour(Icon::CatModulationFX, c.textMuted);
    iconLibrary_.setTintColour(Icon::CatTimeFX, c.textMuted);
    iconLibrary_.setTintColour(Icon::CatDynamics, c.textMuted);
    iconLibrary_.setTintColour(Icon::CatUtility, c.textMuted);
    // CatIO (I/O category header + the Audio Output card's identity glyph) is appended after
    // FollowPlayhead in the enum (see IconLibrary.h), but tints the same as every other category
    // icon above.
    iconLibrary_.setTintColour(Icon::CatIO, c.textMuted);

    // Waveform glyphs: rendered as inline combo-box item icons. Tinted the same as the combo
    // text colour so they remain legible across all themes.
    iconLibrary_.setTintColour(Icon::WaveformSine, c.textPrimary);
    iconLibrary_.setTintColour(Icon::WaveformSaw, c.textPrimary);
    iconLibrary_.setTintColour(Icon::WaveformSquare, c.textPrimary);
    iconLibrary_.setTintColour(Icon::WaveformTriangle, c.textPrimary);

    // Timeline edit-tool strip glyphs: primary chrome, same tint as the other toolbar action
    // icons above. Which tool is the ACTIVE one is a per-button highlight painted with the
    // `toolActive` token, not a different icon tint — the glyph itself never changes colour.
    iconLibrary_.setTintColour(Icon::ToolSelect, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToolSplit, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToolGlue, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToolErase, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToolMute, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToolDraw, c.textPrimary);

    // Track-header kind glyphs (MIDI/Audio/Automation) are quiet chrome, same convention as the
    // library category headers above. FollowPlayhead is a toolbar-style toggle, so it follows
    // the toolbar action set's textPrimary base instead.
    iconLibrary_.setTintColour(Icon::TrackMidi, c.textMuted);
    iconLibrary_.setTintColour(Icon::TrackAudio, c.textMuted);
    iconLibrary_.setTintColour(Icon::TrackAutomation, c.textMuted);
    iconLibrary_.setTintColour(Icon::FollowPlayhead, c.textPrimary);

    // FRO12 (P9-6): DetachablePanelHost's icon-only detach/dock-back control -- muted base, same
    // convention as the toolbar action set above (DetachablePanelHost clones its own hover variant
    // the same way MainComponent::applyToolbarIcons does).
    iconLibrary_.setTintColour(Icon::ActionDetachWindow, c.textMuted);
}

} // namespace synth::theme
