#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <functional>
#include <memory>
#include <vector>

namespace synth::ui {

// What an automation lane's "Add modulator..." picker offers for one parameter, and the picker built from it.
// Like the "Add automation..." picker it is the Mod Matrix's searchable list (ModMatrixPicker), reused as it
// is: a "New" group ("New LFO", "New envelope") first, then every LFO and every envelope in the project with where
// it lives and what it already moves. A source that already moves this parameter is listed greyed out, with that as
// its reason. It offers the same source types as the modulator row's "Change source" picker (one shared builder).

struct AddModulatorPick {
    bool isNew = true;     // "New LFO" / "New envelope"
    juce::String lfoUuid;  // the chosen existing LFO (or envelope) when !isNew
    int sourceChannel = 0; // its output channel; for a new module, the output it is read from
    // For a new module, the factory type to create ("LFO", "ADSR").
    juce::String newTypeName;
};

struct AddModulatorChoices {
    std::vector<TrackHeaderHost::LfoChoice> lfos;
    // Item id 1 = New LFO; item id n + 2 = lfos[n]; the "New" rows are in newSources.
    std::vector<ModMatrixPicker::Item> items;
    // The "New" rows: item id, factory type and output channel to read from.
    struct NewSource {
        int id = 0;
        juce::String typeName;
        int channel = 0;
    };
    std::vector<NewSource> newSources;
};

/** The picker's rows for `sources` (LFOs and envelopes, as the host described them for `parameterName`): the "New"
 *  group first, then "LFOs", then "Envelopes". `choices.lfos[n]` is item id n + 2. */
AddModulatorChoices collectAddModulatorChoices(std::vector<TrackHeaderHost::LfoChoice> lfos,
                                               const juce::String& parameterName);

/** The picker for `choices`; `onPick` gets the chosen row. The caller launches it (a CallOutBox in the app, the
 *  hook below in tests). */
std::unique_ptr<ModMatrixPicker> buildAddModulatorPicker(const AddModulatorChoices& choices,
                                                         const juce::String& parameterName,
                                                         std::function<void(const AddModulatorPick&)> onPick);

/** The rows of a modulator row's "Change source" picker: under "LFOs" and "Envelopes", every LFO and envelope output,
 *  the current source (`currentSourceUuid`, `currentSourceChannel`) ticked and disabled rather than left out, then a
 *  "New" group with "New LFO" and "New envelope". `choices.lfos[n]` is item id n + 2 as above. */
AddModulatorChoices collectChangeSourceChoices(std::vector<TrackHeaderHost::LfoChoice> lfos,
                                               const juce::String& currentSourceUuid, int currentSourceChannel,
                                               const juce::String& parameterName);

namespace test_hooks {
/** When set, a lane header hands its picker here instead of opening a call-out. */
std::function<void(std::unique_ptr<ModMatrixPicker>)>& addModulatorPickerHookForTest();
/** Same for a modulator row's "Change source..." picker. */
std::function<void(std::unique_ptr<ModMatrixPicker>)>& changeSourcePickerHookForTest();
} // namespace test_hooks

} // namespace synth::ui
