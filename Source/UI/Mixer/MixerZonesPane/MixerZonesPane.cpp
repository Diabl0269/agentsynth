// Concern: MixerZonesPane's controls, channel list content (grouping, filter, chips, hidden summary)
// and layout. The row drag is MixerZonesPaneDrag.cpp.
#include "MixerZonesPane.h"

#include "UI/Layout/SearchMatch.h"

namespace synth::ui {

namespace {
constexpr int kRowHeight = 26;
constexpr int kHeaderHeight = 24;
constexpr int kPad = 6;
constexpr int kFieldHeight = 22;
constexpr int kGap = 4;
constexpr int kChipRadioGroup = 0x5a0e;
constexpr synth::MixerZone kGroupOrder[] = {synth::MixerZone::Left, synth::MixerZone::Scrolling,
                                            synth::MixerZone::Right};

size_t groupIndex(synth::MixerZone zone) { return static_cast<size_t>(zone); }
} // namespace

int MixerZonesPane::Item::height() const { return isHeader ? kHeaderHeight : kRowHeight; }

MixerZonesPane::MixerZonesPane() {
    setWantsKeyboardFocus(true);

    addAndMakeVisible(filter_);
    filter_.setTextToShowWhenEmpty("Filter channels", synth::theme::themeOf(*this).colors.textMuted);
    filter_.setFont(juce::Font(juce::FontOptions(12.0f)));
    filter_.setSelectAllWhenFocused(true);
    filter_.setTitle("Filter channels");
    filter_.setTooltip("Filter the channel list by name");
    filter_.setDescription("Narrows the channel list below by name. The mixer itself is not filtered.");
    filter_.onTextChange = [this] { rebuildList(); };
    auto release = [this] {
        filter_.giveAwayKeyboardFocus();
        if (onFocusReleased)
            onFocusReleased();
    };
    filter_.onReturnKey = release;
    filter_.onEscapeKey = release;

    static const char* const kChipNames[] = {"All", "Tracks", "Buses"};
    static const char* const kChipHelp[] = {"Lists every channel", "Lists only track channels",
                                            "Lists only bus channels"};
    for (size_t i = 0; i < chips_.size(); ++i) {
        auto& chip = chips_[i];
        chip.setButtonText(kChipNames[i]);
        chip.setTitle(juce::String(kChipNames[i]) + " channels");
        chip.setDescription(kChipHelp[i]);
        chip.setTooltip(kChipHelp[i]);
        chip.setRadioGroupId(kChipRadioGroup);
        chip.setClickingTogglesState(true);
        chip.setWantsKeyboardFocus(true);
        chip.addShortcut(juce::KeyPress(juce::KeyPress::spaceKey));
        chip.setToggleState(i == 0, juce::dontSendNotification);
        chip.onClick = [this, i] { setChip(static_cast<Chip>(i)); };
        addAndMakeVisible(chip);
    }

    addAndMakeVisible(hiddenLabel_);
    hiddenLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    hiddenLabel_.setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
    hiddenLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(showAll_);
    showAll_.setWantsKeyboardFocus(false);
    showAll_.setDescription("Shows every hidden channel in the mixer again");
    showAll_.setTooltip("Show every hidden channel in the mixer again");
    showAll_.onClick = [this] {
        if (onShowAll)
            onShowAll();
    };

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&listContent_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setWantsKeyboardFocus(false);
    for (size_t i = 0; i < headers_.size(); ++i) {
        headers_[i] = std::make_unique<MixerZonesGroupHeader>(kGroupOrder[i]);
        listContent_.addAndMakeVisible(*headers_[i]);
    }
}

MixerZonesPane::~MixerZonesPane() = default;

void MixerZonesPane::setChannels(std::vector<MixerZoneChannel> channels) {
    // A rebuild under a held drag would leave it waiting on rows that are about to change; a drop being
    // committed (committing_) is the one rebuild the drag survives.
    if (!committing_ && (reorder_.isDragging() || reorder_.isPressed()))
        discardRowDrag();
    channels_ = std::move(channels);
    rebuildList();
}

const MixerZoneChannel* MixerZonesPane::findChannel(const juce::String& id) const {
    for (const auto& channel : channels_)
        if (channel.id == id)
            return &channel;
    return nullptr;
}

bool MixerZonesPane::passesFilter(const MixerZoneChannel& channel) const {
    if (chip_ == Chip::Tracks && channel.kind != MixerZoneChannelKind::Track)
        return false;
    if (chip_ == Chip::Buses && channel.kind != MixerZoneChannelKind::Bus)
        return false;
    const auto text = filter_.getText().trim();
    return synth::ui::searchMatches(channel.name, text);
}

std::vector<MixerZonesPane::Item> MixerZonesPane::buildItems() const {
    std::vector<Item> items;
    for (const auto zone : kGroupOrder) {
        Item header;
        header.isHeader = true;
        header.zone = zone;
        items.push_back(header);
        for (const auto& channel : channels_) {
            if (channel.zone != zone || !passesFilter(channel))
                continue;
            Item row;
            row.channelId = channel.id;
            items.push_back(row);
        }
    }
    return items;
}

MixerZonesRow& MixerZonesPane::rowFor(const MixerZoneChannel& channel) {
    auto& slot = rows_[channel.id];
    if (slot == nullptr) {
        MixerZonesRow::Hooks hooks;
        hooks.onGrab = [this](const juce::String& id, const juce::MouseEvent& e) {
            setCursor(id, false);
            beginRowDrag(id, e);
        };
        hooks.onDrag = [this](const juce::MouseEvent& e) { dragRow(e); };
        hooks.onRelease = [this](const juce::MouseEvent& e) { endRowDrag(e); };
        hooks.onToggleHidden = [this](const juce::String& id) {
            if (const auto* channelNow = findChannel(id); channelNow != nullptr && onSetHidden)
                onSetHidden(id, !channelNow->hidden);
        };
        hooks.onSoloShow = [this](const juce::String& id) {
            if (onSoloShow)
                onSoloShow(id);
        };
        slot = std::make_unique<MixerZonesRow>(std::move(hooks));
        listContent_.addChildComponent(*slot);
    }
    return *slot;
}

// Rows are reused by channel id, so a drop that changes the list keeps the very components the settle
// animation is moving. Rows of channels that no longer exist are destroyed; rows the filter hides stay.
void MixerZonesPane::rebuildList() {
    items_ = buildItems();
    for (auto it = rows_.begin(); it != rows_.end();)
        it = findChannel(it->first) == nullptr ? rows_.erase(it) : std::next(it);
    for (const auto& channel : channels_) {
        auto& row = rowFor(channel);
        row.setChannel(channel);
        row.setHighlightQuery(filter_.getText().trim());
        const bool listed = std::any_of(items_.begin(), items_.end(), [&](const Item& item) {
            return !item.isHeader && item.channelId == channel.id;
        });
        row.setVisible(listed);
    }
    refreshSummary();
    layoutList();
    syncCursorVisuals();
}

void MixerZonesPane::refreshSummary() {
    int hidden = 0;
    for (const auto& channel : channels_)
        hidden += channel.hidden ? 1 : 0;
    const bool any = hidden > 0;
    const auto dot = juce::String::fromUTF8("\xC2\xB7");
    hiddenLabel_.setText(any ? juce::String(hidden) + " hidden " + dot : juce::String("No hidden channels"),
                         juce::dontSendNotification);
    hiddenLabel_.setTitle(any ? juce::String(hidden) + " hidden" : juce::String("No hidden channels"));
    showAll_.setVisible(any);
}

void MixerZonesPane::setChip(Chip chip) {
    if (chip_ == chip)
        return;
    chip_ = chip;
    rebuildList();
}

void MixerZonesPane::layoutList() {
    int total = 0;
    for (const auto& item : items_)
        total += item.height();
    listContent_.setSize(juce::jmax(1, viewport_.getMaximumVisibleWidth()), total);
    placeItems();
}

// Static slots stacked top to bottom; while a drag is in flight every item that was in the list at the
// press takes its place from the animator instead (matched by identity, so the swap a drop causes does
// not lose them).
void MixerZonesPane::placeItems() {
    const bool reordering = reorder_.isReordering();
    int y = 0;
    for (const auto& item : items_) {
        float top = static_cast<float>(y);
        float lift = 0.0f;
        const auto key = reordering ? std::find(dragIdentities_.begin(), dragIdentities_.end(), item.identity())
                                    : dragIdentities_.end();
        if (key != dragIdentities_.end()) {
            const int k = static_cast<int>(key - dragIdentities_.begin());
            const bool dragged = k == reorder_.getDraggedKey();
            top = dragged ? reorder_.getDraggedStart() : reorder_.getLayoutStart(k);
            lift = dragged ? reorder_.getLift() : 0.0f;
        }
        const juce::Rectangle<int> bounds(0, static_cast<int>(std::lround(top)), listContent_.getWidth(),
                                          item.height());
        if (item.isHeader) {
            headers_[groupIndex(item.zone)]->setBounds(bounds);
        } else if (auto row = rows_.find(item.channelId); row != rows_.end()) {
            row->second->setBounds(bounds);
            row->second->setLift(lift);
            if (lift > 0.0f)
                row->second->toFront(false);
        }
        y += item.height();
    }
}

void MixerZonesPane::resized() {
    auto area = getLocalBounds().reduced(kPad);
    filter_.setBounds(area.removeFromTop(kFieldHeight));
    area.removeFromTop(kGap);
    auto chipRow = area.removeFromTop(kFieldHeight);
    const int chipWidth = chipRow.getWidth() / static_cast<int>(chips_.size());
    for (size_t i = 0; i < chips_.size(); ++i)
        chips_[i].setBounds(i + 1 == chips_.size() ? chipRow : chipRow.removeFromLeft(chipWidth).withTrimmedRight(2));
    area.removeFromTop(kGap);
    auto summary = area.removeFromTop(18);
    showAll_.setBounds(summary.removeFromRight(56));
    hiddenLabel_.setBounds(summary);
    area.removeFromTop(kGap);
    viewport_.setBounds(area.withTrimmedLeft(-kPad).withTrimmedRight(-kPad));
    layoutList();
}

// The constructor runs before the pane sits under the app's look and feel, so the two muted colours
// are re-applied whenever the look and feel (or the parent it comes from) changes.
void MixerZonesPane::applyThemeColours() {
    const auto muted = synth::theme::themeOf(*this).colors.textMuted;
    filter_.setTextToShowWhenEmpty("Filter channels", muted);
    hiddenLabel_.setColour(juce::Label::textColourId, muted);
    filter_.repaint();
}

void MixerZonesPane::lookAndFeelChanged() { applyThemeColours(); }

void MixerZonesPane::parentHierarchyChanged() { applyThemeColours(); }

void MixerZonesPane::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.fillAll(theme.colors.bg0);
}

MixerZonesRow* MixerZonesPane::getRowForTest(int index) const {
    int n = 0;
    for (const auto& item : items_) {
        if (item.isHeader)
            continue;
        if (n++ == index)
            return findRowForTest(item.channelId);
    }
    return nullptr;
}

MixerZonesRow* MixerZonesPane::findRowForTest(const juce::String& channelId) const {
    const auto it = rows_.find(channelId);
    return it == rows_.end() ? nullptr : it->second.get();
}

int MixerZonesPane::getRowCountForTest() const {
    return static_cast<int>(
        std::count_if(items_.begin(), items_.end(), [](const Item& item) { return !item.isHeader; }));
}

} // namespace synth::ui
