// Concern: FRO11 (P9-5) -- MixerPanelComponent's rebuild-from-snapshot and click-to-select-macro
// routing (shared by every column kind's onColumnClicked/onEditOnCanvas/onMakeChannelRequested).
#include "MixerPanelComponent.h"

#include "AppUndoManager.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"

namespace synth::ui {

namespace {
constexpr int kColumnWidth = 140;
constexpr int kColumnGap = 4;
} // namespace

MixerPanelComponent::MixerPanelComponent() {
    // FRO18: the mixer's own keyboard-focus region ROOT (docs/shortcuts.md's "Mixer column
    // navigation") -- every child control gives up keyboard focus (see MixerColumnComponent's own
    // ctor comment), so this panel must claim it instead, or grabKeyboardFocus() has nothing to
    // land on.
    setWantsKeyboardFocus(true);
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(false, true);
}

MixerPanelComponent::~MixerPanelComponent() = default;

void MixerPanelComponent::configure(juce::AudioProcessorGraph& graph, synth::TimelineDoc& doc, synth::MacroSet& macros,
                                    AppUndoManager& undoManager, GraphEditor& graphEditor, AudioEngine& audioEngine) {
    graph_ = &graph;
    doc_ = &doc;
    macros_ = &macros;
    undoManager_ = &undoManager;
    graphEditor_ = &graphEditor;
    audioEngine_ = &audioEngine;
    directColumn_ = std::make_unique<MixerDirectColumn>();
    directColumn_->configure(graph);
    directColumn_->onMakeChannelRequested = [this](juce::AudioProcessorGraph::NodeID source) {
        if (onMakeChannelForNode)
            onMakeChannelForNode(source);
    };
    masterColumn_ = std::make_unique<MixerMasterColumn>();
    masterColumn_->configure(graph, undoManager);
}

void MixerPanelComponent::selectOnCanvas(const juce::String& targetId) {
    if (graphEditor_ == nullptr || macros_ == nullptr || targetId.isEmpty())
        return;
    // `targetId` is either a macro id directly (MixerInsertList's editOnCanvasTargetUuid, per
    // MixerModel.h's own contract) or a node uuid (a column's own strip uuid, from
    // onColumnClicked) -- try the macro id first, then "which macro (if any) boxes this node",
    // else select the node itself (§5.10: "clicking a column selects its macro", falling back to
    // the node when it isn't boxed).
    if (macros_->find(targetId) != nullptr) {
        graphEditor_->selectMacro(targetId, false);
        return;
    }
    if (const auto* macro = macros_->findByMember(targetId)) {
        graphEditor_->selectMacro(macro->id, false);
        return;
    }
    for (auto* node : graph_->getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == targetId) {
            graphEditor_->selectModule(node->nodeID, false);
            return;
        }
}

void MixerPanelComponent::rebuild() {
    if (graph_ == nullptr || doc_ == nullptr || macros_ == nullptr)
        return;

    // FRO18: capture the currently focused column's IDENTITY before the columns it points at are
    // destroyed below -- resolveFocusAfterRebuild() re-finds it afterwards by identity (uuid, or
    // kind alone for Direct), never by the raw index, which an unrelated strip insert/removal
    // elsewhere in the column order would otherwise silently reattach to the wrong column.
    const bool hadFocus = focusedColumnIndex_ >= 0 && focusedColumnIndex_ < (int)columnEntries_.size();
    const auto previousKind = hadFocus ? columnEntries_[(size_t)focusedColumnIndex_].kind : ColumnEntry::Kind::Strip;
    const auto previousUuid = hadFocus ? columnEntries_[(size_t)focusedColumnIndex_].uuid : juce::String();
    columnEntries_.clear();
    focusedColumnIndex_ = -1;

    const auto snapshot = synth::buildMixerSnapshot(*graph_, *doc_, *macros_);

    stripColumns_.clear();
    for (const auto& column : snapshot.columns) {
        if (column.kind != synth::MixerColumn::Kind::Strip)
            continue;
        auto widget = std::make_unique<MixerColumnComponent>();
        widget->configure(*graph_, *undoManager_, *macros_, *graphEditor_, *audioEngine_);

        juce::StringArray trackNames;
        for (auto trackId : column.feedingTracks)
            if (const auto* track = doc_->getTrack(trackId))
                trackNames.add(track->name);
        widget->setColumn(column, trackNames.joinIntoString(", "));

        widget->onColumnClicked = [this, uuid = column.uuid] { selectOnCanvas(uuid); };
        widget->onEditOnCanvas = [this](const juce::String& target) { selectOnCanvas(target); };
        widget->onMutated = [this] {
            if (onGraphMutated)
                onGraphMutated();
        };
        content_.addAndMakeVisible(*widget);

        ColumnEntry entry;
        entry.kind = ColumnEntry::Kind::Strip;
        entry.component = widget.get();
        entry.nodeId = column.nodeId;
        entry.uuid = column.uuid;
        entry.linkedToTrack = column.linkedToTrack;
        entry.feedingTracks = column.feedingTracks;
        columnEntries_.push_back(std::move(entry));

        stripColumns_.push_back(std::move(widget));
    }

    if (directColumn_ != nullptr) {
        directColumn_->setVisible(snapshot.hasDirect);
        if (snapshot.hasDirect) {
            directColumn_->refreshEnablement();
            content_.addAndMakeVisible(*directColumn_);

            ColumnEntry entry;
            entry.kind = ColumnEntry::Kind::Direct;
            entry.component = directColumn_.get();
            columnEntries_.push_back(std::move(entry));
        }
    }
    if (masterColumn_ != nullptr) {
        masterColumn_->setVisible(snapshot.hasMaster);
        if (snapshot.hasMaster) {
            for (const auto& column : snapshot.columns)
                if (column.kind == synth::MixerColumn::Kind::Master) {
                    masterColumn_->setNodeId(column.nodeId);

                    ColumnEntry entry;
                    entry.kind = ColumnEntry::Kind::Master;
                    entry.component = masterColumn_.get();
                    entry.nodeId = column.nodeId;
                    entry.uuid = column.uuid;
                    columnEntries_.push_back(std::move(entry));
                }
            content_.addAndMakeVisible(*masterColumn_);
        }
    }

    resolveFocusAfterRebuild(hadFocus, previousKind, previousUuid);
    syncFocusVisuals();

    resized();
}

void MixerPanelComponent::unbindAllColumns() {
    for (auto& column : stripColumns_)
        if (column != nullptr)
            column->unbindFromGraph();
    if (masterColumn_ != nullptr)
        masterColumn_->unbindFromGraph();
    // directColumn_ binds no parameters (no fader/pan/M-S -- MixerDirectColumn.h's own comment),
    // just a graph_ pointer to the (never freed here) AudioProcessorGraph object itself, so it has
    // nothing to unbind.
}

bool MixerPanelComponent::revealColumn(juce::AudioProcessorGraph::NodeID stripId) {
    MixerColumnComponent* target = nullptr;
    for (auto& column : stripColumns_) {
        const bool match = column->getNodeId() == stripId;
        column->setSelected(match);
        if (match)
            target = column.get();
    }
    if (target == nullptr)
        return false;
    viewport_.setViewPosition(target->getX(), 0);
    return true;
}

void MixerPanelComponent::refreshMeters() {
    for (auto& column : stripColumns_)
        column->refreshMeter();
    if (masterColumn_ != nullptr && masterColumn_->isVisible())
        masterColumn_->refreshMeter();
}

void MixerPanelComponent::resized() {
    viewport_.setBounds(getLocalBounds());

    int totalColumns = (int)stripColumns_.size();
    if (directColumn_ != nullptr && directColumn_->isVisible())
        ++totalColumns;
    if (masterColumn_ != nullptr && masterColumn_->isVisible())
        ++totalColumns;

    const int contentWidth = juce::jmax(getWidth(), totalColumns * (kColumnWidth + kColumnGap));
    content_.setSize(contentWidth, getHeight());

    int x = 0;
    for (auto& column : stripColumns_) {
        column->setBounds(x, 0, kColumnWidth, getHeight());
        x += kColumnWidth + kColumnGap;
    }
    if (directColumn_ != nullptr && directColumn_->isVisible()) {
        directColumn_->setBounds(x, 0, kColumnWidth, getHeight());
        x += kColumnWidth + kColumnGap;
    }
    if (masterColumn_ != nullptr && masterColumn_->isVisible())
        masterColumn_->setBounds(x, 0, kColumnWidth, getHeight());
}

} // namespace synth::ui
