// TimelineClipLaneInternal.h
//
// Private constants shared by two or more TimelineClipLane*.cpp translation units.
// TimelineClipLaneArea itself is declared in TimelineClipLaneArea.h; its implementation is
// split across sibling TimelineClipLane*.cpp files in this directory. Not registered as a
// CMake source -- header only. `inline` (not `static`) so a constant pulled in by several
// .cpp files still resolves to one definition, matching the single-TU behaviour before the
// split.
#pragma once

namespace synth::ui::detail {

// Edge-drag zones (right = resize length, left = move+resize keeping the end fixed) — a clip's
// own resize-grab width, unrelated to (and deliberately distinctly named from) the
// component-edge auto-scroll zone in EdgeAutoScroll.h's synth::ui::kEdgeZonePx.
inline constexpr int kResizeEdgeZonePx = 6;

// TimelineDoc has no explicit minimum clip length; this mirrors TimelineViewState::Snap's own
// finest grid (Sixteenth = 1/16 beat, the same unit TransportService's kMinLoopLengthBeats uses)
// so a right-edge trim can never collapse a clip past what the grid itself can represent.
inline constexpr double kMinClipLengthBeats = 0.0625;

inline constexpr int kMinWidthForName = 40;

// bpm fallback when there is no live transport (a headless test, or a TimelineClipLaneArea built
// with setTransport() never called) — the same "no transport, assume 120 bpm" convention
// currentBeatsPerBar() uses for beatsPerBar.
inline constexpr double kFallbackBpm = 120.0;

} // namespace synth::ui::detail
