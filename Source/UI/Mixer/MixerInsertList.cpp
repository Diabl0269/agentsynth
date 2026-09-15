// Concern: FRO11 (P9-5) -- MixerInsertList's paint, right-click menus and the three mutations
// (add/reorder/remove), each ONE recordGraphAndMacroChange around MixerModel's Core splice
// primitives.
#include "MixerInsertList.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {
namespace {

// A small, curated set of insert-eligible effects -- a deliberate scope trim from the plan's
// "reuse the module library's full category/search picker" (see the PR description's
// deviations): this ticket's own budget did not fit a second copy of that picker's UI.
const juce::StringArray kAddableModuleTypes{"Parametric EQ", "Compressor", "Distortion", "Chorus", "Phaser", "Flanger"};

} // namespace

MixerInsertList::MixerInsertList() { setInterceptsMouseClicks(true, false); }

void MixerInsertList::configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                                GraphEditor& graphEditor) {
    graph_ = &graph;
    undoManager_ = &undoManager;
    macros_ = &macros;
    graphEditor_ = &graphEditor;
}

void MixerInsertList::setEntries(const std::vector<synth::MixerInsertEntry>& entries, bool linear,
                                 const juce::String& editOnCanvasTargetUuid,
                                 juce::AudioProcessorGraph::NodeID sourceNodeId,
                                 juce::AudioProcessorGraph::NodeID stripNodeId) {
    entries_ = entries;
    linear_ = linear;
    editOnCanvasTargetUuid_ = editOnCanvasTargetUuid;
    sourceNodeId_ = sourceNodeId;
    stripNodeId_ = stripNodeId;
    resized();
    repaint();
}

int MixerInsertList::getPreferredHeight() const noexcept {
    const int rows = juce::jmax(1, (int)entries_.size());
    return rows * kRowHeight + (linear_ ? 0 : kRowHeight); // + the "Edit on canvas" link row
}

int MixerInsertList::rowIndexAt(juce::Point<int> position) const {
    if (position.y < 0)
        return -1;
    const int index = position.y / kRowHeight;
    return index >= 0 && index < (int)entries_.size() ? index : -1;
}

void MixerInsertList::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto text = laf != nullptr ? laf->getTheme().colors.textPrimary : juce::Colour(0xffEAEEF3);
    const auto muted = laf != nullptr ? laf->getTheme().colors.textMuted : juce::Colour(0xff8A93A0);
    const auto disabled = laf != nullptr ? laf->getTheme().colors.textDisabled : juce::Colour(0xff5C6470);
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    if (entries_.empty()) {
        g.setColour(muted);
        g.drawText("(no inserts)", getLocalBounds().removeFromTop(kRowHeight), juce::Justification::centredLeft);
    }
    for (int i = 0; i < (int)entries_.size(); ++i) {
        const auto& entry = entries_[(size_t)i];
        auto row = getLocalBounds().withY(i * kRowHeight).withHeight(kRowHeight);
        g.setColour(entry.bypassed ? disabled : text);
        g.drawText(entry.name, row.reduced(2, 0), juce::Justification::centredLeft, true);
    }
    if (!linear_) {
        auto linkRow = getLocalBounds().withY((int)entries_.size() * kRowHeight).withHeight(kRowHeight);
        g.setColour(accent);
        g.drawText("Edit on canvas", linkRow.reduced(2, 0), juce::Justification::centredLeft);
    }
}

void MixerInsertList::resized() {}

void MixerInsertList::mouseDown(const juce::MouseEvent& event) {
    if (!linear_) {
        const auto linkRowTop = (int)entries_.size() * kRowHeight;
        if (event.y >= linkRowTop && onEditOnCanvas)
            onEditOnCanvas(editOnCanvasTargetUuid_);
        return;
    }
    const int row = rowIndexAt(event.getPosition());
    if (event.mods.isPopupMenu()) {
        if (row >= 0)
            showRowMenu(row);
        else
            showAddMenu();
    }
}

void MixerInsertList::showRowMenu(int rowIndex) {
    juce::PopupMenu menu;
    menu.addItem(1, "Move Up", rowIndex > 0);
    menu.addItem(2, "Move Down", rowIndex < (int)entries_.size() - 1);
    menu.addItem(3, "Remove");
    menu.addSeparator();
    menu.addItem(4, "Add...");
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, rowIndex](int result) {
        if (result == 1)
            moveRow(rowIndex, -1);
        else if (result == 2)
            moveRow(rowIndex, 1);
        else if (result == 3)
            removeRow(rowIndex);
        else if (result == 4)
            showAddMenu();
    });
}

void MixerInsertList::showAddMenu() {
    juce::PopupMenu menu;
    int id = 1;
    for (const auto& name : kAddableModuleTypes)
        menu.addItem(id++, name);
    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result >= 1 && result <= kAddableModuleTypes.size())
            addModule(kAddableModuleTypes[result - 1]);
    });
}

void MixerInsertList::mutateAndNotify(const std::function<bool()>& mutation) {
    if (graph_ == nullptr || undoManager_ == nullptr || macros_ == nullptr || graphEditor_ == nullptr)
        return;
    bool changed = false;
    undoManager_->recordGraphAndMacroChange(*graph_, *macros_, [&] {
        changed = mutation();
        graphEditor_->updateComponents();
    });
    if (changed && onMutated)
        onMutated();
}

void MixerInsertList::moveRow(int rowIndex, int delta) {
    const int targetIndex = rowIndex + delta;
    if (rowIndex < 0 || rowIndex >= (int)entries_.size() || targetIndex < 0 || targetIndex >= (int)entries_.size())
        return;
    const auto nodeId = entries_[(size_t)rowIndex].nodeId;

    // The new neighbours: remove `rowIndex` from the list, then read off whichever entries land
    // either side of `targetIndex` in what remains -- the track source / the strip itself stand in
    // for a missing neighbour at either end of the chain.
    std::vector<synth::MixerInsertEntry> without = entries_;
    without.erase(without.begin() + rowIndex);
    const int insertAt = juce::jlimit(0, (int)without.size(), targetIndex);
    const auto predecessorId = insertAt == 0 ? sourceNodeId_ : without[(size_t)insertAt - 1].nodeId;
    const auto successorId = insertAt >= (int)without.size() ? stripNodeId_ : without[(size_t)insertAt].nodeId;

    mutateAndNotify([&] { return synth::reorderInsert(*graph_, nodeId, predecessorId, successorId); });
}

void MixerInsertList::removeRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const auto& entry = entries_[(size_t)rowIndex];
    const auto nodeId = entry.nodeId;
    const auto uuid = entry.uuid;
    mutateAndNotify([&] {
        if (!synth::spliceOutInsert(*graph_, nodeId))
            return false;
        // FRO16 UAF fix: unbind any UI object holding a raw pointer into `nodeId` (the column's
        // own MixerEqThumbnail, if this is its bound EQ) BEFORE removeNode() frees the processor
        // it points at -- see onBeforeNodeRemoved's own comment.
        if (onBeforeNodeRemoved)
            onBeforeNodeRemoved(nodeId);
        graph_->removeNode(nodeId);
        macros_->removeMemberEverywhere(uuid);
        return true;
    });
}

void MixerInsertList::addModule(const juce::String& moduleTypeName) {
    if (entries_.empty() && sourceNodeId_ == juce::AudioProcessorGraph::NodeID{})
        return;
    const auto predecessorId = entries_.empty() ? sourceNodeId_ : entries_.back().nodeId;
    const auto successorId = stripNodeId_;

    mutateAndNotify([&] {
        auto processor = synth::AIStateMapper::createModule(moduleTypeName);
        if (processor == nullptr)
            return false;
        auto node = graph_->addNode(std::move(processor));
        if (node == nullptr)
            return false;
        const juce::String uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);

        if (!synth::spliceInInsert(*graph_, predecessorId, successorId, node->nodeID)) {
            graph_->removeNode(node->nodeID);
            return false;
        }

        // Join the same macro as an already-boxed neighbour, so a linear chain's box keeps
        // covering every one of its own modules (plan (e)'s "join the existing macro" rule).
        if (const auto* neighbourMacro =
                macros_->findByMember(entries_.empty() ? juce::String() : entries_.back().uuid))
            macros_->addMember(neighbourMacro->id, uuid);
        return true;
    });
}

} // namespace synth::ui
