// Concern: MixerPanelComponent's rebuild-from-snapshot and click-to-select-macro
// routing (shared by every column kind's onColumnClicked/onEditOnCanvas/onMakeChannelRequested).
#include "MixerPanelComponent.h"
#include <utility>

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/RemoteModel.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelMacroLookup.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "MixerPanelInternal.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// Horizontal step between the four cards a new bus channel places on the canvas.
constexpr int kBusCardGap = 220;

const char* const kEmptyHintText =
    "No channels yet. Add a track from the Timeline's + Track button, or a bus with + Bus above.";
} // namespace

MixerPanelComponent::MixerPanelComponent() {
    // The mixer's own keyboard-focus region ROOT (docs/control/shortcuts.md's "Mixer column
    // navigation") -- every child control gives up keyboard focus (see MixerColumnComponent's own
    // ctor comment), so this panel must claim it instead, or grabKeyboardFocus() has nothing to
    // land on.
    setWantsKeyboardFocus(true);
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&content_, false);
    content_.onPaint = [this](juce::Graphics& g) { paintColumnDragChrome(g); };
    viewport_.setScrollBarsShown(false, true);
    viewport_.setWantsKeyboardFocus(false);
    viewport_.onScrolled = [this] { syncVerticalScroll(viewport_); };
    for (auto* zone : {&leftZone_, &rightZone_}) {
        addChildComponent(zone->viewport);
        addChildComponent(zone->divider);
        zone->viewport.setViewedComponent(&zone->content, false);
        zone->viewport.setWantsKeyboardFocus(false);
        zone->viewport.onScrolled = [this, zone] { syncVerticalScroll(zone->viewport); };
        zone->divider.setInterceptsMouseClicks(false, false);
    }
    // Always shown, even with no columns: "+ Bus" is how an empty mixer gets its first bus.
    addAndMakeVisible(toolbar_);
    addChildComponent(sidePane_);
    toolbar_.bindSidePane(sidePane_);
    sidePane_.setContent(&zonesPane_);
    sidePane_.onOccupiedWidthChanged = [this] { resized(); };
    zonesPane_.onSetZone = [this](const juce::String& id, synth::MixerZone zone) { pinChannel(id, zone); };
    zonesPane_.onMoveWithinZone = [this](const juce::String& id, const juce::String& target) {
        moveChannelWithinZone(id, target);
    };
    zonesPane_.onSetHidden = [this](const juce::String& id, bool hidden) { setChannelHidden(id, hidden); };
    zonesPane_.onSoloShow = [this](const juce::String& id) { soloShowChannel(id); };
    zonesPane_.onShowAll = [this] { showAllChannels(); };
    zonesPane_.onFocusReleased = [this] { grabKeyboardFocus(); };
    toolbar_.shortcutTextFor = [this](MixerSection section) { return sectionToggleShortcutText(section); };
    toolbar_.setLayout(sectionLayout_);
    toolbar_.onAddBus = [this] {
        createBus();
        rebuild(); // the new bus's own column, without waiting for the owner's reconcile
    };
    toolbar_.onResetMeters = [this] { resetAllMeterReadouts(); };
    onOpenEqWindow = [this](juce::AudioProcessorGraph::NodeID nodeId) { openEqWindowOnCanvas(nodeId); };
    wireSectionLayout();

    // A direct child of THIS panel, not content_/viewport_ (which scroll and would clip or
    // slide it). This panel is the single focusable leaf, so the hint is purely decorative: no
    // keyboard focus, no mouse hit-testing.
    addChildComponent(emptyHint_);
    emptyHint_.setWantsKeyboardFocus(false);
    emptyHint_.setInterceptsMouseClicks(false, false);
    emptyHint_.setJustificationType(juce::Justification::centred);
    emptyHint_.setText(kEmptyHintText, juce::dontSendNotification);
    emptyHint_.setTitle(kEmptyHintText); // accessibility: name equals the visible text
    setTitle("Mixer");
}

MixerPanelComponent::~MixerPanelComponent() = default;

// `meterReader` defaults to the pre-existing `MeterReader::Mixer` slot -- the Mixer's
// optional second live view (MixerMirrorController) passes `MeterReader::MixerMirror` instead, so
// its columns' meter polls never race the docked view's for the same consume-on-read latch slot
// (Source/Mixer/PeakMeterLatch.h). Threaded straight through to every strip/Master column configure()
// call below, and into masterColumn_'s post-insert outputPeakProvider.
void MixerPanelComponent::configure(juce::AudioProcessorGraph& graph, synth::TimelineDoc& doc, synth::MacroSet& macros,
                                    AppUndoManager& undoManager, GraphEditor& graphEditor, AudioEngine& audioEngine,
                                    synth::MeterReader meterReader) {
    graph_ = &graph;
    doc_ = &doc;
    macros_ = &macros;
    undoManager_ = &undoManager;
    graphEditor_ = &graphEditor;
    audioEngine_ = &audioEngine;
    meterReader_ = meterReader;
    directColumn_ = std::make_unique<MixerDirectColumn>();
    directColumn_->configure(graph);
    directColumn_->onMakeChannelRequested = [this](juce::AudioProcessorGraph::NodeID source) {
        if (onMakeChannelForNode)
            onMakeChannelForNode(source);
    };
    masterColumn_ = std::make_unique<MixerMasterColumn>();
    masterColumn_->setSectionLayout(sectionLayout_);
    masterColumn_->configure(graph, undoManager, macros, graphEditor, audioEngine, meterReader_);
    // Post-insert level for the Master meter once the chain has inserts (docs/mixer/meters.md).
    masterColumn_->outputPeakProvider = [this](int leg) -> float {
        return audioEngine_ != nullptr ? audioEngine_->takeOutputMeterPeak(meterReader_, leg) : 0.0f;
    };
    masterColumn_->onEditOnCanvas = [this](const juce::String& target) { selectOnCanvas(target); };
    masterColumn_->onMutated = [this] {
        if (onGraphMutated)
            onGraphMutated();
    };
    // onLiveMixerStateChanged is deliberately NOT in copyWiringFrom()'s blanket copy below --
    // each instance's own copy must reach its SIBLING's refreshLiveMixerVisuals() (BottomDockComponent/
    // MixerMirrorController do that cross-wire), never its own, or an instance would refresh itself
    // instead of the other live view.
    masterColumn_->onLiveStateChanged = [this] {
        if (onLiveMixerStateChanged)
            onLiveMixerStateChanged();
    };
    masterColumn_->onResetAllMetersRequested = [this] { resetAllMeterReadouts(); };
    masterColumn_->getInsertList().bypassShortcutText = [this] { return bypassShortcutText(); };
    wireHeaderMenus();
}

// Everything MainComponent wires directly onto a panel instance (never touched by
// configure() above, which only wires the panel's OWN internal callbacks) -- the one-shot setup a
// freshly created MixerMirrorController view needs so a right-click MIDI-learn menu, solo-learn
// arming, MIDI Remote doc lookups and the M/S/R keyboard shortcuts all resolve the same way the
// docked panel's already do. Deliberately does NOT copy per-view state (columnEntries_,
// focusedColumnIndex_, scroll position) -- those stay independent by design (each view is its own
// live view of the same model, not a clone of the other's on-screen state).
void MixerPanelComponent::copyWiringFrom(const MixerPanelComponent& other) {
    onMakeChannelForNode = other.onMakeChannelForNode;
    onGraphMutated = other.onGraphMutated;
    onArmTrack = other.onArmTrack;
    buildTrackColourPicker = other.buildTrackColourPicker;
    onMoveTrack = other.onMoveTrack;
    onPublishMidiRemoteAssignments = other.onPublishMidiRemoteAssignments;
    onSoloMidiLearnRequested = other.onSoloMidiLearnRequested;
    onSoloMidiForgetRequested = other.onSoloMidiForgetRequested;
    onQuerySoloMidiMapping = other.onQuerySoloMidiMapping;
    setMidiRemoteDoc(other.midiRemoteDoc_);
    setShortcutManager(other.shortcuts_);
    setViewDoc(other.viewDoc_);
    onMixerViewChanged = other.onMixerViewChanged;
    setSettingsStore(other.settings_);
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
    if (const auto* macro = synth::nearestChannelMacro(*graph_, *macros_, targetId)) {
        graphEditor_->getMacroController().selectMacro(macro->id, false);
        return;
    }
    for (auto* node : graph_->getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == targetId) {
            graphEditor_->selectModule(node->nodeID, false);
            return;
        }
}

namespace {
std::vector<juce::uint32> currentTrackColours(const synth::TimelineDoc& doc) {
    std::vector<juce::uint32> colours;
    for (const auto& track : doc.getTracks())
        colours.push_back(track.colourArgb);
    return colours;
}
} // namespace

// Re-tints each channel column from the timeline's current track colours without rebuilding it. A no-op unless a
// track colour actually changed since the last rebuild/refresh, so it is safe to call on every TimelineDoc
// notification.
void MixerPanelComponent::refreshTrackColours() {
    if (graph_ == nullptr || doc_ == nullptr || macros_ == nullptr || columnsUnbound_)
        return;
    auto colours = currentTrackColours(*doc_);
    if (colours == trackColoursSeen_)
        return;
    trackColoursSeen_ = std::move(colours);

    const auto snapshot = synth::buildMixerSnapshot(*graph_, *doc_, *macros_);
    for (const auto& column : snapshot.columns)
        for (const auto& entry : columnEntries_)
            if (entry.kind == ColumnEntry::Kind::Strip && entry.nodeId == column.nodeId) {
                static_cast<MixerColumnComponent*>(entry.component)->setHeaderColour(column.colour);
                break;
            }
}

// The snapshot is the one source both the columns and the side pane read, so ordering it here keeps them
// in agreement. Track-less columns keep the positions they occupy; only which of them sits where changes.
// Hidden buses stay in the saved order (busUuidsInOrder_) so a drag merges into the full list.
void MixerPanelComponent::applySavedBusOrder(synth::MixerSnapshot& snapshot) {
    std::vector<size_t> slots;
    std::vector<juce::String> uuids;
    for (size_t i = 0; i < snapshot.columns.size(); ++i) {
        const auto& column = snapshot.columns[i];
        const bool channel =
            column.kind == synth::MixerColumn::Kind::Strip || column.kind == synth::MixerColumn::Kind::Bus;
        if (channel && column.feedingTracks.empty()) {
            slots.push_back(i);
            uuids.push_back(column.uuid);
        }
    }
    busUuidsInOrder_ = viewDoc_->orderBuses(uuids);

    std::vector<synth::MixerColumn> original;
    for (auto slot : slots)
        original.push_back(snapshot.columns[slot]);
    for (size_t n = 0; n < slots.size(); ++n) {
        const auto source = std::find_if(original.begin(), original.end(),
                                         [&](const synth::MixerColumn& c) { return c.uuid == busUuidsInOrder_[n]; });
        if (source != original.end())
            snapshot.columns[slots[n]] = *source;
    }
}

void MixerPanelComponent::rebuild() {
    if (graph_ == nullptr || doc_ == nullptr || macros_ == nullptr)
        return;

    // Every column below is built and bound fresh, so whatever unbindAllColumns() detached is
    // live again once this returns.
    columnsUnbound_ = false;
    // A drag still held would be left waiting on a header that is about to be destroyed; a drop
    // settling in (or being committed by endColumnDrag) survives, its columns are re-found by uuid.
    if (!committingColumnDrag_ && (columnReorder_.isDragging() || columnReorder_.isPressed()))
        discardColumnDrag();

    // Capture the currently focused column's IDENTITY before the columns it points at are
    // destroyed below -- resolveFocusAfterRebuild() re-finds it afterwards by identity (uuid, or
    // kind alone for Direct), never by the raw index, which an unrelated strip insert/removal
    // elsewhere in the column order would otherwise silently reattach to the wrong column.
    const bool hadFocus = focusedColumnIndex_ >= 0 && focusedColumnIndex_ < (int)columnEntries_.size();
    const auto previousKind = hadFocus ? columnEntries_[(size_t)focusedColumnIndex_].kind : ColumnEntry::Kind::Strip;
    const auto previousUuid = hadFocus ? columnEntries_[(size_t)focusedColumnIndex_].uuid : juce::String();
    columnEntries_.clear();
    focusedColumnIndex_ = -1;

    auto snapshot = synth::buildMixerSnapshot(*graph_, *doc_, *macros_);
    applySavedBusOrder(snapshot);
    trackColoursSeen_ = currentTrackColours(*doc_);

    stripColumns_.clear();
    for (const auto& column : snapshot.columns) {
        // Strips AND buses: a bus is an ordinary strip column with a BUS badge and a feeding-strips
        // source line (docs/mixer/sends-and-buses.md), not a column kind of its own with its own widget.
        if (column.kind != synth::MixerColumn::Kind::Strip && column.kind != synth::MixerColumn::Kind::Bus)
            continue;
        // A hidden channel gets no column at all; its zone is kept, so showing it again puts it back.
        const auto channelId = channelIdFor(column);
        if (viewDoc_->isHidden(channelId))
            continue;
        auto widget = std::make_unique<MixerColumnComponent>();
        widget->setSectionLayout(sectionLayout_);
        widget->configure(*graph_, *undoManager_, *macros_, *graphEditor_, *audioEngine_, meterReader_);

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
        widget->setMoveSendRowProvider([this](juce::AudioProcessorGraph::NodeID stripNodeId, int fromRow, int toRow) {
            return moveSendRow(stripNodeId, fromRow, toRow);
        });

        widget->setSendSettlePendingHandler([this, stripId = column.nodeId](int finalRow, float fromY) {
            pendingSendSettle_ = {stripId, finalRow, fromY};
        });

        widget->onColumnClicked = [this, uuid = column.uuid] { selectOnCanvas(uuid); };
        widget->onEditOnCanvas = [this](const juce::String& target) { selectOnCanvas(target); };
        widget->onMutated = [this] {
            if (onGraphMutated)
                onGraphMutated();
        };
        widget->onLiveStateChanged = [this] {
            if (onLiveMixerStateChanged)
                onLiveMixerStateChanged();
        };
        widget->onResetAllMetersRequested = [this] { resetAllMeterReadouts(); };
        // Forwards this panel's single set of Solo-learn callbacks down to the column,
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
        widget->setHeaderContextMenu([this, channelId](const juce::MouseEvent&) { showChannelMenu(channelId); });
        widget->setColourEditable(colourRouteFor(column.feedingTracks, column.uuid) != ColourRoute::None);
        widget->onColourPickRequested = [this, uuid = column.uuid](juce::Rectangle<int> dotScreenBounds) {
            openColourPicker(uuid, dotScreenBounds);
        };
        widget->getInsertList().bypassShortcutText = [this] { return bypassShortcutText(); };
        widget->getSendList().bypassShortcutText = [this] { return bypassShortcutText(); };
        content_.addAndMakeVisible(*widget);
        // Only the scrolling group reorders: a pinned column stays where the zone puts it.
        if (viewDoc_->getZone(channelId) == synth::MixerZone::Scrolling)
            wireColumnReorder(*widget, column.uuid);

        ColumnEntry entry;
        entry.kind = ColumnEntry::Kind::Strip;
        entry.component = widget.get();
        entry.nodeId = column.nodeId;
        entry.uuid = column.uuid;
        entry.channelId = channelId;
        entry.zone = viewDoc_->getZone(channelId);
        entry.linkedToTrack = column.linkedToTrack;
        entry.feedingTracks = column.feedingTracks;
        columnEntries_.push_back(std::move(entry));

        stripColumns_.push_back(std::move(widget));
    }

    if (directColumn_ != nullptr) {
        const bool showDirect = snapshot.hasDirect && !viewDoc_->isHidden(synth::MixerViewDoc::kDirectId);
        directColumn_->setVisible(showDirect);
        if (showDirect) {
            directColumn_->refreshEnablement();
            content_.addAndMakeVisible(*directColumn_);

            ColumnEntry entry;
            entry.kind = ColumnEntry::Kind::Direct;
            entry.component = directColumn_.get();
            entry.channelId = synth::MixerViewDoc::kDirectId;
            entry.zone = viewDoc_->getZone(entry.channelId);
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
                    entry.channelId = synth::MixerViewDoc::kMasterId;
                    entry.zone = viewDoc_->getZone(entry.channelId);
                    columnEntries_.push_back(std::move(entry));
                }
            content_.addAndMakeVisible(*masterColumn_);
        }
    }

    assignZones();
    publishChannelsToPane(snapshot);

    // An empty graph (no strips, no Direct, no Master -- a brand-new project) otherwise
    // rendered a blank panel with nothing telling the user how to get started.
    emptyHint_.setVisible(columnEntries_.empty());

    resolveFocusAfterRebuild(hadFocus, previousKind, previousUuid);
    reconcileRowFocus();
    syncFocusVisuals();

    resized();
    syncRowVisuals(); // again, now the columns have a height to scroll the focused row into

    // A dropped send row glides into its slot on the new column of the same strip; the pending
    // settle is cleared whether or not that column exists.
    const auto settle = std::exchange(pendingSendSettle_, PendingSendSettle{});
    if (settle.row >= 0)
        for (auto& column : stripColumns_)
            if (column != nullptr && column->getNodeId() == settle.strip)
                column->startSendSettle(settle.row, settle.fromY);
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
    const synth::DefaultChannelLayout layout{{x, 0},
                                             {x + kBusCardGap, 0},
                                             {x + 2 * kBusCardGap, 0},
                                             {x + 3 * kBusCardGap, 0},
                                             {x + 4 * kBusCardGap, 0},
                                             [this](juce::AudioProcessor& processor, const juce::String& moduleType) {
                                                 graphEditor_->applyDualIODefaultTo(processor, moduleType);
                                             }};

    juce::AudioProcessorGraph::NodeID created;
    undoManager_->recordGraphAndMacroChange(*graph_, *macros_, [&] {
        const auto channel = synth::buildBusChannel(*graph_, layout);
        if (channel.strip == nullptr)
            return;
        created = channel.strip->nodeID;

        synth::Macro macro;
        macro.name = synth::busFallbackName(*graph_, created);
        macro.members = {channel.gateUuid, channel.eqUuid, channel.compressorUuid, channel.stripUuid};
        macros_->add(macro);
        // Core built Strip -> Master as plain edges; the bus leaves its macro by an output port like a track does.
        graphEditor_->routeChannelOutputThroughMacroPort(channel.stripUuid);
        graphEditor_->updateComponents();
    });

    if (created != juce::AudioProcessorGraph::NodeID{} && onGraphMutated)
        onGraphMutated();
    return created;
}

// This is the one place graph, TimelineDoc AND macros are all reachable at once, so the physical
// slot swap (synth::moveSendRow) and every automation lane it carries along
// (TimelineDoc::swapLaneParams) land in a SINGLE
// AppUndoManager::recordGraphTimelineAndMacroChange step
// (see docs/mixer/sends-and-buses.md#reordering-sends).
bool MixerPanelComponent::moveSendRow(juce::AudioProcessorGraph::NodeID stripNodeId, int fromRow, int toRow) {
    if (graph_ == nullptr || doc_ == nullptr || macros_ == nullptr || undoManager_ == nullptr ||
        graphEditor_ == nullptr || fromRow == toRow)
        return false;

    bool changed = false;
    auto mutation = [&] {
        std::vector<std::pair<int, int>> swaps;
        if (!synth::moveSendRow(*graph_, stripNodeId, fromRow, toRow, &swaps))
            return;
        changed = true;

        // Replay the exact same slot-swap sequence against every automation lane AND every
        // MIDI Learn assignment bound to either swapped slot's sendNLevel/sendNPan, inside the SAME
        // transaction -- a send's lane and its hardware mapping both follow it, same as its cables
        // and parameter values (docs/mixer/sends-and-buses.md#reordering-sends).
        if (auto* node = graph_->getNodeForId(stripNodeId)) {
            const juce::String uuid = node->properties["uuid"].toString();
            if (uuid.isNotEmpty())
                for (const auto& swap : swaps) {
                    const auto levelA = ChannelStripModule::getSendLevelParameterId(swap.first);
                    const auto levelB = ChannelStripModule::getSendLevelParameterId(swap.second);
                    const auto panA = ChannelStripModule::getSendPanParameterId(swap.first);
                    const auto panB = ChannelStripModule::getSendPanParameterId(swap.second);
                    doc_->swapLaneParams(uuid, levelA, levelB);
                    doc_->swapLaneParams(uuid, panA, panB);
                    if (midiRemoteDoc_ != nullptr) {
                        midiRemoteDoc_->swapParameterAssignments(uuid, levelA, levelB);
                        midiRemoteDoc_->swapParameterAssignments(uuid, panA, panB);
                    }
                }
        }
        graphEditor_->updateComponents();
    };

    undoManager_->recordGraphTimelineAndMacroChange(*graph_, *doc_, *macros_, mutation, midiRemoteDoc_, [this] {
        if (onPublishMidiRemoteAssignments)
            onPublishMidiRemoteAssignments();
    });

    // Republish right away too, not just on a later undo/redo restore -- recordGraphTimelineAndMacroChange's
    // postRestore only fires from perform()/undo() on the UNDO STACK, never for the initial edit itself.
    if (changed && midiRemoteDoc_ != nullptr && onPublishMidiRemoteAssignments)
        onPublishMidiRemoteAssignments();
    if (!changed)
        pendingSendSettle_ = {}; // a refused move rebuilds nothing, so nothing is left to settle
    return changed;
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
    for (const auto& entry : columnEntries_)
        if (entry.component == target)
            scrollToColumn(entry);
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

// Mirrors setMidiLearnArmed()/clearMidiLearnArmed() above -- kept as a separate
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

// The pick-target overlay's view of the mixer -- every strip column, plus Master's fader.
// The Direct column has nothing learnable. Columns that are not showing are skipped by the overlay.
void MixerPanelComponent::collectPickCandidates(std::vector<PickCandidate>& out) const {
    for (const auto& column : stripColumns_)
        if (column != nullptr)
            column->collectPickCandidates(out);
    if (masterColumn_ != nullptr)
        masterColumn_->collectPickCandidates(out);
}

// Called after something OTHER than a click on THIS instance changed mute/solo/
// pan-law -- a hardware nodeCommand press (MainComponentRemoteActionInvoker::invokeNodeCommand,
// via BottomDockComponent::refreshLiveMixerVisualsEverywhere()) or a sibling live view's own
// interactive change (onLiveMixerStateChanged, cross-wired in BottomDockComponent/
// MixerMirrorController -- see docs/mixer/panel.md#detach-mode-move-or-both-places).
// MixerColumnComponent::toggleMuted()/toggleSoloed()'s own refresh only runs on ITS OWN click, so
// nothing else re-syncs a column's or Master's visuals otherwise. Cheap enough to refresh every
// column unconditionally (a handful of strips, never per-frame) rather than resolving which one
// node id maps to.
void MixerPanelComponent::refreshLiveMixerVisuals() {
    for (auto& column : stripColumns_)
        if (column != nullptr)
            column->refreshMuteSoloVisual();
    if (masterColumn_ != nullptr)
        masterColumn_->refreshLiveVisuals();
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
    // Same "muted" colour source as MixerInsertList::paint()'s empty-state text -- resolved
    // here (rather than once in the ctor) so a theme switch's re-skin pass is picked up the next
    // time this panel lays out, the same staleness window MixerColumnHeader accepts for its own
    // theme-derived text colours.
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto muted = laf != nullptr ? laf->getTheme().colors.textMuted : juce::Colour(0xff8A93A0);
    emptyHint_.setColour(juce::Label::textColourId, muted);
    emptyHint_.setFont(juce::Font(juce::FontOptions(13.0f)));
    // The toolbar sits outside the scrolling viewport, so it stays put while columns scroll; the side pane
    // takes the left of the body, and the zones and the scrolling middle share what is left.
    auto area = getLocalBounds();
    toolbar_.setBounds(area.removeFromTop(MixerPanelToolbar::kHeight));
    sidePane_.setBounds(area.removeFromLeft(sidePane_.getOccupiedWidth()));
    emptyHint_.setBounds(area.reduced(24));

    // A host that cannot grow (a detached window) keeps every section at its full height and lets
    // the whole column scroll vertically instead of squeezing the sections.
    const int columnHeight = layoutZones(area, contentScrollsVertically());
    placeColumns(columnHeight, /*relayout=*/true);

    toolbar_.refresh();
}

} // namespace synth::ui
