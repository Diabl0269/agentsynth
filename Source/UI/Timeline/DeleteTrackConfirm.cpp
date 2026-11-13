// Concern: the "Delete track?" and "Delete the macro of <track>?" confirm windows and their words.
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Chrome/ConfirmDontAskDialog.h"

namespace synth::ui {

namespace {
// The undo key a person presses to bring the track back, as their platform writes it.
juce::String undoShortcutText() {
#if JUCE_MAC
    return "Cmd+Z";
#else
    return "Ctrl+Z";
#endif
}
} // namespace

DeleteTrackConfirmText deleteTrackConfirmText(const juce::String& trackName) {
    return {"Delete " + trackName + "?", "The track, its clips, its macro and any modules only it uses go. " +
                                             undoShortcutText() + " brings it back."};
}

DeleteTrackConfirmText deleteMacroConfirmText(const juce::StringArray& trackNames) {
    const auto who = trackNames.size() == 1   ? trackNames[0]
                     : trackNames.size() <= 3 ? trackNames.joinIntoString(", ")
                                              : juce::String(trackNames.size()) + " tracks";
    const juce::String verb = trackNames.size() == 1 ? " plays" : " play";
    return {"Delete the macro of " + who + "?",
            who + verb + " this macro. Remove track also deletes the " + (trackNames.size() == 1 ? "track" : "tracks") +
                " and the modules only it uses; Keep track deletes only the macro and its modules. " +
                undoShortcutText() + " brings it all back."};
}

DeleteTrackConfirmText deleteTrackMacroConfirmText(const juce::String& trackName) {
    return {"Delete " + trackName + "?",
            trackName +
                " has a macro. Remove macro also deletes the macro and the modules only the track uses; Keep "
                "macro deletes only the track and leaves the macro, its modules and its mixer channel. " +
                undoShortcutText() + " brings it all back."};
}

DeleteTrackConfirmText deleteTrackMacroConfirmText(int trackCount, int withMacro) {
    const juce::String who = withMacro == trackCount ? "They each have" : juce::String(withMacro) + " of them have";
    return {"Delete " + juce::String(trackCount) + " tracks?",
            who +
                " a macro. Remove macro also deletes each macro and the modules only its track uses; Keep macro "
                "deletes only the tracks and leaves those macros, their modules and mixer channels. " +
                undoShortcutText() + " brings it all back."};
}

DeleteTrackConfirmText deleteTrackConfirmText(int count) {
    return {"Delete " + juce::String(count) + " tracks?",
            "The tracks, their clips and their instrument nodes go. " + undoShortcutText() + " brings them back."};
}

void confirmDeleteTrack(const DeleteTrackConfirmText& text, std::function<void(bool, bool)> done) {
    if (auto& hook = test_hooks::deleteTrackConfirmHookForTest()) {
        hook(text, std::move(done));
        return;
    }
    showConfirmDontAsk({text.title, text.message, "Delete", "deleteTrackDontAskAgain",
                        "Stop asking before deleting a track. You can turn it back on in "
                        "Preferences, Timeline."},
                       std::move(done));
}

void confirmDeleteMacro(const DeleteTrackConfirmText& text, std::function<void(DeleteMacroChoice, bool)> done) {
    if (auto& hook = test_hooks::deleteMacroConfirmHookForTest()) {
        hook(text, std::move(done));
        return;
    }
    ConfirmDontAskSpec spec{text.title,
                            text.message,
                            "Remove track",
                            "deleteMacroRememberChoice",
                            "Stop asking and do this every time. You can turn the question back on in "
                            "Preferences, Timeline.",
                            "Keep track",
                            "Remember my choice"};
    showConfirmChoice(spec, [done = std::move(done)](ConfirmChoice choice, bool remember) {
        if (!done)
            return;
        done(choice == ConfirmChoice::confirm     ? DeleteMacroChoice::removeTrack
             : choice == ConfirmChoice::alternate ? DeleteMacroChoice::keepTrack
                                                  : DeleteMacroChoice::cancel,
             remember);
    });
}

void confirmDeleteTrackMacro(const DeleteTrackConfirmText& text,
                             std::function<void(DeleteTrackMacroChoice, bool)> done) {
    if (auto& hook = test_hooks::deleteTrackMacroConfirmHookForTest()) {
        hook(text, std::move(done));
        return;
    }
    ConfirmDontAskSpec spec{text.title,
                            text.message,
                            "Remove macro",
                            "deleteTrackMacroRememberChoice",
                            "Stop asking and do this every time. You can turn the question back on in "
                            "Preferences, Timeline.",
                            "Keep macro",
                            "Remember my choice"};
    showConfirmChoice(spec, [done = std::move(done)](ConfirmChoice choice, bool remember) {
        if (!done)
            return;
        done(choice == ConfirmChoice::confirm     ? DeleteTrackMacroChoice::removeMacro
             : choice == ConfirmChoice::alternate ? DeleteTrackMacroChoice::keepMacro
                                                  : DeleteTrackMacroChoice::cancel,
             remember);
    });
}

void rememberDeleteTrackMacroChoice(juce::PropertiesFile& settings, DeleteTrackMacroChoice choice) {
    if (choice == DeleteTrackMacroChoice::cancel)
        return;
    settings.setValue(kAskBeforeDeletingTrackKey, "0");
    settings.setValue(kDeleteTrackMacroChoiceKey, choice == DeleteTrackMacroChoice::keepMacro ? "keep" : "remove");
    settings.saveIfNeeded();
}

DeleteTrackMacroChoice rememberedDeleteTrackMacroChoice(const juce::PropertiesFile& settings) {
    return settings.getValue(kDeleteTrackMacroChoiceKey) == "keep" ? DeleteTrackMacroChoice::keepMacro
                                                                   : DeleteTrackMacroChoice::removeMacro;
}

void rememberDeleteMacroChoice(juce::PropertiesFile& settings, DeleteMacroChoice choice) {
    if (choice == DeleteMacroChoice::cancel)
        return;
    settings.setValue(kAskBeforeDeletingTrackKey, "0");
    settings.setValue(kDeleteMacroChoiceKey, choice == DeleteMacroChoice::keepTrack ? "keep" : "remove");
    settings.saveIfNeeded();
}

DeleteMacroChoice rememberedDeleteMacroChoice(const juce::PropertiesFile& settings) {
    return settings.getValue(kDeleteMacroChoiceKey) == "keep" ? DeleteMacroChoice::keepTrack
                                                              : DeleteMacroChoice::removeTrack;
}

namespace test_hooks {
std::function<void(const DeleteTrackConfirmText&, std::function<void(DeleteTrackMacroChoice, bool)>)>&
deleteTrackMacroConfirmHookForTest() {
    static std::function<void(const DeleteTrackConfirmText&, std::function<void(DeleteTrackMacroChoice, bool)>)> hook;
    return hook;
}

std::function<void(const DeleteTrackConfirmText&, std::function<void(DeleteMacroChoice, bool)>)>&
deleteMacroConfirmHookForTest() {
    static std::function<void(const DeleteTrackConfirmText&, std::function<void(DeleteMacroChoice, bool)>)> hook;
    return hook;
}

std::function<void(const DeleteTrackConfirmText&, std::function<void(bool, bool)>)>& deleteTrackConfirmHookForTest() {
    static std::function<void(const DeleteTrackConfirmText&, std::function<void(bool, bool)>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
