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

    // Alignment guide toggle (UI Phase 7 - Item 4)
    bounds.removeFromBottom(8);
    alignmentGuideToggle.setBounds(bounds.removeFromTop(24));

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

void AppearanceSettingsTab::changeListenerCallback(juce::ChangeBroadcaster*) {
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
}
