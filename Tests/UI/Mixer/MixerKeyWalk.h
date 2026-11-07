#pragma once

// MixerKeyWalk.h -- shared by the mixer keyboard-reach tests (header-only, not registered in Tests/CMakeLists.txt):
// walks the mixer panel to a control with the panel's own keys only. `press` delivers one key (a panel's keyPressed, or
// a real ComponentPeer's dispatch). The walk starts from wherever the panel is and leaves row mode and the top bar
// ring.
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerDirectColumn.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerZonesPane/MixerZonesPane.h"
#include "UI/Mixer/MixerZonesPane/MixerZonesRow.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::test::mixer {

using KeyPresser = std::function<bool(const juce::KeyPress&)>;

inline juce::KeyPress walkKey(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

/** Back to column mode with the ring off: Escape leaves the rows or the ring. */
inline void leaveToColumn(synth::ui::MixerPanelComponent& panel, const KeyPresser& press) {
    if (panel.getFocusedRowForTest().has_value() || panel.getTopBarRingForTest().isActive())
        press(walkKey(juce::KeyPress::escapeKey));
}

/** Left until the first column (clamped), so a walk starts from one known place. */
inline void homeColumn(synth::ui::MixerPanelComponent& panel, const KeyPresser& press) {
    leaveToColumn(panel, press);
    press(walkKey(juce::KeyPress::rightKey)); // from no focus this seeds column 0
    for (int i = 0; i < panel.getColumnCount(); ++i)
        press(walkKey(juce::KeyPress::leftKey));
}

/** Whether the row the panel's focus sits on now contains `control` (its centre, in panel coordinates). */
inline bool focusedRowHolds(synth::ui::MixerPanelComponent& panel, juce::Component& control) {
    const auto ring = panel.getFocusedRowBoundsForTest();
    return !ring.isEmpty() && ring.contains(panel.getLocalArea(&control, control.getLocalBounds()).getCentre());
}

/** Column by column with Right, and down each column's rows with Down: true once a focused row holds `control`. */
inline bool walkRowsTo(synth::ui::MixerPanelComponent& panel, juce::Component& control, const KeyPresser& press) {
    homeColumn(panel, press);
    for (int column = 0; column < panel.getColumnCount(); ++column) {
        if (column > 0)
            press(walkKey(juce::KeyPress::rightKey));
        if (press(walkKey(juce::KeyPress::downKey)) && panel.getFocusedRowForTest().has_value()) {
            for (int guard = 0; guard < 64; ++guard) {
                if (focusedRowHolds(panel, control))
                    return true;
                const auto before = panel.getFocusedRowForTest();
                press(walkKey(juce::KeyPress::downKey));
                if (panel.getFocusedRowForTest() == before)
                    break;
            }
        }
        leaveToColumn(panel, press);
    }
    return false;
}

/** Whether the column holding `control` (a strip, Direct or Master) paints the keyboard focus. */
inline bool columnOfIsFocused(juce::Component& control) {
    if (auto* strip = control.findParentComponentOfClass<synth::ui::MixerColumnComponent>())
        return strip->isKeyboardFocusedForTest();
    if (auto* master = control.findParentComponentOfClass<synth::ui::MixerMasterColumn>())
        return master->isKeyboardFocusedForTest();
    if (auto* direct = control.findParentComponentOfClass<synth::ui::MixerDirectColumn>())
        return direct->isKeyboardFocusedForTest();
    return false;
}

/** Column by column with Right: true once the focused column is the one holding `control`. */
inline bool walkColumnsTo(synth::ui::MixerPanelComponent& panel, juce::Component& control, const KeyPresser& press) {
    homeColumn(panel, press);
    for (int column = 0; column < panel.getColumnCount(); ++column) {
        if (column > 0)
            press(walkKey(juce::KeyPress::rightKey));
        if (columnOfIsFocused(control))
            return true;
    }
    return false;
}

/** Cmd+Up onto the top bar ring, then Right along it: true once the ring sits on `control`. */
inline bool walkTopBarTo(synth::ui::MixerPanelComponent& panel, juce::Component& control, const KeyPresser& press) {
    homeColumn(panel, press);
    if (!press(walkKey(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier)))
        return false;
    auto& ring = panel.getTopBarRingForTest();
    press(walkKey(juce::KeyPress::homeKey));
    for (int guard = 0; guard < 64; ++guard) {
        if (ring.getRingedStop() == &control)
            return true;
        const auto* before = ring.getRingedStop();
        press(walkKey(juce::KeyPress::rightKey));
        if (ring.getRingedStop() == before)
            break;
    }
    leaveToColumn(panel, press);
    return false;
}

/** The side pane's channel list: opened the way its key (Cmd+Shift+B, an app command) opens it, which puts the
 *  keyboard in the list, then from its first row Down along them: true once the cursor row is the one whose eye is
 * `control`. */
inline bool walkZonesListTo(synth::ui::MixerPanelComponent& panel, juce::Component& control, const KeyPresser& press) {
    auto* row = control.findParentComponentOfClass<synth::ui::MixerZonesRow>();
    auto& pane = panel.getZonesPaneForTest();
    if (row == nullptr || &row->getEyeForTest() != &control || !pane.isParentOf(row) || !panel.toggleSidePane(true))
        return false;
    for (int guard = 0; guard < 64; ++guard) { // Up to the first row (the cursor is kept from the last visit)
        const auto before = pane.getCursorIdForTest();
        if (!press(walkKey(juce::KeyPress::upKey)) || pane.getCursorIdForTest() == before)
            break;
    }
    bool reached = false;
    for (int guard = 0; guard < 64 && !reached; ++guard) {
        const auto before = pane.getCursorIdForTest();
        if (!before.isEmpty())
            reached = pane.findRowForTest(before) == row;
        if (!reached && (!press(walkKey(juce::KeyPress::downKey)) || pane.getCursorIdForTest() == before))
            break;
    }
    press(walkKey(juce::KeyPress::escapeKey)); // the list gives the keyboard back
    panel.grabKeyboardFocus();
    return reached;
}

} // namespace synth::test::mixer
