#pragma once

#include "UI/Layout/ReorderDrag/ReorderCancelKey.h"
#include "UI/Layout/ReorderDrag/ReorderDragAnimator.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include "UI/Layout/SidePane/SidePane.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/Mixer/MixerZonesPane/MixerZonesRow.h"
#include <array>
#include <map>
#include <memory>
#include <vector>

// MixerZonesPane.h (docs/mixer/panel.md#side-pane-zones-and-visibility): the Mixer's side-pane content. One list of the
// mixer's channels in three groups (Left zone, Scrolling, Right zone) with an eye toggle on every row, a filter box and
// All / Tracks / Buses chips that narrow the LIST (never the mixer), and an "N hidden - Show all" line. It edits
// nothing itself: every change goes out through the callbacks and comes back as a fresh setChannels(). The list is
// keyboard-driven too: Up/Down move a row cursor, Space shows or hides, Alt+Up/Down move a channel between groups.
namespace synth::ui {

class MixerZonesPane
    : public juce::Component
    , public SidePaneContent {
public:
    MixerZonesPane();
    ~MixerZonesPane() override;

    /** The full channel list in the mixer's own order, with each channel's current zone and hidden flag. */
    void setChannels(std::vector<MixerZoneChannel> channels);

    std::function<void(const juce::String& channelId, synth::MixerZone zone)> onSetZone;
    /** A row dropped elsewhere in its own group: move `channelId` to the place `targetId` held. */
    std::function<void(const juce::String& channelId, const juce::String& targetId)> onMoveWithinZone;
    std::function<void(const juce::String& channelId, bool hidden)> onSetHidden;
    /** Alt-click on an eye: show only that channel, or restore the previous set when it already is. */
    std::function<void(const juce::String& channelId)> onSoloShow;
    std::function<void()> onShowAll;
    /** Fired when the filter box gives up keyboard focus (Return / Esc) so the owner can take it back. */
    std::function<void()> onFocusReleased;

    juce::Component& getPaneComponent() override { return *this; }
    juce::String getPaneTitle() const override { return "Zones and visibility"; }

    /** Takes keyboard focus for the channel list and puts the cursor on a row (the first one if none). */
    void focusList();
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

    // ---- Test seams ----
    enum class Chip { All, Tracks, Buses };
    juce::TextEditor& getFilterForTest() noexcept { return filter_; }
    juce::Button& getChipForTest(Chip chip) noexcept { return chips_[static_cast<size_t>(chip)]; }
    juce::Button& getShowAllForTest() noexcept { return showAll_; }
    const juce::Label& getHiddenLabelForTest() const noexcept { return hiddenLabel_; }
    /** The rows currently listed, top to bottom; null past the end. */
    MixerZonesRow* getRowForTest(int index) const;
    MixerZonesRow* findRowForTest(const juce::String& channelId) const;
    int getRowCountForTest() const;
    juce::Component& getListContentForTest() noexcept { return listContent_; }
    juce::Component& getGroupHeaderForTest(synth::MixerZone zone) noexcept {
        return *headers_[static_cast<size_t>(zone)];
    }
    bool isDragActiveForTest() const noexcept { return reorder_.isReordering(); }
    const juce::String& getCursorIdForTest() const noexcept { return cursorId_; }
    bool sendEscapeToDragForTest() { return cancelKey_.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), this); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Item {
        bool isHeader = false;
        synth::MixerZone zone = synth::MixerZone::Scrolling; // the group, for a header
        juce::String channelId;                              // the channel, for a row
        juce::String identity() const {
            return isHeader ? juce::String("h:") + mixerZoneToString(zone) : "r:" + channelId;
        }
        int height() const;
    };

    struct ListContent : juce::Component {};

    // ---- List content -- MixerZonesPane.cpp ----
    bool passesFilter(const MixerZoneChannel& channel) const;
    std::vector<Item> buildItems() const;
    void rebuildList();
    void layoutList();
    void refreshSummary();
    void setChip(Chip chip);
    MixerZonesRow& rowFor(const MixerZoneChannel& channel);
    const MixerZoneChannel* findChannel(const juce::String& id) const;

    // ---- Row drag -- MixerZonesPaneDrag.cpp ----
    void beginRowDrag(const juce::String& id, const juce::MouseEvent& e);
    void dragRow(const juce::MouseEvent& e);
    void endRowDrag(const juce::MouseEvent& e);
    void commitRowDrag();
    void cancelRowDrag();
    void discardRowDrag();
    void onDragFrame();
    void startDragFramesIfNeeded();
    void placeItems();
    float pointerYInList(const juce::MouseEvent& e);
    synth::MixerZone zoneForDrop(const std::vector<int>& newOrder) const;
    juce::String targetWithinZone(const std::vector<int>& newOrder, synth::MixerZone zone) const;

    // ---- Keyboard -- MixerZonesPaneKeyboard.cpp ----
    std::vector<juce::String> listedRowIds() const;
    bool moveCursor(int step);
    void setCursor(const juce::String& id, bool announce);
    bool toggleCursorHidden();
    bool moveCursorZone(int step);
    void syncCursorVisuals();

    std::vector<MixerZoneChannel> channels_;
    std::vector<Item> items_;
    std::map<juce::String, std::unique_ptr<MixerZonesRow>> rows_;
    std::array<std::unique_ptr<MixerZonesGroupHeader>, 3> headers_;

    juce::TextEditor filter_;
    std::array<juce::TextButton, 3> chips_;
    Chip chip_ = Chip::All;
    juce::Label hiddenLabel_;
    TextLinkButton showAll_{"Show all", juce::Justification::centredRight};
    juce::Viewport viewport_;
    ListContent listContent_;

    ReorderDragAnimator reorder_;
    ReorderFramePump frames_{*this};
    ReorderCancelKey cancelKey_;
    std::vector<juce::String> dragIdentities_; // animator key -> item identity, captured at press
    juce::String draggedId_;
    juce::String cursorId_;   // the keyboard cursor's channel, kept across rebuilds by id
    float minPointer_ = 0.0f; // list y the pointer is floored at, so the row never lands above the first heading
    unsigned generationSeen_ = 0;
    bool dragCancelled_ = false;
    bool committing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerZonesPane)
};

} // namespace synth::ui
