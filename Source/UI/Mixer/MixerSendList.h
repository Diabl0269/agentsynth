#pragma once

#include "MacroSet.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/ReorderDrag/ReorderDragSession.h"
#include "UI/Mixer/MixerHeader/MixerIconButton.h"
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class AppUndoManager;
class GraphEditor;

// MixerSendList.h (docs/mixer/sends-and-buses.md): a column's send rows, sibling of
// MixerInsertList and laid out directly under it.
//
// One row per ACTIVE slot, in slot order: the target bus's name (click to retarget -- the same menu
// also carries a ticked "Mono" item), a small rotary level knob, a small rotary pan knob, a bypass icon
// toggle, an "M" mute toggle, a PRE/POST toggle and an `x` remove; then a "+ Send" row while the strip has a free
// slot. Each mutation is ONE AppUndoManager::recordGraphAndMacroChange around synth::MixerSends'
// Core flows (which have no undo of their own), exactly as MixerInsertList wraps the insert
// splices.
//
// The level and pan knobs attach straight onto the strip's own `sendNLevel`/`sendNPan`
// AudioParameterFloats, so both are host-visible and automatable with no lane plumbing of their
// own. Those attachments are live pointers into a graph node's parameters, which makes
// unbindFromGraph() below load-bearing: without it, an undo that REPLACES the graph frees the
// parameters this list is still attached to.
namespace synth::ui {

class MixerSendList
    : public juce::Component
    , public juce::TooltipClient {
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

    /** Fires once per row from rebuildKnobs(), right after that row's knob/attachment are
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

    /** Drag-reorder a row -- fired ONCE, on release; `fromRow`/`toRow` are VISIBLE row indices (positions in
     *  `entries_`, same addressing as removeRow/retargetRow above). Supplied by MixerPanelComponent,
     *  the one component that owns both the AppUndoManager (for ONE
     *  recordGraphTimelineAndMacroChange step, docs/mixer/sends-and-buses.md#reordering-sends) and
     *  the TimelineDoc (to replay the same slot-swap sequence against any automation lane on a moved
     *  send) -- this list stays TimelineDoc-free, same pattern as createBus staying canvas-free.
     *  Returns whether anything actually changed (false for `fromRow == toRow` or a refused swap),
     *  which is what moveRow() below uses to decide whether to fire onMutated(). */
    std::function<bool(juce::AudioProcessorGraph::NodeID, int fromRow, int toRow)> moveSendRow;

    /** Fired just before a drag's commit, with the dropped row's final index and where it was drawn
     *  (this list's coordinates). The commit normally rebuilds the mixer and destroys this list, so
     *  the panel keeps the settle and hands it to the NEW list's startSettleFrom(). */
    std::function<void(int finalRow, float fromY)> onSettlePending;

    /** Glides row `rowIndex` from `fromY` into its slot (the settle of a drop this list did not see). */
    void startSettleFrom(int rowIndex, float fromY);

    int getPreferredHeight() const noexcept;

    // ---- Keyboard row focus (docs/mixer/panel.md#keyboard-navigation-and-accessibility) ----
    // Row indices address the visible rows, like the test seams below. The panel owns which row is
    // focused and draws its ring; this list only scrolls it into view and answers questions about it.
    int getRowCount() const noexcept { return (int)entries_.size(); }
    /** -1 clears; any other row is scrolled into the section's frame. */
    void setFocusedRow(int rowIndex);
    int getFocusedRow() const noexcept { return focusedRow_; }
    /** Row `rowIndex`'s bounds in this list's coordinates; empty when out of range. */
    juce::Rectangle<int> getRowBounds(int rowIndex) const;
    /** What a screen reader says for the row: "Send to Reverb Bus, -6.0 dB". Empty when out of range. */
    juce::String describeRow(int rowIndex) const;
    /** Moves the row's level by `deltaDb` (clamped to the parameter's range) as ONE undo step, through
     *  the same change-gesture bracket a knob drag uses. False when the row has no level parameter. */
    bool nudgeLevel(int rowIndex, float deltaDb);

    /** The tooltip for whatever part of a row the pointer is over. */
    juce::String getTooltip() override;

    /** Drops every SliderParameterAttachment and this list's own graph pointers -- called
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
    /** Either target kind -- the "Key: ..." menu items call this with `key` set. */
    void addSendTo(const synth::SendTarget& target);
    void removeRow(int rowIndex);
    void togglePreFaderForRow(int rowIndex);
    /** Flips row `rowIndex`'s mute bit via synth::setSendMuted, one recordGraphAndMacroChange. */
    void toggleMuteForRow(int rowIndex);
    /** Flips row `rowIndex`'s bypass bit via synth::setSendBypassed, one recordGraphAndMacroChange -- the bypass
     *  button's click and the panel's bypass key (B) both land here. */
    void toggleBypassForRow(int rowIndex);
    /** Names the bypass key for the buttons' tooltips ("B"); null leaves the tooltips without a key. */
    std::function<juce::String()> bypassShortcutText;
    /** The row's real bypass button. Null out of range. */
    MixerIconButton* getBypassButtonForTest(int rowIndex) const;
    /** Flips row `rowIndex`'s mono bit via synth::setSendMono, one recordGraphAndMacroChange
     *  -- the target menu's "Mono" item and this class's own headless test seam both call this. */
    void toggleMonoForRow(int rowIndex);
    void retargetRow(int rowIndex, juce::AudioProcessorGraph::NodeID target);
    void retargetRow(int rowIndex, const synth::SendTarget& target);
    /** The real mutation behind a completed drag -- calls the moveSendRow callback and, on
     *  success, onMutated(), same shape as every other row mutation's mutateAndNotify. A synthesized
     *  test drag (mouseDown/mouseDrag past the threshold/mouseUp on the name area) reaches this
     *  through the real mouse path; a test that wants to skip the gesture can call it directly.
     *  onMutated() normally rebuilds the mixer and destroys this list: nothing may follow moveRow(). */
    void moveRow(int fromRow, int toRow);
    std::vector<juce::AudioProcessorGraph::NodeID> availableTargets() const;
    /** Modules whose Key input this strip may feed (synth::enumerateKeySendTargets). */
    std::vector<juce::AudioProcessorGraph::NodeID> availableKeyTargets() const;
    bool canAddSend() const;
    bool isAttachedForTest(int rowIndex) const;
    juce::Slider* getKnobForTest(int rowIndex) const;
    /** The row's pan knob, attached to sendNPan the same way getKnobForTest's level knob
     *  attaches to sendNLevel. */
    juce::Slider* getPanKnobForTest(int rowIndex) const;
    /** The row's real "M" button -- a juce::Button child, same type/LookAndFeel as the column's own
     *  channel mute button, so a test can click it through the real mouse path. */
    juce::Button* getMuteButtonForTest(int rowIndex) const;
    int getRowCountForTest() const noexcept { return (int)entries_.size(); }
    /** The transparent, name-only "Add send" proxy -- see AddSendAccessibilityProxy's own
     *  comment. Always exists; only actually reachable (setVisible(true)) while canAddSend(). */
    juce::Component& getAddSendAccessibilityComponentForTest() noexcept { return addSendProxy_; }

    /** Same seam as MixerColumnComponent::setShowContextMenuHookForTest -- juce::PopupMenu
     *  never runs in a test process (docs/development/test-patterns.md), so a test overrides this to
     *  inspect the built menu (item count, an item's text) or invoke an item's action directly,
     *  instead of the real showMenuAsync(). A null hook restores the real behaviour. */
    void setShowMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showMenuHook_ = hook ? std::move(hook)
                             : [](juce::PopupMenu& m) { m.showMenuAsync(synth::ui::contextMenuOptionsAtPointer()); };
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    /** The drag half of a name-area press -- see mouseDown's own comment on why the
     *  click-vs-drag decision is deferred to here/mouseUp rather than taken in mouseDown. */
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    /** The target-name area is a grab handle: the grab cursor on hover and during the drag. */
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    /** Test seams for the reorder drag: whether a row is lifted or still gliding, where a row's
     *  content is drawn (its top in this list's coordinates), and Esc delivered through the listener
     *  a real key press reaches. */
    bool isRowDragActiveForTest() const noexcept { return rowDrag_.isReordering(); }
    int getRowTopForTest(int rowIndex) const { return (int)std::lround(rowTop(rowIndex)); }
    /** Whether the row is gliding into its slot, and where it is being drawn right now. */
    bool isSettlingForTest() const noexcept { return rowDrag_.isReordering(); }
    /** The last startSettleFrom() this list received: row -1 if none. */
    struct SettleForTest {
        int row = -1;
        float fromY = 0.0f;
    };
    SettleForTest getLastSettleForTest() const noexcept { return lastSettle_; }
    bool sendEscapeToRowDragForTest() { return rowDrag_.sendEscapeForTest(); }

private:
    static constexpr int kRowHeight = 20;
    static constexpr int kKnobWidth = 18;
    static constexpr int kPanKnobWidth = 18;
    static constexpr int kToggleWidth = 26;
    static constexpr int kMuteWidth = 16;
    static constexpr int kBypassWidth = 16;
    static constexpr int kRemoveWidth = 14;
    // Painted (not a real component) only when a row is mono -- see paint()'s own comment.
    static constexpr int kMonoMarkerWidth = 6;

    struct Row {
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
        // Same shape as knob/attachment above, attached to the slot's sendNPan instead.
        std::unique_ptr<juce::Slider> panKnob;
        std::unique_ptr<juce::SliderParameterAttachment> panAttachment;
        // A real juce::TextButton, not painted text like PRE/POST -- reuses
        // MixerColumnComponent's own "M" button type/LookAndFeel (setClickingTogglesState(false),
        // manual setToggleState mirroring the strip's own mute button convention) so mute gets the
        // same real hit area, accessibility role and on/off colouring with no new ad-hoc paint.
        std::unique_ptr<juce::TextButton> muteButton;
        // The bypass icon toggle, sitting between the knobs and the M button.
        std::unique_ptr<MixerIconButton> bypassButton;
    };

    // paint() above draws the "+ Send" row itself (plain text, no component), so VoiceOver/
    // NVDA had nothing to land on -- mirrors MixerInsertList::RowAccessibilityProxy's own comment.
    // setInterceptsMouseClicks(false, false) keeps mouseDown() below the sole owner of real clicks.
    class AddSendAccessibilityProxy : public juce::Component {
    public:
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::button);
        }
    };

    int rowIndexAt(juce::Point<int> position) const;
    /** Whether `position` is on a row's target name (a grab handle), not its buttons or knobs. */
    bool isNameArea(juce::Point<int> position) const;
    void updateHoverCursor(juce::Point<int> position);
    /** Where row `rowIndex` is drawn: its static slot, or the animator's place for it while a drag
     *  is live or settling. */
    float rowTop(int rowIndex) const;
    /** Moves every row's child controls to match rowTop() and repaints. */
    void placeRows();
    void paintRow(juce::Graphics& g, int rowIndex, float lift);
    void beginRowDrag(int rowIndex, const juce::MouseEvent& event);
    void commitRowDrag(int pressedRow);
    void rebuildKnobs();
    void showTargetMenu(int rowIndex);
    void showAddMenu();
    void mutateAndNotify(const std::function<bool()>& mutation);
    juce::String targetNameFor(juce::AudioProcessorGraph::NodeID target) const;
    /** "Bypass Send to Bus 1  (B)", or "Turn ... back on" while it is bypassed. */
    juce::String bypassTooltip(int rowIndex) const;
    /** Re-reads row `rowIndex`'s bypass bit into its button, its title and the dimmed look of its controls. */
    void applyBypassLook(int rowIndex);
    static juce::String bypassTitle(const synth::MixerSendEntry& entry);
    juce::String targetNameFor(const synth::SendTarget& target) const;
    /** The separator + one "Key: ..." item per legal Key target, calling `choose` on pick. */
    void appendKeyTargetItems(juce::PopupMenu& menu, const std::function<void(synth::SendTarget)>& choose) const;

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    synth::MacroSet* macros_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;

    std::vector<synth::MixerSendEntry> entries_;
    std::vector<Row> rows_;
    juce::AudioProcessorGraph::NodeID stripNodeId_;
    AddSendAccessibilityProxy addSendProxy_;

    // Drag-reorder gesture. pressedRow_ is armed on a name-area mouseDown and stays set for the whole
    // press (drag or not); the animator's keys are the rows' indices at the press. The press only
    // becomes a drag past the animator's threshold, so a plain click still opens the target menu.
    int pressedRow_ = -1;
    int focusedRow_ = -1;
    SettleForTest lastSettle_;
    ReorderDragSession rowDrag_{*this, [this] { placeRows(); }};

    // See setShowMenuHookForTest's own comment.
    std::function<void(juce::PopupMenu&)> showMenuHook_ = [](juce::PopupMenu& m) {
        m.showMenuAsync(synth::ui::contextMenuOptionsAtPointer());
    };

    static int liveUnbindCalls_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerSendList)
};

} // namespace synth::ui
