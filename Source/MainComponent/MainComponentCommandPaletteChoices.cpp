// MainComponentCommandPaletteChoices.cpp -- the settings the command palette sets from a list of choices in its own bar
// (Theme, Smart connections, Connect side by side, Autosave interval). Each pick writes the same key and runs the same
// live effect as the dropdown that holds it in Settings, so the two never disagree.
#include "MainComponent.h"
#include "MainComponentCommandPalette.h"
#include "UI/CommandPalette/CommandPaletteRows.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/ThemeManager.h"
#include "UserSettings.h"

using synth::ui::PaletteEntry;
using synth::ui::PaletteKind;

namespace {

constexpr const char* kSmartConnectionKey = "smartConnectionMode";
constexpr const char* kSideBySideKey = "smartConnectionSideBySide";
constexpr const char* kAutosaveIntervalKey = "autosaveIntervalMinutes";
constexpr int kDefaultAutosaveMinutes = 2;
const int kAutosaveMinutes[] = {1, 2, 5, 10, 15, 30, 60};

juce::String minutesText(int minutes) { return juce::String(minutes) + (minutes == 1 ? " minute" : " minutes"); }

// What a setting reads and writes, handed over by the palette (a friend of MainComponent) so this file touches none of
// MainComponent's private members.
struct Env {
    synth::theme::ThemeManager& themes;
    juce::PropertiesFile& settings;
    GraphEditor& graph;
};

void setCurrent(ChoiceSetting& setting, int index) {
    setting.current = index;
    setting.currentText = index >= 0 ? setting.choices[index] : juce::String();
}

ChoiceSetting makeThemeSetting(Env env) {
    ChoiceSetting s;
    s.name = "Theme";
    s.keywords = "colours dark light look appearance neon obsidian";
    const auto& themes = env.themes.getThemes();
    int current = -1;
    for (int i = 0; i < (int)themes.size(); ++i) {
        s.choices.add(themes[(size_t)i].name);
        if (themes[(size_t)i].id == env.themes.getActiveThemeId())
            current = i;
    }
    setCurrent(s, current);
    // As in Settings: the pick becomes the default of its kind, and the mode follows so it shows at once.
    s.apply = [env](int index) {
        const auto& all = env.themes.getThemes();
        if (index < 0 || index >= (int)all.size())
            return;
        const auto& theme = all[(size_t)index];
        if (theme.isDark)
            env.themes.setDefaultDarkThemeId(theme.id);
        else
            env.themes.setDefaultLightThemeId(theme.id);
        env.themes.setThemeMode(theme.isDark ? synth::theme::ThemeManager::ThemeMode::Dark
                                             : synth::theme::ThemeManager::ThemeMode::Light);
        env.settings.saveIfNeeded();
    };
    return s;
}

ChoiceSetting makeSmartConnectionSetting(Env env) {
    using Mode = GraphEditor::SmartConnectionMode;
    static const Mode modes[] = {Mode::Off, Mode::NewOnly, Mode::NewAndUnwired, Mode::AllMoves};
    ChoiceSetting s;
    s.name = "Smart connections";
    s.keywords = "auto wire cables connect suggest";
    s.choices = {"Off", "New modules only", "When main I/O is free", "All module moves"};
    const auto now = env.graph.getSmartConnections().getSmartConnectionMode();
    setCurrent(s, (int)(std::find(std::begin(modes), std::end(modes), now) - std::begin(modes)));
    s.apply = [env](int index) {
        if (index < 0 || index >= (int)std::size(modes))
            return;
        auto* settings = &env.settings;
        settings->setValue(kSmartConnectionKey, GraphEditor::smartConnectionModeToString(modes[index]));
        settings->saveIfNeeded();
        env.graph.getSmartConnections().setSmartConnectionMode(modes[index]);
    };
    return s;
}

ChoiceSetting makeSideBySideSetting(Env env) {
    using Mode = SmartConnectionEngine::SideBySideMode;
    ChoiceSetting s;
    s.name = "Connect side by side";
    s.keywords = "smart connections insert chain ctrl hold wire";
    s.choices = {"Always", "Only while holding Ctrl"};
    setCurrent(s, env.graph.getSmartConnections().getSideBySideMode() == Mode::Always ? 0 : 1);
    s.apply = [env](int index) {
        const auto mode = index == 0 ? Mode::Always : Mode::OnlyWithCtrl;
        auto* settings = &env.settings;
        settings->setValue(kSideBySideKey, SmartConnectionEngine::sideBySideModeToString(mode));
        settings->saveIfNeeded();
        env.graph.getSmartConnections().setSideBySideMode(mode);
    };
    return s;
}

// The Settings field takes any whole number of minutes; the list offers the usual ones, and a stored value that is
// none of them shows at the row's right without a tick.
ChoiceSetting makeAutosaveSetting(Env env) {
    ChoiceSetting s;
    s.name = "Autosave interval";
    s.keywords = "minutes backups save crash recovery";
    for (const int minutes : kAutosaveMinutes)
        s.choices.add(minutesText(minutes));
    const int stored = env.settings.getIntValue(kAutosaveIntervalKey, kDefaultAutosaveMinutes);
    const auto it = std::find(std::begin(kAutosaveMinutes), std::end(kAutosaveMinutes), stored);
    setCurrent(s, it == std::end(kAutosaveMinutes) ? -1 : (int)(it - std::begin(kAutosaveMinutes)));
    if (s.current < 0)
        s.currentText = minutesText(stored);
    s.apply = [env](int index) {
        if (index < 0 || index >= (int)std::size(kAutosaveMinutes))
            return;
        auto* settings = &env.settings;
        settings->setValue(kAutosaveIntervalKey, kAutosaveMinutes[index]); // read at use, nothing to push
        settings->saveIfNeeded();
    };
    return s;
}

} // namespace

std::vector<ChoiceSetting> MainComponentCommandPalette::buildChoiceSettings(MainComponent& mc) {
    const Env env{*mc.themeManager, *mc.appProperties.getUserSettings(), mc.getGraphEditor()};
    std::vector<ChoiceSetting> settings;
    settings.push_back(makeThemeSetting(env));
    settings.push_back(makeSmartConnectionSetting(env));
    settings.push_back(makeSideBySideSetting(env));
    settings.push_back(makeAutosaveSetting(env));
    for (auto& s : settings)
        s.key = "settingChoice:" + s.name;
    return settings;
}

// One row per dropdown setting: its current choice at the right, and a pick opens the choices in the same bar.
void MainComponentCommandPalette::addChoiceSettingEntries() {
    choiceSettings_ = buildChoiceSettings(mc_);
    for (auto& setting : choiceSettings_) {
        PaletteEntry entry;
        entry.kind = PaletteKind::Setting;
        entry.key = setting.key;
        entry.text = setting.name;
        entry.keywords = setting.keywords + " settings preference choose dropdown";
        entry.hint = setting.currentText;
        entry.opensChoices = true;
        setting.entry = entries_.size();
        entries_.push_back(entry);
        runners_.push_back({});
    }
}
