// MixerInsertListAcrossTests.cpp (docs/mixer/mixer.md#adding-an-insert): the add affordances next to parallel paths.
// A chain that ends in a split still ends in "+ Insert" (the add goes behind every path), the line above a split adds
// in front of every path and the line below one behind them. InsertAddAffordanceMatrix is the guard for the sibling-gap
// kind: every chain shape x every entry point (the "+ Insert" row, each gap line, each path's "+") offers a target, and
// each add keeps every path wired and undoes in one step.
#include "MixerInsertListSplitFixture.h"

#include "UI/Graph/ModMatrixPicker.h"

#include <queue>

using namespace mixer_split_test;

namespace {
using synth::ui::MixerInsertList;
using Edges = std::set<std::tuple<juce::uint32, int, juce::uint32, int>>;

Edges rawEdges(SplitListRig& r) {
    Edges out;
    for (const auto& c : r.graph.getConnections())
        out.insert({c.source.nodeID.uid, c.source.channelIndex, c.destination.nodeID.uid, c.destination.channelIndex});
    return out;
}

std::set<juce::uint32> nodeUids(SplitListRig& r) {
    std::set<juce::uint32> ids;
    for (auto* n : r.graph.getNodes())
        ids.insert(n->nodeID.uid);
    return ids;
}

std::set<juce::uint32> reach(SplitListRig& r, NodeID from, bool forward) {
    std::set<juce::uint32> seen{from.uid};
    std::queue<NodeID> todo;
    todo.push(from);
    while (!todo.empty()) {
        const auto node = todo.front();
        todo.pop();
        for (const auto& c : r.graph.getConnections()) {
            const auto next = forward ? (c.source.nodeID == node ? c.destination.nodeID : NodeID{})
                                      : (c.destination.nodeID == node ? c.source.nodeID : NodeID{});
            if (next != NodeID{} && seen.insert(next.uid).second)
                todo.push(next);
        }
    }
    return seen;
}

int splitCount(SplitListRig& r) {
    int n = 0;
    for (const auto& e : r.column().inserts)
        n += e.kind == synth::MixerInsertEntry::Kind::Split ? 1 : 0;
    return n;
}

struct PickerCapture {
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    PickerCapture() {
        MixerInsertList::pickerHookForTest() = [this](auto p) { picker = std::move(p); };
    }
    ~PickerCapture() { MixerInsertList::pickerHookForTest() = nullptr; }
    void pick(const juce::String& text) {
        ASSERT_NE(picker, nullptr);
        const auto items = picker->getVisibleItemTextsForTest();
        const auto it = std::find(items.begin(), items.end(), text);
        ASSERT_NE(it, items.end()) << text;
        picker->chooseVisibleItemForTest((int)(it - items.begin()));
    }
};

// One chain shape: how to wire it, and which gap lines (row boundaries) must offer an add.
struct Shape {
    const char* name;
    std::function<void(SplitListRig&)> build;
    std::vector<int> gapRows;
    int splitRows;
};

const std::vector<Shape>& shapes() {
    static const std::vector<Shape> all{
        {"flat",
         [](SplitListRig& r) {
             const auto filter = r.add("Filter", 300, 400);
             const auto comp = r.add("Compressor", 600, 400);
             r.wire(r.source, filter);
             r.wire(filter, comp);
             r.wire(comp, r.strip);
         },
         {0, 1},
         0},
        {"split-first",
         [](SplitListRig& r) {
             const auto delay = r.add("Delay", 300, 250);
             const auto reverb = r.add("Reverb", 300, 550);
             const auto vca = r.add("VCA", 700, 400);
             r.wire(r.source, delay);
             r.wire(r.source, reverb);
             r.wire(delay, vca);
             r.wire(reverb, vca);
             r.wire(vca, r.strip);
         },
         {0, 3},
         1},
        {"split-last",
         [](SplitListRig& r) {
             const auto filter = r.add("Filter", 300, 400);
             const auto delay = r.add("Delay", 600, 250);
             const auto reverb = r.add("Reverb", 600, 550);
             r.wire(r.source, filter);
             r.wire(filter, delay);
             r.wire(filter, reverb);
             r.wire(delay, r.strip);
             r.wire(reverb, r.strip);
         },
         {0, 1},
         1},
        {"whole chain a split with a dry path",
         [](SplitListRig& r) {
             const auto delay = r.add("Delay", 300, 250);
             r.wire(r.source, delay);
             r.wire(r.source, r.strip);
             r.wire(delay, r.strip);
         },
         {0},
         1},
        {"nested split, paths open",
         [](SplitListRig& r) {
             const auto delay = r.add("Delay", 300, 250);
             const auto chorus = r.add("Chorus", 600, 150);
             const auto phaser = r.add("Phaser", 600, 350);
             const auto mix = r.add("VCA", 900, 250);
             const auto reverb = r.add("Reverb", 300, 550);
             const auto out = r.add("Compressor", 1200, 400);
             r.wire(r.source, delay);
             r.wire(r.source, reverb);
             r.wire(delay, chorus);
             r.wire(delay, phaser);
             r.wire(chorus, mix);
             r.wire(phaser, mix);
             r.wire(mix, out);
             r.wire(reverb, out);
             r.wire(out, r.strip);
         },
         {0, 2, 5, 7},
         2},
    };
    return all;
}

void openEverySplit(SplitListRig& r) {
    for (int row = 0; row < r.list.getRowCount(); ++row)
        if (r.list.isSplitRow(row))
            r.list.setSplitOpen(row, true);
}

SplitListRig* buildRig(const Shape& shape, std::unique_ptr<SplitListRig>& holder) {
    holder = std::make_unique<SplitListRig>();
    shape.build(*holder);
    holder->refresh();
    openEverySplit(*holder);
    return holder.get();
}

struct EntryPoint {
    juce::String name;
    std::function<bool(SplitListRig&)> add; // false: this entry point offers no target
};

// Every entry point the list has for this shape: the end row, each gap line, each path of each split row.
std::vector<EntryPoint> entryPoints(SplitListRig& r, const Shape& shape) {
    std::vector<EntryPoint> out;
    out.push_back({"+ Insert row", [](SplitListRig& rig) {
                       if (!rig.list.hasAddRow())
                           return false;
                       PickerCapture capture;
                       rig.list.activateAddRow();
                       if (capture.picker == nullptr)
                           return false;
                       capture.pick("Compressor");
                       return true;
                   }});
    for (int row = 0; row < r.list.getRowCount(); ++row) {
        out.push_back({"gap above row " + juce::String(row), [row](SplitListRig& rig) {
                           const auto target = rig.list.gapTargetAt({40, rig.list.getRowBounds(row).getY()});
                           if (!target)
                               return false;
                           rig.list.addModuleAt("Compressor", *target);
                           return true;
                       }});
        if (r.list.isSplitRow(row))
            for (int path = 0; path < 2; ++path)
                out.push_back({"path " + juce::String(path + 1) + " of split row " + juce::String(row),
                               [row, path](SplitListRig& rig) {
                                   const auto target = rig.list.pathAddTarget(row, path);
                                   if (!target)
                                       return false;
                                   rig.list.addModuleAt("Compressor", *target);
                                   return true;
                               }});
    }
    (void)shape;
    return out;
}

bool isGapEntry(const EntryPoint& e) { return e.name.startsWith("gap above row "); }
int gapRowOf(const EntryPoint& e) { return e.name.fromLastOccurrenceOf(" ", false, false).getIntValue(); }

} // namespace

// Bug kind sibling-gap. Every shape offers its "+ Insert" row, every gap line the shape names, and every path's "+";
// each add wires the new module in, keeps every earlier module on a route from the source to the strip, keeps the
// number of splits, and one Cmd+Z puts every raw edge back.
TEST(MixerInsertListAcrossTests, InsertAddAffordanceMatrix) {
    for (const auto& shape : shapes()) {
        std::unique_ptr<SplitListRig> probeHolder;
        auto* probe = buildRig(shape, probeHolder);
        const auto points = entryPoints(*probe, shape);
        int splitRows = 0;
        for (int row = 0; row < probe->list.getRowCount(); ++row)
            splitRows += probe->list.isSplitRow(row) ? 1 : 0;
        ASSERT_EQ(splitRows, shape.splitRows) << shape.name;

        for (size_t k = 0; k < points.size(); ++k) {
            std::unique_ptr<SplitListRig> holder;
            auto* r = buildRig(shape, holder);
            const auto& point = points[k];
            SCOPED_TRACE(juce::String(shape.name) + " / " + point.name);

            const auto edgesBefore = rawEdges(*r);
            const auto nodesBefore = nodeUids(*r);
            const int splitsBefore = splitCount(*r);
            const bool offered = point.add(*r);

            const bool gapMustExist = isGapEntry(point) && std::find(shape.gapRows.begin(), shape.gapRows.end(),
                                                                     gapRowOf(point)) != shape.gapRows.end();
            if (!offered) {
                EXPECT_FALSE(gapMustExist) << "the gap line the shape names offers no add";
                EXPECT_FALSE(point.name.startsWith("+ Insert") || point.name.startsWith("path "))
                    << "this entry point must always have a target";
                EXPECT_EQ(rawEdges(*r), edgesBefore);
                continue;
            }
            if (isGapEntry(point))
                EXPECT_TRUE(gapMustExist) << "a gap line the shape does not name offers an add";

            EXPECT_EQ(r->mutations, 1);
            const auto nodesAfter = nodeUids(*r);
            ASSERT_EQ(nodesAfter.size(), nodesBefore.size() + 1);
            NodeID added;
            for (auto uid : nodesAfter)
                if (nodesBefore.count(uid) == 0)
                    added = NodeID{uid};
            const auto fromSource = reach(*r, r->source, true);
            const auto toStrip = reach(*r, r->strip, false);
            EXPECT_EQ(fromSource.count(added.uid), 1u) << "the new module is fed by the track";
            EXPECT_EQ(toStrip.count(added.uid), 1u) << "the new module feeds the strip";
            for (auto uid : nodesBefore) {
                if (uid == r->source.uid || uid == r->strip.uid)
                    continue;
                EXPECT_EQ(fromSource.count(uid), 1u) << "module " << (int)uid << " lost its feed";
                EXPECT_EQ(toStrip.count(uid), 1u) << "module " << (int)uid << " no longer reaches the strip";
            }
            EXPECT_EQ(splitCount(*r), splitsBefore) << "no path was lost or merged";

            ASSERT_TRUE(r->undo.undo()) << "the add is one undo step";
            EXPECT_EQ(rawEdges(*r), edgesBefore);
            EXPECT_EQ(nodeUids(*r), nodesBefore);
            EXPECT_FALSE(r->undo.canUndo()) << "and only one";
        }
    }
}

// The ticket's own check: a track whose chain ends in two paths keeps a "+ Insert" row; its pick lands after the merge
// so every path runs through it, as raw edges.
TEST(MixerInsertListAcrossTests, PlusInsertAfterTheLastSplitRoutesEveryPathThroughTheNewModule) {
    SplitListRig r;
    const auto delay = r.add("Delay", 300, 250);
    r.wire(r.source, delay);
    r.wire(r.source, r.strip);
    r.wire(delay, r.strip);
    r.refresh();
    ASSERT_TRUE(r.list.hasAddRow());
    EXPECT_EQ(r.list.getAddRowIndex(), 1);
    EXPECT_EQ(r.list.describeRow(1), "Add an insert after all paths");
    EXPECT_EQ(r.list.tooltipAt({40, r.list.getRowBounds(1).getCentreY()}), "Add an effect after all paths");
    ASSERT_NE(r.list.getRowAccessibilityComponentForTest(1), nullptr);
    EXPECT_EQ(r.list.getRowAccessibilityComponentForTest(1)->getTitle(), "Add an insert after all paths")
        << "the screen-reader name of the row says where the add lands";

    const auto nodes = nodeUids(r);
    PickerCapture capture;
    r.list.activateAddRow();
    capture.pick("Compressor");
    NodeID comp;
    for (auto uid : nodeUids(r))
        if (nodes.count(uid) == 0)
            comp = NodeID{uid};
    ASSERT_NE(comp.uid, 0u);
    EXPECT_TRUE(r.wired(delay, comp));
    EXPECT_TRUE(r.wired(r.source, comp)) << "the dry cable runs through it too";
    EXPECT_TRUE(r.wired(comp, r.strip));
    EXPECT_FALSE(r.wired(delay, r.strip));
    EXPECT_FALSE(r.wired(r.source, r.strip));
    EXPECT_TRUE(r.wired(r.source, delay));
}

// The line above the first split row adds in front of every path: the track feeds the new module, which feeds each
// path.
TEST(MixerInsertListAcrossTests, TheLineAboveASplitAddsInFrontOfEveryPath) {
    SplitListRig r;
    const auto delay = r.add("Delay", 300, 250);
    const auto reverb = r.add("Reverb", 300, 550);
    r.wire(r.source, delay);
    r.wire(r.source, reverb);
    r.wire(delay, r.strip);
    r.wire(reverb, r.strip);
    r.refresh();

    const auto target = r.list.gapTargetAt({40, 0});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(r.list.tooltipAt({40, 0}), "Add an effect before all paths");
    const auto nodes = nodeUids(r);
    r.list.addModuleAt("Chorus", *target);
    NodeID chorus;
    for (auto uid : nodeUids(r))
        if (nodes.count(uid) == 0)
            chorus = NodeID{uid};
    ASSERT_NE(chorus.uid, 0u);
    EXPECT_TRUE(r.wired(r.source, chorus));
    EXPECT_TRUE(r.wired(chorus, delay));
    EXPECT_TRUE(r.wired(chorus, reverb));
    EXPECT_FALSE(r.wired(r.source, delay));
    EXPECT_FALSE(r.wired(r.source, reverb));
    EXPECT_EQ(r.rows()[0], "Insert 1, Chorus");
    EXPECT_EQ(r.mutations, 1);
}

// A dangling module hung off the fork is not a path: the add in front of the paths leaves its cable alone.
TEST(MixerInsertListAcrossTests, ADanglingModuleOffTheForkIsNotRoutedThroughTheNewModule) {
    SplitListRig r;
    const auto delay = r.add("Delay", 300, 250);
    const auto reverb = r.add("Reverb", 300, 550);
    const auto loose = r.add("Distortion", 300, 750);
    r.wire(r.source, delay);
    r.wire(r.source, reverb);
    r.wire(delay, r.strip);
    r.wire(reverb, r.strip);
    r.wire(r.source, loose);
    r.refresh();

    const auto target = r.list.gapTargetAt({40, 0});
    ASSERT_TRUE(target.has_value());
    r.list.addModuleAt("Chorus", *target);
    EXPECT_TRUE(r.wired(r.source, loose)) << "the dangling cable stays on the fork";
}

// Mid-chain: the line below a split row's last path adds behind all of them, before the module they merge into.
TEST(MixerInsertListAcrossTests, TheLineBelowASplitAddsBehindEveryPath) {
    TicketPatch p;
    p.list.setSplitOpen(1, true);
    int vcaRow = -1;
    for (int row = 0; row < p.list.getRowCount(); ++row)
        if (p.list.describeRow(row).contains("VCA"))
            vcaRow = row;
    ASSERT_GT(vcaRow, 0);
    const auto target = p.list.gapTargetAt({40, p.list.getRowBounds(vcaRow).getY()});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(p.list.tooltipAt({40, p.list.getRowBounds(vcaRow).getY()}), "Add an effect after all paths");
    const auto nodes = nodeUids(p);
    p.list.addModuleAt("Chorus", *target);
    NodeID chorus;
    for (auto uid : nodeUids(p))
        if (nodes.count(uid) == 0)
            chorus = NodeID{uid};
    ASSERT_NE(chorus.uid, 0u);
    EXPECT_TRUE(p.wired(p.delay, chorus));
    EXPECT_TRUE(p.wired(p.reverb, chorus));
    EXPECT_TRUE(p.wired(chorus, p.vca));
    EXPECT_FALSE(p.wired(p.delay, p.vca));
    EXPECT_FALSE(p.wired(p.reverb, p.vca));
}

// Keyboard: Shift+F10 on a focused split row opens its menu beside the row, and the two new items add across every
// path. The menu never runs headless, so the hook hands over the menu and the callback its choice calls.
TEST(MixerInsertListAcrossTests, TheSplitRowMenuOpenedFromTheKeyboardOffersAddBeforeAndAfterAllPaths) {
    SplitListRig r;
    const auto delay = r.add("Delay", 300, 250);
    r.wire(r.source, delay);
    r.wire(r.source, r.strip);
    r.wire(delay, r.strip);
    r.refresh();
    ASSERT_TRUE(r.list.isSplitRow(0));

    std::vector<juce::String> texts;
    std::function<void(int)> chosen;
    MixerInsertList::menuHookForTest() = [&](juce::PopupMenu& menu, std::function<void(int)> callback) {
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            texts.push_back(it.getItem().text);
        chosen = std::move(callback);
    };
    ASSERT_TRUE(r.list.showMenuForKeyboardRow(0));
    MixerInsertList::menuHookForTest() = nullptr;
    EXPECT_NE(std::find(texts.begin(), texts.end(), "Add before all paths"), texts.end());
    EXPECT_NE(std::find(texts.begin(), texts.end(), "Add after all paths"), texts.end());

    const auto nodes = nodeUids(r);
    PickerCapture capture;
    chosen(4);
    capture.pick("Compressor");
    NodeID comp;
    for (auto uid : nodeUids(r))
        if (nodes.count(uid) == 0)
            comp = NodeID{uid};
    ASSERT_NE(comp.uid, 0u);
    EXPECT_TRUE(r.wired(delay, comp));
    EXPECT_TRUE(r.wired(r.source, comp));
    EXPECT_TRUE(r.wired(comp, r.strip));
}
