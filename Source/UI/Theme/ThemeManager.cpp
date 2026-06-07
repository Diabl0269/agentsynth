#include "ThemeManager.h"
#include "BuiltInThemes.h"
#include "ThemeLoader.h"

namespace gsynth::theme {

ThemeManager::ThemeManager() {
    // Populate built-ins immediately so getActiveTheme() is always valid (even before
    // initialise() is called — e.g. when the app installs the LnF before MainComponent
    // configures appProperties).
    registerBuiltInThemes();
}

void ThemeManager::initialise(juce::ApplicationProperties* props) {
    properties = props;

    // Re-register built-ins (idempotent; clears any stale state from a second call).
    registerBuiltInThemes();

    // Load user JSON themes from the well-known folder.
    loadUserThemesFromFolder();

    // Restore the persisted selection, falling back to the default built-in.
    juce::String restoredId = getDefaultThemeId();
    if (properties != nullptr) {
        auto* settings = properties->getUserSettings();
        if (settings != nullptr) {
            juce::String saved = settings->getValue("themeId");
            if (saved.isNotEmpty())
                restoredId = saved;
        }
    }

    // Silently set the index without broadcasting — we are still in the initialisation path.
    int idx = indexOfId(restoredId);
    if (idx < 0)
        idx = indexOfId(getDefaultThemeId());
    if (idx < 0)
        idx = 0; // defensive: should never happen since built-ins always exist

    activeIndex = idx;
}

bool ThemeManager::setActiveTheme(const juce::String& id) {
    int idx = indexOfId(id);
    if (idx < 0)
        return false;

    // Idempotent: re-selecting the active theme is a silent no-op.
    if (idx == activeIndex)
        return true;

    activeIndex = idx;

    // Persist.
    if (properties != nullptr) {
        auto* settings = properties->getUserSettings();
        if (settings != nullptr)
            settings->setValue("themeId", id);
    }

    sendSynchronousChangeMessage();
    return true;
}

void ThemeManager::addUserTheme(Theme theme) {
    // Resolve id collision with a built-in.
    static const juce::StringArray builtInIds{"obsidian", "neon", "warm"};
    if (builtInIds.contains(theme.id, true /* ignore case */))
        theme.id = theme.id + "-user";

    theme.isUserTheme = true;

    int existing = indexOfId(theme.id);
    if (existing >= 0) {
        // Replace in-place (keeps the active index stable).
        themes[(size_t)existing] = std::move(theme);
    } else {
        themes.push_back(std::move(theme));
    }
}

// static
juce::File ThemeManager::getUserThemesFolder() {
    juce::File folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("Gravisynth")
                            .getChildFile("Themes");

    if (!folder.exists())
        folder.createDirectory();

    return folder;
}

int ThemeManager::loadUserThemesFromFolder() {
    // Remember whether the active theme is a user theme (it may vanish).
    bool activeWasUser =
        (activeIndex >= 0 && activeIndex < (int)themes.size()) && themes[(size_t)activeIndex].isUserTheme;
    juce::String activeIdBefore = getActiveThemeId();

    // Remove all previously-loaded user themes (built-ins sit at the front and are preserved).
    themes.erase(std::remove_if(themes.begin(), themes.end(), [](const Theme& t) { return t.isUserTheme; }),
                 themes.end());

    // Re-clamp the active index to a valid built-in position after erasure.
    // We'll fix it up properly after loading.
    activeIndex = 0;

    juce::File folder = getUserThemesFolder();
    juce::Array<juce::File> files;
    folder.findChildFiles(files, juce::File::findFiles, false, "*.gtheme.json");

    int loaded = 0;
    for (const auto& file : files) {
        juce::String content = file.loadFileAsString();
        juce::var json = juce::JSON::parse(content);

        // Derive a suggested id from the filename (lowercased slug).
        juce::String slug = file.getFileNameWithoutExtension().toLowerCase().replaceCharacters(" \t_.", "----");
        // Strip the second ".gtheme" if present (e.g. "my-theme.gtheme" -> "my-theme").
        slug = slug.replace(".gtheme", "");

        auto parsed = ThemeLoader::parseTheme(json, slug);
        if (!parsed.has_value()) {
            // Log ONCE per failed file (respect the UI-thread logger caveat — one line only).
            juce::Logger::writeToLog("[Theme] Skipped " + file.getFileName() + ": " + ThemeLoader::getLastError());
            continue;
        }

        addUserTheme(std::move(*parsed));
        ++loaded;
    }

    // Restore the previously-active theme if it still exists; otherwise fall back.
    int restoredIdx = indexOfId(activeIdBefore);
    if (restoredIdx >= 0) {
        activeIndex = restoredIdx;
    } else {
        // The active user theme vanished — fall back to the default built-in and broadcast.
        int defaultIdx = indexOfId(getDefaultThemeId());
        activeIndex = (defaultIdx >= 0) ? defaultIdx : 0;

        if (activeWasUser) {
            // Persist the new fallback.
            if (properties != nullptr) {
                auto* settings = properties->getUserSettings();
                if (settings != nullptr)
                    settings->setValue("themeId", themes[(size_t)activeIndex].id);
            }
            sendSynchronousChangeMessage();
        }
    }

    return loaded;
}

// private -----------------------------------------------------------------------

void ThemeManager::registerBuiltInThemes() {
    themes.clear();
    activeIndex = 0;

    for (auto& t : builtInThemes())
        themes.push_back(std::move(t));
}

int ThemeManager::indexOfId(const juce::String& id) const noexcept {
    for (int i = 0; i < (int)themes.size(); ++i) {
        if (themes[(size_t)i].id == id)
            return i;
    }
    return -1;
}

} // namespace gsynth::theme
