// DrumMachineComponent: the menu of a drum row's name. Right-click on the name (or Shift+F10, the shared
// openContextMenu action, on a focused name or on the grid's focused row) opens Length, Swing and Reset for that drum.
// Every choice is ONE undo step. docs/timeline/drum-machine.md#per-drum-length-and-swing.

#include "DrumMachineComponent.h"

#include "UI/Layout/ContextMenuPlacement.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace test_hooks {
std::function<void(int, juce::PopupMenu, std::function<void(int)>)>& drumRowMenuHookForTest() {
    static std::function<void(int, juce::PopupMenu, std::function<void(int)>)> hook;
    return hook;
}
} // namespace test_hooks

namespace {
constexpr int kSwingPercentStep = 10;
constexpr int kMaxSwingPercent = 100;

juce::String stepsText(int steps) { return juce::String(steps) + (steps == 1 ? " step" : " steps"); }
} // namespace

// Length: "Clip length" then every step count of the clip (up to the model's cap); the current one is ticked. Choosing
// the clip's own step count is the same as "Clip length". Swing: "Clip swing" then 0% to 100% in tens.
juce::PopupMenu DrumMachineComponent::buildRowMenu(int pitch) const {
    juce::PopupMenu menu, length, swing;
    const int stored = storedRowLength(pitch);
    length.addItem(kRowMenuClipLength, "Clip length", true, stored == 0);
    for (int n = 1; n <= std::min(getStepCount(), synth::Clip::kMaxRowLengthSteps); ++n)
        length.addItem(kRowMenuLengthBase + n, stepsText(n), true, stored == n);

    const bool own = hasRowSwing(pitch);
    const int ownPercent = juce::roundToInt(swingToPercent(getRowSwing(pitch)));
    swing.addItem(kRowMenuClipSwing, "Clip swing", true, !own);
    for (int percent = 0; percent <= kMaxSwingPercent; percent += kSwingPercentStep)
        swing.addItem(kRowMenuSwingBase + percent / kSwingPercentStep, juce::String(percent) + "%", true,
                      own && ownPercent == percent);

    menu.addSubMenu("Length", length);
    menu.addSubMenu("Swing", swing);
    menu.addSeparator();
    menu.addItem(kRowMenuReset, "Reset", rawRowLength(pitch) > 0 || own);
    return menu;
}

void DrumMachineComponent::applyRowMenuChoice(int pitch, int itemId) {
    if (clip() == nullptr)
        return;
    const auto name = drumLaneForPitch(pitch).name;
    if (itemId == kRowMenuClipLength) {
        setRowLength(pitch, 0);
        announce(name + " follows the clip's length");
    } else if (itemId > kRowMenuLengthBase) {
        const int steps = itemId - kRowMenuLengthBase;
        setRowLength(pitch, steps >= getStepCount() ? 0 : steps);
        announce(steps >= getStepCount() ? name + " follows the clip's length" : name + " length " + stepsText(steps));
    } else if (itemId == kRowMenuClipSwing) {
        setRowSwing(pitch, -1.0);
        announce(name + " follows the clip's swing");
    } else if (itemId >= kRowMenuSwingBase && itemId <= kRowMenuSwingBase + kMaxSwingPercent / kSwingPercentStep) {
        const int percent = (itemId - kRowMenuSwingBase) * kSwingPercentStep;
        setRowSwing(pitch, percentToSwing(percent));
        announce(name + " swing " + juce::String(percent) + "%");
    } else if (itemId == kRowMenuReset) {
        resetRow(pitch);
        announce("Reset " + name + " length and swing");
    }
}

void DrumMachineComponent::showRowMenu(int pitch, std::optional<juce::Rectangle<int>> screenAnchor) {
    if (!clipId_.isValid())
        return;
    juce::Component::SafePointer<DrumMachineComponent> safe(this);
    auto choose = [safe, pitch](int result) {
        if (safe != nullptr && result > 0)
            safe->applyRowMenuChoice(pitch, result);
    };
    auto menu = buildRowMenu(pitch);
    if (auto& hook = test_hooks::drumRowMenuHookForTest()) {
        hook(pitch, std::move(menu), std::move(choose));
        return;
    }
    menu.showMenuAsync(contextMenuOptions(screenAnchor), std::move(choose));
}

// Shift+F10 (the openContextMenu action) lands here from the focused name button or the grid; the menu is for the same
// row the Alt+arrow actions would change, placed at the name or the focused cell.
bool DrumMachineComponent::showContextMenuForKeyboardFocus() {
    const int pitch = keyboardRowPitch();
    if (!clipId_.isValid() || pitch < 0)
        return false;
    const auto* name = labels_.getNameButton(pitch);
    const auto anchor = name != nullptr && name->hasKeyboardFocus(false)
                            ? name->getScreenBounds()
                            : localAreaToGlobal(cellBounds(focus_.row, focus_.step));
    showRowMenu(pitch, anchor.isEmpty() ? getScreenBounds() : anchor);
    return true;
}

} // namespace synth::ui
