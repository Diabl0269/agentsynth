#include "AppearanceSettingsTab.h"
#include "ColourPickerPopup.h"
#include "GraphEditor.h"

namespace {
// Pitch-class labels, C first — matches synth::ui::NoteColourOverrides' pitch % 12 indexing.
const char* const kPitchClassNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
} // namespace

using synth::theme::Theme;
using synth::theme::ThemeManager;

namespace {
// Active theme's row index in the registration-order list, or -1.
int activeRowIndex(const ThemeManager& mgr) {
    const auto& themes = mgr.getThemes();
    const auto& activeId = mgr.getActiveThemeId();
    for (int i = 0; i < (int)themes.size(); ++i)
        if (themes[(size_t)i].id == activeId)
            return i;
    return -1;
}
} // namespace

//==============================================================================
// ThemeListModel - draws each theme's name + a small color-swatch row.
//==============================================================================
class AppearanceSettingsTab::ThemeListModel : public juce::ListBoxModel {
public:
    explicit ThemeListModel(ThemeManager& mgr, AppearanceSettingsTab& tab)
        : themeManager(mgr)
        , owner(tab) {}

    int getNumRows() override { return (int)themeManager.getThemes().size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
        const auto& themes = themeManager.getThemes();
        if (rowNumber < 0 || rowNumber >= (int)themes.size())
            return;

        const auto& theme = themes[(size_t)rowNumber];
        const bool isActive = (theme.id == themeManager.getActiveThemeId());

        auto bounds = juce::Rectangle<int>(0, 0, width, height);

        // Row background: highlight selected/active rows.
        if (isActive)
            g.setColour(theme.colors.accent.withAlpha(0.22f));
        else if (rowIsSelected)
            g.setColour(theme.colors.accent.withAlpha(0.10f));
        else
            g.setColour(theme.colors.surface.withAlpha(0.0f));
        g.fillRect(bounds.reduced(2));

        if (isActive) {
            g.setColour(theme.colors.accent);
            g.fillRect(bounds.removeFromLeft(3));
        }

        auto content = bounds.reduced(10, 6);

        // Name (+ "user" tag) on top.
        auto nameRow = content.removeFromTop(content.getHeight() / 2);
        g.setColour(themeManager.getActiveTheme().colors.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(14.0f, isActive ? juce::Font::bold : juce::Font::plain)));
        g.drawText(theme.name, nameRow, juce::Justification::centredLeft, true);

        if (theme.isUserTheme) {
            g.setColour(themeManager.getActiveTheme().colors.textMuted);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("user", nameRow, juce::Justification::centredRight, true);
        }

        // Swatch row: accent / audioWire / modWire / surface.
        auto swatchRow = content;
        const juce::Colour swatches[] = {theme.colors.accent, theme.colors.audioWire, theme.colors.modWire,
                                         theme.colors.surface};
        const int swatchW = 18;
        const int gap = 5;
        int sx = swatchRow.getX();
        const int sy = swatchRow.getCentreY() - 6;
        for (auto col : swatches) {
            g.setColour(col);
            g.fillRoundedRectangle((float)sx, (float)sy, (float)swatchW, 12.0f, 3.0f);
            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.drawRoundedRectangle((float)sx, (float)sy, (float)swatchW, 12.0f, 3.0f, 1.0f);
            sx += swatchW + gap;
        }
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override { owner.selectThemeRow(row); }

private:
    ThemeManager& themeManager;
    AppearanceSettingsTab& owner;
};

//==============================================================================
// CableSwatchRow - clickable colour swatches for the active cable-colour mode.
//
// Left-click opens a picker; right-click resets that swatch to the theme. A pinned swatch is
// marked with a brighter ring so "following the theme" and "user override" are distinguishable
// at a glance — otherwise Reset looks like a no-op.
//==============================================================================
class AppearanceSettingsTab::CableSwatchRow
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    explicit CableSwatchRow(AppearanceSettingsTab& t)
        : owner(t) {}

    void paint(juce::Graphics& g) override {
        const int n = owner.getCableSwatchCount();
        if (n <= 0)
            return;

        const float w = (float)getWidth() / (float)n;
        for (int i = 0; i < n; ++i) {
            juce::Rectangle<float> cell((float)i * w, 0.0f, w, (float)getHeight());
            auto reducedCell = cell.reduced(3.0f);
            // 16px chip (down from 18) frees a couple more px for the label strip below, which is
            // the thing that was actually cramped.
            auto swatch = reducedCell.removeFromTop(16.0f);

            g.setColour(owner.getCableSwatchColour(i));
            g.fillRoundedRectangle(swatch, 3.0f);

            const bool pinned = owner.isCableSwatchOverridden(i);
            g.setColour(pinned ? juce::Colours::white.withAlpha(0.9f) : juce::Colours::black.withAlpha(0.4f));
            g.drawRoundedRectangle(swatch, 3.0f, pinned ? 1.8f : 1.0f);

            // Secondary text under a colour chip — textMuted is the token meant for exactly that,
            // not a hardcoded literal (the black/white pin ring above stays literal on purpose: it
            // has to read against ANY swatch colour the user might pick, not just the theme's).
            g.setColour(owner.themeManager.getActiveTheme().colors.textMuted);
            g.setFont(juce::Font(juce::FontOptions(9.5f)));
            g.drawFittedText(owner.getCableSwatchLabel(i), reducedCell.removeFromBottom(28.0f).toNearestInt(),
                             juce::Justification::centredTop, 2, 0.8f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const int n = owner.getCableSwatchCount();
        if (n <= 0)
            return;
        const int index = juce::jlimit(0, n - 1, (int)((float)e.x / ((float)getWidth() / (float)n)));

        if (e.mods.isPopupMenu()) {
            owner.resetCableSwatch(index);
            return;
        }

        const int cellW = getWidth() / n;
        owner.openCableColourPicker(index, localAreaToGlobal(juce::Rectangle<int>(index * cellW, 0, cellW, 24)));
    }

private:
    AppearanceSettingsTab& owner;
};

//==============================================================================
// NoteSwatchRow - clickable pitch-class swatches for the piano roll's note colours.
//
// Same interaction as CableSwatchRow (left-click opens a picker, right-click resets) but an
// un-overridden swatch draws hollow/dimmed rather than filled-with-a-thin-ring: a note colour
// swatch has no "always has a value" theme token backing every entry the way a cable signal kind
// does, so "not set, currently showing the theme's noteFill" has to read as visually different
// from "set to a colour that happens to be close to noteFill".
//==============================================================================
class AppearanceSettingsTab::NoteSwatchRow
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    explicit NoteSwatchRow(AppearanceSettingsTab& t)
        : owner(t) {}

    void paint(juce::Graphics& g) override {
        constexpr int n = AppearanceSettingsTab::kNoteSwatchCount;
        const float w = (float)getWidth() / (float)n;
        for (int i = 0; i < n; ++i) {
            juce::Rectangle<float> cell((float)i * w, 0.0f, w, (float)getHeight());
            auto reducedCell = cell.reduced(3.0f);
            auto swatch = reducedCell.removeFromTop(16.0f);

            const juce::Colour colour = owner.getNoteSwatchColour(i);
            const bool overridden = owner.isNoteSwatchOverridden(i);
            if (overridden) {
                g.setColour(colour);
                g.fillRoundedRectangle(swatch, 3.0f);
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.drawRoundedRectangle(swatch, 3.0f, 1.8f);
            } else {
                // Hollow/dimmed: a faint fill plus a faint outline, never the solid+bright-ring
                // treatment a pinned swatch gets — "not set" must not look like "set to grey".
                g.setColour(colour.withAlpha(0.2f));
                g.fillRoundedRectangle(swatch, 3.0f);
                g.setColour(colour.withAlpha(0.5f));
                g.drawRoundedRectangle(swatch, 3.0f, 1.0f);
            }

            g.setColour(owner.themeManager.getActiveTheme().colors.textMuted);
            g.setFont(juce::Font(juce::FontOptions(9.5f)));
            g.drawFittedText(owner.getNoteSwatchLabel(i), reducedCell.removeFromBottom(14.0f).toNearestInt(),
                             juce::Justification::centredTop, 1, 0.8f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        constexpr int n = AppearanceSettingsTab::kNoteSwatchCount;
        const int index = juce::jlimit(0, n - 1, (int)((float)e.x / ((float)getWidth() / (float)n)));

        if (e.mods.isPopupMenu()) {
            owner.resetNoteSwatch(index);
            return;
        }

        const int cellW = getWidth() / n;
        owner.openNoteColourPicker(index, localAreaToGlobal(juce::Rectangle<int>(index * cellW, 0, cellW, 24)));
    }

private:
    AppearanceSettingsTab& owner;
};

//==============================================================================
// AppearanceSettingsTab
//==============================================================================
AppearanceSettingsTab::AppearanceSettingsTab(ThemeManager& manager, juce::ApplicationProperties& props)
    : themeManager(manager)
    , appProperties(props) {
    listModel = std::make_unique<ThemeListModel>(themeManager, *this);

    // The tab itself only hosts the Viewport; every actual control below is a child of
    // contentHost, which is free to grow taller than the tab and scroll (see ContentHost's
    // comment in the header for why this exists).
    addAndMakeVisible(contentViewport);
    contentViewport.setViewedComponent(&contentHost, false);
    contentViewport.setScrollBarsShown(true, false);

    // Section headers. Same font for all three (13pt bold, matching modeLabel's existing weight)
    // so "Theme" / "Theme Gallery" / "Cables" read as one family of group titles.
    auto sectionHeaderFont = juce::Font(juce::FontOptions(13.0f, juce::Font::bold));
    contentHost.addAndMakeVisible(themeSectionLabel);
    themeSectionLabel.setText("Theme", juce::dontSendNotification);
    themeSectionLabel.setFont(sectionHeaderFont);

    contentHost.addAndMakeVisible(themeGallerySectionLabel);
    themeGallerySectionLabel.setText("Theme Gallery", juce::dontSendNotification);
    themeGallerySectionLabel.setFont(sectionHeaderFont);

    // Mode selector
    contentHost.addAndMakeVisible(modeLabel);
    modeLabel.setText("Theme Mode:", juce::dontSendNotification);
    modeLabel.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));

    contentHost.addAndMakeVisible(modeCombo);
    modeCombo.addItem("Dark", 1);
    modeCombo.addItem("Light", 2);
    modeCombo.addItem("System", 3);
    auto currentMode = themeManager.getThemeMode();
    modeCombo.setSelectedId(
        currentMode == ThemeManager::ThemeMode::Dark ? 1 : (currentMode == ThemeManager::ThemeMode::Light ? 2 : 3),
        juce::dontSendNotification);
    modeCombo.onChange = [this] {
        int id = modeCombo.getSelectedId();
        if (id == 1)
            themeManager.setThemeMode(ThemeManager::ThemeMode::Dark);
        else if (id == 2)
            themeManager.setThemeMode(ThemeManager::ThemeMode::Light);
        else if (id == 3)
            themeManager.setThemeMode(ThemeManager::ThemeMode::System);
    };

    // Default Dark Theme selector
    contentHost.addAndMakeVisible(defaultDarkLabel);
    defaultDarkLabel.setText("Default Dark Theme:", juce::dontSendNotification);
    defaultDarkLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(defaultDarkCombo);
    const auto& themes = themeManager.getThemes();
    for (int i = 0; i < (int)themes.size(); ++i) {
        defaultDarkCombo.addItem(themes[(size_t)i].name, i + 1);
    }
    juce::String currentDarkId = themeManager.getDefaultDarkThemeId();
    for (int i = 0; i < (int)themes.size(); ++i) {
        if (themes[(size_t)i].id == currentDarkId) {
            defaultDarkCombo.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    }
    defaultDarkCombo.onChange = [this] {
        int itemId = defaultDarkCombo.getSelectedId();
        if (itemId > 0) {
            const auto& ths = themeManager.getThemes();
            if (itemId - 1 < (int)ths.size())
                themeManager.setDefaultDarkThemeId(ths[(size_t)(itemId - 1)].id);
        }
    };

    // Default Light Theme selector
    contentHost.addAndMakeVisible(defaultLightLabel);
    defaultLightLabel.setText("Default Light Theme:", juce::dontSendNotification);
    defaultLightLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(defaultLightCombo);
    for (int i = 0; i < (int)themes.size(); ++i) {
        defaultLightCombo.addItem(themes[(size_t)i].name, i + 1);
    }
    juce::String currentLightId = themeManager.getDefaultLightThemeId();
    for (int i = 0; i < (int)themes.size(); ++i) {
        if (themes[(size_t)i].id == currentLightId) {
            defaultLightCombo.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    }
    defaultLightCombo.onChange = [this] {
        int itemId = defaultLightCombo.getSelectedId();
        if (itemId > 0) {
            const auto& ths = themeManager.getThemes();
            if (itemId - 1 < (int)ths.size())
                themeManager.setDefaultLightThemeId(ths[(size_t)(itemId - 1)].id);
        }
    };

    contentHost.addAndMakeVisible(themeList);
    themeList.setModel(listModel.get());
    themeList.setRowHeight(48);
    // A real fill (instead of transparentBlack) is what makes the list read as a distinct,
    // framed, scrollable surface rather than blending into the rest of the tab — the themed
    // ListBox scrollbar (AppLookAndFeel::drawScrollbar) was nearly invisible against nothing.
    themeList.setColour(juce::ListBox::backgroundColourId,
                        themeManager.getActiveTheme().colors.surface.withAlpha(0.5f));
    themeList.setColour(juce::ListBox::outlineColourId, themeManager.getActiveTheme().colors.border);
    themeList.setOutlineThickness(1);

    contentHost.addAndMakeVisible(openFolderButton);
    openFolderButton.onClick = [this] { themeManager.getUserThemesFolder().revealToUser(); };

    contentHost.addAndMakeVisible(reloadButton);
    reloadButton.onClick = [this] {
        themeManager.loadUserThemesFromFolder();
        themeList.updateContent();
        themeList.repaint();
    };

    // ---- Cable colours (issue #157) ----
    cableColourMode = synth::ui::loadCableColourMode(*appProperties.getUserSettings());
    cableColourOverrides = synth::ui::loadCableColourOverrides(*appProperties.getUserSettings());

    contentHost.addAndMakeVisible(cablesTitleLabel);
    cablesTitleLabel.setText("Cables", juce::dontSendNotification);
    // Same section-header style as "Theme" / "Theme Gallery" above, not a one-off 15pt.
    cablesTitleLabel.setFont(sectionHeaderFont);

    contentHost.addAndMakeVisible(cableModeLabel);
    cableModeLabel.setText("Colour by", juce::dontSendNotification);
    cableModeLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

    contentHost.addAndMakeVisible(cableModeCombo);
    cableModeCombo.addItem("Signal type", 1);
    cableModeCombo.addItem("Source module", 2);
    cableModeCombo.setSelectedId(cableColourMode == synth::ui::CableColourMode::BySourceCategory ? 2 : 1,
                                 juce::dontSendNotification);
    cableModeCombo.onChange = [this] {
        setCableColourMode(cableModeCombo.getSelectedId() == 2 ? synth::ui::CableColourMode::BySourceCategory
                                                               : synth::ui::CableColourMode::BySignalType);
    };

    cableSwatchRow = std::make_unique<CableSwatchRow>(*this);
    contentHost.addAndMakeVisible(*cableSwatchRow);
    cableSwatchRow->setTooltip("Click a swatch to pick a colour; right-click to reset it to the theme.");

    contentHost.addAndMakeVisible(resetCableColoursButton);
    resetCableColoursButton.onClick = [this] { resetAllCableColours(); };

    // ---- Piano roll note colours ----
    noteColourOverrides = synth::ui::loadNoteColourOverrides(*appProperties.getUserSettings());

    contentHost.addAndMakeVisible(noteColoursTitleLabel);
    noteColoursTitleLabel.setText("Piano Roll Notes", juce::dontSendNotification);
    noteColoursTitleLabel.setFont(sectionHeaderFont);

    noteSwatchRow = std::make_unique<NoteSwatchRow>(*this);
    contentHost.addAndMakeVisible(*noteSwatchRow);
    noteSwatchRow->setTooltip("Click a note to pick a colour; right-click to clear the override.");

    contentHost.addAndMakeVisible(resetNoteColoursButton);
    resetNoteColoursButton.onClick = [this] { resetAllNoteColours(); };

    // Reflect the active theme selection in the list.
    const int activeRow = activeRowIndex(themeManager);
    if (activeRow >= 0)
        themeList.selectRow(activeRow);

    themeManager.addChangeListener(this);
}

AppearanceSettingsTab::~AppearanceSettingsTab() { themeManager.removeChangeListener(this); }

void AppearanceSettingsTab::paint(juce::Graphics& g) {
    // contentHost draws the section dividers in its OWN coordinate space via paintContent() below
    // — this fill is what shows through any part of the Viewport contentHost's bounds don't reach
    // (e.g. the scrollbar gutter, or a content area shorter than the viewport itself).
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void AppearanceSettingsTab::paintContent(juce::Graphics& g) {
    // Group separators — same alpha-on-text-colour hairline as PreferencesSettingsTab, so both
    // tabs' dividers look and behave identically.
    g.setColour(findColour(juce::Label::textColourId).withAlpha(0.18f));
    for (const auto& divider : dividerBounds)
        g.fillRect(divider);
}

void AppearanceSettingsTab::resized() {
    contentViewport.setBounds(getLocalBounds());

    // Width the controls are laid out to: the viewport minus its scrollbar gutter, so a swatch row
    // or the theme gallery never sits under the thumb — same rationale, and the same
    // jmax(layoutWidth, viewport width) idiom for the HOST's own width below, as
    // ShortcutsSettingsTab::rebuildLayout(). Reserved unconditionally rather than measured: whether
    // the bar is shown depends on the content height this very pass computes.
    const int layoutWidth = juce::jmax(1, contentViewport.getWidth() - contentViewport.getScrollBarThickness());

    // Laid out in a scratch rectangle taller than this tab could ever need, then trimmed to the
    // height actually consumed below — contentHost (NOT this tab) owns that height, and the
    // Viewport clips/scrolls it. This is the fix for the "Piano Roll Notes" section rendering as
    // nothing: SettingsWindow opens this tab at a FIXED size (MainComponent's
    // settingsComp->setSize(500, 450), which becomes roughly 498x419 for this tab once
    // TabbedComponent's tab bar depth and outline are subtracted) that is shorter than every
    // section's combined natural height. Before this Viewport existed, resized() laid the trailing
    // sections out into an already-exhausted Rectangle and handed some of them a zero-height
    // bounds: present in the tree via addAndMakeVisible, but with no area to paint or hit-test.
    constexpr int kScratchHeight = 1 << 16;
    auto bounds = juce::Rectangle<int>(0, 0, layoutWidth, kScratchHeight).reduced(12);
    dividerBounds.clear();

    // Each group is followed by a hairline rule, matching PreferencesSettingsTab's addDivider().
    auto addDivider = [this, &bounds] {
        bounds.removeFromTop(10);
        dividerBounds.push_back(bounds.removeFromTop(1));
        bounds.removeFromTop(10);
    };

    // ---- 1. Theme ----
    themeSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(6);

    auto modeRow = bounds.removeFromTop(24);
    modeLabel.setBounds(modeRow.removeFromLeft(140));
    modeCombo.setBounds(modeRow.removeFromLeft(160));
    bounds.removeFromTop(6);

    auto darkRow = bounds.removeFromTop(24);
    defaultDarkLabel.setBounds(darkRow.removeFromLeft(140));
    defaultDarkCombo.setBounds(darkRow.removeFromLeft(160));
    bounds.removeFromTop(6);

    auto lightRow = bounds.removeFromTop(24);
    defaultLightLabel.setBounds(lightRow.removeFromLeft(140));
    defaultLightCombo.setBounds(lightRow.removeFromLeft(160));
    addDivider();

    // ---- 2. Theme Gallery ----
    // FIXED height now that the tab scrolls (rather than "whatever's left once every other
    // section's needs are met") — tall enough for ~3 rows of the 48px-high theme list without the
    // gallery itself immediately demanding a scroll.
    constexpr int kGalleryHeight = 160;
    themeGallerySectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(6);
    themeList.setBounds(bounds.removeFromTop(kGalleryHeight));
    addDivider();

    // ---- 3. Theme actions ----
    auto buttonRow = bounds.removeFromTop(30);
    openFolderButton.setBounds(buttonRow.removeFromLeft(160));
    buttonRow.removeFromLeft(8);
    reloadButton.setBounds(buttonRow.removeFromLeft(130));
    addDivider();

    // ---- 4. Cables ----
    cablesTitleLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(6);

    auto cableModeRow = bounds.removeFromTop(26);
    cableModeLabel.setBounds(cableModeRow.removeFromLeft(70));
    cableModeCombo.setBounds(cableModeRow.removeFromLeft(180));
    bounds.removeFromTop(6);

    cableSwatchRow->setBounds(bounds.removeFromTop(56));
    bounds.removeFromTop(6);

    resetCableColoursButton.setBounds(bounds.removeFromTop(26).removeFromLeft(170));
    addDivider();

    // ---- 5. Piano roll notes ----
    noteColoursTitleLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(6);

    noteSwatchRow->setBounds(bounds.removeFromTop(44));
    bounds.removeFromTop(6);

    resetNoteColoursButton.setBounds(bounds.removeFromTop(26).removeFromLeft(170));

    // No explicit trailing margin needed: the initial .reduced(12) above already reserved 12px at
    // BOTH the top and the bottom of the scratch rect, so kScratchHeight - bounds.getHeight() below
    // already includes that bottom margin even though nothing was laid out into it.
    const int consumedHeight = kScratchHeight - bounds.getHeight();
    contentHost.setBounds(0, 0, juce::jmax(layoutWidth, contentViewport.getWidth()),
                          juce::jmax(consumedHeight, contentViewport.getHeight()));
}

juce::String AppearanceSettingsTab::getSelectedThemeId() const {
    const int row = themeList.getSelectedRow();
    const auto& themes = themeManager.getThemes();
    if (row < 0 || row >= (int)themes.size())
        return {};
    return themes[(size_t)row].id;
}

void AppearanceSettingsTab::selectThemeRow(int row) {
    const auto& themes = themeManager.getThemes();
    if (row < 0 || row >= (int)themes.size())
        return;

    themeList.selectRow(row);
    themeManager.setActiveTheme(themes[(size_t)row].id); // instant apply (broadcasts)
}

void AppearanceSettingsTab::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // A live ColourSelector callout: apply as the user drags, so the canvas previews the colour.
    if (auto* selector = dynamic_cast<juce::ColourSelector*>(source)) {
        if (activeSwatchIndex >= 0)
            setCableSwatchColour(activeSwatchIndex, selector->getCurrentColour());
        return;
    }

    // External theme change (or our own setActiveTheme): re-sync selection + redraw.
    auto currentMode = themeManager.getThemeMode();
    modeCombo.setSelectedId(
        currentMode == ThemeManager::ThemeMode::Dark ? 1 : (currentMode == ThemeManager::ThemeMode::Light ? 2 : 3),
        juce::dontSendNotification);

    juce::String currentDarkId = themeManager.getDefaultDarkThemeId();
    const auto& ths = themeManager.getThemes();
    for (int i = 0; i < (int)ths.size(); ++i) {
        if (ths[(size_t)i].id == currentDarkId) {
            defaultDarkCombo.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    }

    juce::String currentLightId = themeManager.getDefaultLightThemeId();
    for (int i = 0; i < (int)ths.size(); ++i) {
        if (ths[(size_t)i].id == currentLightId) {
            defaultLightCombo.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    }

    const int activeRow = activeRowIndex(themeManager);
    if (activeRow >= 0)
        themeList.selectRow(activeRow);
    // The gallery's frame fill/outline is derived from the active theme, so it must be refreshed
    // along with the rows on any theme change, not just at construction.
    themeList.setColour(juce::ListBox::backgroundColourId,
                        themeManager.getActiveTheme().colors.surface.withAlpha(0.5f));
    themeList.setColour(juce::ListBox::outlineColourId, themeManager.getActiveTheme().colors.border);
    themeList.updateContent();
    themeList.repaint();

    // Un-overridden swatches follow the theme, so they have to be redrawn too.
    if (cableSwatchRow)
        cableSwatchRow->repaint();
    if (noteSwatchRow)
        noteSwatchRow->repaint();
}

//==============================================================================
// Cable colours (issue #157)
//==============================================================================

void AppearanceSettingsTab::setGraphEditor(GraphEditor* ge) {
    graphEditor = ge;
    applyCableColoursToEditor(); // the editor starts on defaults; push the persisted config
}

void AppearanceSettingsTab::applyCableColoursToEditor() {
    if (graphEditor == nullptr)
        return;
    graphEditor->setCableColourMode(cableColourMode);
    graphEditor->setCableColourOverrides(cableColourOverrides);
}

void AppearanceSettingsTab::setCableColourMode(synth::ui::CableColourMode mode) {
    cableColourMode = mode;
    synth::ui::saveCableColourMode(*appProperties.getUserSettings(), mode);
    cableModeCombo.setSelectedId(mode == synth::ui::CableColourMode::BySourceCategory ? 2 : 1,
                                 juce::dontSendNotification);
    if (cableSwatchRow)
        cableSwatchRow->repaint(); // the strip shows a different swatch set per mode
    applyCableColoursToEditor();
}

int AppearanceSettingsTab::getCableSwatchCount() const noexcept {
    return cableColourMode == synth::ui::CableColourMode::BySourceCategory ? synth::ui::kModuleCategoryCount
                                                                           : synth::ui::kCableSignalCount;
}

juce::String AppearanceSettingsTab::getCableSwatchLabel(int index) const {
    if (index < 0 || index >= getCableSwatchCount())
        return {};
    return cableColourMode == synth::ui::CableColourMode::BySourceCategory
               ? juce::String(synth::ui::moduleCategoryLabel((synth::ui::ModuleCategory)index))
               : juce::String(synth::ui::cableSignalLabel((synth::ui::CableSignal)index));
}

juce::Colour AppearanceSettingsTab::getCableSwatchColour(int index) const {
    if (index < 0 || index >= getCableSwatchCount())
        return juce::Colours::transparentBlack;

    const auto& colors = themeManager.getActiveTheme().colors;
    // Deliberately the same resolver the canvas uses (minus the bypass alpha), so a swatch can
    // never show a colour the wire does not actually take.
    if (cableColourMode == synth::ui::CableColourMode::BySourceCategory)
        return synth::ui::resolveCableBaseColour(cableColourMode, synth::ui::CableSignal::Audio,
                                                 (synth::ui::ModuleCategory)index, colors, cableColourOverrides);
    return synth::ui::resolveCableBaseColour(cableColourMode, (synth::ui::CableSignal)index,
                                             synth::ui::ModuleCategory::Utility, colors, cableColourOverrides);
}

bool AppearanceSettingsTab::isCableSwatchOverridden(int index) const {
    if (index < 0 || index >= getCableSwatchCount())
        return false;
    return cableColourMode == synth::ui::CableColourMode::BySourceCategory
               ? cableColourOverrides.category[(size_t)index].has_value()
               : cableColourOverrides.signal[(size_t)index].has_value();
}

void AppearanceSettingsTab::setCableSwatchColour(int index, juce::Colour colour) {
    if (index < 0 || index >= getCableSwatchCount())
        return;

    if (cableColourMode == synth::ui::CableColourMode::BySourceCategory)
        cableColourOverrides.setCategory((synth::ui::ModuleCategory)index, colour);
    else
        cableColourOverrides.setSignal((synth::ui::CableSignal)index, colour);

    synth::ui::saveCableColourOverrides(*appProperties.getUserSettings(), cableColourOverrides);
    if (cableSwatchRow)
        cableSwatchRow->repaint();
    applyCableColoursToEditor();
}

void AppearanceSettingsTab::resetCableSwatch(int index) {
    if (index < 0 || index >= getCableSwatchCount())
        return;

    if (cableColourMode == synth::ui::CableColourMode::BySourceCategory)
        cableColourOverrides.clearCategory((synth::ui::ModuleCategory)index);
    else
        cableColourOverrides.clearSignal((synth::ui::CableSignal)index);

    synth::ui::saveCableColourOverrides(*appProperties.getUserSettings(), cableColourOverrides);
    if (cableSwatchRow)
        cableSwatchRow->repaint();
    applyCableColoursToEditor();
}

void AppearanceSettingsTab::resetAllCableColours() {
    cableColourOverrides.clearAll(); // both modes, so one click really does mean "back to theme"
    synth::ui::saveCableColourOverrides(*appProperties.getUserSettings(), cableColourOverrides);
    if (cableSwatchRow)
        cableSwatchRow->repaint();
    applyCableColoursToEditor();
}

void AppearanceSettingsTab::openCableColourPicker(int index, juce::Rectangle<int> screenArea) {
    if (index < 0 || index >= getCableSwatchCount())
        return;

    activeSwatchIndex = index;

    auto selector = std::make_unique<juce::ColourSelector>(juce::ColourSelector::showColourAtTop |
                                                           juce::ColourSelector::showSliders |
                                                           juce::ColourSelector::showColourspace);
    selector->setName(getCableSwatchLabel(index) + " cable colour");
    selector->setCurrentColour(getCableSwatchColour(index), juce::dontSendNotification);
    selector->addChangeListener(this);
    selector->setSize(280, 340);

    // The callout takes ownership of the selector; we keep only activeSwatchIndex.
    juce::CallOutBox::launchAsynchronously(std::move(selector), screenArea, nullptr);
}

//==============================================================================
// Piano roll note colours
//==============================================================================

juce::String AppearanceSettingsTab::getNoteSwatchLabel(int pitchClass) const {
    if (pitchClass < 0 || pitchClass >= kNoteSwatchCount)
        return {};
    return kPitchClassNames[pitchClass];
}

juce::Colour AppearanceSettingsTab::getNoteSwatchColour(int pitchClass) const {
    if (pitchClass < 0 || pitchClass >= kNoteSwatchCount)
        return juce::Colours::transparentBlack;
    if (const auto& o = noteColourOverrides.perPitchClass[(size_t)pitchClass])
        return *o;
    return themeManager.getActiveTheme().colors.noteFill;
}

bool AppearanceSettingsTab::isNoteSwatchOverridden(int pitchClass) const noexcept {
    if (pitchClass < 0 || pitchClass >= kNoteSwatchCount)
        return false;
    return noteColourOverrides.perPitchClass[(size_t)pitchClass].has_value();
}

void AppearanceSettingsTab::setNoteSwatchColour(int pitchClass, juce::Colour colour) {
    if (pitchClass < 0 || pitchClass >= kNoteSwatchCount)
        return;
    noteColourOverrides.set(pitchClass, colour);
    synth::ui::saveNoteColourOverrides(*appProperties.getUserSettings(), noteColourOverrides);
    if (noteSwatchRow)
        noteSwatchRow->repaint();
}

void AppearanceSettingsTab::resetNoteSwatch(int pitchClass) {
    if (pitchClass < 0 || pitchClass >= kNoteSwatchCount)
        return;
    noteColourOverrides.clear(pitchClass);
    synth::ui::saveNoteColourOverrides(*appProperties.getUserSettings(), noteColourOverrides);
    if (noteSwatchRow)
        noteSwatchRow->repaint();
}

void AppearanceSettingsTab::resetAllNoteColours() {
    noteColourOverrides.clearAll();
    synth::ui::saveNoteColourOverrides(*appProperties.getUserSettings(), noteColourOverrides);
    if (noteSwatchRow)
        noteSwatchRow->repaint();
}

juce::Rectangle<int> AppearanceSettingsTab::getNoteSwatchRowBoundsForTest() const {
    return noteSwatchRow ? noteSwatchRow->getBounds() : juce::Rectangle<int>();
}

void AppearanceSettingsTab::openNoteColourPicker(int pitchClass, juce::Rectangle<int> screenArea) {
    if (pitchClass < 0 || pitchClass >= kNoteSwatchCount)
        return;

    const juce::Colour initial = getNoteSwatchColour(pitchClass);
    synth::ui::ColourPickerPopup::show(
        screenArea, initial, appProperties.getUserSettings(),
        [this, pitchClass](juce::Colour c) { setNoteSwatchColour(pitchClass, c); },
        [](juce::Colour) {
            // Every preview already persisted (setNoteSwatchColour saves on each change) — the
            // final commit here would just repeat the last preview's write, so there is nothing
            // left to do once the popup closes.
        });
}
