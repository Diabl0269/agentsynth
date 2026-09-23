#pragma once

#include "Mixer/MixerModel/MixerModel.h"
#include "MixerColumnHeader.h"
#include "MixerEqThumbnail.h"
#include "MixerFader.h"
#include "MixerInsertList.h"
#include "MixerMeter.h"
#include "MixerMeterReadout.h"
#include "MixerSendList.h"
#include "UI/Graph/PickTargetOverlay/PickCandidate.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class AppUndoManager;
class GraphEditor;
class AudioEngine;
class ModuleBase;
class ChannelStripModule;

// MixerColumnComponent.h -- FRO11 (P9-5, docs/mixer/panel.md#what-the-mixer-shows): one ChannelStrip's column --
// header, source line, insert list, pan, fader + meter, dB readout (inside MixerFader), M/S, and
// the tracks-feeding row. A background click (not on a control) selects the strip's macro on the
// canvas (docs/mixer/panel.md#what-the-mixer-shows's "clicking a column selects its macro").
namespace synth::ui {

class MixerColumnComponent : public juce::Component {
public:
    MixerColumnComponent();

    /** References must outlive this component -- same lifetime contract MixerDockComponent's own
     *  constructor threads down through MixerPanelComponent. */
    void configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                   GraphEditor& graphEditor, AudioEngine& audioEngine);

    /** `sourceLine`: the feeding tracks' names, comma-joined (computed by the caller, which holds
     *  the TimelineDoc -- Core's MixerColumn only carries TrackIds). */
    void setColumn(const synth::MixerColumn& column, const juce::String& sourceLine);

    juce::AudioProcessorGraph::NodeID getNodeId() const noexcept { return nodeId_; }

    /** FRO11: unbinds the fader/pan/mute/solo/meter from whatever live processor/parameters they
     *  currently reference, and clears this column's own raw pointers into the graph -- called by
     *  MixerPanelComponent::unbindAllColumns() from GraphEditor::onBeforeDetachAllModuleComponents,
     *  i.e. BEFORE a graph-replacing mutation (undo/redo restore, New Patch, Load, AI patch apply)
     *  frees the nodes/parameters this column is bound to. The column itself is left alive --
     *  MixerPanelComponent::rebuild() destroys and replaces it afterwards; this only makes that
     *  eventual destruction (and this method itself, idempotent/null-safe like MixerFader::unbind())
     *  safe to run against freed memory in between. */
    void unbindFromGraph();

    /** True once bind() has run and unbindFromGraph()/rebindControls() hasn't cleared it since. */
    bool isFaderBoundForTest() const noexcept { return fader_.isBoundForTest(); }

    /** A stable handle onto the column's own EQ thumbnail -- null-safe to call regardless of
     *  whether the column currently has an EQ insert (the component always exists; it is just
     *  hidden when there is nothing to show). */
    MixerEqThumbnail& getEqThumbnailForTest() noexcept { return eqThumbnail_; }

    /** The column's own insert list -- juce::PopupMenu never runs in a test process (see
     *  docs/development/test-patterns.md), so a test drives MixerInsertList::removeRow()/moveRow()/
     *  addModule() directly through this, exactly like the row menu's own async callbacks would. */
    MixerInsertList& getInsertListForTest() noexcept { return insertList_; }

    /** FRO18: toggles this strip's mute/solo through exactly the same path the M/S buttons'
     *  onClick already used (undo bracket, ChannelStripModule::isSoloed via
     *  AudioEngine::setChannelStripSoloed -- never a direct setSoloed(), root CLAUDE.md's
     *  invariant). Reachable both from a real click and from MixerPanelComponent's keyPressed, so
     *  the two paths can never diverge. A no-op after unbindFromGraph() (graph_/undoManager_/
     *  audioEngine_ null then), same guard the former inline lambdas already had. */
    void toggleMuted();
    void toggleSoloed();

    /** FRO18: the fader nudged one undo step, or false when nothing is bound (Direct-column-style
     *  no-op) or the fader has no live param (post-unbind). */
    bool nudgeFader(float deltaDb) { return fader_.nudge(deltaDb); }

    MixerFader& getFaderForTest() noexcept { return fader_; }
    juce::Slider& getPanSliderForTest() noexcept { return panSlider_; }
    juce::Button& getMuteButtonForTest() noexcept { return muteButton_; }
    juce::Button& getSoloButtonForTest() noexcept { return soloButton_; }

    /** FRO18: the strip's own leaf-level keyboard-focus outline, painted in paintOverChildren --
     *  distinct from setSelected()'s reveal highlight (they may co-paint). MixerPanelComponent
     *  sets this when focusedColumnIndex_ changes (real hasKeyboardFocus() is always false
     *  headless with no native peer -- same accepted gap TimelineTrackFocusTests documents). */
    void setKeyboardFocused(bool focused);
    bool isKeyboardFocusedForTest() const noexcept { return keyboardFocused_; }

    /** FRO18 review fix: MixerPanelComponent calls this from setFocusedColumnIndex() (real
     *  navigation only, never from a rebuild()-preserved refocus -- see MixerPanelKeyboard.cpp's
     *  own comment on why) so VoiceOver's accessibility cursor tracks the visual keyboard-focus
     *  outline instead of only painting it. Targets the fader -- the control the ticket's own
     *  click path names ("arrow through columns ... it should announce each fader's dB value") --
     *  not the column group, so arrowing to a strip reads its dB value directly. */
    void grabAccessibilityFocus() { fader_.grabAccessibilityFocus(); }

    /** The component grabAccessibilityFocus() targets -- exposed so a test can assert WHICH
     *  control the arrow-walk points accessibility focus at without needing the native peer real
     *  focus movement itself needs (getAccessibilityHandler() returns null headless either way). */
    juce::Component& getAccessibilityFocusTargetForTest() noexcept { return fader_.getSlider(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

    /** Fires when the header (or empty column background) is clicked -- MixerPanelComponent wires
     *  this to select the strip's macro (or the strip itself, if unboxed) on the canvas. */
    std::function<void()> onColumnClicked;
    /** Forwarded from the insert list -- see MixerInsertList::onEditOnCanvas. */
    std::function<void(const juce::String&)> onEditOnCanvas;
    /** Forwarded from the insert list and the send list after a topology-changing mutation. */
    std::function<void()> onMutated;

    /** FRO15: forwarded to the send list's "+ Send > New bus..." -- see MixerSendList::createBus. */
    void setCreateBusProvider(std::function<juce::AudioProcessorGraph::NodeID()> provider) {
        sendList_.createBus = std::move(provider);
    }

    /** FRO15 test seam: the send rows this column is showing. */
    MixerSendList& getSendListForTest() noexcept { return sendList_; }

    /** One 10 Hz tick -- see MixerMeter's own header comment for the driving chain.
     *  `elapsedSeconds`: measured once by MixerPanelComponent::refreshMeters() and threaded down
     *  so every column's ballistics (and this one's clip readout) advance by the same real time,
     *  independent of the poll's actual (tab-visibility-gated) cadence. */
    void refreshMeter(float elapsedSeconds);

    /** FRO146: the meter itself -- a test seam for reading displayed/peak-hold dB directly. */
    MixerMeter& getMeterForTest() noexcept { return meter_; }
    /** FRO146: the clip readout -- Cubase's "Meter Peak Level" field. A test seam for reading its
     *  text/clip state and driving its real mouse-click reset path. */
    MixerMeterReadout& getMeterReadoutForTest() noexcept { return meterReadout_; }
    /** FRO146: resets this column's clip readout -- called by the header's "Reset Meters" action
     *  and by an Option/Alt-click on ANY column's readout (see onResetAllMetersRequested below). */
    void resetMeterReadout() { meterReadout_.reset(); }

    /** FRO146: fires when this column's readout is Option/Alt-clicked -- MixerPanelComponent wires
     *  every column's instance of this to its own resetAllMeterReadouts(). */
    std::function<void()> onResetAllMetersRequested;

    /** FRO11's revealColumnForStrip highlight -- an accent border while true, so the channel
     *  chip's click has a visible "found it" result the same way Locate Master's canvas select
     *  does. Exactly one column is selected at a time (MixerPanelComponent enforces it). */
    void setSelected(bool selected);
    // FRO12 (P9-6): proves a detach/redock (a plain reparent, never a rebuild()) leaves selection
    // untouched -- see Tests/UI/Layout/DetachablePanelHost/DetachRedockStateTests.cpp.
    bool isSelectedForTest() const noexcept { return selected_; }

    // ---- MIDI Learn (FRO133, MixerColumnMidiLearn.cpp -- see its file comment for the design;
    // reuses the FRO130 module-card pattern via Source/UI/MidiRemote/MidiLearnMenu.h) ----

    /** Test/inspection: the param a right-click on `component` would open MIDI Learn for, or null
     *  -- mirrors ModuleComponent::findMidiLearnableParamForTest. */
    juce::RangedAudioParameter* findMidiLearnableParamForTest(const juce::Component* component) const;

    /** Test/inspection: `component`'s MIDI-mapped badge cache, as of the last refreshMidiLearnBadges(). */
    bool isMidiLearnBadgeMappedForTest(const juce::Component* component) const;

    // FRO256: test seam for the armed breathing outline's per-tick repaint -- see refreshMeter().
    int getMidiLearnArmedRepaintCountForTest() const noexcept { return midiLearnArmedRepaintCount_; }

    /** FRO135: appends every learnable control here (parameters, and Solo as a node command) for the
     *  pick-target overlay. */
    void collectPickCandidates(std::vector<PickCandidate>& out) const;

    /** Same seam as ModuleComponent::setShowContextMenuHookForTest -- juce::PopupMenu never runs
     *  in a test process (docs/development/test-patterns.md), so a test installs a capturing hook
     *  to inspect the menu a real right-click mouseDown() built, without ever opening a popup. A
     *  null hook restores the real showMenuAsync() behaviour. */
    void setShowContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showContextMenuHook_ =
            hook ? std::move(hook) : [](juce::PopupMenu& m) { m.showMenuAsync(juce::PopupMenu::Options()); };
    }

    void mouseDown(const juce::MouseEvent& event) override;

    /** Arms/clears (empty id) the breathing outline for the control bound to `paramId`. Message
     *  thread only -- called by MidiLearnController via MixerPanelComponent::setMidiLearnArmed. */
    void setMidiLearnArmedParam(const juce::String& paramId);

    /** FRO253: same contract as setMidiLearnArmedParam() above, for the Solo node command's own
     *  breathing outline -- called by MixerPanelComponent::setMidiLearnArmedSolo/clearMidiLearnArmedSolo. */
    void setMidiLearnArmedSolo(bool armed);

    /** FRO253: forwarded to MixerPanelComponent's single set of Solo-learn callbacks (rebuild()) --
     *  fired from this column's own mouseDown() on a right-click on soloButton_, mirroring how a
     *  registered parameter control fires graphEditor_->onMidiLearnRequested/onMidiForgetRequested.
     *  Null (the default) is a wired-nothing no-op, same contract as GraphEditor's own callbacks. */
    std::function<void()> onSoloMidiLearnRequested;
    std::function<void()> onSoloMidiForgetRequested;
    std::function<juce::String()> onQuerySoloMidiMapping;

    /** FRO253: re-reads mute/solo from the bound module/strip and repaints -- called by
     *  MixerPanelComponent::refreshMuteSoloVisuals() after something OTHER than this column's own
     *  click changes solo (a MIDI Remote node-command press). A no-op post-unbind (graph_ null),
     *  same guard toggleSoloed() above already uses. */
    void refreshMuteSoloVisual();

private:
    void rebindControls();
    void refreshMuteSoloAccessibility(ModuleBase* module, ChannelStripModule* strip);

    /** Registers `control` as a MIDI-learnable target for `param` (a no-op if `param` is null,
     *  mirroring ModuleComponent::MidiLearnableRegistry::add) and, the FIRST time `control` is
     *  seen, attaches this column as its MouseListener so a right-click on it reaches mouseDown()
     *  below -- see MixerColumnMidiLearn.cpp. */
    void registerMidiLearnable(juce::Component& control, juce::RangedAudioParameter* param);
    /** FRO253: same registration shape as registerMidiLearnable() above, for the Solo node command
     *  entry (no juce::RangedAudioParameter -- ChannelStripModule::soloed_ is engine state). Only
     *  called from rebindControls() when soloButton_ is visible (a ChannelStrip column). */
    void registerSoloMidiLearnable();
    /** Split out of mouseDown() (MixerColumnMidiLearn.cpp) -- one branch per registry entry kind. */
    void showParamMidiLearnMenu(juce::RangedAudioParameter& param);
    void showSoloMidiLearnMenu();
    void refreshMidiLearnBadges();
    void repaintArmedMidiLearnOutline(); // FRO256: keeps the armed breathing outline animating; see .cpp
    void paintMidiLearnOverlays(juce::Graphics& g);

    /** One entry per learnable control currently bound -- cleared and rebuilt by rebindControls()
     *  (and, for send rows, by sendList_'s own onSendKnobBuilt callback fired from inside it),
     *  and cleared again by unbindFromGraph() since `param` is a raw pointer into the graph node
     *  this column is about to be detached from (Source/UI/CLAUDE.md's mixer-unbind invariant).
     *  FRO253: `param` is null for the Solo entry -- `isSolo` (and `targetName`, used for its menu
     *  label/query in place of param->getName()) distinguish it from "not yet resolved". */
    struct MidiLearnableEntry {
        juce::Component* component = nullptr;
        juce::RangedAudioParameter* param = nullptr;
        bool isSolo = false;
        juce::String targetName;
        bool mapped = false;
    };
    std::vector<MidiLearnableEntry> midiLearnableEntries_;
    /** Controls this column has already addMouseListener'd itself onto -- registerMidiLearnable()
     *  consults this so a control shared across two rebindControls() calls (the fader, pan, mute)
     *  is never double-registered as a listener. Send-row knobs are recreated by
     *  MixerSendList::rebuildKnobs() on every setEntries(), so they're never in here twice either. */
    std::vector<juce::Component*> midiLearnListenerTargets_;
    juce::String midiLearnArmedParamId_;
    double midiLearnArmedSinceMs_ = 0.0;
    // FRO253: Solo's own armed flag/timestamp -- separate from the paramId-keyed pair above since
    // Solo has no juce::RangedAudioParameter identity to key on.
    bool midiLearnArmedSolo_ = false;
    double midiLearnArmedSoloSinceMs_ = 0.0;
    // FRO256: backs getMidiLearnArmedRepaintCountForTest() -- test-only, never read in production.
    int midiLearnArmedRepaintCount_ = 0;
    /** Set in the constructor to `[](juce::PopupMenu& m) { m.showMenuAsync(...); }`; a test
     *  replaces it via setShowContextMenuHookForTest(). */
    std::function<void(juce::PopupMenu&)> showContextMenuHook_ = [](juce::PopupMenu& m) {
        m.showMenuAsync(juce::PopupMenu::Options());
    };

    GraphEditor* graphEditor_ = nullptr;
    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    AudioEngine* audioEngine_ = nullptr;

    juce::AudioProcessorGraph::NodeID nodeId_;
    juce::String uuid_;
    juce::String sourceLine_;
    /** The uuid of the first (signal-order) Parametric EQ among this column's inserts -- see
     *  rebindControls(). Empty when the column has no EQ insert. */
    juce::String eqNodeUuid_;
    /** The same EQ's NodeID -- compared against MixerInsertList::onBeforeNodeRemoved's argument to
     *  unbind eqThumbnail_ before a live row-menu removal frees the module it's bound to (FRO16 UAF
     *  fix; unbindFromGraph() above handles the graph-replacing-restore case, not this one). Empty
     *  (default-constructed) when the column has no EQ insert. */
    juce::AudioProcessorGraph::NodeID eqNodeId_;

    MixerColumnHeader header_;
    juce::Label sourceLineLabel_;
    MixerInsertList insertList_;
    MixerEqThumbnail eqThumbnail_;
    MixerSendList sendList_;
    juce::Slider panSlider_;
    std::unique_ptr<juce::SliderParameterAttachment> panAttachment_;
    MixerFader fader_;
    MixerMeter meter_;
    MixerMeterReadout meterReadout_;
    // FRO133/FRO253: right-click-safe so a MIDI Learn menu can open on Mute/Solo without also
    // toggling them -- see Source/UI/MidiRemote/MidiLearnMenu.h's RightClickSafeButton comment.
    // Solo's target is a NODE COMMAND, not a juce::RangedAudioParameter (ChannelStripModule::soloed_
    // is engine state, Source/Modules/ChannelStripModule.h) -- see MixerColumnMidiLearn.cpp.
    synth::ui::midilearn::RightClickSafeButton<juce::TextButton> muteButton_{"M"};
    synth::ui::midilearn::RightClickSafeButton<juce::TextButton> soloButton_{"S"};
    bool selected_ = false;
    bool keyboardFocused_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerColumnComponent)
};

} // namespace synth::ui
