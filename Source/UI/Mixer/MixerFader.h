#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;

// MixerFader.h -- FRO11 (P9-5, docs/mixer.md §5.10): a dB-scale vertical fader bound to a
// ChannelStripModule/MasterModule's own `gain` AudioParameterFloat*, reused by both (their ranges
// are identical -- ChannelStripModule::kMinGainDb/kMaxGainDb).
//
// Undo: a juce::AudioProcessorParameter::Listener on the bound param brackets ONE
// captureBeforeState()/pushSnapshotFromCapture() pair per drag gesture -- the exact mechanism
// ModuleComponent::parameterGestureChanged already uses for every module's own knobs
// (Source/UI/Graph/ModuleComponent/ModuleComponentInteraction.cpp), just triggered from the
// mixer's own slider instead of a module card's.

namespace synth::ui {

class MixerFader
    : public juce::Component
    , private juce::AudioProcessorParameter::Listener {
public:
    MixerFader();
    ~MixerFader() override;

    /** Binds to `param` (already added to a live processor). `graph`/`undoManager` must outlive
     *  every gesture made while bound. Call unbind() (or bind() again) before the param's owning
     *  node can be destroyed. */
    void bind(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, juce::AudioParameterFloat& param);
    /** Idempotent and null-safe -- a call with nothing bound (param_ already null) is a no-op, so
     *  destruction, a defensive rebind, and FRO11's pre-restore unbind hook can all call it freely
     *  without checking bind state first. */
    void unbind();

    bool isBoundForTest() const noexcept { return param_ != nullptr; }

    /** FRO18: nudges the bound param by `deltaDb` (clamped to the param's own range), as exactly
     *  ONE undo step -- begin/end the gesture around a single setValueNotifyingHost the same way a
     *  slider drag does, so parameterGestureChanged (already listening -- see the class comment)
     *  brackets it, not a second undo mechanism. A no-op (false) when nothing is bound, so the
     *  keyboard path stays inert after unbindFromGraph() the same way the click handlers already
     *  are (MixerColumnComponent::unbindFromGraph nulls onClick). */
    bool nudge(float deltaDb);

    /** Counts only unbind() calls that actually detached a live parameter (param_ was non-null at
     *  entry) -- a defensive no-op unbind() on an already-unbound fader never bumps this. Lets a
     *  test prove the FRO11 pre-restore hook (MixerPanelComponent::unbindAllColumns(), reached via
     *  GraphEditor::onBeforeDetachAllModuleComponents) actually ran and did real work, not just
     *  that nothing crashed. */
    static int getLiveUnbindCallCountForTest() noexcept { return liveUnbindCallCountForTest_; }

    juce::Slider& getSlider() noexcept { return slider_; }

    /** FRO18: "<channel name> fader" (e.g. "Lead 1 fader") -- the accessible name VoiceOver reads
     *  ahead of the slider's own value text below. */
    void setChannelName(const juce::String& name);

    /** FRO18 review fix: moves REAL accessibility focus (not keyboard focus) to the fader slider's
     *  handler when the column it belongs to becomes the panel's keyboard-walked focus. Safe to
     *  call unconditionally -- getAccessibilityHandler() returns null with no native peer yet
     *  (headless CI, or before the window is shown), the exact case slider_.getAccessibilityHandler()
     *  guards below. AccessibilityHandler::grabFocus() only calls Component::grabKeyboardFocus()
     *  when the component itself wants keyboard focus, which the slider deliberately doesn't
     *  (setWantsKeyboardFocus(false) above, the T160 trap) -- so this moves VoiceOver's cursor
     *  without stealing the panel's own real OS keyboard focus (MixerPanelComponent is the single
     *  focusable leaf). */
    void grabAccessibilityFocus();

    void resized() override;

private:
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    juce::Slider slider_;
    juce::Label readout_;
    std::unique_ptr<juce::SliderParameterAttachment> attachment_;

    juce::AudioProcessorGraph* graph_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    juce::AudioParameterFloat* param_ = nullptr;
    bool gestureActive_ = false;

    static int liveUnbindCallCountForTest_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFader)
};

} // namespace synth::ui
