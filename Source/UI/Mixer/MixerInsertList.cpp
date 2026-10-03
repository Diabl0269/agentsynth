// Concern: MixerInsertList's paint, right-click menus and the three mutations
// (add/reorder/remove), each ONE recordGraphAndMacroChange around MixerModel's Core splice
// primitives.
#include "MixerInsertList.h"
#include "UI/Layout/ContextMenuPlacement.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Mixer/MixerSections/MixerSectionViewport.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {
namespace {

// The insert-eligible effects the add menu offers -- a curated stereo-in/stereo-out set rather than the module
// library's full category/search picker (a second copy of that picker's UI is not worth it here). "Limiter" and "Gate"
// join the set (the Master column's post-fader ceiling, and a strip's gate); the names are the factory's display names
// (AIStateMapper::createModule).
const juce::StringArray kAddableModuleTypes{"Parametric EQ", "Compressor", "Limiter", "Gate",
                                            "Distortion",    "Chorus",     "Phaser",  "Flanger"};

} // namespace

const juce::StringArray& MixerInsertList::getAddableModuleTypes() noexcept { return kAddableModuleTypes; }

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
    rebuildRowAccessibilityProxies();
    rebuildBypassButtons();
    resized();
    repaint();
}

// Rebuilt wholesale with the proxies: the row count and every row's identity can both change on one edit, and a
// button's title and tooltip carry the row's name and bypass state. The tooltip is computed when shown, so a rebound
// key shows up without a rebuild.
void MixerInsertList::rebuildBypassButtons() {
    bypassButtons_.clear();
    for (int i = 0; i < (int)entries_.size(); ++i) {
        const auto& entry = entries_[(size_t)i];
        auto button = std::make_unique<MixerIconButton>("mixerInsertBypass");
        button->setIcon(synth::theme::Icon::ModuleBypass);
        button->setToggleState(entry.bypassed, juce::dontSendNotification);
        button->setTitle(entry.name + " bypass, " + (entry.bypassed ? "on" : "off"));
        button->tooltipProvider = [this, i] { return bypassTooltip(i); };
        button->onClick = [this, i] { toggleBypassForRow(i); };
        addAndMakeVisible(*button);
        bypassButtons_.push_back(std::move(button));
    }
}

juce::String MixerInsertList::bypassTooltip(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return {};
    const auto& entry = entries_[(size_t)rowIndex];
    const auto base = entry.bypassed ? "Turn " + entry.name + " back on" : "Bypass " + entry.name;
    return formatShortcutHint(base, bypassShortcutText ? bypassShortcutText() : juce::String());
}

// The module's own bypass parameter is part of the graph state, so one captured before/after pair is a complete
// undo step. The change reaches the canvas card through the parameter itself; onMutated refreshes every mixer view.
// The rebuild it triggers destroys this list, so nothing may touch it afterwards.
void MixerInsertList::toggleBypassForRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size() || graph_ == nullptr || undoManager_ == nullptr)
        return;
    auto* node = graph_->getNodeForId(entries_[(size_t)rowIndex].nodeId);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr)
        return;
    undoManager_->captureBeforeState(*graph_);
    module->setBypassed(!module->isBypassed());
    undoManager_->pushSnapshotFromCapture(*graph_);
    // The row reads right even where nothing rebuilds the list afterwards (a bare list in a test).
    auto& entry = entries_[(size_t)rowIndex];
    entry.bypassed = module->isBypassed();
    bypassButtons_[(size_t)rowIndex]->setToggleState(entry.bypassed, juce::dontSendNotification);
    bypassButtons_[(size_t)rowIndex]->setTitle(entry.name + " bypass, " + (entry.bypassed ? "on" : "off"));
    rowProxies_[(size_t)rowIndex]->setTitle(entry.bypassed ? entry.name + ", bypassed" : entry.name);
    repaint();
    if (onMutated)
        onMutated();
}

void MixerInsertList::rebuildRowAccessibilityProxies() {
    // Rebuilt wholesale on every setEntries() (same idiom as MixerSendList::rebuildKnobs()) rather
    // than diffed in place -- the row count and every row's identity can both change on a single
    // insert/remove/reorder, and this list is never large enough for the rebuild itself to matter.
    rowProxies_.clear();
    for (const auto& entry : entries_) {
        auto proxy = std::make_unique<RowAccessibilityProxy>();
        // An overlay purely for accessibility -- mouseDown() above still owns every real
        // click via rowIndexAt()/hit-testing on `this`, unchanged.
        proxy->setInterceptsMouseClicks(false, false);
        proxy->setTitle(entry.bypassed ? entry.name + ", bypassed" : entry.name);
        addAndMakeVisible(*proxy);
        rowProxies_.push_back(std::move(proxy));
    }
}

juce::Rectangle<int> MixerInsertList::getRowBounds(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return {};
    return getLocalBounds().withY(rowIndex * kRowHeight).withHeight(kRowHeight);
}

void MixerInsertList::setFocusedRow(int rowIndex) {
    focusedRow_ = rowIndex >= 0 && rowIndex < (int)entries_.size() ? rowIndex : -1;
    if (focusedRow_ >= 0)
        if (auto* viewport = findParentComponentOfClass<MixerSectionViewport>()) {
            const auto row = getRowBounds(focusedRow_);
            viewport->revealRange(row.getY(), row.getBottom());
        }
}

juce::String MixerInsertList::describeRow(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return {};
    const auto& entry = entries_[(size_t)rowIndex];
    return "Insert " + juce::String(rowIndex + 1) + ", " + entry.name + (entry.bypassed ? ", bypassed" : "");
}

juce::String MixerInsertList::getRowUuid(int rowIndex) const {
    return rowIndex >= 0 && rowIndex < (int)entries_.size() ? entries_[(size_t)rowIndex].uuid : juce::String();
}

juce::String MixerInsertList::getTooltip() {
    const int row = rowIndexAt(getMouseXYRelative());
    if (row >= 0)
        return entries_[(size_t)row].name + (linear_ ? ": right-click to move, remove or add an insert"
                                                     : ": a branching chain, edit it on the canvas");
    if (!linear_)
        return "Select this channel's insert chain on the canvas";
    return "Insert chain: right-click to add an insert";
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
    const auto text = synth::theme::themeOf(*this).colors.textPrimary;
    const auto muted = synth::theme::themeOf(*this).colors.textMuted;
    const auto disabled = synth::theme::themeOf(*this).colors.textDisabled;
    const auto accent = synth::theme::themeOf(*this).colors.accent;

    // The empty-state placeholder always occupies row 0 (jmax(1, size) below matches
    // getPreferredHeight()'s own row count), so anchor the "Edit on canvas" link AFTER it rather
    // than at entries_.size() * kRowHeight -- with zero entries that was also 0, drawing both texts
    // in the same row on top of each other (a bus with no inserts and a branching chain, before the
    // insert-discovery fix above, hit exactly this).
    const int contentRows = juce::jmax(1, (int)entries_.size());

    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    if (entries_.empty()) {
        g.setColour(muted);
        g.drawText("(no inserts)", getLocalBounds().removeFromTop(kRowHeight), juce::Justification::centredLeft, true);
    }
    for (int i = 0; i < (int)entries_.size(); ++i) {
        const auto& entry = entries_[(size_t)i];
        auto row = getLocalBounds().withY(i * kRowHeight).withHeight(kRowHeight);
        g.setColour(entry.bypassed ? disabled : text);
        g.drawText(entry.name, row.withTrimmedRight(kBypassWidth).reduced(2, 0), juce::Justification::centredLeft,
                   true);
    }
    if (!linear_) {
        auto linkRow = getLocalBounds().withY(contentRows * kRowHeight).withHeight(kRowHeight);
        g.setColour(accent);
        g.drawText("Edit on canvas", linkRow.reduced(2, 0), juce::Justification::centredLeft, true);
    }
}

void MixerInsertList::resized() {
    for (int i = 0; i < (int)rowProxies_.size(); ++i)
        rowProxies_[(size_t)i]->setBounds(getLocalBounds().withY(i * kRowHeight).withHeight(kRowHeight));
    for (int i = 0; i < (int)bypassButtons_.size(); ++i)
        bypassButtons_[(size_t)i]->setBounds(
            getLocalBounds().withY(i * kRowHeight).withHeight(kRowHeight).removeFromRight(kBypassWidth).reduced(1));
}

void MixerInsertList::mouseDown(const juce::MouseEvent& event) {
    if (!linear_) {
        // Same anchor as paint()'s linkRow -- see that method's own comment on why this is
        // jmax(1, size) and not the raw entry count.
        const auto linkRowTop = juce::jmax(1, (int)entries_.size()) * kRowHeight;
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
    menu.showMenuAsync(synth::ui::contextMenuOptionsAtPointer(), [this, rowIndex](int result) {
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
    menu.showMenuAsync(synth::ui::contextMenuOptionsAtPointer(), [this](int result) {
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

void MixerInsertList::removeRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const auto& entry = entries_[(size_t)rowIndex];
    const auto nodeId = entry.nodeId;
    const auto uuid = entry.uuid;
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

void MixerInsertList::addModule(const juce::String& moduleTypeName) {
    if (entries_.empty() && sourceNodeId_ == juce::AudioProcessorGraph::NodeID{})
        return;
    // Nothing to splice in front of -- e.g. a Master column whose output never reaches a Rec Tap / Audio
    // Output.
    if (stripNodeId_ == juce::AudioProcessorGraph::NodeID{})
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
