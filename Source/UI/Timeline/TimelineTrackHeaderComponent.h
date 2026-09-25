#pragma once

#include "ChannelChipComponent.h"
#include "MidiDestinationPicker.h"
#include "Mixer/TrackPresetManager.h"
#include "Plugin/Hosting/HostedPluginBackend.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TrackChannelLinkSurface.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace juce {
class ApplicationProperties; // forward declaration — only a pointer crosses this header
} // namespace juce

class ShortcutManager; // forward declaration (Source/ShortcutManager/ShortcutManager.h) — same non-owning,
                       // may-stay-null pointer pattern as TimelinePanelComponent's own copy.

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

    /** A parameter's display name, or empty when it doesn't resolve (callers fall back to paramId). */
    virtual juce::String getParameterDisplayName(const juce::String& /*uuid*/, const juce::String& /*paramId*/) {
        return {};
    }

    /** One-click re-bind. NEVER called automatically: a binding is only ever changed by an explicit
     *  user choice from the chip menu. Matching an orphaned track back onto a node BY NAME is
     *  forbidden — two nodes can share a display name and a silent re-bind would point a track at
     *  someone else's instrument (see docs/timeline/tracks.md#a-binding-is-never-re-established-automatically). */
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

    /** The "+ Track" button's Instrument submenu (T183/P9-3b): `instrumentModuleType` ("Oscillator",
     *  "Wavetable" or "Sampler") wired as Track In -> instrument -> default chain (EQ/Compressor
     *  bypassed -> Channel Strip Stereo) -> Master, plus a Midi-kind track bound to the Track In, as
     *  ONE compound undo step — the exact mirror of addAudioTrack(). `poly` (FRO48; Oscillator/
     *  Wavetable only, ignored for Sampler which has no "poly" parameter) turns the new instrument's
     *  poly mode on before the rest of the wiring runs, so the poly-envelope auto-wire (P9-3j) picks
     *  it up. */
    virtual void addInstrumentTrack(const juce::String& instrumentModuleType, bool poly) = 0;

    /** FRO26 (P9-3e, docs/mixer/mixer.md#creating-channels-in-an-existing-project): true when at least one track's
     * chain still reaches the output without passing through a ChannelStripModule — the "+ Track" menu's "Create
     * Channels" entry is enabled exactly when this is true. Non-pure with an inert `false` default so every existing
     * TrackHeaderHost implementer (test stubs included) keeps compiling. */
    virtual bool hasTracksNeedingChannels() const { return false; }

    /** FRO26 (P9-3e, docs/mixer/mixer.md#creating-channels-in-an-existing-project): the "+ Track" menu's "Create
     * Channels" entry — wraps every channel-less track's chain into a mixer channel (docs/mixer/mixer.md's factory
     * default: EQ -> Compressor, both bypassed -> Channel Strip -> Master), as ONE undo step covering every track it
     * touches. A track that already has a channel is left untouched; a no-op (nothing pushed to the undo stack) when
     * hasTracksNeedingChannels() would return false. Non-pure with an inert no-op default so every existing
     * TrackHeaderHost implementer keeps compiling. */
    virtual void createChannelsForExistingTracks() {}

    /** FRO25 (P9-3d, docs/mixer/mixer.md#make-channel-and-shared-modules): true when the header menu's "Make Channel"
     * would build something for `track` — its bound node's chain has no Channel Strip of its own yet (or merges into a
     * shared module that has none). Non-pure with an inert `false` default, for the same reason
     * `hasTracksNeedingChannels()` above uses one. */
    virtual bool canMakeChannelForTrack(synth::TrackId track) const {
        juce::ignoreUnused(track);
        return false;
    }

    /** FRO25 (P9-3d, docs/mixer/mixer.md#make-channel-and-shared-modules): the header menu's "Make Channel" — gathers
     * the track's exclusive chain into a channel macro with the default EQ -> Compressor -> Channel Strip -> Master
     * chain as ONE undo step (a merge point becomes its own bus channel). A no-op when canMakeChannelForTrack() is
     * false. Non-pure with an inert no-op default. */
    virtual void makeChannelForTrack(synth::TrackId track) { juce::ignoreUnused(track); }

    /** FRO42 (P9-3h): instrument-capable hosted plugins for the "+ Track -> Instrument -> Plugin"
     *  submenu — every scanned plugin whose juce::PluginDescription::isInstrument is true, sorted by
     *  name like the library sidebar's own list (effects are never offered here, and neither is this
     *  app's own VST3/AU build — see MainComponent::getInstrumentPluginOptions). A pure read of
     *  whatever PluginScanService currently knows — never triggers a scan itself; see
     *  ensureInstrumentPluginsScanned() below for that. Non-pure with an inert empty-list default so
     *  every existing TrackHeaderHost implementer keeps compiling unchanged (same reasoning as
     *  getMidiDestinationOptions below). */
    virtual std::vector<synth::PluginIdentity> getInstrumentPluginOptions() const { return {}; }

    /** True while a plugin scan is running. The menu shows a disabled "Scanning for plugins..." row
     *  instead of an empty submenu while this holds and getInstrumentPluginOptions() above is still
     *  empty (once it finds anything, whether or not the scan is still going, the real rows show
     *  instead — same "list is visible immediately, regardless of scan state" contract
     *  PluginScanService::ensureScanned documents). Non-pure default: never scanning. */
    virtual bool isPluginScanInProgress() const { return false; }

    /** Kicks off (or no-ops past the first call this session — see PluginScanService::
     *  ensureScanned) the scan that populates getInstrumentPluginOptions() above. Called once when
     *  the "+ Track" menu opens, never on every keystroke or click. MainComponent delegates to its
     *  own maybeStartEagerPluginScan(), so opening this menu inside a hosted DAW build never
     *  re-launches the host binary — the same guard the eager startup scan and the library
     *  sidebar's manual "Scan for plugins..." row both already honour. Non-pure no-op default. */
    virtual void ensureInstrumentPluginsScanned() {}

    /** FRO42 (P9-3h): "+ Track -> Instrument -> Plugin -> <name>" — builds the same
     *  Track In -> instrument -> default chain (EQ bypassed -> Compressor bypassed -> Channel
     *  Strip) -> Master flow addInstrumentTrack() builds for a factory instrument, boxed into one
     *  collapsed macro, as ONE undo step — except the instrument is a hosted plugin instance loaded
     *  ASYNCHRONOUSLY rather than a factory module created on the spot. Nothing touches the graph or
     *  the undo stack until the load either succeeds (the whole chain lands in one step) or fails /
     *  the identity no longer resolves (nothing lands at all — see MainComponent's implementation).
     *  No ADSR+VCA is added: a hosted synth has its own envelope. Non-pure no-op default, same
     *  reasoning as the two methods above. */
    virtual void addInstrumentPluginTrack(const synth::PluginIdentity& identity) { juce::ignoreUnused(identity); }

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

    /** NOTE AUDITION — sends one preview note-on (`noteOn` true) or note-off into the track's bound
     *  Track In node, so it reaches exactly the destination modules the track's clips play through
     *  (they are graph connections FROM that node — see setMidiDestinationConnected above). Driven by
     *  the piano roll's note click (PianoRollComponent::onAuditionNote, forwarded by
     *  TimelinePanelComponent). A no-op when the track's binding no longer resolves — clicking a note
     *  on an unbound track must be silent, never a crash.
     *
     *  Called on the MESSAGE thread; the implementation hands the event to the node through a
     *  wait-free FIFO (TimelineMidiSourceModule::pushAuditionNote) rather than touching the audio
     *  thread. Non-pure with an inert default so every existing TrackHeaderHost implementer — and
     *  every test stub — keeps compiling. */
    virtual void auditionTrackNote(synth::TrackId, int /*pitch*/, int /*velocity*/, bool /*noteOn*/) {}

    /** FRO13 (P9-7, docs/mixer/track-presets.md): true when `track`'s bound node has a channel of its own
     *  (its macro boxes a Channel Strip) — gates the header menu's "Save track as preset.../Set as
     *  default" pair, same disabled-not-hidden precedent canMakeChannelForTrack states. Non-pure
     *  with an inert `false` default. */
    virtual bool canSaveTrackPresetForTrack(synth::TrackId track) const {
        juce::ignoreUnused(track);
        return false;
    }

    /** Opens the naming prompt and saves `track`'s channel (plus every outside module feeding it
     *  through a port) as a track preset. A no-op when canSaveTrackPresetForTrack() is false.
     *  Non-pure no-op default. */
    virtual void saveTrackAsPreset(synth::TrackId track) { juce::ignoreUnused(track); }

    /** One-click "Set as default for <type>": saves (auto-naming if nothing was saved yet this
     *  session) and writes the per-type Preferences -> Mixer default in one step. A no-op when
     *  canSaveTrackPresetForTrack() is false. Non-pure no-op default. */
    virtual void setTrackPresetAsDefault(synth::TrackId track) { juce::ignoreUnused(track); }

    /** The "+ Track" menu's grouped preset-list click: inserts the named saved preset as a new
     *  track, independent of the per-type default. Non-pure no-op default. */
    virtual void addTrackFromPreset(const juce::String& presetName, synth::TrackPresetKind kind) {
        juce::ignoreUnused(presetName, kind);
    }

    /** The "+ Track" menu's "Insert Track Preset from File..." entry: a native chooser filtered to
     *  `*.agtrackpreset`, then the same insert path as addTrackFromPreset(). Non-pure no-op
     *  default. */
    virtual void addTrackFromPresetFile() {}

    /** FRO14 (P9-4, docs/mixer/mixer.md#channels-follow-audio-not-tracks): everything the header needs about the
     * CHANNEL its track plays into -- the channel chip, and the linked-track name/colour/mute/solo fan-out. ONE
     *  accessor rather than a method per feature, so this interface (and MainComponent, which
     *  implements it) does not grow one line per channel behaviour; the real surface is
     *  TrackChannelLinkSurface. Non-pure with a null default: a header built against a stub host
     *  simply has no channel behaviour, and every existing implementer keeps compiling. */
    virtual TrackChannelLinkSurface* getChannelLinkSurface() { return nullptr; }
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
    // FRO25 (P9-3d): the header context menu's "Make Channel", same id space as kDeleteTrackMenuId.
    static constexpr int kMakeChannelMenuId = 2001;
    // FRO13 (P9-7): "Save track as preset.../Set as default", same id space.
    static constexpr int kSaveTrackPresetMenuId = 2002;
    static constexpr int kSetTrackPresetDefaultMenuId = 2003;

    TimelineTrackHeaderComponent(synth::TimelineDoc& doc, synth::TrackId trackId, TrackHeaderHost* host);

    synth::TrackId getTrackId() const noexcept { return trackId_; }

    /** Re-reads every displayed value from the doc. Called by TimelinePanelComponent whenever the
     *  doc notifies — the ONLY thing that refreshes a header. */
    void refreshFromDoc();

    void paint(juce::Graphics& g) override;
    // T161: the per-row keyboard-focus outline (see setWantsKeyboardFocus below) — painted OVER
    // children for the same reason TimelinePanelComponent's own region-outline override is: the
    // colour swatch and the M/S/R/A toggles sit flush against this row's left/right edges, so an
    // outline drawn in paint() would be hidden under them.
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    // T166: whole-row drag-to-reorder (see onRowDragStarted below for why this row only ever
    // reports raw screen positions rather than trying to reorder anything itself).
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    // Re-derives every colour this component bakes via setColour (the binding chip, M/S/R active
    // states) from the newly-installed LookAndFeel — see applyThemeDerivedColours(). Without this,
    // a theme switch left the chip and the M/S/R active colours frozen on whatever theme was
    // active at the last refreshFromDoc() call.
    void lookAndFeelChanged() override;

    // ---- T161: keyboard focus + M/S/R shortcuts -------------------------------
    //
    // A row is a real focusable leaf (setWantsKeyboardFocus(true) in the constructor, matching
    // TimelineClipLaneArea/PianoRollComponent's own pattern) rather than a virtual row painted by
    // one big component (contrast ModuleLibraryComponent's T160 row nav) — TimelineTrackHeaderComponent
    // already IS one-component-per-track. Up/Down and M/S/R only ever reach keyPressed() below while
    // THIS row genuinely holds real OS keyboard focus, by JUCE's own key-dispatch rule — no extra
    // "am I the focused one" guard is needed or wanted (see docs/timeline/tracks.md#keyboard-focus-and-msr).
    bool keyPressed(const juce::KeyPress& key) override;
    // The four toggle buttons opt OUT of taking focus for themselves (juce::Button opts in by
    // default) — otherwise clicking M/S/R/A would silently move real focus off the row and onto the
    // button, exactly the trap TimelinePanelComponent's own tool-strip buttons avoid the same way.
    // nameLabel_ is deliberately excluded: it needs its own focus machinery for double-click rename.
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;
    // Because the toggle buttons opt out of focus (see above) this fires only for the name label
    // (mid-rename) and the binding chip — repaints the row's outline either way, since
    // hasKeyboardFocus(true) (what paintOverChildren checks) includes descendants.
    void focusOfChildComponentChanged(juce::Component::FocusChangeType cause) override;

    /** Non-owning; may stay null (falls back to the hardcoded bare m/s/r defaults, same contract as
     *  every other surface-resolved key in this app). Forwarded by TimelinePanelComponent to every
     *  header it owns (setShortcutManager both propagates to existing rows and is re-applied to a
     *  freshly built one in syncTrackHeaders()). */
    void setShortcutManager(ShortcutManager* manager) { shortcuts_ = manager; }

    /** Fired from keyPressed() on a bare Up/Down that this row claims (direction -1/+1) — the row
     *  cannot move focus to a sibling itself (it is deliberately host/graph/panel-free), so it hands
     *  the direction back for TimelinePanelComponent::moveFocusedTrack to resolve. Always returns
     *  true from keyPressed() regardless of whether this is wired (a row built directly, e.g. by a
     *  test, still "owns" the key while it has focus). */
    std::function<void(int direction)> onFocusMoveRequested;

    /** Fired from mouseDown() on anything that isn't a right-click (a background click, or one that
     *  lands on the row rather than being consumed by a child) — deliberately a callback rather than
     *  relying on the real focusGained() notification round-trip: grabKeyboardFocus() is a best-effort
     *  no-op without a native peer (headless tests included — see docs/control/shortcuts.md's Focus regions
     *  section), so TimelinePanelComponent::focusedTrackIndex_ has to be told directly rather than
     *  waiting on an OS focus event that may never arrive. */
    std::function<void()> onSelectRequested;

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

    // ---- T166: whole-row drag-to-reorder -----------------------------------------
    //
    // "Whole row" on purpose: a small dedicated handle is too fiddly a target for dragging a track
    // (see the ticket) — everything except the interactive children (name label, swatch, M/S/R/A
    // buttons, binding chip; each is its own child Component and intercepts its own mouseDown) is a
    // drag surface. This row is deliberately sibling-blind (it never sees the other rows or the
    // scroll state — see the class comment), so it hands raw SCREEN Y positions up rather than
    // computing an insertion index itself; TimelinePanelComponent (which owns the ordered header
    // list) is the one place that can turn a Y position into "between which two tracks". Modelled
    // on PanelResizeHandle's own screen-coordinate drag (Source/UI/Layout/PanelResizeHandle.h).
    //
    // Fired once a background mouseDrag crosses a small pixel threshold past mouseDown — a plain
    // click (select) or a click that lands on a child component never reaches here.
    std::function<void(int screenY)> onRowDragStarted;
    // Fired on every further mouseDrag once a drag is underway.
    std::function<void(int screenY)> onRowDragged;
    // Fired from mouseUp, but ONLY when a drag was actually underway (never for a plain click).
    //
    // ORDERING HAZARD: this is the last thing mouseUp does, and for good reason. The mutation this
    // callback triggers (TimelinePanelComponent::endTrackDrag -> TimelineDoc::moveTrack) changes
    // which TrackId sits at which index, which makes TimelinePanelComponent::syncTrackHeaders()
    // rebuild the ENTIRE header column — destroying this very row from inside its own mouseUp
    // stack frame. Every write this component makes to its own state must happen BEFORE this call;
    // nothing may touch `this` after it returns.
    std::function<void(int screenY)> onRowDragEnded;

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

    /** FRO14: one meter tick for the channel chip, driven by TimelinePanelComponent's single 15 Hz
     *  timer rather than a per-row one (see ChannelChipComponent's header). Returns true only when
     *  the chip actually repainted -- the gate that keeps this off the "unconditional per-tick
     *  repaint" list. A no-op (false) for a row with no channel chip showing. */
    bool tickChannelMeter();

    /** Applies a header context-menu choice (kDeleteTrackMenuId today). Same test seam as
     *  applyBindingMenuChoice. */
    void applyContextMenuChoice(int menuId);

    /** FRO25: the header context menu (Make Channel, Delete Track) as showContextMenu() builds it —
     *  item ids are the k*MenuId constants above, fed back through applyContextMenuChoice(). */
    juce::PopupMenu buildContextMenu() const;

    /** Test seam, same shape as ModuleComponent::setShowContextMenuHookForTest: a real right-click
     *  mouseDown() builds the menu and hands it here instead of showing it. Null = the real menu. */
    void setShowContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showContextMenuHook_ = std::move(hook);
    }

    // ---- Test accessors -------------------------------------------------------
    juce::Label& getNameLabel() noexcept { return nameLabel_; }
    juce::Button& getMuteButton() noexcept { return muteButton_; }
    juce::Button& getSoloButton() noexcept { return soloButton_; }
    juce::Button& getArmButton() noexcept { return armButton_; }
    juce::Button& getColourSwatch() noexcept { return colourSwatch_; }
    juce::Button& getBindingChip() noexcept { return bindingChip_; }
    ChannelChipComponent& getChannelChip() noexcept { return channelChip_; }
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

    // FRO60: JUCE hands a click to whichever child component is directly under the cursor and never
    // bubbles it up to an ancestor on its own — mouseDown() below only ever sees a right-click that
    // lands on this row's own background pixels. nameLabel_ and the M/S/R/A toggles cover almost
    // every OTHER pixel of the row and have no popup menu of their own, so a right-click there used
    // to be swallowed silently instead of ever reaching showContextMenu() (found live in FRO25's
    // check: 4 real right-click spots that a direct `header.mouseDown(...)` test can't catch, since
    // that bypasses the child entirely). This subclass is the fix for the label: a right-click goes
    // straight to the SAME showContextMenu() a right-click on the background uses; anything else is
    // untouched juce::Label behaviour. The binding chip and the colour swatch are deliberately left
    // as plain buttons — they already open a menu/picker of their own on click, and that stays (see
    // docs/timeline/tracks.md#row-context-menu).
    class ContextMenuForwardingLabel : public juce::Label {
    public:
        explicit ContextMenuForwardingLabel(TimelineTrackHeaderComponent& owner)
            : owner_(owner) {}
        void mouseDown(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) {
                owner_.showContextMenu();
                return;
            }
            juce::Label::mouseDown(e);
        }

    private:
        TimelineTrackHeaderComponent& owner_;
    };

    // Same fix as ContextMenuForwardingLabel, for the M/S/R/A toggle buttons — plus one thing those
    // need that the label doesn't: juce::Button (unlike juce::Slider, which already checks
    // `e.mods.isPopupMenu()` itself before starting a value-drag — see juce_Slider.cpp) fires
    // onClick from ANY mouse button. Left un-forwarded, a bare juce::TextButton here would have BOTH
    // opened the context menu AND toggled mute/solo/arm/automation on the very same right-click.
    // Never calling the base class's mouseDown() for a popup click keeps Button::isDown() false for
    // the whole gesture, so the matching mouseUp() (still the plain juce::TextButton one — no
    // override needed) finds `wasDown` false and skips the click it would otherwise fire.
    class ContextMenuForwardingButton : public juce::TextButton {
    public:
        ContextMenuForwardingButton(TimelineTrackHeaderComponent& owner, const juce::String& text)
            : juce::TextButton(text)
            , owner_(owner) {}
        void mouseDown(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) {
                owner_.showContextMenu();
                return;
            }
            juce::TextButton::mouseDown(e);
        }

    private:
        TimelineTrackHeaderComponent& owner_;
    };

    const synth::Track* track() const { return doc_.getTrack(trackId_); }
    // Routes a doc mutation through the host (one undo step). Falls back to running it directly
    // when there is no host, so a header built for a test is still functional.
    void performEdit(const std::function<void()>& mutation);
    // T161: read-flip-write for M/S/R, shared by the toggle buttons' own onClick and keyPressed()'s
    // bare m/s/r — one path so a button click and a keystroke can never disagree about what
    // "toggle" means. No-op when the track is gone (mid-delete race).
    void toggleMuted();
    void toggleSoloed();
    void toggleArmed();
    // Position of this track in the doc's track list — the index resolveTrackColour falls back to
    // for a track that has never been coloured. -1 when the track is gone.
    int trackIndex() const;

    void showBindingMenu();
    void showContextMenu();

    // FRO14: the app's channel surface, or null (no host, or a host that wires none -- a test
    // stub). Every caller re-asks rather than caching: one cable drag can form or break the link.
    synth::ui::TrackChannelLinkSurface* linkSurface() const {
        return host_ != nullptr ? host_->getChannelLinkSurface() : nullptr;
    }
    // FRO14: the channel state refreshFromDoc() last read, so paint/resized and the M/S toggles
    // agree with what is on screen without each re-walking the graph.
    TrackChannelLinkSurface::ChannelInfo channelInfo_;

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

    // T161: bare m/s/r resolution — the same "unset binding has no key at all, once a manager is
    // installed" contract every other surface action in this app follows. See matchesAction().
    bool matchesAction(const juce::KeyPress& key, const juce::String& actionId, const juce::KeyPress& fallback) const;

    synth::TimelineDoc& doc_;
    synth::TrackId trackId_;
    TrackHeaderHost* host_ = nullptr;
    // Non-owning; may stay null. LIFETIME: unlike TimelinePanelComponent's own copy of this pointer,
    // this component never subscribes as a juce::ChangeListener on it (no cached tooltip to
    // refresh), so there is no matching teardown-ordering hazard to document here.
    ShortcutManager* shortcuts_ = nullptr;

    // Set in the constructor to `[this] { openMidiDestinationsPicker(); }`; a test replaces it via
    // setOpenMidiDestinationsPickerHookForTest() so applyBindingMenuChoice(kMidiDestinationsMenuId)
    // is exercisable without a live juce::CallOutBox.
    std::function<void()> openMidiDestinationsPickerHook_;
    // FRO25: see setShowContextMenuHookForTest. Null = show the real async menu.
    std::function<void(juce::PopupMenu&)> showContextMenuHook_;

    // FRO60: see ContextMenuForwardingLabel/ContextMenuForwardingButton above — these four are the
    // children with NO popup menu of their own, so a right-click on them forwards to
    // showContextMenu(). bindingChip_ keeps its own Track In menu and colourSwatch_ (SwatchButton)
    // keeps its own colour picker — neither changes here.
    ContextMenuForwardingLabel nameLabel_{*this};
    SwatchButton colourSwatch_;
    ContextMenuForwardingButton muteButton_{*this, "M"};
    ContextMenuForwardingButton soloButton_{*this, "S"};
    ContextMenuForwardingButton armButton_{*this, "R"};
    ContextMenuForwardingButton automationButton_{*this, "A"};
    juce::TextButton bindingChip_;
    // FRO14: the CHANNEL chip -- what this track's audio ends up in, as opposed to bindingChip_'s
    // "which node feeds it". Hidden whenever the track reaches no channel yet.
    ChannelChipComponent channelChip_;

    juce::Colour resolvedColour_{juce::Colours::grey};
    bool chipWarning_ = false;
    // T166: true from the moment a background mouseDrag crosses the reorder threshold (see
    // onRowDragStarted) until the matching mouseUp. Reset to false BEFORE onRowDragEnded fires —
    // see that callback's own ordering-hazard comment.
    bool draggingRow_ = false;
    // Set in resized(); the kind badge itself is drawn straight from track()->kind in paint(), so
    // this is only a hit-rect for tests, not a cache of the badge's text.
    juce::Rectangle<int> kindBadgeBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineTrackHeaderComponent)
};

} // namespace synth::ui
