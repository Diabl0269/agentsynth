#pragma once

#include "UI/Layout/ExitEnterTimeline.h"
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <vector>

// ExitEnterListPlan.h (docs/layout/animation.md "Delete and undo animation"): the pure half of the row/column motion
// of a list that loses or regains an item (the timeline's track rows, the mixer's strip columns). A picture of the list
// is cut into slices along one axis, one per item, and the plan says where each slice is drawn at every moment of
// the ExitEnterTimeline. No components and no clock, so it is unit-tested headlessly.
//
//   delete : the picture is of the list BEFORE (it holds the item). The item's slice shrinks in place (exit); the
//            slices after it then slide to where the model put them (gap).
//   undo   : the picture is of the list AFTER (it holds the item again). The slices after the item start where they
//            would be without it and slide to their places (gap), then the item's slice grows (grow) and a 1 px accent
//            outline fades (outline).
namespace synth::ui {

enum class ListAxis { Vertical, Horizontal };

/** One item of a list, along the axis: where it starts and how long it is (both in the picture's coordinates). */
struct ExitEnterListRow {
    juce::String key;
    float start = 0.0f;
    float extent = 0.0f;
};

struct ExitEnterListItem {
    enum class Role { Stay, Exit, Enter };

    juce::String key; // empty for the tail: whatever the picture shows past the last item
    Role role = Role::Stay;
    float srcStart = 0.0f; // the slice of the picture
    float extent = 0.0f;
    float fromStart = 0.0f; // Stay: the gap phase slides the slice from here ...
    float toStart = 0.0f;   // ... to here. Exit and Enter slices sit at srcStart.

    bool isTail() const noexcept { return key.isEmpty(); }
};

struct ExitEnterListPlan {
    using Row = ExitEnterListRow;
    using Item = ExitEnterListItem;

    /** `before` holds the removed items; the picture (`pictureExtent` long) is of that state. */
    static std::vector<Item> forRemoval(const std::vector<Row>& before, const std::vector<Row>& after,
                                        float pictureExtent);
    /** `after` holds the `added` items; the picture is of that state. The others start where they stood without them.
     */
    static std::vector<Item> forInsertion(const std::vector<Row>& after, const std::vector<juce::String>& added,
                                          float pictureExtent);

    /** Which phases the items need: exit if one leaves, enter if one comes back, gap if a slice slides. */
    static ExitEnterTimeline timelineFor(const std::vector<Item>& items);

    /** Where and how a slice is drawn. `start` is along the axis; the slice is `extent` long there, scaled about its
     *  centre by `scale` and drawn at `alpha`; `outlineAlpha` > 0 draws the 1 px accent outline around its slot. */
    struct Drawn {
        float start = 0.0f;
        float extent = 0.0f;
        float scale = 1.0f;
        float alpha = 1.0f;
        float outlineAlpha = 0.0f;
    };
    /** Empty when the slice is not drawn at this moment (an exited item, an item not yet grown). */
    static std::optional<Drawn> drawnAt(const Item& item, const ExitEnterTimeline::Frame& frame, bool reducedMotion);
};

} // namespace synth::ui
