#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include <functional>
#include <memory>
#include <vector>

namespace synth::ui {

// The searchable picker behind the timeline's "+ Track" button. The entries still come from one place, the panel's
// add-track menu; this unit flattens that menu (submenus and all) into rows of the shared ModMatrixPicker, so a
// row's id is the menu item's id and a pick resolves exactly as a menu click did.

/** One picker row per enabled-or-disabled menu item, in menu order. Separators and header-only entries are dropped;
 *  an item inside a submenu sits under a header named after that submenu ("Plugin" reads as "Plugins", "Instrument
 *  Track" as "Instrument Tracks"), the plain track entries under "Tracks" and the rest under "More". A disabled item
 *  stays as a greyed row whose detail line says why. */
std::vector<ModMatrixPicker::Item> flattenAddTrackMenu(const juce::PopupMenu& menu);

/** The picker for `items`; `onPick` gets the chosen row's menu id, `onClosed` runs when the picker goes away. The
 *  caller launches it. */
std::unique_ptr<ModMatrixPicker> buildAddTrackPicker(std::vector<ModMatrixPicker::Item> items,
                                                     std::function<void(int)> onPick, std::function<void()> onClosed);

/** Opens `picker` in a call-out under `anchor` (or hands it to the test hook below). Message thread only. */
void showAddTrackPicker(juce::Component& anchor, std::unique_ptr<ModMatrixPicker> picker);

namespace test_hooks {
/** When set, showAddTrackPicker() hands its picker here instead of opening a call-out. */
std::function<void(std::unique_ptr<ModMatrixPicker>)>& addTrackPickerHookForTest();
} // namespace test_hooks

} // namespace synth::ui
