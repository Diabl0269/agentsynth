#pragma once

// ShapeEditorWindow.h -- opens the shape editor in its own window. The window grows out of the card that asked for
// it (`anchor`) and shrinks back into it when it closes (PopupMotion).
#include "ShapeListEditor.h"
#include <functional>
#include <memory>

namespace synth::ui {

/** Opens a window titled by `adapter->getWindowTitle()` on shape `selected`, centred on `anchor`'s window. Returns it,
 *  or null when a test hook took the content instead. Escape or Done closes it. `onStripMenu` is set on the editor (a
 *  right-click on its Morph X / Y slider). Message thread only. */
juce::DialogWindow* openShapeEditorWindow(std::shared_ptr<ShapeListAdapter> adapter, int selected,
                                          juce::Component* anchor,
                                          std::function<void(MorphMap::Axis, juce::Rectangle<int>)> onStripMenu = {});

/** Brings `window` to the front with keyboard focus on its first Tab stop, so Tab works at once. A shape editor window
 *  opened by openShapeEditorWindow already does this once it is showing; call it to bring an open one back. */
void focusShapeEditorWindow(juce::DialogWindow& window);

namespace test_hooks {
/** When set, opening hands the editor to this instead of creating a window (which cannot open on a headless runner).
 *  The test keeps the pointer alive. */
std::function<void(std::unique_ptr<ShapeListEditor>)>& shapeEditorWindowHookForTest();
} // namespace test_hooks

} // namespace synth::ui
