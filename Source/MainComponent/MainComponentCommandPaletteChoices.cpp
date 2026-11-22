// MainComponentCommandPaletteChoices.cpp -- the settings the command palette sets from a list of choices in its own bar
// (Theme, Smart connections, Connect side by side, Autosave interval, and the Preferences dropdowns with a short list:
// Animations, Duplicate points, Macro auto-ports, panel detach, Mixer placement, section drop, and the Appearance
// pickers Depth, Knob style and Fader style). Each pick writes the same key and runs the same live effect as the
// control that holds it in Settings, so the two never disagree.
#include "MainComponent.h"
#include "MainComponentCommandPalette.h"
#include "UI/CommandPalette/CommandPaletteRows.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/PointEditorDuplicate.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/DepthLevel.h"
#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Timeline/TimelineSectionStrip/SectionDropWords.h"
#include "UI/Timeline/TimelineSectionStrip/SectionStripSettings.h"
#include "UserSettings.h"
#include <algorithm>
#include <iterator>

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

// A setting stored as one of a few strings: `values[i]` is what choice i writes, `fallback` what an unset key reads.
// Anything the app reads at use time, or reaches through the settings-file listener, needs nothing pushed.
ChoiceSetting makeStringSetting(Env env, const char* name, const char* keywords, const char* key, const char* fallback,
                                juce::StringArray choices, std::vector<const char*> values) {
    ChoiceSetting s;
    s.name = name;
    s.keywords = keywords;
    s.choices = std::move(choices);
    const auto stored = env.settings.getValue(key, fallback);
    int current = -1;
    for (int i = 0; i < (int)values.size(); ++i)
        if (stored.equalsIgnoreCase(values[(size_t)i]))
            current = i;
    setCurrent(s, current);
    s.apply = [env, key, values](int index) {
        if (index < 0 || index >= (int)values.size())
            return;
        auto* settings = &env.settings;
        settings->setValue(key, values[(size_t)index]);
        settings->saveIfNeeded();
    };
    return s;
}

// Insert (the default) or Replace the sections under it, in the Preferences combo's words; written as that combo writes
// it.
ChoiceSetting makeSectionDropSetting(Env env) {
    using Words = synth::ui::SectionDropWords;
    ChoiceSetting s;
    s.name = "Timeline: dropping a section on other music";
    s.keywords = "sections strip arrangement drop insert replace cover shift song";
    s.choices = {Words::insertChoice(), Words::replaceChoice()};
    setCurrent(s, synth::ui::sectionsCoverOnDrop(&env.settings) ? 1 : 0);
    s.apply = [env](int index) {
        if (index < 0 || index > 1)
            return;
        auto* settings = &env.settings;
        settings->setValue(synth::ui::kTimelineSectionsCoverOnDropKey, index == 1 ? "1" : "0"); // read at use
        settings->saveIfNeeded();
    };
    return s;
}

ChoiceSetting makeAnimationsSetting(Env env) {
    using Mode = synth::ui::AnimationMode;
    static const Mode modes[] = {Mode::followSystem, Mode::full, Mode::reduced, Mode::off};
    ChoiceSetting s;
    s.name = "Animations";
    s.keywords = "motion reduce reduced fades transitions";
    s.choices = {"Follow system (macOS Reduce Motion)", "Full", "Reduced", "Off"};
    const auto now = synth::ui::animationModeFromString(env.settings.getValue(synth::ui::kAnimationModeKey, "follow"));
    setCurrent(s, (int)(std::find(std::begin(modes), std::end(modes), now) - std::begin(modes)));
    s.apply = [env](int index) {
        if (index < 0 || index >= (int)std::size(modes))
            return;
        auto* settings = &env.settings;
        settings->setValue(synth::ui::kAnimationModeKey, synth::ui::animationModeToString(modes[index]));
        settings->saveIfNeeded();
        synth::ui::setAnimationMode(modes[index]);
    };
    return s;
}

ChoiceSetting makeDuplicatePointsSetting(Env env) {
    using Placement = synth::ui::DuplicatePlacement;
    static const Placement placements[] = {Placement::afterLast, Placement::onePointSpacing, Placement::nextGridLine};
    ChoiceSetting s;
    s.name = "Duplicate points";
    s.keywords = "cmd d copy automation curve points";
    s.choices = {"Right after the last point", "One point-spacing after", "At the next grid line after the selection"};
    const auto now =
        synth::ui::duplicatePlacementFromString(env.settings.getValue(synth::ui::kDuplicatePlacementKey, "afterLast"));
    setCurrent(s, (int)(std::find(std::begin(placements), std::end(placements), now) - std::begin(placements)));
    s.apply = [env](int index) {
        if (index < 0 || index >= (int)std::size(placements))
            return;
        auto* settings = &env.settings;
        settings->setValue(synth::ui::kDuplicatePlacementKey, synth::ui::duplicatePlacementToString(placements[index]));
        settings->saveIfNeeded();
        synth::ui::setDuplicatePlacement(placements[index]);
    };
    return s;
}

ChoiceSetting makeMacroAutoPortsSetting(Env env) {
    using Pref = GraphEditor::MacroAutoPortPreference;
    static const Pref prefs[] = {Pref::Unset, Pref::AutoCreatePorts, Pref::LeaveCablesAsIs};
    auto s = makeStringSetting(env, "Macro auto-ports", "group macro cable crossing boundary ports ask",
                               "macroAutoCreatePorts", "ask", {"Always ask", "Auto-create ports", "Leave cables as is"},
                               {"ask", "auto", "leave"});
    auto write = s.apply;
    s.apply = [env, write](int index) {
        write(index);
        if (index >= 0 && index < (int)std::size(prefs))
            env.graph.setMacroAutoPortPreference(prefs[index]);
    };
    return s;
}

// Flat, Soft or Rich, as the Depth picker in Settings > Appearance sets it: the theme manager stamps it into every
// theme, persists it and broadcasts, and the window cross-fades.
ChoiceSetting makeDepthSetting(Env env) {
    using synth::theme::DepthLevel;
    static const DepthLevel levels[] = {DepthLevel::Flat, DepthLevel::Soft, DepthLevel::Rich};
    ChoiceSetting s;
    s.name = "Depth";
    s.keywords = "flat soft rich look shadow lift glow immersive";
    for (const auto level : levels)
        s.choices.add(synth::theme::depthLevelLabel(level));
    setCurrent(s,
               (int)(std::find(std::begin(levels), std::end(levels), env.themes.getDepthLevel()) - std::begin(levels)));
    s.apply = [env](int index) {
        if (index >= 0 && index < (int)std::size(levels))
            env.themes.setDepthLevel(levels[index]);
    };
    return s;
}

// The look of every rotary knob or of every fader (the two rows of the Appearance picker). Written with the other
// appearance keys, which MainComponent's settings listener re-reads to re-skin every control.
ChoiceSetting makeControlStyleSetting(Env env, bool fader) {
    using synth::theme::KnobStyle;
    ChoiceSetting s;
    s.name = fader ? "Fader style" : "Knob style";
    s.keywords = fader ? "faders sliders look appearance controls classic chunky analog neon ring"
                       : "knobs dials look appearance controls classic chunky analog neon ring";
    for (int i = 0; i < synth::theme::kKnobStyleCount; ++i)
        s.choices.add(synth::theme::knobStyleLabel((KnobStyle)i));
    const auto now = synth::theme::loadKnobAppearance(env.settings);
    setCurrent(s, (int)(fader ? now.faderStyle : now.style));
    s.apply = [env, fader](int index) {
        if (index < 0 || index >= synth::theme::kKnobStyleCount)
            return;
        auto appearance = synth::theme::loadKnobAppearance(env.settings);
        (fader ? appearance.faderStyle : appearance.style) = (KnobStyle)index;
        synth::theme::writeKnobAppearance(env.settings, appearance);
        env.settings.saveIfNeeded();
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
    settings.push_back(makeSectionDropSetting(env));
    settings.push_back(makeAnimationsSetting(env));
    settings.push_back(makeDuplicatePointsSetting(env));
    settings.push_back(makeMacroAutoPortsSetting(env));
    settings.push_back(makeDepthSetting(env));
    settings.push_back(makeControlStyleSetting(env, false));
    settings.push_back(makeControlStyleSetting(env, true));
    // Applied by MainComponent's settings listener once written, so a write is all they need.
    settings.push_back(makeStringSetting(env, "When a panel opens in its own window", "detach float mixer both places",
                                         "detachedPanelBothPlaces", "move", {"Move it there", "Show it in both places"},
                                         {"move", "both"}));
    settings.push_back(makeStringSetting(env, "Mixer placement", "mixer tab panel window dock", "mixerPlacement", "tab",
                                         {"Tab beside the Timeline", "Own panel", "Window"},
                                         {"tab", "ownPanel", "window"}));
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
