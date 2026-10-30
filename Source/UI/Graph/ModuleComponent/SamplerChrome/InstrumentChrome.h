#pragma once

// What ModuleComponent asks of the chrome a card draws above its body when the module plays an Instrument (the
// Sampler's SamplerChrome, the Drum Kit's DrumKitChrome). Message thread only for every member.

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;

namespace synth::ui {

class InstrumentChrome {
public:
    struct Host {
        juce::Component* card = nullptr; ///< Parent of every component the chrome adds; outlives it.
        AppUndoManager* undo = nullptr;  ///< Null: instrument loads record no undo step.
        juce::AudioProcessorGraph* graph = nullptr;
        std::function<void()> kitChanged;           ///< Called when the loaded instrument changed (may be empty).
        std::function<bool()> drumMachineAvailable; ///< Drum Kit: does a track play it? (empty: no)
        std::function<void()> openDrumMachine;      ///< Drum Kit: the "Drum machine" button was pressed.
        std::function<void()> relayout;             ///< The chrome's height changed: lay the card out again.
        std::function<void(juce::AudioProcessorGraph::NodeID)> showNode; ///< Drum Kit: show a module on the canvas.
    };

    virtual ~InstrumentChrome() = default;

    /** Places the chrome from `y` (the card's content origin) and returns the y below it. */
    virtual int layout(int y, int contentX, int contentW, int narrowX, int narrowW, bool apply) = 0;
    /** Places what the chrome shows under the card's body controls from `y`; returns the y below it. */
    virtual int layoutBelowBody(int y, int /*contentX*/, int /*contentW*/, bool /*apply*/) { return y; }
    /** The card's gated tick. */
    virtual void poll() = 0;
    /** Re-reads the module (after a project's deferred loads landed). */
    virtual void refreshLoaded() = 0;

    // ---- File drop and the Sampler's sample name: nothing for a chrome that has none ----
    virtual void refreshSampleLabel(const juce::String& /*fallbackMessage*/ = {}) {}
    virtual bool isInterestedInDrop(const juce::StringArray&) const { return false; }
    virtual void dragEntered(const juce::StringArray&) {}
    virtual void dragLeft() {}
    virtual bool dropFiles(const juce::StringArray&) { return false; }
    virtual void paintDropHint(juce::Graphics&, juce::Rectangle<int>) const {}
};

} // namespace synth::ui
