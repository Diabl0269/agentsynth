// ShapeListEditorRing.cpp -- the Filter shape window's Ring helpers menu: each item rewrites the selected shape's
// curve into peaks at the pitches the grid rows name (the Harmonics root, the Notes key), as one undo step.
#include "PitchGrid.h"
#include "PitchGridRows.h"
#include "ShapeListEditor.h"
#include "ShapeRingHelpers.h"

namespace synth::ui {

// The items are plain actions; their longer explanations are in the button's tooltip and the docs, since a popup
// menu item has no tooltip of its own.
void ShapeListEditor::openRingMenu() {
    if (pitchRows_ == nullptr)
        return;
    juce::PopupMenu menu;
    juce::Component::SafePointer<ShapeListEditor> safe(this);
    for (const auto helper : ringhelpers::kHelpers)
        menu.addItem(ringhelpers::helperName(helper), !shapes_.empty(), false, [safe, helper] {
            if (safe != nullptr)
                safe->applyRingHelper(helper);
        });
    showMenuNow(menu, ringButton_);
}

// The root and key come from the grid rows even when the grid is set to another kind, so the peaks always land on
// what the rows would show for Harmonics (root) and Notes (key).
void ShapeListEditor::applyRingHelper(ringhelpers::Helper helper) {
    if (pitchRows_ == nullptr || shapes_.empty())
        return;
    using ringhelpers::Helper;
    const auto key = pitchRows_->activeKey();
    const double root = pitchRows_->rootHz();
    const auto& current = shapes_[(size_t)selected_].curve;
    std::optional<LfoCustomWave> next;
    juce::String done;
    switch (helper) {
    case Helper::tunePeak:
        next = ringhelpers::tunePeak(current, curve_.getSelectedIndex(), key);
        done = "Peak tuned to a note";
        break;
    case Helper::stackHarmonics:
        next = ringhelpers::curveWithPeaks(ringhelpers::stackHarmonics(root));
        done = "Harmonics of " + pitchgrid::formatHz(root);
        break;
    case Helper::chordPeaks:
        next = ringhelpers::curveWithPeaks(ringhelpers::chordPeaks(key));
        done = "Chord peaks from the key";
        break;
    case Helper::octaveBells:
        next = ringhelpers::curveWithPeaks(ringhelpers::octaveBells(root), ringhelpers::kBellHalfWidthOctaves);
        done = "Octave bells from " + pitchgrid::formatHz(root);
        break;
    }
    if (!next.has_value()) {
        showStatus("No peak to tune: add a point between the ends first");
        return;
    }
    const auto curve = *next;
    edit([&](std::vector<ShapeItem>& shapes) { shapes[(size_t)selected_].curve = curve; });
    showStatus(done, false);
}

} // namespace synth::ui
