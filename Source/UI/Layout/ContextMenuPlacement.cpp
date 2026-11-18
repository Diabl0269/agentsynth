// ContextMenuPlacement.cpp -- the pointer-reading halves of ContextMenuPlacement.h (kept out of the header so the
// widely included header does not pull in PointerState.h).
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/PointerState.h"

namespace synth::ui {

juce::PopupMenu::Options contextMenuOptionsAtPointer() { return contextMenuOptionsAtPoint(pointerPosition()); }

juce::PopupMenu::Options contextMenuOptions(const std::optional<juce::Rectangle<int>>& keyboardAnchor) {
    return keyboardAnchor ? juce::PopupMenu::Options().withTargetScreenArea(*keyboardAnchor)
                          : contextMenuOptionsAtPointer();
}

} // namespace synth::ui
