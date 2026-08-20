#include "AppLookAndFeel.h"

#ifdef HAS_FONT_ASSETS
#include "BinaryData.h"
#endif

namespace synth::theme {

namespace {
// Map a family name + weight to an embedded typeface. Only meaningful when the app is
// built with HAS_FONT_ASSETS; otherwise returns nullptr so the caller falls
// back to the JUCE default (tests / Core).
juce::Typeface::Ptr loadEmbeddedTypeface(const juce::String& family, bool bold, bool medium) {
#ifdef HAS_FONT_ASSETS
    const void* data = nullptr;
    int dataSize = 0;

    auto pick = [&](const void* d, int s) {
        data = d;
        dataSize = s;
    };

    if (family == "Inter") {
        if (bold)
            pick(BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);
        else if (medium)
            pick(BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
        else
            pick(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    } else if (family == "JetBrains Mono") {
        if (bold || medium)
            pick(BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
        else
            pick(BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    } else if (family == "Manrope") {
        if (bold)
            pick(BinaryData::ManropeBold_ttf, BinaryData::ManropeBold_ttfSize);
        else if (medium)
            pick(BinaryData::ManropeSemiBold_ttf, BinaryData::ManropeSemiBold_ttfSize);
        else
            pick(BinaryData::ManropeRegular_ttf, BinaryData::ManropeRegular_ttfSize);
    } else if (family == "Space Mono") {
        if (bold || medium)
            pick(BinaryData::SpaceMonoBold_ttf, BinaryData::SpaceMonoBold_ttfSize);
        else
            pick(BinaryData::SpaceMonoRegular_ttf, BinaryData::SpaceMonoRegular_ttfSize);
    } else if (family == "IBM Plex Sans") {
        if (bold || medium)
            pick(BinaryData::IBMPlexSansSemiBold_ttf, BinaryData::IBMPlexSansSemiBold_ttfSize);
        else
            pick(BinaryData::IBMPlexSansRegular_ttf, BinaryData::IBMPlexSansRegular_ttfSize);
    } else if (family == "IBM Plex Mono") {
        if (bold || medium)
            pick(BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
        else
            pick(BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
    }

    if (data == nullptr || dataSize == 0)
        return nullptr;

    // No caching here — getTypefaceForFont caches results in the per-instance map.
    return juce::Typeface::createSystemTypefaceFor(data, (size_t)dataSize);
#else
    juce::ignoreUnused(family, bold, medium);
    return nullptr;
#endif
}

// Build the dotted-grid tile once (22x22, single 1px dot). Tiled across the visible clip
// in fillThemedBackground so we never iterate over the 10000x10000 content bounds.
juce::Image makeGridTile(juce::Colour dot) {
    constexpr int kTile = 22;
    juce::Image img(juce::Image::ARGB, kTile, kTile, true);
    juce::Graphics g(img);
    g.setColour(dot);
    g.fillEllipse(0.0f, 0.0f, 1.5f, 1.5f);
    return img;
}
} // namespace

//==============================================================================
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

//==============================================================================
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

    setColour(juce::TextButton::buttonColourId, c.surface);
    setColour(juce::TextButton::buttonOnColourId, c.accent);
    setColour(juce::TextButton::textColourOffId, c.textPrimary);
    setColour(juce::TextButton::textColourOnId, c.bg0);

    setColour(juce::DrawableButton::textColourId, c.textPrimary);
    setColour(juce::DrawableButton::textColourOnId, c.textPrimary);

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
    // not link; the on-screen keyboard keeps JUCE defaults for now (themed in a later phase).

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

    // Toolbar actions + transport-stop + panel toggles render as primary chrome.
    iconLibrary_.setTintColour(Icon::TransportStop, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ActionNew, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ActionUndo, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ActionRedo, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ActionSave, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ActionLoad, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ActionSettings, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ActionAutoArrange, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToggleAI, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToggleMatrix, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToggleLibrary, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ThemeToggle, c.textPrimary);
    iconLibrary_.setTintColour(Icon::ToggleMinimap, c.textPrimary);

    // TransportPlay is scaffolding only (no DrawableButton wired this phase); tint muted so it
    // reads as inactive if ever surfaced.
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
}

void AppLookAndFeel::refreshTypefaces() {
    uiTypeface = loadEmbeddedTypeface(theme.type.uiFamily, false, false);
    monoTypeface = loadEmbeddedTypeface(theme.type.monoFamily, false, false);
}

juce::Typeface::Ptr AppLookAndFeel::getTypefaceForFont(const juce::Font& font) {
    const bool bold = font.isBold();
    const bool medium = false; // JUCE Font has no "medium" flag; bold/regular only.

    const juce::String family = font.getTypefaceName();

    // The mono family is requested explicitly by name; the UI family is the JUCE default
    // sans-serif name. Resolve both against the embedded set.
    juce::String resolved = family;
    if (family == juce::Font::getDefaultSansSerifFontName())
        resolved = theme.type.uiFamily;
    else if (family == juce::Font::getDefaultMonospacedFontName())
        resolved = theme.type.monoFamily;

    const juce::String key = resolved + (bold ? "|b" : "|r");
    {
        const juce::SpinLock::ScopedLockType sl(typefaceCacheLock);
        if (typefaceCache.contains(key))
            return typefaceCache[key];
    }

    if (auto face = loadEmbeddedTypeface(resolved, bold, medium)) {
        const juce::SpinLock::ScopedLockType sl(typefaceCacheLock);
        typefaceCache.set(key, face);
        return face;
    }

    return juce::LookAndFeel_V4::getTypefaceForFont(font);
}

//==============================================================================
// Stock widget overrides
//==============================================================================
void AppLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPosProportional, float /*rotaryStartAngle*/, float /*rotaryEndAngle*/,
                                      juce::Slider& slider) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const auto& tr = theme.treatment;

    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float bodyRadius = size * 0.52f * 0.5f; // ~0.52 * size diameter
    const float arcRadius = size * 0.5f - m.knobTrackWidth;

    const float startAngle = kRotaryStart;
    const float endAngle = kRotaryEnd;
    const float valueAngle = startAngle + sliderPosProportional * (endAngle - startAngle);

    // 1. Track arc.
    {
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour(c.border);
        g.strokePath(
            track, juce::PathStrokeType(m.knobTrackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 2. Value arc (+ Neon bloom).
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, valueAngle, true);

        if (tr.glow > 0.0f) {
            g.setColour(c.accent.withAlpha(tr.glow * 0.5f));
            g.strokePath(value, juce::PathStrokeType(m.knobTrackWidth * 2.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        g.setColour(c.accent);
        g.strokePath(
            value, juce::PathStrokeType(m.knobTrackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 3. Body: radial gradient surfaceHi (38%,32%) -> knobBody.
    {
        const auto bodyBounds = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
        juce::Point<float> focal(bodyBounds.getX() + bodyBounds.getWidth() * 0.38f,
                                 bodyBounds.getY() + bodyBounds.getHeight() * 0.32f);
        juce::ColourGradient grad(c.surfaceHi, focal, c.knobBody, bodyBounds.getBottomRight(), true);
        g.setGradientFill(grad);
        g.fillEllipse(bodyBounds);

        g.setColour(c.border);
        g.drawEllipse(bodyBounds, m.borderWidth);
    }

    // 4. Pointer.
    {
        const float pointerLen = bodyRadius * 0.92f; // ~0.46 * diameter
        juce::Point<float> tip(centre.x + std::sin(valueAngle) * pointerLen,
                               centre.y - std::cos(valueAngle) * pointerLen);
        g.setColour(c.knobPointer);
        g.drawLine(juce::Line<float>(centre, tip), 2.0f);
    }

    // 5. Textured striations.
    if (tr.style == ThemeStyle::Textured && tr.texture > 0.0f) {
        g.setColour(c.border.withAlpha(tr.texture * 0.25f));
        for (int i = 1; i <= 3; ++i) {
            const float r = bodyRadius * (0.4f + 0.18f * (float)i);
            g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(centre), 0.6f);
        }
    }

    juce::ignoreUnused(slider);
}

void AppLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float /*minSliderPos*/, float /*maxSliderPos*/, juce::Slider::SliderStyle style,
                                      juce::Slider& slider) {
    const auto& c = theme.colors;
    const bool vertical = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical);

    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

    if (vertical) {
        const float trackW = 4.0f;
        auto track = juce::Rectangle<float>(trackW, bounds.getHeight()).withCentre(bounds.getCentre());
        g.setColour(c.surface);
        g.fillRoundedRectangle(track, trackW * 0.5f);

        auto filled = track.withTop(sliderPos);
        g.setColour(c.accent);
        g.fillRoundedRectangle(filled, trackW * 0.5f);

        g.setColour(c.knobPointer);
        g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre({bounds.getCentreX(), sliderPos}));
    } else {
        const float trackH = 4.0f;
        auto track = juce::Rectangle<float>(bounds.getWidth(), trackH).withCentre(bounds.getCentre());
        g.setColour(c.surface);
        g.fillRoundedRectangle(track, trackH * 0.5f);

        auto filled = track.withRight(sliderPos);
        g.setColour(c.accent);
        g.fillRoundedRectangle(filled, trackH * 0.5f);

        g.setColour(c.knobPointer);
        g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre({sliderPos, bounds.getCentreY()}));
    }

    juce::ignoreUnused(slider);
}

void AppLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;

    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    juce::Colour fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.darker(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.12f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, m.pillRadius);

    g.setColour(c.border);
    g.drawRoundedRectangle(bounds, m.pillRadius, m.borderWidth);
}

juce::Font AppLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return juce::Font(juce::FontOptions((float)juce::jmin(15, buttonHeight - 6)));
}

void AppLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*shouldDrawButtonAsHighlighted*/,
                                    bool /*shouldDrawButtonAsDown*/) {
    g.setFont(getTextButtonFont(button, button.getHeight()));
    const auto colourId =
        button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;
    g.setColour(button.findColour(colourId).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

void AppLookAndFeel::drawDrawableButton(juce::Graphics& g, juce::DrawableButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    button.setColour(juce::DrawableButton::textColourId, findColour(juce::DrawableButton::textColourId));
    button.setColour(juce::DrawableButton::textColourOnId, findColour(juce::DrawableButton::textColourOnId));
    LookAndFeel_V4::drawDrawableButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

void AppLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int /*buttonX*/,
                                  int /*buttonY*/, int /*buttonW*/, int /*buttonH*/, juce::ComboBox& box) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const bool enabled = box.isEnabled();

    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);

    // Fill keyed by state: pressed → raised surface; disabled → dimmed surface; else surface.
    juce::Colour fill = c.surface;
    if (!enabled)
        fill = c.surface.withAlpha(0.5f);
    else if (isButtonDown)
        fill = c.surfaceHi;
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, m.pillRadius);

    // Outline: accent when focused, else themed border.
    g.setColour(box.hasKeyboardFocus(false) ? c.accent : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, m.pillRadius, m.borderWidth);

    // Chevron: a 2-segment "v" path stroked in the muted arrow colour (dimmed when disabled).
    const float cx = (float)width - 14.0f;
    const float cy = (float)height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - kComboArrowSize, cy - kComboArrowSize * 0.4f);
    chevron.lineTo(cx, cy + kComboArrowSize * 0.6f);
    chevron.lineTo(cx + kComboArrowSize, cy - kComboArrowSize * 0.4f);

    auto arrowCol = box.findColour(juce::ComboBox::arrowColourId);
    g.setColour(enabled ? arrowCol : arrowCol.withMultipliedAlpha(0.4f));
    g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Waveform icon: if the selected item carries a Drawable icon, render it inside the closed
    // combo box to the left of the text label (which positionComboBoxText positions at x=8).
    // We iterate the root menu to find the item matching the current selection.
    const int selectedId = box.getSelectedId();
    if (selectedId > 0) {
        const juce::PopupMenu* rootMenu = box.getRootMenu();
        if (rootMenu != nullptr) {
            juce::PopupMenu::MenuItemIterator it(*rootMenu, false);
            while (it.next()) {
                const auto& item = it.getItem();
                if (item.itemID == selectedId && item.image != nullptr) {
                    constexpr int kIconSize = 14;
                    // Place the icon at x=6, vertically centred. positionComboBoxText shifts
                    // the text label right to x=24 (6 + 14 px icon + 4 px gap) when an icon is present.
                    const int iconX = 6;
                    const int iconY = (height - kIconSize) / 2;
                    auto iconBounds =
                        juce::Rectangle<float>((float)iconX, (float)iconY, (float)kIconSize, (float)kIconSize);
                    g.saveState();
                    g.reduceClipRegion(iconBounds.toNearestInt());
                    item.image->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
                    g.restoreState();
                    break;
                }
            }
        }
    }
}

void AppLookAndFeel::drawComboBoxTextWhenNothingSelected(juce::Graphics& g, juce::ComboBox& box, juce::Label& label) {
    g.setColour(box.findColour(juce::ComboBox::textColourId).withMultipliedAlpha(0.4f));
    g.setFont(label.getFont());
    g.drawFittedText(box.getTextWhenNothingSelected(), label.getBounds(), label.getJustificationType(), 1);
}

void AppLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    // If the selected item carries a Drawable icon, shift the text label right to leave room
    // for the ~14 px icon (painted in drawComboBox) plus a 4 px gap.
    int leftOffset = 8;
    const int selectedId = box.getSelectedId();
    if (selectedId > 0) {
        const juce::PopupMenu* rootMenu = box.getRootMenu();
        if (rootMenu != nullptr) {
            juce::PopupMenu::MenuItemIterator it(*rootMenu, false);
            while (it.next()) {
                const auto& item = it.getItem();
                if (item.itemID == selectedId) {
                    if (item.image != nullptr)
                        leftOffset = 6 + 14 + 4; // iconX + kIconSize + gap
                    break;
                }
            }
        }
    }
    label.setBounds(leftOffset, 1, box.getWidth() - leftOffset - 22, box.getHeight() - 2);
    label.setFont(juce::Font(juce::FontOptions(theme.type.label + 2.0f)));
}

void AppLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height);
    g.setColour(c.surface);
    g.fillRoundedRectangle(bounds, m.cornerRadius * 0.6f);
    g.setColour(c.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), m.cornerRadius * 0.6f, m.borderWidth);
}

void AppLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                       bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                       const juce::String& text, const juce::String& shortcutKeyText,
                                       const juce::Drawable* icon, const juce::Colour* textColour) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;

    if (isSeparator) {
        g.setColour(c.border);
        g.fillRect(area.reduced(6, area.getHeight() / 2).withHeight(1));
        return;
    }

    // Highlight only a live (active) item under the cursor.
    if (isHighlighted && isActive) {
        g.setColour(c.accent.withAlpha(0.22f));
        g.fillRoundedRectangle(area.toFloat().reduced(2.0f, 1.0f), m.cornerRadius * 0.6f);
    }

    // Text colour: explicit override > active primary > disabled dim.
    const juce::Colour col = textColour != nullptr ? *textColour : (isActive ? c.textPrimary : c.textDisabled);
    g.setColour(col);
    g.setFont(juce::Font(juce::FontOptions(theme.type.label + 2.5f)));

    auto textArea = area.reduced(10, 0);

    // Drawn checkmark (no Unicode glyph — avoids font-dependent rendering).
    // Skipped when the item has an icon: the glyph + the closed-combo selection already
    // communicate the choice, and drawing both causes a visual overlap.
    if (isTicked && icon == nullptr) {
        const auto tickArea =
            juce::Rectangle<float>((float)area.getX() + 4.0f, (float)area.getY(), 18.0f, (float)area.getHeight());
        juce::Path tick;
        tick.startNewSubPath(tickArea.getX() + tickArea.getWidth() * 0.2f, tickArea.getCentreY());
        tick.lineTo(tickArea.getX() + tickArea.getWidth() * 0.42f,
                    tickArea.getCentreY() + tickArea.getHeight() * 0.18f);
        tick.lineTo(tickArea.getX() + tickArea.getWidth() * 0.78f,
                    tickArea.getCentreY() - tickArea.getHeight() * 0.22f);
        g.setColour(c.accent);
        g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(col);

        // Shift text right so it does not overlap the checkmark tick
        textArea = textArea.withTrimmedLeft(16);
    }

    // Waveform glyph icon: paint a ~14x14 Drawable to the left of the text.
    if (icon != nullptr) {
        constexpr int kIconSize = 14;
        const int iconX = textArea.getX();
        const int iconY = area.getY() + (area.getHeight() - kIconSize) / 2;
        auto iconBounds = juce::Rectangle<float>((float)iconX, (float)iconY, (float)kIconSize, (float)kIconSize);
        g.saveState();
        g.reduceClipRegion(iconBounds.toNearestInt());
        icon->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
        g.restoreState();
        // Shift text right so it doesn't overlap the icon (icon width + 4px gap).
        textArea = textArea.withTrimmedLeft(kIconSize + 4);
    }

    g.drawText(text, textArea, juce::Justification::centredLeft, true);

    // Submenu arrow takes priority over the shortcut readout on the right edge.
    if (hasSubMenu) {
        const float ax = (float)area.getRight() - 12.0f;
        const float ay = (float)area.getCentreY();
        const float h = 4.0f;
        juce::Path arrow;
        arrow.startNewSubPath(ax - 2.0f, ay - h);
        arrow.lineTo(ax + 2.0f, ay);
        arrow.lineTo(ax - 2.0f, ay + h);
        g.setColour(c.textMuted);
        g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else if (shortcutKeyText.isNotEmpty()) {
        g.setColour(c.textMuted);
        g.drawText(shortcutKeyText, textArea, juce::Justification::centredRight, true);
    }
}

void AppLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& /*scrollbar*/, int x, int y, int width,
                                   int height, bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                   bool isMouseOver, bool isMouseDown) {
    const auto& c = theme.colors;

    // Slim track underlay.
    auto trackBounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(1.0f);
    g.setColour(c.bg1.withAlpha(0.4f));
    g.fillRoundedRectangle(trackBounds, (float)juce::jmin(width, height) * 0.5f);

    // Thumb (clamped to a sensible minimum length so it stays grabbable).
    const int minLen = 20;
    const int len = juce::jmax(minLen, thumbSize);
    juce::Rectangle<int> thumb;
    if (isScrollbarVertical)
        thumb = {x + 1, thumbStartPosition, width - 2, len};
    else
        thumb = {thumbStartPosition, y + 1, len, height - 2};

    auto col = c.border.brighter(0.3f);
    if (isMouseDown)
        col = col.brighter(0.3f);
    else if (isMouseOver)
        col = col.brighter(0.15f);

    g.setColour(col);
    g.fillRoundedRectangle(thumb.toFloat().reduced(1.0f),
                           (float)juce::jmin(thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

int AppLookAndFeel::getDefaultScrollbarWidth() { return kScrollbarWidth; }

void AppLookAndFeel::drawScrollbarButton(juce::Graphics& g, juce::ScrollBar& /*scrollbar*/, int width, int height,
                                         int buttonDirection, bool /*isScrollbarVertical*/, bool isMouseOverButton,
                                         bool isButtonDown) {
    // Minimal filled triangle pointing in buttonDirection (0=up,1=right,2=down,3=left).
    // Rarely shown on macOS overlay scrollbars; needed for Win/Linux parity.
    const auto& c = theme.colors;
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height).reduced(2.0f);
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.4f;

    juce::Path tri;
    switch (buttonDirection) {
    case 0:
        tri.addTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r);
        break; // up
    case 1:
        tri.addTriangle(cx + r, cy, cx - r, cy - r, cx - r, cy + r);
        break; // right
    case 2:
        tri.addTriangle(cx, cy + r, cx - r, cy - r, cx + r, cy - r);
        break; // down
    default:
        tri.addTriangle(cx - r, cy, cx + r, cy - r, cx + r, cy + r);
        break; // left
    }

    g.setColour((isButtonDown || isMouseOverButton) ? c.textPrimary : c.textMuted);
    g.fillPath(tri);
}

void AppLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, theme.metrics.pillRadius);
}

void AppLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    if (editor.isEnabled()) {
        const bool focused = editor.hasKeyboardFocus(true);
        g.setColour(focused ? theme.colors.accent : editor.findColour(juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, theme.metrics.pillRadius,
                               theme.metrics.borderWidth);
    }
}

void AppLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (!label.isBeingEdited()) {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(label.getFont());

        auto area = label.getBorderSize().subtractedFrom(label.getLocalBounds());
        g.drawFittedText(label.getText(), area, label.getJustificationType(),
                         juce::jmax(1, (int)((float)area.getHeight() / label.getFont().getHeight())),
                         label.getMinimumHorizontalScale());
    }
}

void AppLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                                      bool /*shouldDrawButtonAsDown*/) {
    const auto& c = theme.colors;

    const float boxSize = juce::jmin(18.0f, (float)button.getHeight() - 2.0f);
    juce::Rectangle<float> box(4.0f, ((float)button.getHeight() - boxSize) * 0.5f, boxSize, boxSize);

    g.setColour(button.getToggleState() ? button.findColour(juce::ToggleButton::tickColourId) : c.surface);
    g.fillRoundedRectangle(box, 4.0f);

    g.setColour(button.getToggleState() ? button.findColour(juce::ToggleButton::tickColourId)
                                        : button.findColour(juce::ToggleButton::tickDisabledColourId));
    g.drawRoundedRectangle(box, 4.0f, theme.metrics.borderWidth);

    if (button.getToggleState()) {
        g.setColour(c.bg0);
        juce::Path tick;
        tick.startNewSubPath(box.getX() + box.getWidth() * 0.25f, box.getCentreY());
        tick.lineTo(box.getX() + box.getWidth() * 0.45f, box.getY() + box.getHeight() * 0.7f);
        tick.lineTo(box.getX() + box.getWidth() * 0.78f, box.getY() + box.getHeight() * 0.28f);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (button.getButtonText().isNotEmpty()) {
        g.setColour(button.findColour(juce::ToggleButton::textColourId)
                        .withMultipliedAlpha(shouldDrawButtonAsHighlighted ? 1.0f : 0.9f));
        g.setFont(juce::Font(juce::FontOptions(theme.type.label + 2.0f)));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft((int)boxSize + 10),
                         juce::Justification::centredLeft, 1);
    }
}

juce::Rectangle<int> AppLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                      juce::Rectangle<int> parentArea) {
    // The stock LookAndFeel places the tip 24 px right of (or 12 px left of) the cursor, which
    // reads as detached from small controls like the timeline transport buttons. Keep it hugging
    // the pointer instead: horizontally centred on it, 14 px below the hotspot (a typical cursor's
    // height), flipped to just above when there is no room. Measured with the SAME font
    // drawTooltip() renders with, so the fitted text never clips.
    const juce::Font font(juce::FontOptions(theme.type.label + 1.0f));
    const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(font, tipText)) + 14;
    const int h = (int)std::ceil(font.getHeight()) + 8;

    const int x = screenPos.x - w / 2;
    const bool below = screenPos.y + 14 + h <= parentArea.getBottom();
    const int y = below ? screenPos.y + 14 : screenPos.y - h - 6;
    return juce::Rectangle<int>(x, y, w, h).constrainedWithin(parentArea);
}

void AppLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    const auto& c = theme.colors;
    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height);
    g.setColour(c.surfaceHi);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(c.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, theme.metrics.borderWidth);

    g.setColour(c.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(theme.type.label + 1.0f)));
    g.drawFittedText(text, juce::Rectangle<int>(0, 0, width, height).reduced(6, 2), juce::Justification::centred, 3);
}

void AppLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver,
                                   bool /*isMouseDown*/) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const bool active = button.getToggleState();
    const auto orientation = button.getTabbedButtonBar().getOrientation();

    auto area = button.getActiveArea().toFloat();
    const float r = m.cornerRadius * 0.6f;

    if (active) {
        // Filled surface with the two corners facing AWAY from the content rounded.
        juce::Path bg;
        switch (orientation) {
        case juce::TabbedButtonBar::TabsAtBottom:
            bg.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, false, false,
                                   true, true);
            break;
        case juce::TabbedButtonBar::TabsAtLeft:
            bg.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, true, false, true,
                                   false);
            break;
        case juce::TabbedButtonBar::TabsAtRight:
            bg.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, false, true,
                                   false, true);
            break;
        case juce::TabbedButtonBar::TabsAtTop:
        default:
            bg.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), r, r, true, true, false,
                                   false);
            break;
        }
        g.setColour(c.surface);
        g.fillPath(bg);

        // 2px accent indicator on the content-facing inner edge.
        g.setColour(c.accent);
        auto edge = area;
        switch (orientation) {
        case juce::TabbedButtonBar::TabsAtBottom:
            g.fillRect(edge.removeFromTop(2.0f));
            break;
        case juce::TabbedButtonBar::TabsAtLeft:
            g.fillRect(edge.removeFromRight(2.0f));
            break;
        case juce::TabbedButtonBar::TabsAtRight:
            g.fillRect(edge.removeFromLeft(2.0f));
            break;
        case juce::TabbedButtonBar::TabsAtTop:
        default:
            g.fillRect(edge.removeFromBottom(2.0f));
            break;
        }
    } else {
        if (isMouseOver) {
            g.setColour(c.surface.withAlpha(0.35f));
            g.fillRect(area);
        }
        // Right-edge hairline divider between inactive tabs.
        g.setColour(c.border);
        g.fillRect(area.removeFromRight(1.0f));
    }

    g.setColour(active ? button.findColour(juce::TabbedButtonBar::frontTextColourId)
                       : button.findColour(juce::TabbedButtonBar::tabTextColourId));
    g.setFont(juce::Font(juce::FontOptions(theme.type.label + 2.0f, active ? juce::Font::bold : juce::Font::plain)));
    g.drawFittedText(button.getButtonText(), button.getActiveArea().reduced(6, 0), juce::Justification::centred, 1);
}

void AppLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar, juce::Graphics& g) {
    const auto& c = theme.colors;
    auto bounds = bar.getLocalBounds().toFloat();

    g.setColour(c.bg0);
    g.fillRect(bounds);

    // Hairline along the content-facing edge of the tab-button strip.
    g.setColour(c.border);
    switch (bar.getOrientation()) {
    case juce::TabbedButtonBar::TabsAtBottom:
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.0f);
        break;
    case juce::TabbedButtonBar::TabsAtLeft:
        g.fillRect(bounds.getRight() - 1.0f, bounds.getY(), 1.0f, bounds.getHeight());
        break;
    case juce::TabbedButtonBar::TabsAtRight:
        g.fillRect(bounds.getX(), bounds.getY(), 1.0f, bounds.getHeight());
        break;
    case juce::TabbedButtonBar::TabsAtTop:
    default:
        g.fillRect(bounds.getX(), bounds.getBottom() - 1.0f, bounds.getWidth(), 1.0f);
        break;
    }
}

//==============================================================================
// Public treatment helpers
//==============================================================================
void AppLookAndFeel::drawModulePanel(juce::Graphics& g, juce::Rectangle<float> bounds, int headerHeight,
                                     const juce::String& title, bool selected, bool bypassed) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const auto& tr = theme.treatment;

    const float radius = m.cornerRadius;
    auto body = bounds.reduced(2.0f);

    // Soft drop shadow. We intentionally AVOID juce::DropShadow here: its per-paint gaussian
    // blur re-rasterizes every time a (buffered) card is re-rendered at a new zoom scale, which
    // was the dominant cost behind zoom-in/out lag. Instead, approximate a soft shadow with a
    // few translucent, downward-offset, expanding rounded rects — visually close, but a fraction
    // of the cost (plain fills, no blur), so zooming stays smooth.
    if (tr.shadow > 0.0f) {
        for (int i = 3; i >= 1; --i) {
            const float grow = (float)i * 1.5f;
            g.setColour(juce::Colours::black.withAlpha(tr.shadow * 0.13f / (float)i));
            g.fillRoundedRectangle(body.expanded(grow).translated(0.0f, 2.0f + (float)i), radius + grow);
        }
    }

    // Effective body fill colour (bypass desaturates/dims).
    auto surfaceCol = c.surface;
    auto surfaceHiCol = c.surfaceHi;
    if (bypassed) {
        const auto grey = juce::Colour(0xff808080);
        surfaceCol = surfaceCol.interpolatedWith(grey, 0.4f).withAlpha(0.5f);
        surfaceHiCol = surfaceHiCol.interpolatedWith(grey, 0.4f).withAlpha(0.5f);
    }

    // ---- Body fill per style ----
    {
        // Hoist path to member (avoids stack allocation on every paint)
        bodyPath.emplace();
        bodyPath->addRoundedRectangle(body, radius);
        g.saveState();
        g.reduceClipRegion(*bodyPath);

        if (tr.style == ThemeStyle::Glass) {
            g.setColour(surfaceCol);
            g.fillRect(body);
            // Top highlight gradient over the top ~40%.
            const float hAlpha = 0.05f * tr.blur + 0.05f;
            glassTopHiGradient.emplace(juce::Colours::white.withAlpha(hAlpha), body.getX(), body.getY(),
                                       juce::Colours::transparentWhite, body.getX(),
                                       body.getY() + body.getHeight() * 0.4f, false);
            g.setGradientFill(*glassTopHiGradient);
            g.fillRect(body);
        } else if (tr.style == ThemeStyle::Textured) {
            texturedGradient.emplace(surfaceHiCol, body.getX(), body.getY(), surfaceCol.darker(0.1f), body.getX(),
                                     body.getBottom(), false);
            texturedGradient->addColour(0.5, surfaceCol);
            g.setGradientFill(*texturedGradient);
            g.fillRect(body);
            // Brushed striations: O(width) vertical hairlines.
            if (tr.texture > 0.0f) {
                const int step = 3;
                for (int xi = (int)body.getX(); xi < (int)body.getRight(); xi += step) {
                    g.setColour(((xi / step) % 2 == 0) ? juce::Colours::white.withAlpha(tr.texture * 0.025f)
                                                       : juce::Colours::black.withAlpha(tr.texture * 0.03f));
                    g.drawVerticalLine(xi, body.getY(), body.getBottom());
                }
            }
        } else { // Flat
            flatGradient.emplace(surfaceHiCol, body.getX(), body.getY(), surfaceCol, body.getX(), body.getBottom(),
                                 false);
            g.setGradientFill(*flatGradient);
            g.fillRect(body);
        }

        g.restoreState();
    }

    // ---- Header band ----
    {
        auto header = body.withHeight((float)headerHeight);
        // Hoist path to member (avoids stack allocation on every paint)
        headerPath.emplace();
        headerPath->addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius,
                                        radius, true, true, false, false);
        if (tr.style == ThemeStyle::Glass)
            g.setColour(juce::Colours::white.withAlpha(0.05f));
        else
            g.setColour(surfaceHiCol);
        g.fillPath(*headerPath);

        // Bottom hairline.
        g.setColour(c.border);
        g.drawHorizontalLine((int)header.getBottom(), header.getX(), header.getRight());

        // Title: uppercase, tracked.
        g.setColour(selected ? c.accent : (bypassed ? c.textDisabled : c.textPrimary));
        g.setFont(juce::Font(juce::FontOptions(theme.type.h2, juce::Font::bold)));
        juce::String tracked;
        for (auto ch : title.toUpperCase())
            tracked << juce::String::charToString(ch) << " ";
        g.drawText(tracked.trimEnd(), header.reduced(10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft,
                   true);
    }

    // ---- Border + selection ----
    if (selected) {
        if (tr.glow > 0.0f) {
            g.setColour(c.accent.withAlpha(tr.glow * 0.2f));
            g.drawRoundedRectangle(body.expanded(4.0f), radius + 4.0f, 6.0f);
        }
        g.setColour(c.accent);
        g.drawRoundedRectangle(body, radius, 1.5f);
        g.setColour(c.accent.withAlpha(0.35f));
        g.drawRoundedRectangle(body.reduced(1.5f), radius - 1.5f, 1.0f);
    } else {
        g.setColour(c.border);
        g.drawRoundedRectangle(body, radius, m.borderWidth);
    }

    // Glass inner highlight line just inside the top edge.
    if (tr.style == ThemeStyle::Glass) {
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawHorizontalLine((int)body.getY() + 1, body.getX() + radius, body.getRight() - radius);
    }
}

void AppLookAndFeel::drawConnectionWire(juce::Graphics& g, juce::Point<float> p1, juce::Point<float> p2,
                                        const juce::Path& path, juce::Colour colour, bool isModulation, float activity,
                                        bool hovered) {
    const auto& m = theme.metrics;
    const auto& tr = theme.treatment;

    // Build the path if the caller didn't.
    juce::Path wire = path;
    if (wire.isEmpty()) {
        const float dx = p2.x - p1.x;
        wire.startNewSubPath(p1);
        wire.cubicTo(p1.x + dx * 0.5f, p1.y, p2.x - dx * 0.5f, p2.y, p2.x, p2.y);
    }

    // `activity` is a raw signal peak supplied by the caller, and not every CV source is normalised
    // (Poly MIDI's pitch fan carries Hz). Clamp before it scales any geometry — an unbounded value
    // turns the stroke into a screen-filling filled region rather than a wire.
    const float normalisedActivity = juce::jlimit(0.0f, 1.0f, activity);
    const float coreWidth = m.wireCoreWidth * (1.0f + normalisedActivity * 0.4f);

    // Casing (dark underlay).
    g.setColour(theme.colors.bg0.withAlpha(0.6f));
    g.strokePath(wire,
                 juce::PathStrokeType(m.wireCasingWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Neon glow behind the core (Glass only, cost-gated).
    if (tr.glow > 0.0f) {
        g.setColour(colour.withAlpha(tr.glow * 0.5f));
        g.strokePath(
            wire, juce::PathStrokeType(coreWidth * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Core.
    auto coreColour = colour.withMultipliedBrightness(0.5f + normalisedActivity * 0.5f);
    if (hovered)
        coreColour = colour.brighter(0.3f);
    g.setColour(coreColour);

    const float effWidth = hovered ? coreWidth + 1.0f : coreWidth;
    juce::PathStrokeType stroke(effWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    if (isModulation) {
        const float dashes[] = {6.0f, 4.0f};
        juce::Path dashed;
        stroke.createDashedStroke(dashed, wire, dashes, 2);
        g.fillPath(dashed);
    } else {
        g.strokePath(wire, stroke);
    }
}

void AppLookAndFeel::drawModulationRing(juce::Graphics& g, juce::Point<float> centre, float radius, float baseNorm,
                                        float modNorm, bool positive) {
    if (radius <= 0.0f)
        return;

    const auto& m = theme.metrics;
    const auto ringColour = positive ? theme.colors.modRingPositive : theme.colors.modRingNegative;

    const float baseAngle = kRotaryStart + juce::jlimit(0.0f, 1.0f, baseNorm) * (kRotaryEnd - kRotaryStart);
    const float modAngle = kRotaryStart + juce::jlimit(0.0f, 1.0f, modNorm) * (kRotaryEnd - kRotaryStart);

    juce::Path ring;
    ring.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(baseAngle, modAngle),
                       juce::jmax(baseAngle, modAngle), true);

    g.setColour(ringColour);
    g.strokePath(ring,
                 juce::PathStrokeType(m.knobRingWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AppLookAndFeel::fillThemedBackground(juce::Graphics& g, juce::Rectangle<float> bounds, bool isCanvas) {
    const auto& c = theme.colors;

    if (!isCanvas) {
        g.fillAll(c.bg0);
        return;
    }

    g.setColour(c.bg1);
    g.fillRect(bounds);

    // Dotted grid: tile a precomputed 22x22 image across only the visible clip bounds.
    static juce::Image tile;
    static juce::Colour tileColour;
    const auto dotColour = c.textMuted.withAlpha(0.06f);
    if (!tile.isValid() || tileColour != dotColour) {
        tile = makeGridTile(dotColour);
        tileColour = dotColour;
    }

    auto clip = g.getClipBounds().toFloat().getIntersection(bounds);
    if (!clip.isEmpty()) {
        g.saveState();
        g.reduceClipRegion(clip.toNearestInt());
        g.setTiledImageFill(tile, 0, 0, 1.0f);
        g.fillRect(clip);
        g.restoreState();
    }
}

} // namespace synth::theme
