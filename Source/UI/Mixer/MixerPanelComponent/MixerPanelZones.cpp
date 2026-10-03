// Concern: MixerPanelComponent's zones -- assigning every column to the left zone, the scrolling middle
// or the right zone, sizing the zones (a zone takes at most its share of half the width and scrolls
// inside itself past that), the pin menu on a column header, and feeding the side pane's channel list.
#include "MixerPanelComponent.h"
#include "UI/Layout/ContextMenuPlacement.h"

#include "Mixer/MixerModel/MixerModel.h"
#include "MixerPanelInternal.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <optional>

namespace synth::ui {

namespace {
constexpr int kColumnPitch = kMixerColumnWidth + kMixerColumnGap;
constexpr int kDividerWidth = 2;
} // namespace

void MixerPanelComponent::ZoneDivider::paint(juce::Graphics& g) {
    g.fillAll(synth::theme::themeOf(*this).colors.border.brighter(0.35f));
}

MixerPanelComponent::Zone& MixerPanelComponent::zoneFor(synth::MixerZone zone) noexcept {
    return zone == synth::MixerZone::Right ? rightZone_ : leftZone_;
}

juce::Component& MixerPanelComponent::zoneContentFor(synth::MixerZone zone) noexcept {
    return zone == synth::MixerZone::Scrolling ? static_cast<juce::Component&>(content_) : zoneFor(zone).content;
}

// A Strip's or bus's node uuid survives save and load (a NodeID does not); Master and Direct have no uuid of
// their own and use the fixed ids.
juce::String MixerPanelComponent::channelIdFor(const synth::MixerColumn& column) {
    switch (column.kind) {
    case synth::MixerColumn::Kind::Master:
        return synth::MixerViewDoc::kMasterId;
    case synth::MixerColumn::Kind::Direct:
        return synth::MixerViewDoc::kDirectId;
    case synth::MixerColumn::Kind::Strip:
    case synth::MixerColumn::Kind::Bus:
        break;
    }
    return column.uuid;
}

// The entries end up in the order the columns are laid out left to right (left zone, scrolling, right
// zone), which is also the order the arrow keys walk. The sort is stable, so within a zone the columns
// keep the mixer's own order.
void MixerPanelComponent::assignZones() {
    std::stable_sort(columnEntries_.begin(), columnEntries_.end(),
                     [](const ColumnEntry& a, const ColumnEntry& b) { return (int)a.zone < (int)b.zone; });
    int next[3] = {0, 0, 0};
    for (auto& entry : columnEntries_) {
        entry.zoneIndex = next[(int)entry.zone]++;
        if (entry.component != nullptr)
            zoneContentFor(entry.zone).addAndMakeVisible(*entry.component);
    }
}

// Zones take at most half the width between them. When both need more than their share the cap is split
// evenly, except that a zone which needs less than half of it gives the rest to the other.
int MixerPanelComponent::layoutZones(juce::Rectangle<int> area, bool scrollsVertically) {
    int count[3] = {0, 0, 0};
    for (const auto& entry : columnEntries_)
        ++count[(int)entry.zone];
    const int leftNeed = count[(int)synth::MixerZone::Left] * kColumnPitch;
    const int rightNeed = count[(int)synth::MixerZone::Right] * kColumnPitch;
    const int cap = area.getWidth() / 2;
    int leftWidth = leftNeed;
    int rightWidth = rightNeed;
    if (leftNeed + rightNeed > cap) {
        leftWidth = juce::jmin(leftNeed, juce::jmax(cap / 2, cap - rightNeed));
        rightWidth = juce::jmin(rightNeed, cap - leftWidth);
    }

    const int columnHeight =
        scrollsVertically ? sectionLayout_.requiredColumnHeight() : juce::jmax(0, area.getHeight());
    auto place = [&](Zone& zone, int need, int width, bool leftSide) {
        const bool used = need > 0 && width > 0;
        zone.viewport.setVisible(used);
        zone.divider.setVisible(used);
        if (!used)
            return;
        zone.viewport.setBounds(leftSide ? area.removeFromLeft(width) : area.removeFromRight(width));
        zone.divider.setBounds(leftSide ? area.removeFromLeft(kDividerWidth) : area.removeFromRight(kDividerWidth));
        zone.viewport.setScrollBarsShown(scrollsVertically, true);
        zone.content.setSize(juce::jmax(zone.viewport.getWidth(), need), columnHeight);
    };
    place(leftZone_, leftNeed, leftWidth, true);
    place(rightZone_, rightNeed, rightWidth, false);

    viewport_.setBounds(area);
    viewport_.setScrollBarsShown(scrollsVertically, true);
    const int scrollingNeed = count[(int)synth::MixerZone::Scrolling] * kColumnPitch;
    content_.setSize(juce::jmax(viewport_.getWidth(), scrollingNeed), columnHeight);
    return columnHeight;
}

// The three viewports show the same rows of every column, so they scroll vertically as one.
void MixerPanelComponent::syncVerticalScroll(juce::Viewport& source) {
    if (syncingScroll_)
        return;
    syncingScroll_ = true;
    const int y = source.getViewPositionY();
    for (auto* other : {&viewport_, &leftZone_.viewport, &rightZone_.viewport})
        if (other != &source && other->getViewPositionY() != y)
            other->setViewPosition(other->getViewPositionX(), y);
    syncingScroll_ = false;
}

void MixerPanelComponent::scrollToColumn(const ColumnEntry& entry) {
    if (entry.component == nullptr)
        return;
    juce::Viewport& target = entry.zone == synth::MixerZone::Scrolling ? viewport_ : zoneFor(entry.zone).viewport;
    target.setViewPosition(entry.component->getX(), 0);
}

// Every channel, hidden ones too, in the mixer's own order: strips and buses, then Direct, then Master.
void MixerPanelComponent::publishChannelsToPane(const synth::MixerSnapshot& snapshot) {
    std::vector<MixerZoneChannel> channels;
    auto add = [&](const juce::String& id, const juce::String& name, juce::Colour colour, MixerZoneChannelKind kind) {
        MixerZoneChannel channel;
        channel.id = id;
        channel.name = name;
        channel.colour = colour;
        channel.kind = kind;
        channel.zone = viewDoc_->getZone(id);
        channel.hidden = viewDoc_->isHidden(id);
        channels.push_back(std::move(channel));
    };
    const synth::MixerColumn* master = nullptr;
    for (const auto& column : snapshot.columns) {
        if (column.kind == synth::MixerColumn::Kind::Strip)
            add(column.uuid, column.name, column.colour, MixerZoneChannelKind::Track);
        else if (column.kind == synth::MixerColumn::Kind::Bus)
            add(column.uuid, column.name, column.colour, MixerZoneChannelKind::Bus);
        else if (column.kind == synth::MixerColumn::Kind::Master)
            master = &column;
    }
    if (snapshot.hasDirect)
        add(synth::MixerViewDoc::kDirectId, "Direct", juce::Colour(), MixerZoneChannelKind::Direct);
    if (master != nullptr)
        add(synth::MixerViewDoc::kMasterId, master->name, master->colour, MixerZoneChannelKind::Master);

    channelIds_.clear();
    for (const auto& channel : channels)
        channelIds_.push_back(channel.id);
    zonesPane_.setChannels(std::move(channels));
}

// A side-pane row dropped elsewhere in its group. A track channel's place in the mixer IS its track's place
// in the timeline, so the move goes out exactly like a column drag: the dragged strip's first feeding
// track moves to the index of the target strip's first track (one undo step, and the owner rebuilds the
// mixer). Two buses (or track-less strips) swap places in the saved bus order instead, like a bus column
// drag, as one view edit. Any other pair -- Direct, Master, a hidden track with no column, a track and
// a bus -- keeps its order.
void MixerPanelComponent::moveChannelWithinZone(const juce::String& channelId, const juce::String& targetId) {
    const auto busFrom = std::find(busUuidsInOrder_.begin(), busUuidsInOrder_.end(), channelId);
    const auto busTo = std::find(busUuidsInOrder_.begin(), busUuidsInOrder_.end(), targetId);
    if (busFrom != busUuidsInOrder_.end() && busTo != busUuidsInOrder_.end()) {
        auto order = busUuidsInOrder_;
        const auto to = static_cast<size_t>(busTo - busUuidsInOrder_.begin());
        order.erase(order.begin() + (busFrom - busUuidsInOrder_.begin()));
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(to), channelId);
        applyViewEdit([&](synth::MixerViewDoc& d) { d.setBusOrder(order); });
        return;
    }
    const auto trackOf = [this](const juce::String& id) -> std::optional<synth::TrackId> {
        for (const auto& entry : columnEntries_)
            if (entry.channelId == id && entry.kind == ColumnEntry::Kind::Strip && !entry.feedingTracks.empty())
                return entry.feedingTracks.front();
        return std::nullopt;
    };
    const auto moved = trackOf(channelId);
    const auto target = trackOf(targetId);
    if (!moved || !target || !onMoveTrack || doc_ == nullptr)
        return;
    const auto& tracks = doc_->getTracks();
    const auto anchor =
        std::find_if(tracks.begin(), tracks.end(), [&](const synth::Track& t) { return t.id == *target; });
    if (anchor != tracks.end())
        onMoveTrack(*moved, static_cast<int>(anchor - tracks.begin()));
}

// Right-click on a column header: where should this channel sit? The current zone is ticked; Unpin is
// only offered for a pinned channel.
void MixerPanelComponent::showChannelMenu(const juce::String& channelId, std::optional<juce::Rectangle<int>> anchor) {
    const auto zone = viewDoc_->getZone(channelId);
    juce::Component::SafePointer<MixerPanelComponent> safe(this);
    juce::PopupMenu menu;
    menu.addItem("Pin left", zone != synth::MixerZone::Left, zone == synth::MixerZone::Left, [safe, channelId] {
        if (safe != nullptr)
            safe->pinChannel(channelId, synth::MixerZone::Left);
    });
    menu.addItem("Pin right", zone != synth::MixerZone::Right, zone == synth::MixerZone::Right, [safe, channelId] {
        if (safe != nullptr)
            safe->pinChannel(channelId, synth::MixerZone::Right);
    });
    menu.addItem("Unpin", zone != synth::MixerZone::Scrolling, false, [safe, channelId] {
        if (safe != nullptr)
            safe->pinChannel(channelId, synth::MixerZone::Scrolling);
    });
    if (showZoneMenuHook_) {
        showZoneMenuHook_(menu);
        return;
    }
    const auto options = synth::ui::contextMenuOptions(anchor);
    menu.showMenuAsync(options);
}

void MixerPanelComponent::wireHeaderMenus() {
    if (directColumn_ != nullptr)
        directColumn_->setHeaderContextMenu(
            [this](const juce::MouseEvent&) { showChannelMenu(synth::MixerViewDoc::kDirectId); });
    if (masterColumn_ != nullptr)
        masterColumn_->setHeaderContextMenu(
            [this](const juce::MouseEvent&) { showChannelMenu(synth::MixerViewDoc::kMasterId); });
}

} // namespace synth::ui
