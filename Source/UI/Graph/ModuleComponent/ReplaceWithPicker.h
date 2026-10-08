#pragma once

#include "Modules/ModuleBase.h"
#include "Plugin/Hosting/HostedPluginBackend.h"
#include "UI/Graph/ModMatrixPicker.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class GraphEditor;
class ModuleComponent;

namespace synth::ui {

// What a module card's "Replace with..." offers and the picker built from it. Like the add-modulator and
// add-automation pickers it is the shared searchable list (ModMatrixPicker), reused as it is: every module type
// the old nested submenu listed, grouped under the same category headers, then every scanned hosted plugin under
// "Plugins". The module's own type (or the plugin it hosts) is left out.

struct ReplaceChoice {
    juce::String moduleType;                     // a built-in module's type name, when `plugin` is empty
    std::optional<synth::PluginIdentity> plugin; // set = replace with a hosted plugin
};

struct ReplaceChoices {
    std::vector<ReplaceChoice> choices;
    // Item id n + 1 = choices[n].
    std::vector<ModMatrixPicker::Item> items;
};

/** The rows for a module of type `current` (hosting `currentPlugin`, if it is a Hosted Plugin) given the scanned
 *  `plugins`. */
ReplaceChoices collectReplaceChoices(ModuleType current, const std::optional<synth::PluginIdentity>& currentPlugin,
                                     const std::vector<synth::PluginIdentity>& plugins);

/** The picker for `choices`; `onPick` gets the chosen row. The caller launches it. */
std::unique_ptr<ModMatrixPicker> buildReplacePicker(const ReplaceChoices& choices,
                                                    std::function<void(const ReplaceChoice&)> onPick);

/** Replaces `card`'s module with `choice` (one undo step), keeping its position and compatible cables. */
void applyReplaceChoice(GraphEditor& editor, ModuleComponent* card, const ReplaceChoice& choice);

/** Opens the picker over `card` (a CallOutBox, or the test hook below), listing `editor`'s scanned plugins. */
void showReplacePicker(GraphEditor& editor, ModuleComponent& card, ModuleType current,
                       const std::optional<synth::PluginIdentity>& currentPlugin);

namespace test_hooks {
/** When set, showReplacePicker() hands its picker here instead of opening a call-out. */
std::function<void(std::unique_ptr<ModMatrixPicker>)>& replacePickerHookForTest();
} // namespace test_hooks

} // namespace synth::ui
