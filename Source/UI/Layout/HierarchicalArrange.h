#pragma once

#include "UI/Layout/LayoutUtil.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <map>
#include <vector>

// HierarchicalArrange.h (docs/layout/layout.md#auto-arrange): the pure layout behind GraphEditor::autoArrange().
// No graph, macro or component types: the canvas adapter (GraphEditorAutoArrange.cpp) flattens the patch into blocks
// and edges, this turns them into positions, and the adapter writes them back.
//
// The rule is "rows of tracks, columns of stages": one row per track (in track order) preceded by one row for the
// modulators shared between rows, then one row per remaining connected component; columns are longest-path depth
// from a row's sources and are aligned across rows. An open macro is laid out by the same rule inside itself and
// then placed as ONE block (its hull footprint). Positions depend only on sizes, topology and order, never on where
// anything currently is, so arranging twice gives the same result.

namespace synth::LayoutUtil {

// ---- Open-macro hull geometry (the ONE definition; MacroGroupControllerGeometry.cpp's macroHullBounds calls it) ----
inline constexpr int kHullMargin = 14;           // clear space around the member union (top: see kHullChipRow)
inline constexpr int kHullChipRow = 24;          // name-chip row above the members
inline constexpr int kHullPortRowsBelowChip = 6; // gap between the chip row and the first port row
inline constexpr int kHullPortRowHeight = 16;    // one port row
inline constexpr int kHullPortFooter = 22;       // room below the last port row for the '+' / '-' pair

// The hull an open macro draws around `memberUnion` (the union of its non-port members and child footprints):
// the union grown by kHullMargin, a taller top for the chip row, kMacroHullSideOutset of port strip on each side,
// and tall enough for `portRows` (the busiest side's port rows).
juce::Rectangle<int> openMacroHull(juce::Rectangle<int> memberUnion, int portRows);

enum class ArrangeKind {
    Module,         // one card
    CollapsedMacro, // one card (or any rigid rectangle): its hidden members travel with it, never arranged inside
    OpenMacro,      // a hull around `children`, arranged recursively; without children it is rigid like a card
};

struct ArrangeBlock {
    juce::String id; // unique across the whole tree
    ArrangeKind kind = ArrangeKind::Module;
    juce::Point<int> size;              // Module / CollapsedMacro / childless OpenMacro footprint
    int roleRank = 4;                   // order inside one column of one row: lower is higher up
    long long order = 0;                // stable tie-break (node uid, macro creation order); lower first
    int portRows = 0;                   // OpenMacro: rows of the busiest port side (sets a minimum hull height)
    std::vector<ArrangeBlock> children; // OpenMacro only
};

struct ArrangeEdge {
    juce::String from;
    juce::String to;
    bool modulation = false; // a modulation routing rather than a signal cable; both shape columns the same way
};

struct ArrangeInput {
    std::vector<ArrangeBlock> blocks; // the top level
    // Endpoints may name any block at any level, or an id listed in `aliases` (a hidden member of a collapsed
    // macro, a macro port node: resolved to the block that stands in for it). An endpoint that resolves to no
    // block (the output dock, a hidden helper node) drops its edge.
    std::vector<ArrangeEdge> edges;
    std::vector<std::pair<juce::String, juce::String>> aliases; // id -> block id standing in for it
    // The block (or alias) each track's chain starts at, in track order. Chains claim the blocks reachable from
    // them; a track whose start was already claimed by an earlier one gets no row of its own.
    std::vector<juce::String> trackStarts;
};

struct ArrangeOutput {
    // Every block at every level. Module and collapsed macro: its top-left. Open macro: its ANCHOR, the origin its
    // interior offsets are relative to, which is NOT its hull's top-left (see `hulls`).
    std::map<juce::String, juce::Point<int>> positions;
    std::map<juce::String, juce::Rectangle<int>> hulls; // open macros with children: the hull footprint
    int firstRowY = kArrangeOriginY;                    // the top row's y (the output dock lines up with it)
};

// Pure and deterministic. The top level starts at (kArrangeOriginX, kArrangeOriginY).
ArrangeOutput computeHierarchicalArrange(const ArrangeInput& input);

// Role rank for ordering blocks inside one column: sources first, then processors, effects, modulators.
int arrangeRoleRank(ModuleType type);

} // namespace synth::LayoutUtil
