#pragma once

#include "MacroSet.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Mixer/MixerDirectColumn.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class AppUndoManager;
class GraphEditor;
class AudioEngine;
class ShortcutManager;

namespace synth::ui {
class MixerColumnComponent;
}

// MixerPanelComponent.h -- FRO11 (P9-5, docs/mixer.md §5.10): the columns container -- a
// horizontally scrolling row of columns built from synth::buildMixerSnapshot(), rebuilt on every
// graph/timeline/macro change notification. Pure layout + rebuild-on-change; owns nothing
// audio-specific itself.
//
// FRO18: also the mixer's own keyboard-focus region ROOT (docs/shortcuts.md's "Mixer column
// navigation") -- a region ROOT, not per-column leaves: MixerColumnComponent/MixerMasterColumn/
// MixerDirectColumn's own controls all give up keyboard focus (setWantsKeyboardFocus(false)), so
// this panel is the single focusable leaf and keyPressed() (MixerPanelKeyboard.cpp) owns Left/
// Right column walk, Up/Down fader nudge, Enter select-on-canvas and the rebindable M/S/R actions.
namespace synth::ui {

class MixerPanelComponent : public juce::Component {
public:
    MixerPanelComponent();
    ~MixerPanelComponent() override;

    void configure(juce::AudioProcessorGraph& graph, synth::TimelineDoc& doc, synth::MacroSet& macros,
                   AppUndoManager& undoManager, GraphEditor& graphEditor, AudioEngine& audioEngine);

    /** Forwarded from MixerDirectColumn -- MixerDockComponent wires this to
     *  MainComponent::makeChannelForNode, the same "Make channel" entry point every other trigger
     *  (header menu, canvas menu, module-card menu) already uses. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onMakeChannelForNode;

    /** Forwarded from every column's insert list after an add/reorder/remove mutation --
     *  MixerDockComponent wires this to MainComponent::reconcileTimelineAfterGraphChange. */
    std::function<void()> onGraphMutated;

    /** FRO18: fires when the Arm key (rebindable "timelineArmFocusedTrack") is pressed with a
     *  linked strip focused -- MixerDockComponent wires this to
     *  MainComponent::performTrackEdit([&doc,id]{ doc.setTrackArmed(id, !doc.getTrack(id)->armed); }),
     *  never a direct TimelineDoc write (that would skip timelineChanged/reconcile -- root
     *  CLAUDE.md's "every graph change must reach MainComponent::timelineChanged" invariant, which
     *  this ISN'T a graph change of, but the undo-bracket convention every other track edit goes
     *  through regardless). */
    std::function<void(synth::TrackId)> onArmTrack;

    /** FRO18: the ShortcutManager the M/S/R keys resolve their rebindable bindings against
     *  ("timelineMuteFocusedTrack"/"timelineSoloFocusedTrack"/"timelineArmFocusedTrack" -- the
     *  SAME action ids the Timeline track header row already binds, deliberately: a new id would
     *  not inherit a user's existing rebind). Null (the default) falls back to the hardcoded bare
     *  letters, same "no manager installed" contract every other surface action in this app
     *  follows. Safe to call before or after columns exist -- keyPressed() always reads the
     *  member, never a value captured at bind time. */
    void setShortcutManager(ShortcutManager* manager) { shortcuts_ = manager; }

    /** Re-runs buildMixerSnapshot() and rebuilds the column set. Cheap enough to call on every
     *  graph/timeline/macro change (a handful of strips, never per-frame) -- see MixerModel.h.
     *  FRO18: also re-resolves focusedColumnIndex_ by NodeID/uuid across the rebuild -- see
     *  MixerPanelKeyboard.cpp's resolveFocusAfterRebuild() for the by-identity match rule. */
    void rebuild();

    /** FRO11: unbinds every strip column's + Master's fader/pan/mute/solo/meter from whatever
     *  processor/parameters they currently reference, WITHOUT destroying or rebuilding anything --
     *  wired to GraphEditor::onBeforeDetachAllModuleComponents (MainComponent's own setup), so it
     *  runs before every graph-replacing mutation (undo/redo restore, New Patch, Load, AI patch
     *  apply) frees the nodes those bindings point at. rebuild()'s own stripColumns_.clear() (which
     *  destroys the strip columns' MixerFaders, calling their now-safe idempotent unbind() again)
     *  and buildMixerSnapshot()'s eventual re-bind against the NEW graph both then run afterwards,
     *  from the after-restore hook -- see docs/mixer_implementation.md's FRO11 crash-fix entry. */
    void unbindAllColumns();

    int getColumnCount() const noexcept { return (int)columnEntries_.size(); }

    /** The Nth strip column (in the same track order buildMixerSnapshot returns), or null out of
     *  range -- a stable handle for a test to drive real mouse events against without depending on
     *  juce::Viewport's own internal child layout (scrollbars, the viewed-content wrapper). */
    MixerColumnComponent* getStripColumnForTest(int index) const {
        return index >= 0 && index < (int)stripColumns_.size() ? stripColumns_[(size_t)index].get() : nullptr;
    }
    MixerDirectColumn* getDirectColumnForTest() const { return directColumn_.get(); }
    MixerMasterColumn* getMasterColumnForTest() const { return masterColumn_.get(); }

    /** FRO18: -1 when nothing is focused, else an index into the same left-to-right order
     *  rebuild() lays out (strips, then Direct if visible, then Master if visible). */
    int getFocusedColumnIndexForTest() const noexcept { return focusedColumnIndex_; }

    /** FRO11's revealChannelForTrack redirect: scrolls the column for `stripId` into view and
     *  selects it (MixerColumnComponent::setSelected), clearing selection on every other column.
     *  False when no column matches (nothing to reveal -- the caller falls back to the canvas). */
    bool revealColumn(juce::AudioProcessorGraph::NodeID stripId);

    /** One 10 Hz tick while the mixer tab is showing -- ticks every column's meter. */
    void refreshMeters();

    bool keyPressed(const juce::KeyPress& key) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

private:
    void selectOnCanvas(const juce::String& targetId);

    /** FRO18: one entry per column rebuild() lays out, in the same left-to-right order -- the
     *  panel's own model of "what can be focused", kept separate from MixerColumnComponent so
     *  that header stays uninvolved in feeding-track bookkeeping it has no other use for (plan's
     *  own file-size budget). */
    struct ColumnEntry {
        enum class Kind { Strip, Direct, Master };
        Kind kind = Kind::Strip;
        juce::Component* component = nullptr;
        juce::AudioProcessorGraph::NodeID nodeId;  // invalid for Direct
        juce::String uuid;                         // stable re-resolve key; empty for Direct
        bool linkedToTrack = false;                // Strip only
        std::vector<synth::TrackId> feedingTracks; // Strip only
    };

    // ---- FRO18 keyboard dispatch -- implemented in MixerPanelKeyboard.cpp ----------------------
    bool matchesAction(const juce::KeyPress& key, const juce::String& actionId, const juce::KeyPress& fallback) const;
    bool moveFocus(int direction);
    bool nudgeFocusedFader(float deltaDb);
    bool selectFocusedOnCanvas();
    bool toggleFocusedMuted();
    bool toggleFocusedSoloed();
    bool armFocusedTrack();
    /** Re-resolves focusedColumnIndex_ after a rebuild by IDENTITY, never by raw index -- a
     *  strip insert/removal elsewhere in the column order would otherwise silently reattach focus
     *  to the wrong column. `hadFocus` is false when nothing was focused before the rebuild (then
     *  this is a no-op, focusedColumnIndex_ already reset to -1 by rebuild()). Direct matches by
     *  kind alone (there is only ever one, and it carries no uuid); Strip/Master match by uuid. */
    void resolveFocusAfterRebuild(bool hadFocus, ColumnEntry::Kind previousKind, const juce::String& previousUuid);
    void setFocusedColumnIndex(int index);
    void syncFocusVisuals();
    void revealFocusedColumn();

    juce::Viewport viewport_;
    juce::Component content_;

    juce::AudioProcessorGraph* graph_ = nullptr;
    synth::TimelineDoc* doc_ = nullptr;
    synth::MacroSet* macros_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;
    AudioEngine* audioEngine_ = nullptr;
    ShortcutManager* shortcuts_ = nullptr;

    std::vector<std::unique_ptr<MixerColumnComponent>> stripColumns_;
    std::unique_ptr<MixerDirectColumn> directColumn_;
    std::unique_ptr<MixerMasterColumn> masterColumn_;

    std::vector<ColumnEntry> columnEntries_;
    int focusedColumnIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanelComponent)
};

} // namespace synth::ui
