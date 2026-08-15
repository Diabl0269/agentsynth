#pragma once

#include "../Timeline/TimelineDoc.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// TimelineTrackHeaderComponent — TL5-3: one row in the timeline panel's track-header column.
//
// Owns no timeline state: the TimelineDoc is the truth for every value it shows (name, colour,
// mute/solo/arm, binding), and every edit it makes goes back through the doc via its host so it
// lands on the undo stack. It is rebuilt/refreshed only when the doc notifies — there is no timer
// here and no per-frame repaint (see CLAUDE.md's repaint discipline).
namespace synth::ui {

/**
 * @brief What a track header needs from the app to resolve and re-bind its track's "Track In" node.
 *
 * The header itself is deliberately graph-free: it never sees a juce::AudioProcessorGraph, so it
 * stays headless-testable against a stub. MainComponent is the real implementor — it is the only
 * object that owns the doc, the graph and the undo manager at once.
 */
struct TrackHeaderHost {
    virtual ~TrackHeaderHost() = default;

    /** One entry in the binding chip's menu: a live "Track In" node the track could bind to. */
    struct BindingOption {
        juce::String uuid;
        juce::String displayName;
    };

    /** Live "Track In" nodes that are NOT bound to some other track (the track's own current
     *  binding is included, so the menu always shows where it currently points). */
    virtual std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId forTrack) = 0;

    /** Display name for a bound node's uuid, or an empty string when the uuid resolves to nothing
     *  in the live graph (the "Missing" / orphaned case). */
    virtual juce::String getNodeDisplayName(const juce::String& uuid) = 0;

    /** One-click re-bind. NEVER called automatically: a binding is only ever changed by an explicit
     *  user choice from the chip menu. Matching an orphaned track back onto a node BY NAME is
     *  forbidden — two nodes can share a display name and a silent re-bind would point a track at
     *  someone else's instrument (see docs/layout.md §16). */
    virtual void bindTrackTo(synth::TrackId track, const juce::String& uuid) = 0;

    /** The chip menu's "New Track In node" entry: creates a node and binds this track to it, as one
     *  compound (graph + timeline) undo step. */
    virtual void createAndBindTrackInNode(synth::TrackId track) = 0;

    /** Chip click on a track whose binding resolves: highlight that node in the graph editor's
     *  selection. A highlight only — the canvas is not scrolled and focus is not moved. */
    virtual void selectNodeInGraph(const juce::String& uuid) = 0;

    /** Right-click -> "Delete Track": removes the track AND its bound Track In node as one compound
     *  undo step. */
    virtual void deleteTrack(synth::TrackId track) = 0;

    /** Runs a doc mutation as ONE undoable timeline step. Everything the header writes (name,
     *  colour, mute, solo, arm) goes through here rather than touching the doc directly, so the
     *  header never has to know an AppUndoManager exists. */
    virtual void performTrackEdit(const std::function<void()>& mutation) = 0;

    /** The "+ MIDI Track" button at the top of the header column. Lives on this interface rather
     *  than on a separate callback so the whole header column talks to the app through one seam. */
    virtual void addMidiTrack() = 0;
};

class TimelineTrackHeaderComponent : public juce::Component {
public:
    // Fixed row height. The header column scrolls (juce::Viewport in TimelinePanelComponent) rather
    // than compressing rows, so this stays constant however many tracks there are.
    //
    // TL5-7: the themed source of truth is Theme::Metrics::timelineTrackRowHeight — TimelinePanelComponent::
    // layoutTrackHeaders() reads that (falling back to this literal when no themed LookAndFeel is
    // installed, e.g. headless tests), and TimelineClipLaneArea reads the SAME token so a track's
    // header row and its clip-lane row always land at the same y. Kept equal to the themed default
    // rather than deleted so a header built directly against a stub host (TimelineTrackHeaderTests.cpp)
    // still has a sizing constant to build against.
    static constexpr int kRowHeight = 56;

    // Menu ids. The candidate nodes take 1..N; these sit above any plausible candidate count
    // (TimelineDoc::kMaxTracks is 256, and there can be no more Track In nodes than that in a sane
    // patch) so the two ranges can never collide.
    static constexpr int kNewTrackInNodeMenuId = 1000;
    static constexpr int kDeleteTrackMenuId = 2000;

    TimelineTrackHeaderComponent(synth::TimelineDoc& doc, synth::TrackId trackId, TrackHeaderHost* host);

    synth::TrackId getTrackId() const noexcept { return trackId_; }

    /** Re-reads every displayed value from the doc. Called by TimelinePanelComponent whenever the
     *  doc notifies — the ONLY thing that refreshes a header. */
    void refreshFromDoc();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // ---- Binding chip ---------------------------------------------------------
    // The chip shows the bound node's name; it turns amber when the track is UNBOUND (never had a
    // node) or ORPHANED (had one, and it is gone) — two different messages, one warning colour.

    juce::String getBindingChipText() const { return bindingChip_.getButtonText(); }
    bool isBindingChipWarning() const noexcept { return chipWarning_; }

    /** The chip menu's contents, in menu-id order (option i has menu id i + 1). Exposed so tests
     *  drive the choice without a juce::PopupMenu, which never runs headlessly. */
    std::vector<TrackHeaderHost::BindingOption> collectBindingOptions() const;

    /** Applies a chip-menu choice: 1..N binds to collectBindingOptions()[id - 1],
     *  kNewTrackInNodeMenuId creates a node and binds to it. Anything else is ignored. */
    void applyBindingMenuChoice(int menuId);

    /** The chip's click behaviour, split from the button so tests can exercise the selection
     *  affordance without opening an async menu. `showMenu` false does the highlight only. */
    void handleChipClick(bool showMenu);

    /** Applies a header context-menu choice (kDeleteTrackMenuId today). Same test seam as
     *  applyBindingMenuChoice. */
    void applyContextMenuChoice(int menuId);

    // ---- Test accessors -------------------------------------------------------
    juce::Label& getNameLabel() noexcept { return nameLabel_; }
    juce::Button& getMuteButton() noexcept { return muteButton_; }
    juce::Button& getSoloButton() noexcept { return soloButton_; }
    juce::Button& getArmButton() noexcept { return armButton_; }
    juce::Button& getColourSwatch() noexcept { return colourSwatch_; }
    juce::Button& getBindingChip() noexcept { return bindingChip_; }
    juce::Colour getResolvedColour() const noexcept { return resolvedColour_; }

private:
    // A plain filled swatch: a juce::TextButton would route through the LookAndFeel, which the
    // headless test path doesn't install, and this needs no text at all.
    class SwatchButton : public juce::Button {
    public:
        SwatchButton()
            : juce::Button("trackColourSwatch") {}
        void paintButton(juce::Graphics& g, bool highlighted, bool /*down*/) override;
        juce::Colour colour{juce::Colours::grey};
    };

    const synth::Track* track() const { return doc_.getTrack(trackId_); }
    // Routes a doc mutation through the host (one undo step). Falls back to running it directly
    // when there is no host, so a header built for a test is still functional.
    void performEdit(const std::function<void()>& mutation);
    // Position of this track in the doc's track list — the index resolveTrackColour falls back to
    // for a track that has never been coloured. -1 when the track is gone.
    int trackIndex() const;

    void showBindingMenu();
    void showContextMenu();

    synth::TimelineDoc& doc_;
    synth::TrackId trackId_;
    TrackHeaderHost* host_ = nullptr;

    juce::Label nameLabel_;
    SwatchButton colourSwatch_;
    juce::TextButton muteButton_{"M"};
    juce::TextButton soloButton_{"S"};
    juce::TextButton armButton_{"R"};
    juce::TextButton bindingChip_;

    juce::Colour resolvedColour_{juce::Colours::grey};
    bool chipWarning_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineTrackHeaderComponent)
};

} // namespace synth::ui
