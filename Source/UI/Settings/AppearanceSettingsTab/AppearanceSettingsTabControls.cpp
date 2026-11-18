// AppearanceSettingsTabControls.cpp -- the Appearance tab's "Controls" section: the knob and fader style previews,
// the colour-by-family switch, and the Depth picker below them.
#include "../AppearanceSettingsTab.h"

// A knob or fader change is written back here; MainComponent's settings listener re-reads the keys and re-skins every
// knob and fader. Depth goes to the ThemeManager, which stamps it into every theme, persists it and broadcasts; the
// MainComponent's theme listener then re-skins and cross-fades.
void AppearanceSettingsTab::buildKnobsSection(const juce::Font& sectionHeaderFont) {
    contentHost.addAndMakeVisible(knobsTitleLabel);
    knobsTitleLabel.setText("Controls", juce::dontSendNotification);
    knobsTitleLabel.setFont(sectionHeaderFont);
    knobStylePicker = std::make_unique<synth::ui::KnobStylePicker>();
    contentHost.addAndMakeVisible(*knobStylePicker);
    knobStylePicker->setTheme(themeManager.getActiveTheme());
    knobStylePicker->setAppearance(synth::theme::loadKnobAppearance(*appProperties.getUserSettings()));
    knobStylePicker->onChanged = [this](const synth::theme::KnobAppearance& appearance) {
        // MainComponent's settings listener re-reads these keys and re-skins every knob and fader.
        synth::theme::writeKnobAppearance(*appProperties.getUserSettings(), appearance);
        appProperties.getUserSettings()->saveIfNeeded();
    };

    depthPicker = std::make_unique<synth::ui::DepthPicker>();
    contentHost.addAndMakeVisible(*depthPicker);
    depthPicker->setTheme(themeManager.getActiveTheme());
    depthPicker->setDepthLevel(themeManager.getDepthLevel());
    depthPicker->onChanged = [this](synth::theme::DepthLevel level) { themeManager.setDepthLevel(level); };
}
