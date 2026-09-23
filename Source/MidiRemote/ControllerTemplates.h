#pragma once

// Generic MIDI-controller templates (docs/control/midi-remote.md): a handful of ready-made
// ControllerProfile documents (8 knobs, faders + buttons, transport strip, keyboard) shipped in
// BinaryData (assets/midi-remote-templates/*.json). A template is a starting surface for a
// controller that Learn hasn't seen yet -- applying one to a profile adds its controls; the user
// then maps them like any other. Core: only juce_core + RemoteModel.h, no ApplicationProperties /
// juce_gui; whatever persists the resulting profile is the app-layer ControllerProfileStore.

#include "MidiRemote/RemoteModel.h"

#include <juce_core/juce_core.h>
#include <vector>

namespace synth::midi {

struct TemplateInfo {
    juce::String id;   // == the template's ControllerProfile::id == its BinaryData filename minus ".json"
    juce::String name; // display name, e.g. "8 knobs"
};

/** Every shipped template (BinaryData resources named "template-*.json" that parse), in a fixed
 *  display order: 8 knobs, 8 faders + 8 buttons, transport strip, keyboard. Ids not in that
 *  order table sort after the known ones, alphabetically. Unparseable resources are skipped. */
std::vector<TemplateInfo> listControllerTemplates();

/** Parses the template with `id` into `out`. False (and `out` untouched) if the id is unknown or
 *  the shipped JSON is malformed. */
bool loadControllerTemplate(const juce::String& id, ControllerProfile& out);

struct TemplateApplyResult {
    int added = 0;
    int skippedDuplicates = 0;
};

/** Merges `tmpl`'s controls into `profile`: a control whose MessageSpec already exists on the
 *  profile is skipped (the existing control wins); every added control gets a fresh id. An empty
 *  profile keeps the template's layout; otherwise added controls are shifted below the existing
 *  rows (max existing row + 1) so nothing overlaps. Never touches profile.id/name/input/output/
 *  actions/passMapped. */
TemplateApplyResult applyControllerTemplate(ControllerProfile& profile, const ControllerProfile& tmpl);

} // namespace synth::midi
