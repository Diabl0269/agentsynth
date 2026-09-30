// HierarchicalArrange.cpp
//
// The pure layered layout behind auto-arrange. HierarchicalArrange.h states the rule; this unit is the mechanism:
// resolve edges to the blocks of one level, assign rows (shared modulators, tracks, remaining components), give
// every block a longest-path column, align columns across rows, and recurse into open macros.
// (docs/layout/layout.md#auto-arrange)

#include "HierarchicalArrange.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <tuple>

namespace synth::LayoutUtil {

juce::Rectangle<int> openMacroHull(juce::Rectangle<int> memberUnion, int portRows) {
    // Same shape the canvas draws: the union grown by the margin, a deeper top for the chip row, a fixed port strip
    // on each side, and a bottom that also holds the busiest side's port rows plus the '+' / '-' footer.
    auto hull = memberUnion.expanded(kHullMargin);
    hull.setTop(memberUnion.getY() - kHullChipRow);
    hull.setLeft(hull.getX() - (kMacroHullSideOutset - kHullMargin));
    hull.setRight(hull.getRight() + (kMacroHullSideOutset - kHullMargin));
    hull.setBottom(juce::jmax(hull.getBottom(), hull.getY() + kHullChipRow + kHullPortRowsBelowChip +
                                                    portRows * kHullPortRowHeight + kHullPortFooter));
    return hull;
}

int arrangeRoleRank(ModuleType t) {
    switch (t) {
    case ModuleType::Oscillator:
    case ModuleType::Sequencer:
    case ModuleType::PolySequencer:
    case ModuleType::MidiKeyboard:
    case ModuleType::PolyMidi:
    case ModuleType::ExternalMidi:
    case ModuleType::Noise:
    case ModuleType::Sampler:
    case ModuleType::Wavetable:
        return 0;
    case ModuleType::Filter:
    case ModuleType::ParametricEQ:
    case ModuleType::VCA:
    case ModuleType::VoiceMixer:
        return 1;
    case ModuleType::Delay:
    case ModuleType::Distortion:
    case ModuleType::Reverb:
    case ModuleType::Chorus:
    case ModuleType::Phaser:
    case ModuleType::Compressor:
    case ModuleType::Flanger:
    case ModuleType::Limiter:
    case ModuleType::Gate:
    case ModuleType::PitchShifter:
    case ModuleType::RingModulator:
        return 2;
    case ModuleType::ADSR:
    case ModuleType::LFO:
    case ModuleType::Math:
    case ModuleType::MacroControl:
    case ModuleType::SampleHold:
    case ModuleType::Comparator:
    case ModuleType::EnvelopeFollower:
        return 3;
    default:
        return 4;
    }
}

namespace {

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int gridUp(int v) { return -floorDiv(-v, kGridSize) * kGridSize; }

struct Item {
    const ArrangeBlock* block = nullptr;
    juce::Point<int> pos; // module / card: top-left; open macro: anchor
};

// One level laid out, in its own frame (origin (0,0)).
struct Frame {
    std::vector<Item> items;
    juce::Rectangle<int> bounds; // union of the items' footprints (an open macro's footprint is its hull)
};

struct MacroLayout {
    Frame frame;
    juce::Rectangle<int> hull; // relative to the macro's anchor
};

struct Context {
    const ArrangeInput& input;
    std::map<juce::String, juce::String> parentOf; // block id -> parent block id ("" at the top)
    std::map<juce::String, juce::String> alias;
    std::map<juce::String, MacroLayout> macros;
};

void indexTree(Context& ctx, const std::vector<ArrangeBlock>& blocks, const juce::String& parent) {
    for (const auto& b : blocks) {
        ctx.parentOf[b.id] = parent;
        indexTree(ctx, b.children, b.id);
    }
}

// The block of this level that stands for `id` (itself, its alias, or the nearest ancestor on the level), or -1.
int resolveAt(const Context& ctx, const std::map<juce::String, int>& levelIndex, juce::String id) {
    for (int guard = 0; guard < 64; ++guard) {
        if (auto it = levelIndex.find(id); it != levelIndex.end())
            return it->second;
        if (auto a = ctx.alias.find(id); a != ctx.alias.end()) {
            id = a->second;
            continue;
        }
        auto p = ctx.parentOf.find(id);
        if (p == ctx.parentOf.end() || p->second.isEmpty())
            return -1;
        id = p->second;
    }
    return -1;
}

Frame layoutLevel(Context& ctx, std::vector<const ArrangeBlock*> blocks, const std::vector<juce::String>& starts);

// An open macro's interior is a level of its own; its hull is the union of that level grown by openMacroHull.
const MacroLayout& layoutMacro(Context& ctx, const ArrangeBlock& macro) {
    if (auto it = ctx.macros.find(macro.id); it != ctx.macros.end())
        return it->second;
    std::vector<const ArrangeBlock*> kids;
    for (const auto& c : macro.children)
        kids.push_back(&c);
    MacroLayout layout;
    layout.frame = layoutLevel(ctx, kids, {});
    layout.hull = openMacroHull(layout.frame.bounds, macro.portRows);
    return ctx.macros[macro.id] = std::move(layout);
}

struct Edge {
    int a = 0;
    int b = 0;
    bool mod = false;
};

// Everything one level needs to decide rows and columns, over indices into the level's sorted block list.
struct Graph {
    int n = 0;
    std::vector<Edge> edges;
    std::vector<std::vector<int>> flowOut, flowIn, modOut, modIn, flowAll, all; // adjacency
};

Graph buildGraph(const Context& ctx, const std::map<juce::String, int>& levelIndex, int n) {
    Graph g;
    g.n = n;
    g.flowOut.resize(n);
    g.flowIn.resize(n);
    g.modOut.resize(n);
    g.modIn.resize(n);
    g.flowAll.resize(n);
    g.all.resize(n);
    std::set<std::tuple<int, int, bool>> seen;
    for (const auto& e : ctx.input.edges) {
        const int a = resolveAt(ctx, levelIndex, e.from);
        const int b = resolveAt(ctx, levelIndex, e.to);
        if (a < 0 || b < 0 || a == b || !seen.insert({a, b, e.modulation}).second)
            continue;
        g.edges.push_back({a, b, e.modulation});
        (e.modulation ? g.modOut : g.flowOut)[a].push_back(b);
        (e.modulation ? g.modIn : g.flowIn)[b].push_back(a);
        if (!e.modulation) {
            g.flowAll[a].push_back(b);
            g.flowAll[b].push_back(a);
        }
        g.all[a].push_back(b);
        g.all[b].push_back(a);
    }
    return g;
}

// Which row each block sits in, plus the row's category (0 shared modulators, 1 track, 2 everything else) and its
// sort key within the category. Rows are numbered in creation order; `finalRank` says where each ends up.
struct Rows {
    std::vector<int> rowOf;
    std::vector<int> category;
    std::vector<int> key;
    std::vector<int> stackOrder; // shared-row blocks: their place in the consumer-count order
    std::vector<int> finalRank;  // row -> position, top to bottom
    std::vector<int> anchor;     // row -> the block the row starts at (a track's start, a component's leftmost source)
};

// Every unassigned block adjacent to an assigned one joins the earliest assigned neighbour's row, to a fixed point.
void propagateRows(std::vector<int>& rowOf, const std::vector<std::vector<int>>& adjacency) {
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < rowOf.size(); ++i) {
            if (rowOf[i] >= 0)
                continue;
            int best = -1;
            for (int j : adjacency[i])
                if (rowOf[j] >= 0 && (best < 0 || rowOf[j] < best))
                    best = rowOf[j];
            if (best >= 0) {
                rowOf[i] = best;
                changed = true;
            }
        }
    }
}

// Unassigned blocks with any adjacency in `adjacency` (or all of them when `includeIsolated`), one row per connected
// component, keyed by the component's leftmost source (its first block with nothing feeding it, else its first block).
void assignComponentRows(Rows& rows, const Graph& g, const std::vector<std::vector<int>>& adjacency,
                         const std::vector<std::vector<int>>& incoming, bool includeIsolated) {
    for (int start = 0; start < g.n; ++start) {
        if (rows.rowOf[start] >= 0 || (!includeIsolated && adjacency[start].empty()))
            continue;
        std::vector<int> comp{start};
        rows.rowOf[start] = -2; // visiting
        for (size_t k = 0; k < comp.size(); ++k)
            for (int v : adjacency[comp[k]])
                if (rows.rowOf[v] == -1) {
                    rows.rowOf[v] = -2;
                    comp.push_back(v);
                }
        std::sort(comp.begin(), comp.end());
        int leftmost = comp.front();
        for (int v : comp)
            if (incoming[v].empty()) {
                leftmost = v;
                break;
            }
        const int row = static_cast<int>(rows.category.size());
        rows.category.push_back(2);
        rows.key.push_back(leftmost);
        rows.anchor.push_back(leftmost);
        for (int v : comp)
            rows.rowOf[v] = row;
    }
}

Rows assignRows(const Context& ctx, const Graph& g, const std::map<juce::String, int>& levelIndex,
                const std::vector<juce::String>& starts) {
    Rows rows;
    rows.rowOf.assign(g.n, -1);
    rows.stackOrder.assign(g.n, 0);

    // Track rows: each start claims what its chain reaches, unless an earlier track already did.
    int trackNo = 0;
    for (const auto& s : starts) {
        const int a = resolveAt(ctx, levelIndex, s);
        if (a < 0 || rows.rowOf[a] >= 0)
            continue;
        const int row = static_cast<int>(rows.category.size());
        rows.category.push_back(1);
        rows.key.push_back(trackNo++);
        rows.anchor.push_back(a);
        rows.rowOf[a] = row;
        std::vector<int> stack{a};
        while (!stack.empty()) {
            const int u = stack.back();
            stack.pop_back();
            for (int v : g.flowOut[u])
                if (rows.rowOf[v] < 0) {
                    rows.rowOf[v] = row;
                    stack.push_back(v);
                }
        }
    }
    // Things feeding a track's chain (an instrument before its Track In's channel...) join the earliest track they
    // touch.
    propagateRows(rows.rowOf, g.flowAll);
    assignComponentRows(rows, g, g.flowAll, g.flowIn, /*includeIsolated=*/false);

    // A pure modulator (no signal cables at all, nothing feeding it) whose consumers sit in two or more rows is shared:
    // it goes in the shared row on top. One consumer row: it joins that row.
    std::vector<std::pair<int, int>> shared; // (-consumers, index)
    for (int i = 0; i < g.n; ++i) {
        if (rows.rowOf[i] >= 0 || !g.flowAll[i].empty() || !g.modIn[i].empty() || g.modOut[i].empty())
            continue;
        std::set<int> consumerRows;
        for (int c : g.modOut[i])
            if (rows.rowOf[c] >= 0)
                consumerRows.insert(rows.rowOf[c]);
        if (consumerRows.size() >= 2)
            shared.push_back({-static_cast<int>(g.modOut[i].size()), i});
        else if (consumerRows.size() == 1)
            rows.rowOf[i] = *consumerRows.begin();
    }
    if (!shared.empty()) {
        std::sort(shared.begin(), shared.end());
        const int row = static_cast<int>(rows.category.size());
        rows.category.push_back(0);
        rows.key.push_back(0);
        rows.anchor.push_back(-1);
        for (size_t k = 0; k < shared.size(); ++k) {
            rows.rowOf[shared[k].second] = row;
            rows.stackOrder[shared[k].second] = static_cast<int>(k);
        }
    }

    propagateRows(rows.rowOf, g.all);
    assignComponentRows(
        rows, g, g.all,
        [&] {
            std::vector<std::vector<int>> in(g.n);
            for (int i = 0; i < g.n; ++i)
                in[i] = g.flowIn[i].empty() ? g.modIn[i] : g.flowIn[i];
            return in;
        }(),
        /*includeIsolated=*/true);

    std::vector<int> ids(rows.category.size());
    for (size_t i = 0; i < ids.size(); ++i)
        ids[i] = static_cast<int>(i);
    std::sort(ids.begin(), ids.end(), [&](int x, int y) {
        return std::tie(rows.category[x], rows.key[x], x) < std::tie(rows.category[y], rows.key[y], y);
    });
    rows.finalRank.assign(ids.size(), 0);
    for (size_t rank = 0; rank < ids.size(); ++rank)
        rows.finalRank[ids[rank]] = static_cast<int>(rank);
    return rows;
}

// Longest path from a row's sources, over the edges inside the row; a cycle is broken by ignoring back-edges.
std::vector<int> computeDepths(const Graph& g, const std::vector<int>& rowOf) {
    std::vector<int> depth(g.n, 0);
    std::vector<std::vector<int>> out(g.n);
    std::vector<int> indegree(g.n, 0);
    for (const auto& e : g.edges)
        if (rowOf[e.a] == rowOf[e.b]) {
            out[e.a].push_back(e.b);
            ++indegree[e.b];
        }
    std::set<int> ready;
    for (int i = 0; i < g.n; ++i)
        if (indegree[i] == 0)
            ready.insert(i);
    std::vector<bool> visited(g.n, false);
    while (!ready.empty()) {
        const int u = *ready.begin();
        ready.erase(ready.begin());
        visited[u] = true;
        for (int v : out[u]) {
            if (visited[v])
                continue;
            depth[v] = std::max(depth[v], depth[u] + 1);
            if (--indegree[v] == 0)
                ready.insert(v);
        }
    }
    return depth;
}

// A source block (nothing feeding it inside its row) that is not its row's anchor is pulled right up to its nearest
// consumer: depth = (smallest consumer depth) - 1. Without this every such block sits over column 0, far from what it
// feeds. A consumer has an incoming edge, so it is never a source itself and its depth never changes here.
// `consumerOf[i]` is the consumer a moved block follows (-1 for every block that stayed put).
std::vector<int> placeSourcesAsLateAsPossible(const Graph& g, const Rows& rows, std::vector<int>& depth) {
    std::vector<bool> hasInput(g.n, false);
    std::vector<int> consumerOf(g.n, -1);
    for (const auto& e : g.edges)
        if (rows.rowOf[e.a] == rows.rowOf[e.b])
            hasInput[e.b] = true;
    for (const auto& e : g.edges)
        if (rows.rowOf[e.a] == rows.rowOf[e.b] &&
            (consumerOf[e.a] < 0 || std::tie(depth[e.b], e.b) < std::tie(depth[consumerOf[e.a]], consumerOf[e.a])))
            consumerOf[e.a] = e.b;
    for (int i = 0; i < g.n; ++i) {
        if (hasInput[i] || rows.anchor[rows.rowOf[i]] == i || consumerOf[i] < 0) {
            consumerOf[i] = -1;
            continue;
        }
        depth[i] = std::max(0, depth[consumerOf[i]] - 1);
    }
    return consumerOf;
}

// Stack order of a row (sorted by column): a moved source takes the slot its consumer has in the next column, so it
// lands beside what it feeds instead of under everything else in its column.
void seatMovedSources(std::vector<int>& members, const std::vector<int>& depth, const std::vector<int>& consumerOf) {
    auto slotInColumn = [&](int block) {
        int slot = 0;
        for (int m : members) {
            if (m == block)
                return slot;
            slot += depth[m] == depth[block] ? 1 : 0;
        }
        return slot;
    };
    std::vector<int> moved;
    for (int m : members)
        if (consumerOf[m] >= 0)
            moved.push_back(m);
    for (int m : moved) {
        const int slot = slotInColumn(consumerOf[m]);
        members.erase(std::find(members.begin(), members.end(), m));
        auto at = members.begin();
        int seen = 0;
        for (; at != members.end() && depth[*at] <= depth[m]; ++at)
            if (depth[*at] == depth[m] && seen++ == slot)
                break;
        members.insert(at, m);
    }
}

Frame layoutLevel(Context& ctx, std::vector<const ArrangeBlock*> blocks, const std::vector<juce::String>& starts) {
    Frame frame;
    if (blocks.empty())
        return frame;
    std::stable_sort(blocks.begin(), blocks.end(), [](const ArrangeBlock* a, const ArrangeBlock* b) {
        return std::tie(a->order, a->id) < std::tie(b->order, b->id);
    });
    const int n = static_cast<int>(blocks.size());
    std::map<juce::String, int> levelIndex;
    for (int i = 0; i < n; ++i)
        levelIndex[blocks[i]->id] = i;

    // Footprints. An open macro's is its hull; `anchorOff` is where that hull starts relative to the anchor the
    // macro's interior offsets are measured from (a module's anchor is its own top-left).
    std::vector<juce::Point<int>> size(n), anchorOff(n);
    for (int i = 0; i < n; ++i) {
        if (blocks[i]->kind == ArrangeKind::OpenMacro && !blocks[i]->children.empty()) {
            const auto& hull = layoutMacro(ctx, *blocks[i]).hull;
            size[i] = {hull.getWidth(), hull.getHeight()};
            anchorOff[i] = hull.getPosition();
        } else {
            size[i] = blocks[i]->size;
        }
    }

    const Graph g = buildGraph(ctx, levelIndex, n);
    const Rows rows = assignRows(ctx, g, levelIndex, starts);
    auto depth = computeDepths(g, rows.rowOf);
    const auto consumerOf = placeSourcesAsLateAsPossible(g, rows, depth);

    int maxDepth = 0;
    for (int d : depth)
        maxDepth = std::max(maxDepth, d);
    std::vector<int> colW(maxDepth + 1, 0);
    for (int i = 0; i < n; ++i)
        colW[depth[i]] = std::max(colW[depth[i]], gridUp(size[i].x));
    std::vector<int> colX(maxDepth + 1, 0);
    for (int k = 1; k <= maxDepth; ++k)
        colX[k] = colX[k - 1] + colW[k - 1] + (colW[k - 1] > 0 ? kLayerGapX : 0);

    // Rows top to bottom; inside a column of a row: shared modulators by consumer count, everything else by role
    // rank then the stable order.
    const int rowCount = static_cast<int>(rows.category.size());
    std::vector<std::vector<int>> rowMembers(rowCount);
    for (int i = 0; i < n; ++i)
        rowMembers[rows.finalRank[rows.rowOf[i]]].push_back(i);

    int y = 0;
    for (int rank = 0; rank < rowCount; ++rank) {
        auto& members = rowMembers[rank];
        if (members.empty())
            continue;
        std::stable_sort(members.begin(), members.end(), [&](int a, int b) {
            if (depth[a] != depth[b])
                return depth[a] < depth[b];
            if (rows.category[rows.rowOf[a]] == 0)
                return rows.stackOrder[a] < rows.stackOrder[b];
            return std::make_tuple(blocks[a]->roleRank, a) < std::make_tuple(blocks[b]->roleRank, b);
        });
        seatMovedSources(members, depth, consumerOf);
        std::vector<int> stackY(maxDepth + 1, 0);
        int rowHeight = 0;
        for (int i : members) {
            const int k = depth[i];
            // Left-aligned in its column: a wider block elsewhere in the column never shifts a neighbour sideways.
            const juce::Point<int> slot{colX[k], y + stackY[k]};
            const juce::Point<int> anchor{gridUp(slot.x - anchorOff[i].x), gridUp(slot.y - anchorOff[i].y)};
            frame.items.push_back({blocks[i], anchor});
            const juce::Rectangle<int> rect(anchor + anchorOff[i], juce::Point<int>(anchor + anchorOff[i]) + size[i]);
            frame.bounds = frame.bounds.isEmpty() ? rect : frame.bounds.getUnion(rect);
            stackY[k] += gridUp(size[i].y) + kIntraLayerGapY;
            rowHeight = std::max(rowHeight, stackY[k] - kIntraLayerGapY);
        }
        y += gridUp(rowHeight) + kIntraLayerGapY;
    }
    return frame;
}

void emit(const Context& ctx, const Frame& frame, juce::Point<int> origin, ArrangeOutput& out) {
    for (const auto& item : frame.items) {
        const auto at = origin + item.pos;
        out.positions[item.block->id] = at;
        if (item.block->kind != ArrangeKind::OpenMacro || item.block->children.empty())
            continue;
        const auto& ml = ctx.macros.at(item.block->id);
        out.hulls[item.block->id] = ml.hull.translated(at.x, at.y);
        emit(ctx, ml.frame, at, out);
    }
}

} // namespace

ArrangeOutput computeHierarchicalArrange(const ArrangeInput& input) {
    Context ctx{input, {}, {}, {}};
    indexTree(ctx, input.blocks, {});
    for (const auto& [id, target] : input.aliases)
        ctx.alias[id] = target;

    std::vector<const ArrangeBlock*> top;
    for (const auto& b : input.blocks)
        top.push_back(&b);
    const Frame frame = layoutLevel(ctx, top, input.trackStarts);

    ArrangeOutput out;
    emit(ctx, frame, {kArrangeOriginX, kArrangeOriginY}, out);
    return out;
}

} // namespace synth::LayoutUtil
