#pragma once

// ShapeMapController.h -- wires one MorphMap to a ShapeListAdapter: every map gesture becomes an adapter edit with
// its undo bracket, and the map is refreshed from the adapter. The card's map and the editor window's map both use
// it, so they behave identically.
#include "ShapeListAdapter.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include <functional>

namespace synth::ui {

class ShapeMapController {
public:
    /** Takes over the map's callbacks. Both references must outlive the controller. */
    ShapeMapController(MorphMap& map, ShapeListAdapter& adapter);

    /** Open the shape editor on this shape (double-click, Return, the menu's Edit). */
    std::function<void(int)> onEdit;
    /** The user picked a shape on the map. */
    std::function<void(int)> onSelect;
    /** The list changed through this controller; the owner re-reads whatever else shows it. */
    std::function<void()> onChanged;

    /** Pushes the adapter's shapes and cursor into the map. */
    void refresh();

    /** Each is one undo step. Add and duplicate return the new index, or -1 at the shape cap. */
    int addShapeAt(float x, float y);
    int addShapeInFreeSpot();
    int duplicateShape(int index);
    bool deleteShape(int index);
    void renameShape(int index, const juce::String& name);

private:
    juce::String uniqueName(const std::vector<ShapeItem>& shapes, const juce::String& wanted) const;
    void changed();

    MorphMap& map_;
    ShapeListAdapter& adapter_;
};

} // namespace synth::ui
