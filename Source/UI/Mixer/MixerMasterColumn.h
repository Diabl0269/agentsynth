#pragma once

#include "MacroSet.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerPanLaw.h"
#include "Mixer/PeakMeterLatch.h"
#include "MixerColumnHeader.h"
#include "MixerFader.h"
#include "MixerInsertList.h"
#include "MixerMeter.h"
#include "MixerMeterReadout.h"
#include "UI/Graph/PickTargetOverlay/PickCandidate.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Mixer/MixerSections/MixerSectionControls.h"
#include "UI/Mixer/MixerSections/MixerSectionLayout.h"
#include "UI/Mixer/MixerSections/MixerSectionViewport.h"
#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;
class AudioEngine;
class GraphEditor;

// MixerMasterColumn.h (docs/mixer/panel.md#what-the-mixer-shows): Master's column -- fader/mute/
// meter/dB readout on MasterModule's own gain/mute params, no pan, no solo. Plus an insert list like a
// strip's -- the post-fader chain between Master and the Rec Tap / Audio Output (docs/mixer/mixer.md#master-inserts).
namespace synth::ui {

class MixerMasterColumn : public juce::Component {
public:
    MixerMasterColumn();

    /** `meterReader` defaults to the pre-existing `MeterReader::Mixer` slot -- pass
     *  `MeterReader::MixerMirror` when this column belongs to the Mixer's optional second live view
     *  (MixerMirrorController), so its own meter poll never races the docked view's for the same
     *  consume-on-read latch slot (Source/Mixer/PeakMeterLatch.h). */
    void configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                   GraphEditor& graphEditor, AudioEngine& audioEngine,
                   synth::MeterReader meterReader = synth::MeterReader::Mixer);
    void setNodeId(juce::AudioProcessorGraph::NodeID nodeId);
    /** Binds to `column` (Kind::Master) -- setNodeId(column.nodeId) plus the insert list's rows. Call after
     *  configure(); the column's chain fields come straight off MixerSnapshot. */
    void setColumn(const synth::MixerColumn& column);
    /** Mirrors MixerColumnComponent::getNodeId() -- lets MixerPanelComponent::setMidiLearnArmed()
     *  find Master by nodeId the same way it finds a strip column. */
    juce::AudioProcessorGraph::NodeID getNodeId() const noexcept { return nodeId_; }

    /** Same contract as MixerColumnComponent::unbindFromGraph() -- unlike a strip column,
     *  this component survives a MixerPanelComponent::rebuild() (it's a persistent member, not
     *  recreated), so without this its fader would otherwise stay bound to Master's OLD gain param
     *  across a graph-replacing restore until setNodeId() ran again, which is exactly the freed-
     *  parameter window the pre-restore crash needs. */
    void unbindFromGraph();

    /** The level LEAVING the master chain (AudioEngine::takeOutputMeterPeak(MeterReader::Mixer, leg)) --
     *  what the meter and clip/peak readout read once Master has >= 1 insert. Null falls back to Master's own
     *  pre-insert latch. Survives unbindFromGraph(): it points at the engine, not into the graph. */
    std::function<float(int leg)> outputPeakProvider;
    /** Forwarded from the insert list -- see MixerInsertList::onEditOnCanvas / onMutated. */
    std::function<void(const juce::String&)> onEditOnCanvas;
    std::function<void()> onMutated;
    /** Fired at the end of toggleMuted()/setPanLaw() -- a real interactive change only,
     *  same contract as MixerColumnComponent::onLiveStateChanged (see that member's comment). */
    std::function<void()> onLiveStateChanged;

    MixerInsertList& getInsertListForTest() noexcept { return insertList_; }
    MixerColumnHeader& getHeaderForTest() noexcept { return header_; }
    MixerSectionViewport& getInsertViewportForTest() noexcept { return insertViewport_; }

    /** The panel's shared section layout; must outlive this column. Unset, the column uses its own. */
    void setSectionLayout(MixerSectionLayout& layout);
    /** Repaints the section dividers after the layout's hover/drag state changed. */
    void repaintSectionDividers();

    /** One 10 Hz tick, same driving chain as MixerColumnComponent::refreshMeter(). */
    void refreshMeter(float elapsedSeconds);

    MixerFader& getFaderForTest() noexcept { return fader_; }
    MixerMeter& getMeterForTest() noexcept { return meter_; }
    MixerMeterReadout& getMeterReadoutForTest() noexcept { return meterReadout_; }
    void resetMeterReadout() { meterReadout_.reset(); }

    /** Sibling of MixerColumnComponent::onResetAllMetersRequested -- fires on an
     *  Option/Alt-click of Master's own readout. */
    std::function<void()> onResetAllMetersRequested;

    /** Toggles Master's mute through the same undo bracket the M button's onClick already
     *  used -- MixerPanelComponent's keyPressed calls this directly, same seam as
     *  MixerColumnComponent::toggleMuted(). A no-op after unbindFromGraph(). */
    void toggleMuted();

    /** The fader nudged one undo step, or false when nothing is bound. */
    bool nudgeFader(float deltaDb) { return fader_.nudge(deltaDb); }

    void setKeyboardFocused(bool focused);
    bool isKeyboardFocusedForTest() const noexcept { return keyboardFocused_; }

    /** Same contract as MixerColumnComponent::grabAccessibilityFocus() -- moves
     *  VoiceOver's cursor to Master's own fader when Master becomes the keyboard-walked focus. */
    void grabAccessibilityFocus() { fader_.grabAccessibilityFocus(); }
    juce::Component& getAccessibilityFocusTargetForTest() noexcept { return fader_.getSlider(); }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // ---- MIDI Learn (Master has exactly one learnable control, its fader/"gain" param;
    // see MixerColumnComponent's sibling pattern for the coverage-table entries with more than
    // one) ----
    juce::RangedAudioParameter* findMidiLearnableParamForTest(const juce::Component* component) const;
    bool isMidiLearnBadgeMappedForTest(const juce::Component* component) const;
    /** The fader, for the pick-target overlay. */
    void collectPickCandidates(std::vector<PickCandidate>& out) const;
    /** Mirrors MixerColumnComponent::getMidiLearnArmedRepaintCountForTest -- see that
     *  method's own comment on why this exists. */
    int getMidiLearnArmedRepaintCountForTest() const noexcept { return midiLearnArmedRepaintCount_; }
    /** Arms/clears (empty id) the breathing outline. Message thread only -- called by
     *  MidiLearnController via MixerPanelComponent::setMidiLearnArmed. */
    void setMidiLearnArmedParam(const juce::String& paramId);
    /** Same seam as ModuleComponent::setShowContextMenuHookForTest -- see
     *  MixerColumnComponent::setShowContextMenuHookForTest's comment. */
    void setShowContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showContextMenuHook_ =
            hook ? std::move(hook) : [](juce::PopupMenu& m) { m.showMenuAsync(juce::PopupMenu::Options()); };
    }

    /** The project's pan-law control, next to the mute button -- the mixer has no other
     *  project-settings surface (see docs/mixer/mixer.md#pan-law). */
    juce::TextButton& getPanLawButtonForTest() noexcept { return panLawButton_; }
    /** Same test-seam idiom as setShowContextMenuHookForTest, for the pan-law button's own menu. */
    void setShowPanLawMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showPanLawMenuHook_ =
            hook ? std::move(hook) : [](juce::PopupMenu& m) { m.showMenuAsync(juce::PopupMenu::Options()); };
    }

    /** The cheap per-strip refresh for Master -- mute button + pan-law label, no rebuild.
     *  Called on this instance directly by MixerPanelComponent::refreshLiveMixerVisuals(); see that
     *  method's comment for who calls it and why. */
    void refreshLiveVisuals();

private:
    void refreshPanLawButton();
    void showPanLawMenu();
    void setPanLaw(synth::MixerPanLaw law);

    void refreshMuteAccessibility();
    void refreshMidiLearnBadges();
    /** Mirrors MixerColumnComponent::repaintArmedMidiLearnOutline -- called from
     *  refreshMeter()'s existing 10 Hz tick so the breathing outline actually animates. */
    void repaintArmedMidiLearnOutline();
    void paintMidiLearnOverlays(juce::Graphics& g);

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;
    AudioEngine* audioEngine_ = nullptr; // See setPanLaw()
    juce::AudioProcessorGraph::NodeID nodeId_;

    // The fader's bound param (paramID "gain") -- null when nothing is bound, same
    // lifetime contract as fader_'s own internal param_ (MixerFader::isBoundForTest()). Cleared by
    // unbindFromGraph() before the graph-replacing mutation frees it.
    juce::RangedAudioParameter* midiLearnableFaderParam_ = nullptr;
    bool midiLearnBadgeMapped_ = false;
    juce::String midiLearnArmedParamId_;
    double midiLearnArmedSinceMs_ = 0.0;
    // Backs getMidiLearnArmedRepaintCountForTest() -- test-only, never read in production.
    int midiLearnArmedRepaintCount_ = 0;
    std::function<void(juce::PopupMenu&)> showContextMenuHook_ = [](juce::PopupMenu& m) {
        m.showMenuAsync(juce::PopupMenu::Options());
    };

    // True once setColumn() saw >= 1 insert -- switches the meter to outputPeakProvider.
    bool hasInserts_ = false;
    float takeMeterPeak(int leg);
    // Which consume-on-read latch slot this instance polls -- see configure()'s own comment.
    synth::MeterReader meterReader_ = synth::MeterReader::Mixer;

    MixerColumnHeader header_;
    MixerInsertList insertList_;
    MixerSectionLayout ownSectionLayout_; // until setSectionLayout() hands over the panel's
    MixerSectionLayout* sectionLayout_ = &ownSectionLayout_;
    MixerSectionViewport insertViewport_;
    std::array<MixerSectionDivider, MixerSectionLayout::kSectionCount> dividers_;
    MixerCollapsedSection insertsCollapsed_;
    MixerFader fader_;
    MixerMeter meter_;
    MixerMeterReadout meterReadout_;
    juce::TextButton muteButton_{"M"};
    // Labelled by refreshPanLawButton() ("Pan: Balance" / "Pan: Comp.") -- see the class
    // comment on where this lives and why.
    juce::TextButton panLawButton_;
    std::function<void(juce::PopupMenu&)> showPanLawMenuHook_ = [](juce::PopupMenu& m) {
        m.showMenuAsync(juce::PopupMenu::Options());
    };
    bool keyboardFocused_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerMasterColumn)
};

} // namespace synth::ui
