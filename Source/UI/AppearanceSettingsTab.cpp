#include "AppearanceSettingsTab.h"
#include "GraphEditor.h"   // For setAlignmentGuidesEnabled callback
#include "MainComponent.h" // For setAlignmentGuidesEnabled callback

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
            auto swatch = cell.reduced(3.0f).removeFromTop(18.0f);

            g.setColour(owner.getCableSwatchColour(i));
            g.fillRoundedRectangle(swatch, 3.0f);

            const bool pinned = owner.isCableSwatchOverridden(i);
            g.setColour(pinned ? juce::Colours::white.withAlpha(0.9f) : juce::Colours::black.withAlpha(0.4f));
            g.drawRoundedRectangle(swatch, 3.0f, pinned ? 1.8f : 1.0f);

            g.setColour(juce::Colours::lightgrey);
            g.setFont(juce::Font(juce::FontOptions(9.5f)));
            g.drawFittedText(owner.getCableSwatchLabel(i), cell.removeFromBottom(22.0f).toNearestInt(),
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
// AppearanceSettingsTab
//==============================================================================
AppearanceSettingsTab::AppearanceSettingsTab(ThemeManager& manager, juce::ApplicationProperties& props)
    : themeManager(manager)
    , appProperties(props) {
    listModel = std::make_unique<ThemeListModel>(themeManager, *this);

    // Mode selector
    addAndMakeVisible(modeLabel);
    modeLabel.setText("Theme Mode:", juce::dontSendNotification);
    modeLabel.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));

    addAndMakeVisible(modeCombo);
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
    addAndMakeVisible(defaultDarkLabel);
    defaultDarkLabel.setText("Default Dark Theme:", juce::dontSendNotification);
    defaultDarkLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(defaultDarkCombo);
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
    addAndMakeVisible(defaultLightLabel);
    defaultLightLabel.setText("Default Light Theme:", juce::dontSendNotification);
    defaultLightLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(defaultLightCombo);
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

    addAndMakeVisible(themeList);
    themeList.setModel(listModel.get());
    themeList.setRowHeight(48);
    themeList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);

    addAndMakeVisible(openFolderButton);
    openFolderButton.onClick = [this] { themeManager.getUserThemesFolder().revealToUser(); };

    addAndMakeVisible(reloadButton);
    reloadButton.onClick = [this] {
        themeManager.loadUserThemesFromFolder();
        themeList.updateContent();
        themeList.repaint();
    };

    addAndMakeVisible(alignmentGuideToggle);
    alignmentGuideToggle.setToggleState(appProperties.getUserSettings()->getBoolValue("alignmentGuidesEnabled", true),
                                        juce::dontSendNotification);
    alignmentGuideToggle.onClick = [this] {
        appProperties.getUserSettings()->setValue("alignmentGuidesEnabled",
                                                  alignmentGuideToggle.getToggleState() ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        if (graphEditor)
            graphEditor->setAlignmentGuidesEnabled(alignmentGuideToggle.getToggleState());
    };

    addAndMakeVisible(smartConnectionLabel);
    smartConnectionLabel.setText("Smart connections:", juce::dontSendNotification);
    smartConnectionLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

    addAndMakeVisible(smartConnectionCombo);
    smartConnectionCombo.addItem("Off", 1);
    smartConnectionCombo.addItem("New modules only", 2);
    smartConnectionCombo.addItem("New + unwired moves", 3);
    smartConnectionCombo.addItem("All module moves", 4);
    {
        const auto mode = GraphEditor::smartConnectionModeFromString(
            appProperties.getUserSettings()->getValue("smartConnectionMode", "NewAndUnwired"));
        const int id = mode == GraphEditor::SmartConnectionMode::Off        ? 1
                       : mode == GraphEditor::SmartConnectionMode::NewOnly  ? 2
                       : mode == GraphEditor::SmartConnectionMode::AllMoves ? 4
                                                                            : 3;
        smartConnectionCombo.setSelectedId(id, juce::dontSendNotification);
    }
    smartConnectionCombo.onChange = [this] {
        GraphEditor::SmartConnectionMode mode = GraphEditor::SmartConnectionMode::NewAndUnwired;
        switch (smartConnectionCombo.getSelectedId()) {
        case 1:
            mode = GraphEditor::SmartConnectionMode::Off;
            break;
        case 2:
            mode = GraphEditor::SmartConnectionMode::NewOnly;
            break;
        case 4:
            mode = GraphEditor::SmartConnectionMode::AllMoves;
            break;
        default:
            mode = GraphEditor::SmartConnectionMode::NewAndUnwired;
            break;
        }
        appProperties.getUserSettings()->setValue("smartConnectionMode",
                                                  GraphEditor::smartConnectionModeToString(mode));
        appProperties.getUserSettings()->saveIfNeeded();
        if (graphEditor)
            graphEditor->setSmartConnectionMode(mode);
    };

    // ---- Cable colours (issue #157) ----
    cableColourMode = synth::ui::loadCableColourMode(*appProperties.getUserSettings());
    cableColourOverrides = synth::ui::loadCableColourOverrides(*appProperties.getUserSettings());

    addAndMakeVisible(cablesTitleLabel);
    cablesTitleLabel.setText("Cables", juce::dontSendNotification);
    cablesTitleLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));

    addAndMakeVisible(cableModeLabel);
    cableModeLabel.setText("Colour by", juce::dontSendNotification);
    cableModeLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

    addAndMakeVisible(cableModeCombo);
    cableModeCombo.addItem("Signal type", 1);
    cableModeCombo.addItem("Source module", 2);
    cableModeCombo.setSelectedId(cableColourMode == synth::ui::CableColourMode::BySourceCategory ? 2 : 1,
                                 juce::dontSendNotification);
    cableModeCombo.onChange = [this] {
        setCableColourMode(cableModeCombo.getSelectedId() == 2 ? synth::ui::CableColourMode::BySourceCategory
                                                               : synth::ui::CableColourMode::BySignalType);
    };

    cableSwatchRow = std::make_unique<CableSwatchRow>(*this);
    addAndMakeVisible(*cableSwatchRow);
    cableSwatchRow->setTooltip("Click a swatch to pick a colour; right-click to reset it to the theme.");

    addAndMakeVisible(resetCableColoursButton);
    resetCableColoursButton.onClick = [this] { resetAllCableColours(); };

    // Reflect the active theme selection in the list.
    const int activeRow = activeRowIndex(themeManager);
    if (activeRow >= 0)
        themeList.selectRow(activeRow);

    themeManager.addChangeListener(this);
}

AppearanceSettingsTab::~AppearanceSettingsTab() { themeManager.removeChangeListener(this); }

void AppearanceSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void AppearanceSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(12);

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
    bounds.removeFromTop(10);

    auto buttonRow = bounds.removeFromBottom(30);
    openFolderButton.setBounds(buttonRow.removeFromLeft(160));
    buttonRow.removeFromLeft(8);
    reloadButton.setBounds(buttonRow.removeFromLeft(130));

    // ---- Cables section, stacked above the theme buttons ----
    bounds.removeFromBottom(10);
    resetCableColoursButton.setBounds(bounds.removeFromBottom(26).removeFromLeft(170));
    bounds.removeFromBottom(4);
    cableSwatchRow->setBounds(bounds.removeFromBottom(42));
    bounds.removeFromBottom(4);
    {
        auto modeRow = bounds.removeFromBottom(26);
        cableModeLabel.setBounds(modeRow.removeFromLeft(70));
        cableModeCombo.setBounds(modeRow.removeFromLeft(180));
    }
    cablesTitleLabel.setBounds(bounds.removeFromBottom(22));

    // Alignment guide toggle + smart connections (under the theme list title area).
    bounds.removeFromBottom(8);
    alignmentGuideToggle.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(6);
    {
        auto smartRow = bounds.removeFromTop(24);
        smartConnectionLabel.setBounds(smartRow.removeFromLeft(140));
        smartConnectionCombo.setBounds(smartRow.removeFromLeft(200));
    }

    themeList.setBounds(bounds);
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
    themeList.updateContent();
    themeList.repaint();

    // Un-overridden swatches follow the theme, so they have to be redrawn too.
    if (cableSwatchRow)
        cableSwatchRow->repaint();
}

//==============================================================================
// Cable colours (issue #157)
//==============================================================================

void AppearanceSettingsTab::setGraphEditor(GraphEditor* ge) {
    graphEditor = ge;
    applyCableColoursToEditor(); // the editor starts on defaults; push the persisted config
    if (graphEditor != nullptr) {
        const auto mode = GraphEditor::smartConnectionModeFromString(
            appProperties.getUserSettings()->getValue("smartConnectionMode", "NewAndUnwired"));
        graphEditor->setSmartConnectionMode(mode);
    }
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
