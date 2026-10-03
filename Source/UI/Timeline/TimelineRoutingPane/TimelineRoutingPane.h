#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/SidePane/SidePane.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/Timeline/ChannelChipComponent.h"
#include "UI/Timeline/TimelineRoutingPane/TimelineRoutingPaneControls.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <functional>
#include <memory>
#include <vector>

// TimelineRoutingPane.h (docs/timeline/tracks.md#routing-from-the-side-pane, docs/layout/side-pane.md): the Timeline
// tab's side-pane content. It shows the SELECTED track's routing -- what plays it (its Track In / Track Audio node),
// where its notes go (MIDI tracks) and which mixer channel its sound ends up in -- like Cubase's Inspector.
//
// A view: it reads the TimelineDoc and the TrackHeaderHost, owns no document state and edits only through host calls
// (bindTrackTo, createAndBindTrackInNode, setMidiDestinationConnected, selectNodeInGraph, revealChannelForTrack), each
// one undoable there. It has no timer: the panel calls setTrack()/refresh() on selection, document and graph changes
// and tickMeter() from its existing 15 Hz tick.
namespace synth::ui {

class TimelineRoutingPane
    : public juce::Component
    , public SidePaneContent {
public:
    TimelineRoutingPane();
    ~TimelineRoutingPane() override;

    /** Non-owning, may be null (the pane then shows its empty line). Call refresh() after changing either. */
    void setDoc(synth::TimelineDoc* doc);
    /** Non-owning, may be null (rows are then read-only and empty). */
    void setHost(TrackHeaderHost* host);
    /** Shows `track`'s routing (an invalid id shows the "select a track" line) and refreshes. */
    void setTrack(synth::TrackId track);
    synth::TrackId getTrack() const noexcept { return track_; }
    /** Re-reads the document and the host; cheap and idempotent. Message thread. */
    void refresh();
    /** One meter tick from the panel's shared 15 Hz timer. True only when the meter repainted (gated like the
     *  header's channel chip); false when no channel is showing. */
    bool tickMeter();

    /** Applies a "Canvas node" menu choice (ids as buildTrackBindingMenu documents). Anything else is ignored. */
    void applyBindingMenuChoice(int menuId);
    /** Opens the MIDI-destination picker in a CallOutBox anchored on the "MIDI destinations" button. */
    void openMidiDestinationsPicker();

    /** The side pane shows the pane inside a vertical scroller, so a short bottom panel never clips a section. */
    juce::Component& getPaneComponent() override { return scroller_; }
    /** The height the laid-out sections need at the current width (from the last layout). */
    int getContentHeight() const noexcept { return contentHeight_; }
    juce::String getPaneTitle() const override { return "Track routing"; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ---- Test seams ----
    /** A real click on "Canvas node" builds the menu and hands it here instead of showing it (the header's
     *  setShowContextMenuHookForTest shape); the test then feeds a chosen id to applyBindingMenuChoice(). */
    void setShowBindingMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showBindingMenuHook_ = std::move(hook);
    }
    /** Replaces launching the picker's CallOutBox when "MIDI destinations" is clicked. */
    void setOpenMidiDestinationsHookForTest(std::function<void()> hook) { openDestinationsHook_ = std::move(hook); }
    /** The picker "MIDI destinations" opens, wired exactly like the real one but never launched. */
    std::unique_ptr<MidiDestinationPicker> createMidiDestinationPickerForTest() { return buildPicker(); }
    juce::Button& getCanvasNodeButtonForTest() noexcept { return canvasNode_; }
    juce::Button& getMidiDestinationsButtonForTest() noexcept { return midiDestinations_; }
    juce::Button& getShowOnCanvasLinkForTest() noexcept { return showOnCanvas_; }
    juce::Button& getShowInMixerLinkForTest() noexcept { return showInMixer_; }
    ChannelChipComponent& getChannelChipForTest() noexcept { return channelChip_; }
    /** What the pane shows right now, as text; empty when the row is not showing. */
    juce::String getEmptyLineForTest() const { return view_.emptyLine; }
    juce::String getHeaderNameForTest() const { return view_.name; }
    juce::String getKindBadgeForTest() const { return view_.kindBadge; }
    juce::String getCanvasNodeTextForTest() const { return canvasNode_.isVisible() ? canvasNode_.getButtonText() : ""; }
    bool isCanvasNodeWarningForTest() const noexcept { return canvasNode_.isWarning(); }
    juce::String getMissingNoteForTest() const { return view_.missingNote; }
    bool isMidiDestinationsShownForTest() const noexcept { return midiDestinations_.isVisible(); }
    juce::String getMidiDestinationsTextForTest() const {
        return midiDestinations_.isVisible() ? midiDestinations_.getButtonText() : "";
    }
    /** "Channel · <name>", or "No mixer channel" (muted); empty when the Mixer-channel section is not showing. */
    juce::String getChannelTextForTest() const { return view_.channelText; }

private:
    // Everything the pane paints and shows, derived by refresh() and never edited elsewhere.
    struct View {
        bool hasTrack = false;
        juce::String emptyLine; // the single muted line, when there is no routable track
        juce::String name;
        juce::String kindBadge;
        juce::Colour colour;
        bool isMidi = false;
        juce::String canvasNodeText; // the bound node's name, "Not connected" or "<kind> (missing)"
        bool canvasNodeWarning = false;
        bool bound = false;                // the binding resolves to a live node (the canvas link is offered)
        juce::String missingNote;          // the orphaned-binding explanation, or empty
        juce::String midiDestinationsText; // the connected MIDI destinations, comma-joined, or "None"
        bool hasChannel = false;
        juce::String channelText;
    };

    void computeView();
    void applyViewToChildren();
    void layoutSections();
    void setContentHeight(int height);
    void openBindingMenu();
    void showOnCanvas();
    void showInMixer();
    std::unique_ptr<MidiDestinationPicker> buildPicker();
    void computeCanvasNode(const synth::Track& track);
    void computeNotesAndChannel();

    // Rows and dividers as laid out by layoutSections(), for paint().
    struct Layout {
        juce::Rectangle<int> header;
        juce::Rectangle<int> canvasNodeHeading;
        juce::Rectangle<int> missingNote;
        juce::Rectangle<int> midiDestinationsHeading;
        juce::Rectangle<int> mixerChannelHeading;
        juce::Rectangle<int> noChannel;
        juce::Rectangle<int> emptyLine;
        std::vector<int> dividers; // y of each 1 px section border
    };

    // Hosts the pane in a Viewport: the pane takes the viewport's width and at least its height, taller when the
    // sections need it (docs/layout/side-pane.md: sections scroll inside the pane).
    class Scroller : public juce::Component {
    public:
        explicit Scroller(TimelineRoutingPane& pane);
        void resized() override { fit(); }
        void fit();

    private:
        TimelineRoutingPane& pane_;
        juce::Viewport viewport_;
    };

    synth::TimelineDoc* doc_ = nullptr;
    TrackHeaderHost* host_ = nullptr;
    synth::TrackId track_;
    View view_;
    Layout layout_;
    int contentHeight_ = 0;
    bool fitting_ = false;

    RoutingComboButton canvasNode_{"Canvas node"};
    TextLinkButton showOnCanvas_;
    RoutingComboButton midiDestinations_{"MIDI destinations"};
    ChannelChipComponent channelChip_;
    TextLinkButton showInMixer_;

    std::function<void(juce::PopupMenu&)> showBindingMenuHook_;
    std::function<void()> openDestinationsHook_;
    Scroller scroller_{*this}; // last: it views *this, so it must be destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineRoutingPane)
};

} // namespace synth::ui
