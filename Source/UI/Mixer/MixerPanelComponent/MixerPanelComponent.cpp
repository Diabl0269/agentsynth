// Concern: FRO11 (P9-5) -- MixerPanelComponent's rebuild-from-snapshot and click-to-select-macro
// routing (shared by every column kind's onColumnClicked/onEditOnCanvas/onMakeChannelRequested).
#include "MixerPanelComponent.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"

namespace synth::ui {

namespace {
constexpr int kColumnWidth = 140;
constexpr int kColumnGap = 4;
// Horizontal step between the four cards a new bus channel places on the canvas.
constexpr int kBusCardGap = 220;
} // namespace

MixerPanelComponent::MixerPanelComponent() {
    // FRO18: the mixer's own keyboard-focus region ROOT (docs/control/shortcuts.md's "Mixer column
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
    masterColumn_->configure(graph, undoManager, macros, graphEditor);
    // FRO148: post-insert level for the Master meter once the chain has inserts (docs/mixer/meters.md).
    masterColumn_->outputPeakProvider = [this](int leg) -> float {
        return audioEngine_ != nullptr ? audioEngine_->takeOutputMeterPeak(synth::MeterReader::Mixer, leg) : 0.0f;
    };
    masterColumn_->onEditOnCanvas = [this](const juce::String& target) { selectOnCanvas(target); };
    masterColumn_->onMutated = [this] {
        if (onGraphMutated)
            onGraphMutated();
    };
    masterColumn_->onResetAllMetersRequested = [this] { resetAllMeterReadouts(); };
}

void MixerPanelComponent::selectOnCanvas(const juce::String& targetId) {
    if (graphEditor_ == nullptr || macros_ == nullptr || targetId.isEmpty())
        return;
    // `targetId` is either a macro id directly (MixerInsertList's editOnCanvasTargetUuid, per
    // MixerModel.h's own contract) or a node uuid (a column's own strip uuid, from
    // onColumnClicked) -- try the macro id first, then "which macro (if any) boxes this node",
    // else select the node itself (docs/mixer/panel.md#what-the-mixer-shows: "clicking a column selects its macro",
    // falling back to the node when it isn't boxed).
    if (macros_->find(targetId) != nullptr) {
        graphEditor_->getMacroController().selectMacro(targetId, false);
        return;
    }
    if (const auto* macro = macros_->findByMember(targetId)) {
        graphEditor_->getMacroController().selectMacro(macro->id, false);
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

    // Every column below is built and bound fresh, so whatever unbindAllColumns() detached is
    // live again once this returns (FRO103).
    columnsUnbound_ = false;

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
        // Strips AND buses: a bus is an ordinary strip column with a BUS badge and a feeding-strips
        // source line (docs/mixer/sends-and-buses.md D6), not a column kind of its own with its own widget.
        if (column.kind != synth::MixerColumn::Kind::Strip && column.kind != synth::MixerColumn::Kind::Bus)
            continue;
        auto widget = std::make_unique<MixerColumnComponent>();
        widget->configure(*graph_, *undoManager_, *macros_, *graphEditor_, *audioEngine_);

        juce::StringArray sourceNames;
        if (column.kind == synth::MixerColumn::Kind::Bus) {
            for (const auto& name : column.busSources)
                sourceNames.add(name);
        } else {
            for (auto trackId : column.feedingTracks)
                if (const auto* track = doc_->getTrack(trackId))
                    sourceNames.add(track->name);
        }
        widget->setColumn(column, sourceNames.joinIntoString(", "));
        widget->setCreateBusProvider([this] { return createBus(); });

        widget->onColumnClicked = [this, uuid = column.uuid] { selectOnCanvas(uuid); };
        widget->onEditOnCanvas = [this](const juce::String& target) { selectOnCanvas(target); };
        widget->onMutated = [this] {
            if (onGraphMutated)
                onGraphMutated();
        };
        widget->onResetAllMetersRequested = [this] { resetAllMeterReadouts(); };
        // FRO253: forwards this panel's single set of Solo-learn callbacks down to the column,
        // filling in the nodeId each column already knows about itself -- see this class's own
        // onSoloMidiLearnRequested/onSoloMidiForgetRequested/onQuerySoloMidiMapping doc comments.
        widget->onSoloMidiLearnRequested = [this, nodeId = column.nodeId] {
            if (onSoloMidiLearnRequested)
                onSoloMidiLearnRequested(nodeId);
        };
        widget->onSoloMidiForgetRequested = [this, nodeId = column.nodeId] {
            if (onSoloMidiForgetRequested)
                onSoloMidiForgetRequested(nodeId);
        };
        widget->onQuerySoloMidiMapping = [this, nodeId = column.nodeId]() -> juce::String {
            return onQuerySoloMidiMapping ? onQuerySoloMidiMapping(nodeId) : juce::String();
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
                    masterColumn_->setColumn(column);

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

juce::AudioProcessorGraph::NodeID MixerPanelComponent::createBus() {
    if (graph_ == nullptr || macros_ == nullptr || undoManager_ == nullptr || graphEditor_ == nullptr)
        return {};

    // Core cannot size canvas cards (ChannelFlows.h's DefaultChannelLayout contract), so the
    // positions are worked out here: one card row to the right of everything already placed.
    int rightmost = 0;
    for (auto* node : graph_->getNodes())
        if (node != nullptr)
            rightmost = juce::jmax(rightmost, (int)node->properties["x"]);
    const int x = rightmost + kBusCardGap;
    const synth::DefaultChannelLayout layout{
        {x, 0}, {x + kBusCardGap, 0}, {x + 2 * kBusCardGap, 0}, {x + 3 * kBusCardGap, 0}};

    juce::AudioProcessorGraph::NodeID created;
    undoManager_->recordGraphAndMacroChange(*graph_, *macros_, [&] {
        const auto channel = synth::buildBusChannel(*graph_, layout);
        if (channel.strip == nullptr)
            return;
        created = channel.strip->nodeID;

        synth::Macro macro;
        macro.name = synth::busFallbackName(*graph_, created);
        macro.members = {channel.eqUuid, channel.compressorUuid, channel.stripUuid};
        macros_->add(macro);
        graphEditor_->updateComponents();
    });

    if (created != juce::AudioProcessorGraph::NodeID{} && onGraphMutated)
        onGraphMutated();
    return created;
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
    columnsUnbound_ = true;
}

void MixerPanelComponent::rebuildIfUnbound() {
    if (columnsUnbound_)
        rebuild();
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

void MixerPanelComponent::setMidiLearnArmed(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) {
    midiLearnArmedNodeId_ = nodeId;
    for (auto& column : stripColumns_)
        if (column != nullptr)
            column->setMidiLearnArmedParam(column->getNodeId() == nodeId ? paramId : juce::String());
    if (masterColumn_ != nullptr)
        masterColumn_->setMidiLearnArmedParam(masterColumn_->getNodeId() == nodeId ? paramId : juce::String());
}

void MixerPanelComponent::clearMidiLearnArmed() {
    for (auto& column : stripColumns_)
        if (column != nullptr && column->getNodeId() == midiLearnArmedNodeId_)
            column->setMidiLearnArmedParam({});
    if (masterColumn_ != nullptr && masterColumn_->getNodeId() == midiLearnArmedNodeId_)
        masterColumn_->setMidiLearnArmedParam({});
    midiLearnArmedNodeId_ = {};
}

// FRO253: mirrors setMidiLearnArmed()/clearMidiLearnArmed() above -- kept as a separate
// nodeId-keyed pair rather than overloading paramId with a sentinel, since Solo has no
// juce::RangedAudioParameter identity to key on. Master has no Solo button (MixerMasterColumn.h),
// so only strip columns are ever touched.
void MixerPanelComponent::setMidiLearnArmedSolo(juce::AudioProcessorGraph::NodeID nodeId) {
    midiLearnArmedSoloNodeId_ = nodeId;
    for (auto& column : stripColumns_)
        if (column != nullptr)
            column->setMidiLearnArmedSolo(column->getNodeId() == nodeId);
}

void MixerPanelComponent::clearMidiLearnArmedSolo() {
    for (auto& column : stripColumns_)
        if (column != nullptr && column->getNodeId() == midiLearnArmedSoloNodeId_)
            column->setMidiLearnArmedSolo(false);
    midiLearnArmedSoloNodeId_ = {};
}

// FRO135: the pick-target overlay's view of the mixer -- every strip column, plus Master's fader.
// The Direct column has nothing learnable. Columns that are not showing are skipped by the overlay.
void MixerPanelComponent::collectPickCandidates(std::vector<PickCandidate>& out) const {
    for (const auto& column : stripColumns_)
        if (column != nullptr)
            column->collectPickCandidates(out);
    if (masterColumn_ != nullptr)
        masterColumn_->collectPickCandidates(out);
}

// FRO253: called after a nodeCommand press changes solo OUTSIDE any column's own click (via
// MainComponentRemoteActionInvoker::invokeNodeCommand) -- MixerColumnComponent::toggleSoloed()'s
// own refresh only runs on ITS OWN click, so nothing else re-syncs a column's M/S visuals after a
// hardware toggle. Cheap enough to refresh every column unconditionally (a handful of strips,
// never per-frame) rather than resolving which one node id maps to.
void MixerPanelComponent::refreshMuteSoloVisuals() {
    for (auto& column : stripColumns_)
        if (column != nullptr)
            column->refreshMuteSoloVisual();
}

namespace {
// A tick after a gap this large (the mixer tab was hidden, or this is the very first tick) is
// clamped to this instead -- otherwise the ballistics would see, say, a 30-second "elapsed time"
// and every bar/peak-hold would decay straight to silence in one jump the instant the tab is shown
// again, rather than resuming wherever they last stood.
constexpr double kMaxPlausibleMeterGapSeconds = 0.5;
} // namespace

void MixerPanelComponent::refreshMeters() {
    ++refreshMetersCallCount_;
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    double elapsedSeconds = lastMeterRefreshMs_ > 0.0 ? (nowMs - lastMeterRefreshMs_) / 1000.0 : 0.0;
    elapsedSeconds = juce::jlimit(0.0, kMaxPlausibleMeterGapSeconds, elapsedSeconds);
    lastMeterRefreshMs_ = nowMs;

    for (auto& column : stripColumns_)
        column->refreshMeter((float)elapsedSeconds);
    if (masterColumn_ != nullptr && masterColumn_->isVisible())
        masterColumn_->refreshMeter((float)elapsedSeconds);
}

void MixerPanelComponent::resetAllMeterReadouts() {
    for (auto& column : stripColumns_)
        column->resetMeterReadout();
    if (masterColumn_ != nullptr)
        masterColumn_->resetMeterReadout();
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
