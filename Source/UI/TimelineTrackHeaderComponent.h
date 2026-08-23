#pragma once

#include "../Timeline/TimelineDoc.h"
#include "ColourPickerPopup.h"
#include "MidiDestinationPicker.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace juce {
class ApplicationProperties; // forward declaration — only a pointer crosses this header
} // namespace juce

// TimelineTrackHeaderComponent — one row in the timeline panel's track-header column.
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

    /** The "+ Track" button's MIDI entry. Lives on this interface rather than on a separate callback
     *  so the whole header column talks to the app through one seam. */
    virtual void addMidiTrack() = 0;

    /** The "+ Track" button's Audio entry: a "Track Audio" node wired into the master bus,
     *  plus an Audio-kind track bound to it, as ONE compound undo step — the exact mirror of
     *  addMidiTrack(). */
    virtual void addAudioTrack() = 0;

    /** One hosted-plugin instance parameter with no automation lane yet — the automation
     *  strip's lane picker "Add lane..." entries. `paramId` is the value a created lane would carry
     *  (a real stable id, or the synthetic "legacy:<index>" form — see
     *  HostedPluginModule::InstanceParameterInfo); `paramIndex` is what becomes the lane's
     *  paramIndexHint. */
    struct PluginLaneOption {
        juce::String nodeUuid;
        juce::String paramId;
        int paramIndex = -1;
        juce::String label; // "Module name \xC2\xB7 parameter name"
    };

    /** Every not-yet-automated hosted-plugin instance parameter across the live graph. Empty when
     *  there is nothing to offer (no HostedPluginModule with a published instance, or every one of
     *  its parameters is already automated). */
    virtual std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const = 0;

    /** Creates (find-or-create — the doc-wide one-lane-per-parameter rule may mean it already
     *  exists) the automation lane for `option` and returns its id. An invalid id means it could not
     *  be created (kMaxTracks/kMaxLanesPerTrack reached, or the option's node no longer resolves). */
    virtual synth::LaneId addPluginAutomationLane(const PluginLaneOption& option) = 0;

    /** The properties file the colour picker's favourites shelf persists to, or nullptr for an
     *  in-memory-only picker (a header built for a test, or a host that hasn't wired one up yet).
     *  Non-pure so every existing TrackHeaderHost implementer keeps compiling unchanged. */
    virtual juce::ApplicationProperties* getAppProperties() { return nullptr; }

    /** One entry in the MIDI-destinations picker: a live MIDI-instrument node the track's bound
     *  Track In node could (or already does) send MIDI to. */
    struct MidiDestinationOption {
        juce::String displayName;
        juce::uint32 nodeUid = 0;
        bool connected = false;
        // Instruments list first in the picker; everything else that consumes MIDI (e.g. an
        // envelope's gate input) goes under "Other". Derived host-side from isMidiInstrumentType.
        bool isInstrument = true;
    };

    /** Every MIDI-instrument node in the live graph the track's bound Track In node could send
     *  MIDI to, with `connected` reflecting today's actual graph wiring. Empty when the track's
     *  binding doesn't resolve (unbound or orphaned) — there is nothing to wire from. Non-pure
     *  with an inert default so every existing TrackHeaderHost implementer keeps compiling. */
    virtual std::vector<MidiDestinationOption> getMidiDestinationOptions(synth::TrackId) { return {}; }

    /** Connects or disconnects the track's bound Track In node's MIDI output from `nodeUid`, as
     *  one undoable structural change. A no-op when the track's binding or the target node no
     *  longer resolves (a stale popup must never crash). Non-pure with an inert default so every
     *  existing TrackHeaderHost implementer keeps compiling. */
    virtual void setMidiDestinationConnected(synth::TrackId, juce::uint32, bool) {}
};

class TimelineTrackHeaderComponent : public juce::Component {
public:
    // Fixed row height. The header column scrolls (juce::Viewport in TimelinePanelComponent) rather
    // than compressing rows, so this stays constant however many tracks there are.
    //
    // The themed source of truth is Theme::Metrics::timelineTrackRowHeight — TimelinePanelComponent::
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
    // Sits between the candidate range (1..N, N well under 1000 — see kNewTrackInNodeMenuId's own
    // comment) and kDeleteTrackMenuId's separate context-menu id space, so it can never collide
    // with either.
    static constexpr int kMidiDestinationsMenuId = 1001;
    static constexpr int kDeleteTrackMenuId = 2000;

    TimelineTrackHeaderComponent(synth::TimelineDoc& doc, synth::TrackId trackId, TrackHeaderHost* host);

    synth::TrackId getTrackId() const noexcept { return trackId_; }

    /** Re-reads every displayed value from the doc. Called by TimelinePanelComponent whenever the
     *  doc notifies — the ONLY thing that refreshes a header. */
    void refreshFromDoc();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    // Re-derives every colour this component bakes via setColour (the binding chip, M/S/R active
    // states) from the newly-installed LookAndFeel — see applyThemeDerivedColours(). Without this,
    // a theme switch left the chip and the M/S/R active colours frozen on whatever theme was
    // active at the last refreshFromDoc() call.
    void lookAndFeelChanged() override;

    // ---- Binding chip ---------------------------------------------------------
    // The chip shows the bound node's name; it turns amber when the track is UNBOUND (never had a
    // node) or ORPHANED (had one, and it is gone) — two different messages, one warning colour.

    juce::String getBindingChipText() const { return bindingChip_.getButtonText(); }
    bool isBindingChipWarning() const noexcept { return chipWarning_; }

    // ---- Track-kind badge ------------------------------------------------------
    // A fixed per-TrackKind label ("MIDI" / "AUD" / "AUTO") drawn in the top row, right of the
    // colour swatch. Not editable and not doc-driven beyond the kind itself, so there is no
    // corresponding setter — refreshFromDoc() just repaints when the kind can't have changed anyway
    // (a track's kind, unlike its name or colour, never changes after creation).
    juce::String getKindBadgeTextForTest() const;
    juce::Rectangle<int> getKindBadgeBoundsForTest() const noexcept { return kindBadgeBounds_; }
    // The Icon actually drawn for the current TrackKind, or -1 when there is no themed
    // AppLookAndFeel (or the asset library is absent) and paint() fell back to the text badge —
    // matches getKindBadgeTextForTest's "value or empty" idiom rather than std::optional, since
    // every other test accessor here returns a plain value.
    int getKindBadgeIconForTest() const;

    // ---- Automation open/close button -------------------------------------------
    // Visible only when the track has at least one automation lane. The header never decides
    // open-vs-close itself — it just reports the click and TimelinePanelComponent (which owns the
    // single automation strip) works out whether that means "open this track's lane" or "close the
    // strip that's already showing it".
    std::function<void(synth::TrackId)> onAutomationToggleRequested;

    /** The chip menu's contents, in menu-id order (option i has menu id i + 1). Exposed so tests
     *  drive the choice without a juce::PopupMenu, which never runs headlessly. */
    std::vector<TrackHeaderHost::BindingOption> collectBindingOptions() const;

    /** Applies a chip-menu choice: 1..N binds to collectBindingOptions()[id - 1],
     *  kNewTrackInNodeMenuId creates a node and binds to it, kMidiDestinationsMenuId opens the
     *  MIDI-destinations picker. Anything else is ignored. */
    void applyBindingMenuChoice(int menuId);

    /** Whether showBindingMenu() would add the "MIDI destinations..." entry — a synchronous query
     *  seam for the same rule the (async, non-headless) juce::PopupMenu build uses, so a test can
     *  assert on it without opening a real menu. */
    bool offersMidiDestinationsMenuEntryForTest() const;

    /** Replaces what kMidiDestinationsMenuId's chip-menu choice does, in place of the real
     *  openMidiDestinationsPicker() (which launches a juce::CallOutBox and so cannot run in a
     *  headless test). Defaults to the real behaviour; a test installs its own hook to observe
     *  the "open" event without a live callout. */
    void setOpenMidiDestinationsPickerHookForTest(std::function<void()> hook) {
        openMidiDestinationsPickerHook_ = hook ? std::move(hook) : [this] { openMidiDestinationsPicker(); };
    }

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
    juce::Button& getAutomationButton() noexcept { return automationButton_; }
    juce::Colour getResolvedColour() const noexcept { return resolvedColour_; }

    /** Builds a ColourPickerPopup wired with the EXACT same onPreview/onCommit callbacks the real
     *  colour-swatch click uses (see the constructor), but never launches a juce::CallOutBox —
     *  a test drives it directly via setCurrentColourForTest()/commitForTest() instead of going
     *  through a popup menu that cannot run headlessly. */
    std::unique_ptr<synth::ui::ColourPickerPopup> createColourPickerForTest();

    /** Builds a MidiDestinationPicker wired with the EXACT same provider/apply callbacks the real
     *  "MIDI destinations..." menu entry uses (see openMidiDestinationsPicker()), but never
     *  launches a juce::CallOutBox — mirrors createColourPickerForTest()'s split. */
    std::unique_ptr<synth::ui::MidiDestinationPicker> createMidiDestinationPickerForTest();

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

    // Shared by the real swatch click and createColourPickerForTest(): builds the popup with the
    // preview-writes-directly / commit-restores-then-one-undo-step wiring described on the class'
    // colour-swatch behaviour, WITHOUT launching it in a juce::CallOutBox.
    std::unique_ptr<synth::ui::ColourPickerPopup> buildColourPicker();

    // The "MIDI destinations..." menu entry's handler: builds the picker via
    // createMidiDestinationPickerForTest()'s exact same builder and launches it in a
    // juce::CallOutBox anchored on the binding chip.
    void openMidiDestinationsPicker();
    // Shared by openMidiDestinationsPicker() and createMidiDestinationPickerForTest() — builds a
    // MidiDestinationPicker whose provider/apply callbacks route through host_, WITHOUT launching
    // it in a juce::CallOutBox.
    std::unique_ptr<synth::ui::MidiDestinationPicker> buildMidiDestinationPicker();

    // Re-applies every colour derived from the active theme: the binding chip's warning/normal
    // colours (unchanged from before) PLUS the M/S/R buttons' active-state colours. Called from
    // refreshFromDoc() (so a doc-driven repaint always shows the right colours), from
    // lookAndFeelChanged() (so a theme switch alone — no doc change — also updates them), and once
    // at the end of the constructor (so the very first paint isn't relying on refreshFromDoc()
    // having been called with a LookAndFeel already installed).
    void applyThemeDerivedColours();

    synth::TimelineDoc& doc_;
    synth::TrackId trackId_;
    TrackHeaderHost* host_ = nullptr;

    // Set in the constructor to `[this] { openMidiDestinationsPicker(); }`; a test replaces it via
    // setOpenMidiDestinationsPickerHookForTest() so applyBindingMenuChoice(kMidiDestinationsMenuId)
    // is exercisable without a live juce::CallOutBox.
    std::function<void()> openMidiDestinationsPickerHook_;

    juce::Label nameLabel_;
    SwatchButton colourSwatch_;
    juce::TextButton muteButton_{"M"};
    juce::TextButton soloButton_{"S"};
    juce::TextButton armButton_{"R"};
    juce::TextButton automationButton_{"A"};
    juce::TextButton bindingChip_;

    juce::Colour resolvedColour_{juce::Colours::grey};
    bool chipWarning_ = false;
    // Set in resized(); the kind badge itself is drawn straight from track()->kind in paint(), so
    // this is only a hit-rect for tests, not a cache of the badge's text.
    juce::Rectangle<int> kindBadgeBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineTrackHeaderComponent)
};

} // namespace synth::ui
