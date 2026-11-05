#pragma once

// ShapePicker.h -- the one shape picker: a menu whose entries each show a small picture of the curve, the name and a
// badge saying where it came from (Yours or Factory). The Filter and LFO cards' Pick shape button and the shape
// window's Start from button build their menus from it. Keyboard and screen reader come from the menu: arrows move,
// Return picks, Escape closes, and each entry is announced as "<name>, <badge>".
#include "Modules/Shapes/ShapeLibrary.h"
#include "UI/Chrome/Badge/Badge.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

struct ShapePickerEntry {
    juce::String name;
    LfoCustomWave curve;
    BadgeKind badge = BadgeKind::Factory;
    /** The library entry this came from, when it came from one (a built-in curve with no library twin has none). */
    std::optional<synth::shapes::LibraryShape> libraryShape;
};

/** The entries of `library` for `kind`: the user's saved shapes (Yours), then the factory set (Factory). */
std::vector<ShapePickerEntry> libraryPickerEntries(const synth::shapes::ShapeLibrary& library,
                                                   synth::shapes::ShapeKind kind);

/** What a screen reader says for an entry: "<name>, <badge>". It is also the menu item's text. */
juce::String pickerEntryTitle(const ShapePickerEntry& entry);

/** Adds one entry to `menu` as a thumbnail row; `onPick` runs when it is chosen. The entry is copied in. */
void addShapePickerItem(juce::PopupMenu& menu, const ShapePickerEntry& entry,
                        std::function<void(const ShapePickerEntry&)> onPick);

/** The whole picker for a library kind: "Your shapes" (when there are any) and "Factory shapes" sections. */
juce::PopupMenu makeShapePickerMenu(const std::vector<ShapePickerEntry>& entries,
                                    std::function<void(const ShapePickerEntry&)> onPick);

} // namespace synth::ui
