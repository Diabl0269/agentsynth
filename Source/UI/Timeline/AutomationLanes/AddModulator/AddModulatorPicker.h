#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <functional>
#include <memory>
#include <vector>

namespace synth::ui {

// What an automation lane's "Add modulator..." picker offers for one parameter, and the picker built from it.
// Like the "Add automation..." picker it is the Mod Matrix's searchable list (ModMatrixPicker), reused as it
// is: "New LFO" first, then every LFO in the project with where it lives and what it already moves. An LFO
// that already moves this parameter is listed greyed out, with that as its reason.

struct AddModulatorPick {
    bool isNew = true;    // "New LFO"
    juce::String lfoUuid; // the chosen existing LFO when !isNew
};

struct AddModulatorChoices {
    std::vector<TrackHeaderHost::LfoChoice> lfos;
    // Item id 1 = New LFO; item id n + 2 = lfos[n].
    std::vector<ModMatrixPicker::Item> items;
};

/** The picker's rows for `lfos`, as the host described them for `parameterName`. */
AddModulatorChoices collectAddModulatorChoices(std::vector<TrackHeaderHost::LfoChoice> lfos,
                                               const juce::String& parameterName);

/** The picker for `choices`; `onPick` gets the chosen row. The caller launches it (a CallOutBox in the app, the
 *  hook below in tests). */
std::unique_ptr<ModMatrixPicker> buildAddModulatorPicker(const AddModulatorChoices& choices,
                                                         const juce::String& parameterName,
                                                         std::function<void(const AddModulatorPick&)> onPick);

namespace test_hooks {
/** When set, a lane header hands its picker here instead of opening a call-out. */
std::function<void(std::unique_ptr<ModMatrixPicker>)>& addModulatorPickerHookForTest();
} // namespace test_hooks

} // namespace synth::ui
