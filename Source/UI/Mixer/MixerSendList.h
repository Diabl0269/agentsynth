#pragma once

#include "MacroSet.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerSends/MixerSends.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class AppUndoManager;
class GraphEditor;

// MixerSendList.h -- FRO15 (P9-9, docs/mixer/sends-and-buses.md): a column's send rows, sibling of
// MixerInsertList and laid out directly under it.
//
// One row per ACTIVE slot, in slot order: the target bus's name (click to retarget -- the same menu
// now also carries a ticked "Mono" item, FRO294), a small rotary level knob, a small rotary pan knob
// (FRO294), an "M" mute toggle (FRO295), a PRE/POST toggle and an `x` remove; then a "+ Send" row
// while the strip has a free slot. Each mutation is ONE AppUndoManager::recordGraphAndMacroChange
// around synth::MixerSends' Core flows (which have no undo of their own), exactly as MixerInsertList
// wraps the insert splices.
//
// The level and pan knobs attach straight onto the strip's own `sendNLevel`/`sendNPan`
// AudioParameterFloats, so both are host-visible and automatable with no lane plumbing of their
// own. Those attachments are live pointers into a graph node's parameters, which makes
// unbindFromGraph() below load-bearing: without it, an undo that REPLACES the graph frees the
// parameters this list is still attached to.
namespace synth::ui {

class MixerSendList : public juce::Component {
public:
    MixerSendList();
    ~MixerSendList() override;

    /** Must be called once before setEntries() -- these outlive every entry set on this list. */
    void configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                   GraphEditor& graphEditor);

    void setEntries(const std::vector<synth::MixerSendEntry>& entries, juce::AudioProcessorGraph::NodeID stripNodeId);

    /** Fires after a mutation changed the graph -- the caller runs its own post-topology-change
     *  reconcile, same contract as MixerInsertList::onMutated. */
    std::function<void()> onMutated;

    /** FRO133: fires once per row from rebuildKnobs(), right after that row's knob/attachment are
     *  created against a resolved sendNLevel parameter -- lets the owning MixerColumnComponent
     *  register the knob in its own MIDI-learn registry
     * (docs/control/midi-remote-ui.md#right-click-midi-learn--coverage) without this list needing a menu/registry of
     * its own. Never fires for a row whose param didn't resolve (the same "nothing to register" case every other
     * registration site skips). */
    std::function<void(juce::Slider&, juce::RangedAudioParameter*)> onSendKnobBuilt;

    /** "+ Send > New bus..." -- creates a bus channel (its own undo step) and returns its strip's
     *  node id, or an invalid id on failure. Supplied by MixerPanelComponent, which is the one that
     *  can size canvas cards. */
    std::function<juce::AudioProcessorGraph::NodeID()> createBus;

    /** FRO296: drag-reorder a row -- `fromRow`/`toRow` are VISIBLE row indices (positions in
     *  `entries_`, same addressing as removeRow/retargetRow above). Supplied by MixerPanelComponent,
     *  the one component that owns both the AppUndoManager (for ONE
     *  recordGraphTimelineAndMacroChange step, docs/mixer/sends-and-buses.md#reordering-sends) and
     *  the TimelineDoc (to replay the same slot-swap sequence against any automation lane on a moved
     *  send) -- this list stays TimelineDoc-free, same pattern as createBus staying canvas-free.
     *  Returns whether anything actually changed (false for `fromRow == toRow` or a refused swap),
     *  which is what moveRow() below uses to decide whether to fire onMutated(). */
    std::function<bool(juce::AudioProcessorGraph::NodeID, int fromRow, int toRow)> moveSendRow;

    int getPreferredHeight() const noexcept;

    /** FRO15: drops every SliderParameterAttachment and this list's own graph pointers -- called
     *  from MixerColumnComponent::unbindFromGraph(), i.e. BEFORE a graph-replacing mutation frees
     *  the parameters those attachments point at. Idempotent and null-safe. */
    void unbindFromGraph();

    /** Counts only an unbindFromGraph() that actually detached a LIVE parameter attachment, never a
     *  defensive no-op on an already-unbound list -- the same seam (and the same reason)
     *  MixerFader::getLiveUnbindCallCountForTest exists for: it lets a test prove the pre-restore
     *  hook really ran and did real work, rather than that the process merely didn't crash. */
    static int getLiveUnbindCallCountForTest() noexcept { return liveUnbindCalls_; }

    // ---- Headless test seams. juce::PopupMenu never runs in a test process (see
    // docs/development/test-patterns.md), so the menu callbacks below call these same real methods.
    // Row indices address the VISIBLE rows, i.e. positions in `entries_`, not slot numbers.
    void addSendTo(juce::AudioProcessorGraph::NodeID target);
    /** FRO318: either target kind -- the "Key: ..." menu items call this with `key` set. */
    void addSendTo(const synth::SendTarget& target);
    void removeRow(int rowIndex);
    void togglePreFaderForRow(int rowIndex);
    /** FRO295: flips row `rowIndex`'s mute bit via synth::setSendMuted, one recordGraphAndMacroChange. */
    void toggleMuteForRow(int rowIndex);
    /** FRO294: flips row `rowIndex`'s mono bit via synth::setSendMono, one recordGraphAndMacroChange
     *  -- the target menu's "Mono" item and this class's own headless test seam both call this. */
    void toggleMonoForRow(int rowIndex);
    void retargetRow(int rowIndex, juce::AudioProcessorGraph::NodeID target);
    void retargetRow(int rowIndex, const synth::SendTarget& target);
    /** FRO296: the real mutation behind a completed drag -- calls the moveSendRow callback and, on
     *  success, onMutated(), same shape as every other row mutation's mutateAndNotify. A synthesized
     *  test drag (mouseDown/mouseDrag past the threshold/mouseUp on the name area) reaches this
     *  through the real mouse path; a test that wants to skip the gesture can call it directly. */
    void moveRow(int fromRow, int toRow);
    std::vector<juce::AudioProcessorGraph::NodeID> availableTargets() const;
    /** FRO318: modules whose Key input this strip may feed (synth::enumerateKeySendTargets). */
    std::vector<juce::AudioProcessorGraph::NodeID> availableKeyTargets() const;
    bool canAddSend() const;
    bool isAttachedForTest(int rowIndex) const;
    juce::Slider* getKnobForTest(int rowIndex) const;
    /** FRO294: the row's pan knob, attached to sendNPan the same way getKnobForTest's level knob
     *  attaches to sendNLevel. */
    juce::Slider* getPanKnobForTest(int rowIndex) const;
    /** The row's real "M" button -- a juce::Button child, same type/LookAndFeel as the column's own
     *  channel mute button, so a test can click it through the real mouse path. */
    juce::Button* getMuteButtonForTest(int rowIndex) const;
    int getRowCountForTest() const noexcept { return (int)entries_.size(); }
    /** FRO228: the transparent, name-only "Add send" proxy -- see AddSendAccessibilityProxy's own
     *  comment. Always exists; only actually reachable (setVisible(true)) while canAddSend(). */
    juce::Component& getAddSendAccessibilityComponentForTest() noexcept { return addSendProxy_; }

    /** FRO296: same seam as MixerColumnComponent::setShowContextMenuHookForTest -- juce::PopupMenu
     *  never runs in a test process (docs/development/test-patterns.md), so a test overrides this to
     *  inspect the built menu (item count, an item's text) or invoke an item's action directly,
     *  instead of the real showMenuAsync(). A null hook restores the real behaviour. */
    void setShowMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showMenuHook_ =
            hook ? std::move(hook) : [](juce::PopupMenu& m) { m.showMenuAsync(juce::PopupMenu::Options()); };
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    /** FRO296: the drag half of a name-area press -- see mouseDown's own comment on why the
     *  click-vs-drag decision is deferred to here/mouseUp rather than taken in mouseDown. */
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    /** FRO296 test seam: the insertion boundary a synthesized drag is currently hovering, or -1 when
     *  no drag is live -- lets a test assert the insertion line without decoding paint() output. */
    int getDragInsertionRowForTest() const noexcept { return draggingRow_ ? dragInsertionRow_ : -1; }

private:
    static constexpr int kRowHeight = 20;
    static constexpr int kKnobWidth = 20;
    static constexpr int kPanKnobWidth = 20; // FRO294
    static constexpr int kToggleWidth = 30;
    static constexpr int kMuteWidth = 18;
    static constexpr int kRemoveWidth = 14;
    // FRO294: painted (not a real component) only when a row is mono -- see paint()'s own comment.
    static constexpr int kMonoMarkerWidth = 6;
    // FRO296: pixel distance a name-area press must cross before it commits to a reorder drag rather
    // than the click that opens the target menu -- same value/reasoning as
    // TimelineTrackHeaderComponent's own kRowDragThreshold (T166).
    static constexpr float kRowDragThreshold = 4.0f;

    struct Row {
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
        // FRO294: same shape as knob/attachment above, attached to the slot's sendNPan instead.
        std::unique_ptr<juce::Slider> panKnob;
        std::unique_ptr<juce::SliderParameterAttachment> panAttachment;
        // FRO295: a real juce::TextButton, not painted text like PRE/POST -- reuses
        // MixerColumnComponent's own "M" button type/LookAndFeel (setClickingTogglesState(false),
        // manual setToggleState mirroring the strip's own mute button convention) so mute gets the
        // same real hit area, accessibility role and on/off colouring with no new ad-hoc paint.
        std::unique_ptr<juce::TextButton> muteButton;
    };

    // FRO228: paint() above draws the "+ Send" row itself (plain text, no component), so VoiceOver/
    // NVDA had nothing to land on -- mirrors MixerInsertList::RowAccessibilityProxy's own comment.
    // setInterceptsMouseClicks(false, false) keeps mouseDown() below the sole owner of real clicks.
    class AddSendAccessibilityProxy : public juce::Component {
    public:
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::button);
        }
    };

    int rowIndexAt(juce::Point<int> position) const;
    /** FRO296: the insertion BOUNDARY (0..entries_.size()) nearest `y` -- boundary k sits between
     *  visible rows k-1 and k, and dragging to it means "insert before row k". */
    int insertionRowAt(int y) const;
    void rebuildKnobs();
    void showTargetMenu(int rowIndex);
    void showAddMenu();
    void mutateAndNotify(const std::function<bool()>& mutation);
    juce::String targetNameFor(juce::AudioProcessorGraph::NodeID target) const;
    juce::String targetNameFor(const synth::SendTarget& target) const;
    /** FRO318: the separator + one "Key: ..." item per legal Key target, calling `choose` on pick. */
    void appendKeyTargetItems(juce::PopupMenu& menu, const std::function<void(synth::SendTarget)>& choose) const;

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::MacroSet* macros_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;

    std::vector<synth::MixerSendEntry> entries_;
    std::vector<Row> rows_;
    juce::AudioProcessorGraph::NodeID stripNodeId_;
    AddSendAccessibilityProxy addSendProxy_;

    // FRO296: drag-reorder gesture state. dragFromRow_ is armed on a name-area mouseDown and stays
    // set for the whole press (drag or not); draggingRow_ flips true only once the threshold is
    // crossed, exactly like TimelineTrackHeaderComponent's own draggingRow_.
    int dragFromRow_ = -1;
    bool draggingRow_ = false;
    int dragInsertionRow_ = -1;

    // See setShowMenuHookForTest's own comment.
    std::function<void(juce::PopupMenu&)> showMenuHook_ = [](juce::PopupMenu& m) {
        m.showMenuAsync(juce::PopupMenu::Options());
    };

    static int liveUnbindCalls_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerSendList)
};

} // namespace synth::ui
