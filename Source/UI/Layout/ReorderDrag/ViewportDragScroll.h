#pragma once

// ViewportDragScroll.h: how a drag inside a juce::Viewport makes the view follow it. A pointer held in the edge zone at
// either end of the viewport's visible span scrolls the view one step per drag event, faster the nearer the edge; the
// owner keeps the events coming while the pointer rests with juce::Component::beginDragAutoRepeat (ReorderDragSession
// arms it). The speed curve is the timeline's (UI/Timeline/EdgeAutoScroll.h). Message thread only.
//
// Owner flow, on every drag event while the gesture is past its threshold (never after Esc or the release):
//   const auto pointer = viewport_drag_scroll::pointerOf(e);   // before anything moves
//   viewport_drag_scroll::follow(viewport, pointer, Axis::Vertical);
//   dragTo(viewport_drag_scroll::along(content, pointer, Axis::Vertical));
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::viewport_drag_scroll {

enum class Axis { Vertical, Horizontal };

/** Width (px) of the strip at each end of the view that starts a scroll. */
constexpr int kZonePx = 32;
/** The largest step (px per drag event), with the pointer on the edge or past it. */
constexpr double kMaxStepPx = 18.0;

/** The zone used for a view `extent` px long: `zonePx`, but never more than a third of the view. */
int zoneFor(int extent, int zonePx = kZonePx) noexcept;

/** The signed step (px) for a pointer `pos` px along a view `extent` px long: 0 in the middle, at least 1 in a zone. */
int stepFor(int pos, int extent, int zonePx = kZonePx) noexcept;

/** Where `e`'s pointer is in desktop coordinates. Read it before anything scrolls. */
juce::Point<int> pointerOf(const juce::MouseEvent& e);

/** The step for a pointer at `pointer` (desktop coordinates) over `viewport`; 0 when its content fits along `axis`. */
int stepFor(const juce::Viewport& viewport, juce::Point<int> pointer, Axis axis, int zonePx = kZonePx);

/** Scrolls `viewport` by stepFor, held inside its content; returns the px it really moved (signed). */
int follow(juce::Viewport& viewport, juce::Point<int> pointer, Axis axis, int zonePx = kZonePx);

/** `pointer` (desktop coordinates) along `axis` in `component`'s own coordinates. */
float along(const juce::Component& component, juce::Point<int> pointer, Axis axis);

/** The owner flow in one call for a list inside a viewport: while `dragging` (past the threshold, not after Esc),
 * follows the nearest viewport `content` scrolls in, then returns `pointer` along `axis` in `content`'s coordinates. */
float followAndLocate(juce::Component& content, juce::Point<int> pointer, Axis axis, bool dragging,
                      int zonePx = kZonePx);

} // namespace synth::ui::viewport_drag_scroll
