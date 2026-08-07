#pragma once

#include "../AppUndoManager.h"
#include "../AudioEngine.h"
#include "../Modules/FilterModule.h"
#include "../Modules/MidiKeyboardModule.h"
#include "EQCurveComponent.h"
#include "EQWindow.h"
#include "FrequencyResponseComponent.h"
#include "SampleWaveformComponent.h"
#include "ScopeComponent.h"
#include "WavetableDisplayComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

class GraphEditor; // Forward declaration

class ModuleComponent
    : public juce::Component
    , public juce::Timer
    , public juce::FileDragAndDropTarget
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

    // --- Audio-file drag and drop (Sampler only) ---
    // Returns false for every other module type so the drop falls through to GraphEditor, which
    // creates a new Sampler for it. Dropping onto an existing Sampler replaces its sample.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Test/inspection helper: true while a valid audio file is hovering over this module.
    bool isFileDragHighlighted() const noexcept { return fileDragHighlight; }

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
    // Pop-out EQ editor. The dialog self-deletes when closed, so we only hold a SafePointer and
    // must close it in detachFromProcessor() — it references the module by reference.
    std::unique_ptr<juce::TextButton> eqPopOutButton;
    juce::Component::SafePointer<juce::DialogWindow> eqWindow;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboardComponent;
    std::unique_ptr<WavetableDisplayComponent> wavetableDisplay;
    std::unique_ptr<juce::TextButton> loadWavetableButton;
    std::unique_ptr<juce::FileChooser> wavetableChooser;

    // Sampler-only chrome: waveform overview, "Load Sample…" button and the loaded file name.
    std::unique_ptr<SampleWaveformComponent> sampleWaveform;
    std::unique_ptr<juce::TextButton> loadSampleButton;
    std::unique_ptr<juce::Label> sampleNameLabel;
    std::unique_ptr<juce::FileChooser> sampleChooser;
    bool fileDragHighlight = false;

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

    // First y below the header, the MIDI row and every visible jack. Derived from getPortCenter()
    // rather than re-deriving the geometry, so content can never land on top of a port label —
    // which is exactly what happened when two separate copies of the formula drifted apart.
    int getContentTopY();

    // Single source of truth for the default (non-Sequencer/ADSR/keyboard) body layout.
    // apply == false measures only and touches no bounds; apply == true positions the children.
    // Returns the total height the body needs, including bottom padding.
    int layoutDefaultContent(bool apply);

    // Builds the Sampler's waveform view / load button / file-name label. No-op for other modules.
    void createSamplerControls();

    // Repoints the file-name label at whatever the module currently holds.
    void refreshSampleLabel(const juce::String& fallbackMessage = {});

    // Builds the Wavetable module's frame display and load button. No-op for other modules.
    void createWavetableControls();

    // Opens an async file chooser and, on success, loads the chosen file into the
    // Wavetable module and switches its Table parameter to "Loaded File".
    void openWavetableChooser();

    // Shared step-column layout helper used by Sequencer and PolySequencer.
    // Positions Gate, Pitch/Root, and F.Env/Chord controls for a single step column.
    void layoutSequencerStepColumn(int step, int colX, int startY);

    // --- Parametric EQ card ---
    // One column per band (on/off toggle above Freq / Gain / Q), so the knobs read as a grid
    // instead of the generic two-up flow. Height is computed by parametricEQHeight() from the
    // same constants the layout uses, so the two cannot drift apart.
    void layoutParametricEQ();
    int parametricEQHeight() const;
    void openEqWindow();
    // Brackets a curve's edits in one undo step. Uses a SafePointer, so a pop-out window that
    // outlives this component becomes a no-op rather than a dangling call.
    void wireEqGestureCallbacks(EQCurveComponent& curve);
    // Auto-UI controls are looked up by their parameter display name (set as the componentID in
    // createControls), which keeps the layout independent of parameter ordering.
    juce::ToggleButton* findToggleByName(const juce::String& name) const;
    void layoutNamedKnob(const juce::String& name, int x, int y, int w, int h);

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
