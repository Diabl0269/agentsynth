// Concern: MixerPanelComponent's incremental column rebuild -- which strip columns a rebuild keeps, building the
// ones it cannot keep, and unbinding only the columns a restore's freed nodes are bound to.
#include "MixerPanelComponent.h"

#include "Mixer/MixerModel/MixerModel.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <set>

namespace synth::ui {

MixerPanelComponent::StripColumnsByUuid MixerPanelComponent::releaseStripColumns() {
    StripColumnsByUuid previous;
    for (auto& widget : stripColumns_) {
        if (widget == nullptr)
            continue;
        const auto uuid = widget->getUuid();
        if (previous.count(uuid) == 0) {
            previous.emplace(uuid, std::move(widget));
            continue;
        }
        stripColumnState_.erase(widget.get()); // a second column with the same uuid is never kept
        widget.reset();
    }
    stripColumns_.clear();
    return previous;
}

// The sources line: a bus lists the strips feeding it, a track channel the tracks that play into it.
juce::String MixerPanelComponent::sourcesTextFor(const synth::MixerColumn& column) const {
    juce::StringArray sourceNames;
    if (column.kind == synth::MixerColumn::Kind::Bus) {
        for (const auto& name : column.busSources)
            sourceNames.add(name);
    } else {
        for (auto trackId : column.feedingTracks)
            if (const auto* track = doc_->getTrack(trackId))
                sourceNames.add(track->name);
    }
    return sourceNames.joinIntoString(", ");
}

// A column is kept when everything it was built from is equal: its MixerColumn and sources line (setColumn's whole
// input), and what the panel itself decided for it (colour dot editable, zone). Its controls are bound to the same
// processors, which unbindColumnsFor() would have released had the restore freed one, and they follow parameter
// changes through their own listeners. Mute and solo are module state with no listener, so a kept column re-reads
// them; its reveal highlight is cleared as a new column's would be. Meter ballistics and the clip readout carry on.
std::unique_ptr<MixerColumnComponent> MixerPanelComponent::takeOrBuildStripColumn(StripColumnsByUuid& previous,
                                                                                  const synth::MixerColumn& column,
                                                                                  const juce::String& channelId) {
    StripColumnState state;
    state.colourEditable = colourRouteFor(column.feedingTracks, column.uuid) != ColourRoute::None;
    state.zone = viewDoc_->getZone(channelId);
    const auto sources = sourcesTextFor(column);

    if (const auto found = previous.find(column.uuid); found != previous.end()) {
        auto widget = std::move(found->second);
        previous.erase(found);
        const auto known = stripColumnState_.find(widget.get());
        const bool keep = known != stripColumnState_.end() && !known->second.unbound &&
                          known->second.colourEditable == state.colourEditable && known->second.zone == state.zone &&
                          widget->showsColumn(column, sources);
        if (keep) {
            widget->refreshMuteSoloVisual();
            widget->setSelected(false);
            return widget;
        }
        stripColumnState_.erase(widget.get());
    }
    return buildStripColumn(column, channelId, sources, state);
}

std::unique_ptr<MixerColumnComponent> MixerPanelComponent::buildStripColumn(const synth::MixerColumn& column,
                                                                            const juce::String& channelId,
                                                                            const juce::String& sources,
                                                                            const StripColumnState& state) {
    ++stripColumnsBuilt_;
    auto widget = std::make_unique<MixerColumnComponent>();
    widget->setSectionLayout(sectionLayout_);
    widget->configure(*graph_, *undoManager_, *macros_, *graphEditor_, *audioEngine_, meterReader_);
    widget->setColumn(column, sources);
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
    widget->setColourEditable(state.colourEditable);
    widget->onColourPickRequested = [this, uuid = column.uuid](juce::Rectangle<int> dotScreenBounds) {
        openColourPicker(uuid, dotScreenBounds);
    };
    widget->getInsertList().bypassShortcutText = [this] { return bypassShortcutText(); };
    widget->getSendList().bypassShortcutText = [this] { return bypassShortcutText(); };
    content_.addAndMakeVisible(*widget);
    // Only the scrolling group reorders: a pinned column stays where the zone puts it.
    if (state.zone == synth::MixerZone::Scrolling)
        wireColumnReorder(*widget, column.uuid);
    stripColumnState_[widget.get()] = state;
    return widget;
}

bool MixerPanelComponent::anyStripColumnUnbound() const {
    if (masterColumnUnbound_)
        return true;
    for (const auto& [column, state] : stripColumnState_)
        if (state.unbound)
            return true;
    return false;
}

// Called before a restore frees `doomed` (GraphEditor::onBeforeDetachModuleComponentsFor). Only a column bound to one
// of those nodes holds a pointer the restore invalidates; every other column stays bound and the rebuild after the
// restore keeps it. Master is unbound whenever anything goes (one column; every rebuild() re-binds it anyway).
void MixerPanelComponent::unbindColumnsFor(const std::vector<juce::AudioProcessorGraph::NodeID>& doomed) {
    if (doomed.empty())
        return;
    std::set<juce::uint32> gone;
    for (auto id : doomed)
        gone.insert(id.uid);
    for (auto& column : stripColumns_) {
        if (column == nullptr)
            continue;
        auto& state = stripColumnState_[column.get()];
        if (state.unbound)
            continue;
        if (column->bindsAnyOf(gone)) {
            column->unbindFromGraph();
            state.unbound = true;
        }
    }
    if (masterColumn_ != nullptr) {
        masterColumn_->unbindFromGraph();
        masterColumnUnbound_ = true;
    }
}

} // namespace synth::ui
