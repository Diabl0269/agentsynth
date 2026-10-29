// MainComponentMacroDelete.cpp -- deleting a macro a track plays: who plays it, the question (Remove track / Keep
// track / Cancel, with Remember my choice) and what each answer does. MainComponent is declared in MainComponent.h;
// deleteTrack itself is in MainComponentTrackDelete.cpp.
#include "AppUndoManager.h"
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/DeleteTrackConfirm.h"

#include <set>

namespace {

// A track plays a macro when its bound node is one of the macro's members, or one of a nested macro's. Membership is
// what a delete removes, so this holds however the macro is folded and whatever else sits between it and the track's
// channel (a mixer channel macro, a user group around it, a parent holding several tracks).
std::vector<synth::TrackId> tracksPlayingMacros(const synth::TimelineDoc& timelineDoc, const synth::MacroSet& macros,
                                                const std::vector<juce::String>& macroIds) {
    std::set<juce::String> members;
    for (const auto& macroId : macroIds) {
        const auto inMacro = macros.descendantMembers(macroId);
        members.insert(inMacro.begin(), inMacro.end());
    }
    std::vector<synth::TrackId> tracks;
    for (const auto& track : timelineDoc.getTracks())
        if (track.bindingUuid.isNotEmpty() && members.count(track.bindingUuid) != 0)
            tracks.push_back(track.id);
    return tracks;
}

} // namespace

// Every route that deletes a macro lands here through MacroGroupController (requestDeleteMacro for the menu,
// requestDeleteSelection for the Delete key, Cut and the multi-select menu). The question and its preference are the
// track delete's (kAskBeforeDeletingTrackKey); with the question off, the answer "Remember my choice" stored applies,
// "remove" when none was (rememberedDeleteMacroChoice).
// Remove track runs deleteTrack for each track playing the macros, then deletes what is left (the rest of the
// selection, or a macro deleteTrack left because another track shares it), as one undo step. Keep track deletes the
// macros and the rest of the selection and leaves the tracks.
bool MainComponent::deleteMacrosOfTracks(const std::vector<juce::String>& macroIds, bool wholeSelection) {
    const auto tracks = tracksPlayingMacros(timelineDoc, graphEditor.getMacros(), macroIds);
    if (tracks.empty())
        return false;

    // Removing a track clears the canvas selection, so keep what was selected when the delete was asked for.
    const auto selected = graphEditor.getSelectedNodes();
    juce::Component::SafePointer<MainComponent> safeThis(this);
    const auto apply = [safeThis, macroIds, tracks, wholeSelection, selected](synth::ui::DeleteMacroChoice choice) {
        auto* self = safeThis.getComponent();
        if (self == nullptr || choice == synth::ui::DeleteMacroChoice::cancel)
            return;
        auto& controller = self->graphEditor.getMacroController();
        const auto deleteWhatIsLeft = [&] {
            if (wholeSelection) {
                self->graphEditor.setSelectedNodes(selected);
                self->graphEditor.pruneSelection();
                self->graphEditor.deleteSelection();
                return;
            }
            for (const auto& macroId : macroIds)
                controller.deleteMacroAndMembers(macroId); // does nothing for a macro that is already gone
        };
        const AppUndoManager::ScopedUndoGroup group(self->undoManager);
        if (choice == synth::ui::DeleteMacroChoice::removeTrack)
            for (const auto track : tracks)
                self->deleteTrack(track);
        deleteWhatIsLeft();
    };

    const auto* settings = appProperties.getUserSettings();
    if (settings != nullptr && !settings->getBoolValue(synth::ui::kAskBeforeDeletingTrackKey, true)) {
        apply(synth::ui::rememberedDeleteMacroChoice(*settings));
        return true;
    }
    juce::StringArray names;
    for (const auto id : tracks)
        if (const auto* track = timelineDoc.getTrack(id))
            names.add(track->name);
    synth::ui::confirmDeleteMacro(synth::ui::deleteMacroConfirmText(names),
                                  [safeThis, apply](synth::ui::DeleteMacroChoice choice, bool remember) {
                                      auto* self = safeThis.getComponent();
                                      if (self == nullptr || choice == synth::ui::DeleteMacroChoice::cancel)
                                          return;
                                      if (remember)
                                          if (auto* userSettings = self->appProperties.getUserSettings())
                                              synth::ui::rememberDeleteMacroChoice(*userSettings, choice);
                                      apply(choice);
                                  });
    return true;
}
