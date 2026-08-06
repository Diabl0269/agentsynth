#pragma once

#include "../AppUndoManager.h"
#include "../AudioEngine.h"
#include "../Modules/FilterModule.h"
#include "../Modules/MidiKeyboardModule.h"
#include "EQCurveComponent.h"
#include "FrequencyResponseComponent.h"
#include "ScopeComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

class GraphEditor; // Forward declaration

class ModuleComponent
    : public juce::Component
    , public juce::Timer
    , public juce::AudioProcessorParameter::Listener {
public:
    ModuleComponent(juce::AudioProcessor* module, juce::AudioProcessorGraph::NodeID nodeId, GraphEditor& owner,
                    AppUndoManager* undoMgr = nullptr);

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    ~ModuleComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void moved() override;

    juce::AudioProcessor* getModule() const { return module; }
    juce::AudioProcessorGraph::NodeID getNodeId() const { return nodeId; }

    // Safely detach from the processor before graph rebuild.
    // Removes listeners, destroys attachments, stops timer, nulls module pointer.
    void detachFromProcessor();

    // Interaction Logic
    struct Port {
        juce::Rectangle<int> area;
        int index;
        bool isInput;
        bool isMidi = false;
    };

    std::optional<Port> getPortForPoint(juce::Point<int> localPoint);
    juce::Point<int> getPortCenter(int index, bool isInput);

private:
    juce::AudioProcessor* module;
    juce::AudioProcessorGraph::NodeID nodeId;
    GraphEditor& owner;
    juce::ComponentDragger dragger;

    // Auto-UI
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> sliderLabels;
    juce::OwnedArray<juce::ComboBox> comboBoxes;
    juce::OwnedArray<juce::Label> comboLabels;
    juce::OwnedArray<juce::ToggleButton> toggles;

    // Attachments need to be kept alive.
    // We are using raw pointers for parameters currently.
    juce::OwnedArray<juce::SliderParameterAttachment> sliderAttachments;
    juce::OwnedArray<juce::ComboBoxParameterAttachment> comboAttachments;
    juce::OwnedArray<juce::ButtonParameterAttachment> buttonAttachments;

    std::unique_ptr<ScopeComponent> scopeComponent;
    std::unique_ptr<juce::ToggleButton> scopeToggle;
    std::unique_ptr<FrequencyResponseComponent> freqResponseComponent;
    std::unique_ptr<EQCurveComponent> eqCurveComponent;
    std::unique_ptr<juce::ToggleButton> spectrumToggle;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboardComponent;

    std::unique_ptr<juce::DrawableButton> bypassButton;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
    std::unique_ptr<juce::DrawableButton> muteButton;
    std::unique_ptr<juce::ButtonParameterAttachment> muteAttachment;
    std::unique_ptr<juce::DrawableButton> deleteButton;

    AppUndoManager* undoManager = nullptr;
    std::map<int, float> gestureStartValues;
    juce::Point<int> dragStartPosition;

    float cachedRMS = 0.0f;
    float lastPaintedRMS = -1.0f;
    std::vector<float> rmsReadBuffer;
    int lastActiveStep = -1;

    void createControls();
    void updateLayout();

    // Shared step-column layout helper used by Sequencer and PolySequencer.
    // Positions Gate, Pitch/Root, and F.Env/Chord controls for a single step column.
    void layoutSequencerStepColumn(int step, int colX, int startY);

    // Apply SVG icons to bypass/mute/delete DrawableButtons from the active LnF.
    // No-op when the themed LnF is not installed (headless tests).
    void applyHeaderButtonIcons();

    // Rebuild the root-menu items of each waveform ComboBox with fresh icon clones
    // from the now-retinted IconLibrary, then restore the previous selection without
    // firing the parameter attachment. Called from lookAndFeelChanged() after a theme
    // switch so popup glyphs match the new theme tint.
    void refreshWaveformComboIcons();

    // Refresh icon images whenever the LookAndFeel is changed (e.g. theme switch).
    void lookAndFeelChanged() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleComponent)
};
