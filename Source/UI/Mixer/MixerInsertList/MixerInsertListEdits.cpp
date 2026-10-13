// Concern: MixerInsertList's mouse, menus and edits: the effect picker and add flow, and move/remove, each ONE
// recordGraphAndMacroChange around MixerModel's Core splice primitives (docs/mixer/mixer.md#adding-an-insert).
#include "MixerInsertList.h"

#include "AppUndoManager.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/InsertGap/InsertGapKeyboard.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/ReplaceWithPicker.h"
#include "UI/Layout/ContextMenuPlacement.h"

namespace synth::ui {
namespace {

using Kind = synth::MixerInsertEntry::Kind;

bool isSet(juce::AudioProcessorGraph::NodeID id) { return id != juce::AudioProcessorGraph::NodeID{}; }

// The library categories the effect picker lists: the stereo-in, stereo-out effects that sit in a chain.
const juce::StringArray kEffectHeaders{"Filters", "Modulation FX", "Time FX", "Dynamics"};
constexpr int kMaxRecent = 3;

const juce::StringArray& addableModuleTypes() {
    static const juce::StringArray types = [] {
        juce::StringArray out;
        for (const auto& category : moduleChoiceCategories())
            if (kEffectHeaders.contains(category.header))
                for (const auto& module : category.modules)
                    out.add(module.name);
        return out;
    }();
    return types;
}

// This session's last picks, newest first: the picker's "Recent" group.
juce::StringArray& recentPicks() {
    static juce::StringArray recent;
    return recent;
}

void rememberPick(const juce::String& type) {
    auto& recent = recentPicks();
    recent.removeString(type);
    recent.insert(0, type);
    while (recent.size() > kMaxRecent)
        recent.remove(recent.size() - 1);
}

juce::String nodeUuid(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id) {
    auto* node = graph.getNodeForId(id);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    return module != nullptr ? juce::String(module->getNodeUuid()) : juce::String();
}

juce::AudioProcessorGraph::NodeID nodeWithUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            if (uuid.isNotEmpty() && juce::String(module->getNodeUuid()) == uuid)
                return node->nodeID;
    return {};
}

// True when `from` cables straight into `to` on a signal edge -- the one place an add can splice in.
bool cablesInto(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID from,
                juce::AudioProcessorGraph::NodeID to) {
    const auto connections = graph.getConnections();
    return std::any_of(connections.begin(), connections.end(), [&](const auto& c) {
        return c.source.nodeID == from && c.destination.nodeID == to && isSignalEdge(graph, connections, c);
    });
}

} // namespace

const juce::StringArray& MixerInsertList::getAddableModuleTypes() noexcept { return addableModuleTypes(); }

void MixerInsertList::resetRecentPicksForTest() { recentPicks().clear(); }

std::function<void(std::unique_ptr<ModMatrixPicker>)>& MixerInsertList::pickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}

// ---- Mouse and menus -------------------------------------------------------------------------------------------

void MixerInsertList::mouseDown(const juce::MouseEvent& event) {
    if (!isEditable()) {
        // The link sits right under the rows (or the "(no inserts)" placeholder) -- the same anchor paint() uses.
        if (event.y >= contentHeight() && onEditOnCanvas)
            onEditOnCanvas(editOnCanvasTargetUuid_);
        return;
    }
    if (const auto gap = gapTargetAt(event.getPosition()); gap && !event.mods.isPopupMenu()) {
        showAddPicker(*gap, juce::Rectangle<int>(event.x, event.y, 1, 1));
        return;
    }
    const int row = rowIndexAt(event.getPosition());
    if (isAddRow(row)) {
        activateAddRow();
        return;
    }
    if (event.mods.isPopupMenu()) {
        if (isSplitRow(row))
            showSplitMenu(row);
        else if (row >= 0)
            showRowMenu(row);
        else if (canAddAtEnd())
            showAddPicker(endTarget(), juce::Rectangle<int>(event.x, event.y, 1, 1));
        return;
    }
    if (isSplitRow(row))
        setSplitOpen(row, !isSplitOpen(row));
}

void MixerInsertList::mouseMove(const juce::MouseEvent& event) {
    int gap = -1;
    if (gapTargetAt(event.getPosition())) {
        // The boundary nearest the pointer: the top of the row it is closer to.
        const int row = rowIndexAt(event.getPosition().translated(0, kRowHeight / 2));
        gap = row >= 0 ? row : (int)visible_.size();
    }
    if (gap != hoverGap_) {
        hoverGap_ = gap;
        repaint();
    }
}

void MixerInsertList::mouseExit(const juce::MouseEvent&) {
    if (hoverGap_ >= 0) {
        hoverGap_ = -1;
        repaint();
    }
}

void MixerInsertList::showSplitMenu(int rowIndex) {
    juce::PopupMenu menu;
    menu.addItem(1, isSplitOpen(rowIndex) ? "Fold paths" : "Show paths");
    menu.addItem(2, isSplitBypassed(visible_[(size_t)rowIndex]) ? "Turn every path back on" : "Bypass every path");
    menu.addSeparator();
    const int paths = entryForRow(rowIndex)->pathCount;
    for (int p = 0; p < paths; ++p)
        menu.addItem(10 + p, "Add to path " + juce::String(p + 1), pathAddTarget(rowIndex, p).has_value());
    const auto area = getRowBounds(rowIndex);
    menu.showMenuAsync(synth::ui::contextMenuOptionsAtPointer(), [this, rowIndex, paths, area](int result) {
        if (result == 1)
            setSplitOpen(rowIndex, !isSplitOpen(rowIndex));
        else if (result == 2)
            toggleBypassForRow(rowIndex);
        else if (result >= 10 && result < 10 + paths)
            if (const auto target = pathAddTarget(rowIndex, result - 10))
                showAddPicker(*target, area);
    });
}

void MixerInsertList::showRowMenu(int rowIndex) {
    juce::PopupMenu menu;
    menu.addItem(1, "Move Up", canMoveRow(rowIndex, -1));
    menu.addItem(2, "Move Down", canMoveRow(rowIndex, 1));
    menu.addItem(3, "Remove");
    menu.addSeparator();
    // "Add after": right behind this module, before whatever it feeds (a fork module feeds several, so none).
    const auto node = entryForRow(rowIndex)->nodeId;
    const bool canAddAfter = graph_ != nullptr && isSet(synth::soleSignalNeighbour(*graph_, node, false));
    menu.addItem(4, "Add after", canAddAfter);
    const auto area = getRowBounds(rowIndex);
    menu.showMenuAsync(synth::ui::contextMenuOptionsAtPointer(), [this, rowIndex, node, area](int result) {
        if (result == 1)
            moveRow(rowIndex, -1);
        else if (result == 2)
            moveRow(rowIndex, 1);
        else if (result == 3)
            removeRow(rowIndex);
        else if (result == 4)
            showAddPicker({node, {}}, area);
    });
}

// ---- Mutations -------------------------------------------------------------------------------------------------

void MixerInsertList::mutateAndNotify(const std::function<bool()>& mutation) {
    if (graph_ == nullptr || undoManager_ == nullptr || macros_ == nullptr || graphEditor_ == nullptr)
        return;
    bool changed = false;
    // The edit can rebuild the mixer and destroy this list before it returns: the editor is copied first and the
    // notification only runs while the list is still alive.
    auto* graphEditor = graphEditor_;
    juce::Component::SafePointer<MixerInsertList> alive(this);
    undoManager_->recordGraphAndMacroChange(*graph_, *macros_, [&] {
        changed = mutation();
        graphEditor->updateComponents();
    });
    if (changed && alive != nullptr && onMutated)
        onMutated();
}

// On a split chain a module swaps only with the module right next to it on the same path: the row above or below
// it at the same depth and path, with no split row between them.
bool MixerInsertList::canMoveRow(int rowIndex, int delta) const {
    const auto* entry = entryForRow(rowIndex);
    if (entry == nullptr || entry->kind != Kind::Module || !isEditable())
        return false;
    if (!hasSplits_) {
        const int target = rowIndex + delta;
        return target >= 0 && target < (int)entries_.size();
    }
    const int neighbour = visible_[(size_t)rowIndex] + delta;
    if (neighbour < 0 || neighbour >= (int)entries_.size())
        return false;
    const auto& other = entries_[(size_t)neighbour];
    return other.kind == Kind::Module && other.depth == entry->depth && other.path == entry->path;
}

void MixerInsertList::moveRow(int rowIndex, int delta) {
    if (!canMoveRow(rowIndex, delta))
        return;
    if (hasSplits_) {
        const auto nodeId = entries_[(size_t)visible_[(size_t)rowIndex]].nodeId;
        const auto neighbourId = entries_[(size_t)(visible_[(size_t)rowIndex] + delta)].nodeId;
        mutateAndNotify([&] {
            return delta < 0 ? synth::moveInsertBefore(*graph_, nodeId, neighbourId)
                             : synth::moveInsertAfter(*graph_, nodeId, neighbourId);
        });
        return;
    }
    const int targetIndex = rowIndex + delta;
    const auto nodeId = entries_[(size_t)rowIndex].nodeId;

    // The new neighbours: remove `rowIndex` from the list, then read off whichever entries land
    // either side of `targetIndex` in what remains -- the track source / the strip itself stand in
    // for a missing neighbour at either end of the chain.
    std::vector<synth::MixerInsertEntry> without = entries_;
    without.erase(without.begin() + rowIndex);
    const int insertAt = juce::jlimit(0, (int)without.size(), targetIndex);
    const auto predecessorId = insertAt == 0 ? sourceNodeId_ : without[(size_t)insertAt - 1].nodeId;
    const auto successorId = insertAt >= (int)without.size() ? stripNodeId_ : without[(size_t)insertAt].nodeId;

    // A bus's own chain has no external source (sourceNodeId_ stays invalid -- nothing feeds
    // its EQ from outside, docs/mixer/sends-and-buses.md), so moving a row to the very front would ask
    // spliceInInsert to splice against a predecessor that doesn't exist. reorderInsert's second step
    // (spliceInInsert) has no rollback of its own on failure, so refuse up front -- same "nothing
    // changed" guard addModule already applies for its own empty-chain edge case -- rather than leave
    // the node spliced out but not back in.
    if (predecessorId == juce::AudioProcessorGraph::NodeID{})
        return;

    mutateAndNotify([&] { return synth::reorderInsert(*graph_, nodeId, predecessorId, successorId); });
}

// spliceOutInsert bridges every predecessor to every successor, so a split's fork or merge module, or a module on a
// path, comes out as cleanly as one on a straight chain. A split row is not a node and cannot be removed.
void MixerInsertList::removeRow(int rowIndex) {
    const auto* row = entryForRow(rowIndex);
    if (row == nullptr || row->kind != Kind::Module || !isEditable())
        return;
    const auto nodeId = row->nodeId;
    const auto uuid = row->uuid;
    mutateAndNotify([&] {
        if (!synth::spliceOutInsert(*graph_, nodeId))
            return false;
        // Unbind any UI object holding a raw pointer into `nodeId` (the column's
        // own MixerEqThumbnail, if this is its bound EQ) BEFORE removeNode() frees the processor
        // it points at -- see onBeforeNodeRemoved's own comment.
        if (onBeforeNodeRemoved)
            onBeforeNodeRemoved(nodeId);
        graph_->removeNode(nodeId);
        macros_->removeMemberEverywhere(uuid);
        return true;
    });
}

// An add lands at the end of the chain, between its last top-level row and the strip. A chain whose last top-level
// row is a split meets the strip on several cables, so there is no single cable to splice into there.
bool MixerInsertList::canAddAtEnd() const noexcept {
    if (!isEditable() || stripNodeId_ == juce::AudioProcessorGraph::NodeID{})
        return false;
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
        if (it->depth == 0)
            return it->kind == Kind::Module;
    return sourceNodeId_ != juce::AudioProcessorGraph::NodeID{};
}

MixerInsertList::AddTarget MixerInsertList::endTarget() const noexcept {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
        if (it->depth == 0)
            return {it->nodeId, stripNodeId_};
    return {sourceNodeId_, stripNodeId_};
}

void MixerInsertList::addModule(const juce::String& moduleTypeName) {
    if (canAddAtEnd())
        addModuleAt(moduleTypeName, endTarget());
}

void MixerInsertList::activateAddRow() {
    if (hasAddRow())
        showAddPicker(endTarget(), getRowBounds(getAddRowIndex()));
}

// The line between two rows offers an add when the two share a cable: two neighbouring modules on the same path, or
// (at the very top) the track source and a first top-level module.
std::optional<MixerInsertList::AddTarget> MixerInsertList::gapTargetAt(juce::Point<int> position) const {
    constexpr int kReach = 3;
    if (!isEditable() || position.x >= getWidth() - kBypassWidth || visible_.empty())
        return std::nullopt;
    for (int boundary = 0; boundary < (int)visible_.size(); ++boundary) {
        if (std::abs(position.y - rowTop(boundary)) > kReach)
            continue;
        const auto& below = entries_[(size_t)visible_[(size_t)boundary]];
        if (below.kind != Kind::Module)
            return std::nullopt;
        if (boundary == 0)
            return below.depth == 0 && isSet(sourceNodeId_)
                       ? std::optional<AddTarget>(AddTarget{sourceNodeId_, below.nodeId})
                       : std::nullopt;
        const auto& above = entries_[(size_t)visible_[(size_t)boundary - 1]];
        const bool sameRun = visible_[(size_t)boundary] == visible_[(size_t)boundary - 1] + 1 &&
                             above.kind == Kind::Module && above.depth == below.depth && above.path == below.path;
        return sameRun ? std::optional<AddTarget>(AddTarget{above.nodeId, below.nodeId}) : std::nullopt;
    }
    return std::nullopt;
}

// The end of path `path`: after its last module, or (a dry path) on the fork's own cable to the merge.
std::optional<MixerInsertList::AddTarget> MixerInsertList::pathAddTarget(int rowIndex, int path) const {
    const auto* split = entryForRow(rowIndex);
    if (split == nullptr || split->kind != Kind::Split || graph_ == nullptr)
        return std::nullopt;
    const int entryIndex = visible_[(size_t)rowIndex];
    const int end = subtreeEnd(entryIndex);
    int current = -1;
    juce::AudioProcessorGraph::NodeID last;
    for (int i = entryIndex + 1; i < end; ++i) {
        const auto& entry = entries_[(size_t)i];
        if (entry.depth == split->depth + 1)
            current = entry.path;
        if (current == path && entry.depth == split->depth + 1 && entry.kind == Kind::Module)
            last = entry.nodeId;
    }
    if (isSet(last))
        return AddTarget{last, {}};
    // A dry path: the fork cables straight into the merge, the next row at the split's own depth (or the strip).
    const auto fork = nodeWithUuid(*graph_, split->uuid);
    juce::AudioProcessorGraph::NodeID merge;
    if (end < (int)entries_.size() && entries_[(size_t)end].depth == split->depth &&
        entries_[(size_t)end].kind == Kind::Module)
        merge = entries_[(size_t)end].nodeId;
    else if (end == (int)entries_.size() && split->depth == 0)
        merge = stripNodeId_;
    if (!isSet(fork) || !isSet(merge) || !cablesInto(*graph_, fork, merge))
        return std::nullopt;
    return AddTarget{fork, merge};
}

std::vector<juce::String> MixerInsertList::effectPickerTypesForTest() {
    std::vector<juce::String> types;
    for (const auto& type : recentPicks())
        types.push_back(type);
    for (const auto& type : addableModuleTypes())
        types.push_back(type);
    return types;
}

// The shared searchable list (ModMatrixPicker), as "Replace with..." uses it: "Recent" first, then the effect
// categories under the library's own headers. A pick may land after a rebuild destroyed this list, so it goes
// through a SafePointer.
void MixerInsertList::showAddPicker(AddTarget target, juce::Rectangle<int> area) {
    if (!isEditable())
        return;
    std::vector<ModMatrixPicker::Item> items;
    std::vector<juce::String> types;
    auto add = [&](const juce::String& category, const juce::String& type) {
        types.push_back(type);
        ModMatrixPicker::Item item;
        item.id = (int)types.size();
        item.category = category;
        item.text = type;
        items.push_back(std::move(item));
    };
    for (const auto& type : recentPicks())
        add("Recent", type);
    for (const auto& category : moduleChoiceCategories())
        if (kEffectHeaders.contains(category.header))
            for (const auto& module : category.modules)
                add(category.header, module.name);

    juce::Component::SafePointer<MixerInsertList> safe(this);
    auto picker = std::make_unique<ModMatrixPicker>("effect", std::move(items), 0, [safe, types, target](int id) {
        if (safe != nullptr && id >= 1 && id <= (int)types.size())
            safe->addModuleAt(types[(size_t)id - 1], target);
    });
    picker->setAccessibleNames("Add an insert", "Search effects to add");
    if (auto& hook = pickerHookForTest()) {
        hook(std::move(picker));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(picker), localAreaToGlobal(area), nullptr);
}

// The cable is spliced and the card placed inside one undo record (insertModuleAfterNode). The new module joins the
// macro of its upstream neighbour, or of its downstream one when it lands right after the track source, so a boxed
// chain's box keeps covering every one of its modules.
void MixerInsertList::addModuleAt(const juce::String& moduleTypeName, AddTarget target) {
    if (graph_ == nullptr || undoManager_ == nullptr || macros_ == nullptr || graphEditor_ == nullptr || !isEditable())
        return;
    const auto predecessor = target.predecessor;
    const auto successor =
        isSet(target.successor) ? target.successor : synth::soleSignalNeighbour(*graph_, predecessor, false);
    if (!isSet(predecessor) || !isSet(successor) || !cablesInto(*graph_, predecessor, successor))
        return;
    const auto neighbourUuid = nodeUuid(*graph_, predecessor == sourceNodeId_ ? successor : predecessor);
    rememberPick(moduleTypeName);
    juce::Component::SafePointer<MixerInsertList> alive(this); // the edit can rebuild the mixer, as in mutateAndNotify
    const auto added = synth::insertModuleAfterNode(
        *graphEditor_, *undoManager_, *macros_, moduleTypeName, predecessor,
        [this, predecessor, successor, neighbourUuid](juce::AudioProcessorGraph::NodeID node) {
            if (!synth::spliceInInsert(*graph_, predecessor, successor, node))
                return false;
            const auto uuid = nodeUuid(*graph_, node);
            if (macros_->findByMember(uuid) == nullptr)
                if (const auto* neighbourMacro = macros_->findByMember(neighbourUuid))
                    macros_->addMember(neighbourMacro->id, uuid);
            return true;
        });
    if (isSet(added) && alive != nullptr && onMutated)
        onMutated();
}

} // namespace synth::ui
