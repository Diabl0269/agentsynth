#pragma once

#include "MacroSet.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerColumnHeader.h"
#include "MixerFader.h"
#include "MixerInsertList.h"
#include "MixerMeter.h"
#include "MixerMeterReadout.h"
#include "UI/Graph/PickTargetOverlay/PickCandidate.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;
class GraphEditor;

// MixerMasterColumn.h -- FRO11 (P9-5, docs/mixer/panel.md#what-the-mixer-shows): Master's column -- fader/mute/
// meter/dB readout on MasterModule's own gain/mute params, no pan, no solo. FRO148: plus an insert list like a
// strip's -- the post-fader chain between Master and the Rec Tap / Audio Output (docs/mixer/mixer.md#master-inserts).
namespace synth::ui {

class MixerMasterColumn : public juce::Component {
public:
    MixerMasterColumn();

    void configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                   GraphEditor& graphEditor);
    void setNodeId(juce::AudioProcessorGraph::NodeID nodeId);
    /** FRO148: binds to `column` (Kind::Master) -- setNodeId(column.nodeId) plus the insert list's rows. Call after
     *  configure(); the column's chain fields come straight off MixerSnapshot. */
    void setColumn(const synth::MixerColumn& column);
    /** FRO133: mirrors MixerColumnComponent::getNodeId() -- lets MixerPanelComponent::setMidiLearnArmed()
     *  find Master by nodeId the same way it finds a strip column. */
    juce::AudioProcessorGraph::NodeID getNodeId() const noexcept { return nodeId_; }

    /** FRO11: same contract as MixerColumnComponent::unbindFromGraph() -- unlike a strip column,
     *  this component survives a MixerPanelComponent::rebuild() (it's a persistent member, not
     *  recreated), so without this its fader would otherwise stay bound to Master's OLD gain param
     *  across a graph-replacing restore until setNodeId() ran again, which is exactly the freed-
     *  parameter window the FRO11 crash needs. */
    void unbindFromGraph();

    /** FRO148: the level LEAVING the master chain (AudioEngine::takeOutputMeterPeak(MeterReader::Mixer, leg)) --
     *  what the meter and clip/peak readout read once Master has >= 1 insert. Null falls back to Master's own
     *  pre-insert latch. Survives unbindFromGraph(): it points at the engine, not into the graph. */
    std::function<float(int leg)> outputPeakProvider;
    /** Forwarded from the insert list -- see MixerInsertList::onEditOnCanvas / onMutated. */
    std::function<void(const juce::String&)> onEditOnCanvas;
    std::function<void()> onMutated;

    MixerInsertList& getInsertListForTest() noexcept { return insertList_; }

    /** One 10 Hz tick, same driving chain as MixerColumnComponent::refreshMeter(). */
    void refreshMeter(float elapsedSeconds);

    MixerMeter& getMeterForTest() noexcept { return meter_; }
    MixerMeterReadout& getMeterReadoutForTest() noexcept { return meterReadout_; }
    void resetMeterReadout() { meterReadout_.reset(); }

    /** FRO146: sibling of MixerColumnComponent::onResetAllMetersRequested -- fires on an
     *  Option/Alt-click of Master's own readout. */
    std::function<void()> onResetAllMetersRequested;

    /** FRO18: toggles Master's mute through the same undo bracket the M button's onClick already
     *  used -- MixerPanelComponent's keyPressed calls this directly, same seam as
     *  MixerColumnComponent::toggleMuted(). A no-op after unbindFromGraph(). */
    void toggleMuted();

    /** FRO18: the fader nudged one undo step, or false when nothing is bound. */
    bool nudgeFader(float deltaDb) { return fader_.nudge(deltaDb); }

    void setKeyboardFocused(bool focused);
    bool isKeyboardFocusedForTest() const noexcept { return keyboardFocused_; }

    /** FRO18 review fix: same contract as MixerColumnComponent::grabAccessibilityFocus() -- moves
     *  VoiceOver's cursor to Master's own fader when Master becomes the keyboard-walked focus. */
    void grabAccessibilityFocus() { fader_.grabAccessibilityFocus(); }
    juce::Component& getAccessibilityFocusTargetForTest() noexcept { return fader_.getSlider(); }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // ---- MIDI Learn (FRO133 -- Master has exactly one learnable control, its fader/"gain" param;
    // see MixerColumnComponent's sibling pattern for the coverage-table entries with more than
    // one) ----
    juce::RangedAudioParameter* findMidiLearnableParamForTest(const juce::Component* component) const;
    bool isMidiLearnBadgeMappedForTest(const juce::Component* component) const;
    /** FRO135: the fader, for the pick-target overlay. */
    void collectPickCandidates(std::vector<PickCandidate>& out) const;
    /** FRO256: mirrors MixerColumnComponent::getMidiLearnArmedRepaintCountForTest -- see that
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

private:
    void refreshMuteAccessibility();
    void refreshMidiLearnBadges();
    /** FRO256: mirrors MixerColumnComponent::repaintArmedMidiLearnOutline -- called from
     *  refreshMeter()'s existing 10 Hz tick so the breathing outline actually animates. */
    void repaintArmedMidiLearnOutline();
    void paintMidiLearnOverlays(juce::Graphics& g);

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    GraphEditor* graphEditor_ = nullptr;
    juce::AudioProcessorGraph::NodeID nodeId_;

    // FRO133: the fader's bound param (paramID "gain") -- null when nothing is bound, same
    // lifetime contract as fader_'s own internal param_ (MixerFader::isBoundForTest()). Cleared by
    // unbindFromGraph() before the graph-replacing mutation frees it.
    juce::RangedAudioParameter* midiLearnableFaderParam_ = nullptr;
    bool midiLearnBadgeMapped_ = false;
    juce::String midiLearnArmedParamId_;
    double midiLearnArmedSinceMs_ = 0.0;
    // FRO256: backs getMidiLearnArmedRepaintCountForTest() -- test-only, never read in production.
    int midiLearnArmedRepaintCount_ = 0;
    std::function<void(juce::PopupMenu&)> showContextMenuHook_ = [](juce::PopupMenu& m) {
        m.showMenuAsync(juce::PopupMenu::Options());
    };

    // FRO148: true once setColumn() saw >= 1 insert -- switches the meter to outputPeakProvider.
    bool hasInserts_ = false;
    float takeMeterPeak(int leg);

    MixerColumnHeader header_;
    MixerInsertList insertList_;
    MixerFader fader_;
    MixerMeter meter_;
    MixerMeterReadout meterReadout_;
    juce::TextButton muteButton_{"M"};
    bool keyboardFocused_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerMasterColumn)
};

} // namespace synth::ui
