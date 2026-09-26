#pragma once

// Controller template library (docs/control/midi-remote-ui.md#templates-and-importexport): a
// handful of generic ready-made ControllerProfile documents (8 knobs, faders + buttons, transport
// strip, keyboard) plus real-hardware templates for popular controllers (grouped by vendor in the
// Templates menu), all shipped in BinaryData (assets/midi-remote-templates/*.json). A template is
// a starting surface for a controller that Learn hasn't seen yet -- applying one to a profile adds
// its controls; the user then maps them like any other. Core: only juce_core + RemoteModel.h, no
// ApplicationProperties / juce_gui; whatever persists the resulting profile is the app-layer
// ControllerProfileStore.

#include "MidiRemote/RemoteModel.h"

#include <juce_core/juce_core.h>
#include <vector>

namespace synth::midi {

struct TemplateInfo {
    juce::String id;   // == the template's ControllerProfile::id == its BinaryData filename minus ".json"
    juce::String name; // display name, e.g. "8 knobs"
    // Empty for a generic template (grouped under "Generic" in the Templates menu); a real-hardware
    // template's manufacturer, e.g. "Korg" (docs/control/midi-remote-ui.md#contribute-a-template).
    juce::String vendor;
    // Empty for a generic template; for a vendor template, the exact manual/MIDI-implementation
    // document (with URL and section) its CC/note numbers were sourced from -- required by
    // docs/control/midi-remote-ui.md#contribute-a-template, never left to guesswork.
    juce::String source;
};

/** One Templates-menu section: `vendor` (empty means "Generic") and its templates, in the same
 *  relative order listControllerTemplates() returned them. */
struct TemplateGroup {
    juce::String vendor;
    std::vector<TemplateInfo> templates;
};

/** Every shipped template (BinaryData resources named "template-*.json" that parse), in a fixed
 *  display order: the generic templates first (8 knobs, 8 faders + 8 buttons, transport strip,
 *  keyboard), then vendor templates. Ids not in the generic order table sort after the known ones,
 *  alphabetically. Unparseable resources are skipped. */
std::vector<TemplateInfo> listControllerTemplates();

/** Groups `templates` (as returned by listControllerTemplates()) by TemplateInfo::vendor for the
 *  Templates menu: one "Generic" group (vendor == "") first if any, then one group per distinct
 *  vendor, alphabetically, each keeping the templates' relative order. Pure/headless -- the menu
 *  itself only turns this into juce::PopupMenu sections. */
std::vector<TemplateGroup> groupControllerTemplatesByVendor(const std::vector<TemplateInfo>& templates);

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
