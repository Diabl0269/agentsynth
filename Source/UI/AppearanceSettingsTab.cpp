#include "AppearanceSettingsTab.h"

using gsynth::theme::Theme;
using gsynth::theme::ThemeManager;

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
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(14.0f, isActive ? juce::Font::bold : juce::Font::plain)));
        g.drawText(theme.name, nameRow, juce::Justification::centredLeft, true);

        if (theme.isUserTheme) {
            g.setColour(juce::Colours::lightgrey);
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

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Theme", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));

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
    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(6);

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
    const int activeRow = activeRowIndex(themeManager);
    if (activeRow >= 0)
        themeList.selectRow(activeRow);
    themeList.updateContent();
    themeList.repaint();
}
