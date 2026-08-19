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
#include "ThresholdControlComponent.h"
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

    /** Serum-style modulation drop: the visible knob under `localPoint`, reported as the input
     *  Port its CV jack would be.
     *
     *  Deliberately NOT folded into getPortForPoint — that one also decides what starts a drag
     *  on mouse-down, and a knob has to keep starting a value drag there, not a cable. This is
     *  only consulted when a cable is already in flight and no real jack was hit. */
    std::optional<Port> getModTargetPortForPoint(juce::Point<int> localPoint) const;

    /** Highlights a knob as the pending modulation drop target, or clears it with -1.
     *  Returns true when the highlight changed, so the caller can repaint only on a change. */
    bool setModDropTargetChannel(int channelIndex);
    int getModDropTargetChannel() const noexcept { return modDropTargetChannel; }

    // --- Audio-file drag and drop (Sampler only) ---
    // Returns false for every other module type so the drop falls through to GraphEditor, which
    // creates a new Sampler for it. Dropping onto an existing Sampler replaces its sample.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Test/inspection helper: true while a valid audio file is hovering over this module.
    bool isFileDragHighlighted() const noexcept { return fileDragHighlight; }

    /** Index of the knob a modulation ring for `paramName` should be drawn on, or -1 when no
     *  ring belongs on the card right now.
     *
     *  Visibility is part of the answer, not just a paint-time detail: a knob on an inactive tab
     *  page keeps its last bounds, so a ring drawn from them lands on empty card. Public so the
     *  rule can be tested without a themed LookAndFeel and a live modulation routing. */
    int getModRingSliderIndex(const juce::String& paramName) const;

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
    std::unique_ptr<ThresholdControlComponent> thresholdControl;
    std::unique_ptr<WavetableDisplayComponent> wavetableDisplay;
    std::unique_ptr<juce::TextButton> loadWavetableButton;
    std::unique_ptr<juce::FileChooser> wavetableChooser;

    // Wavetable folder browser: pick a directory once, then walk it with prev/next without
    // reopening a file chooser for every table.
    std::unique_ptr<juce::TextButton> wavetableFolderButton;
    std::unique_ptr<juce::TextButton> wavetablePrevButton;
    std::unique_ptr<juce::TextButton> wavetableNextButton;
    std::unique_ptr<juce::Label> wavetableNameLabel;
    std::unique_ptr<juce::FileChooser> wavetableFolderChooser;

    // Wavetable card tab strip. The module carries 23 controls; showing them all at once made a
    // flat wall with no hierarchy, so they are grouped into pages with the two performance
    // controls (Position, Warp) pinned above the strip. Jacks are NEVER tabbed — every CV input
    // stays on the card so a cable can never point at a hidden port.
    juce::OwnedArray<juce::TextButton> wavetableTabs;
    int activeWavetableTab = 0;
    // Parallel to sliders / comboBoxes: which tab owns each control.
    // kTabPinned = above the strip, kTabChrome = laid out with the display band.
    juce::Array<int> sliderTabIndex;
    juce::Array<int> comboTabIndex;

    // Sampler-only chrome: waveform overview, "Load Sample…" button and the loaded file name.
    std::unique_ptr<SampleWaveformComponent> sampleWaveform;
    std::unique_ptr<juce::TextButton> loadSampleButton;
    std::unique_ptr<juce::Label> sampleNameLabel;
    std::unique_ptr<juce::FileChooser> sampleChooser;
    bool fileDragHighlight = false;
    // Channel index of the knob currently highlighted as a modulation drop target, or -1.
    int modDropTargetChannel = -1;

    std::unique_ptr<juce::DrawableButton> bypassButton;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
    std::unique_ptr<juce::DrawableButton> muteButton;
    std::unique_ptr<juce::ButtonParameterAttachment> muteAttachment;
    std::unique_ptr<juce::DrawableButton> deleteButton;
    std::unique_ptr<juce::DrawableButton> dualIOButton;
    std::unique_ptr<juce::ButtonParameterAttachment> dualIOAttachment;

    AppUndoManager* undoManager = nullptr;
    std::map<int, float> gestureStartValues;
    juce::Point<int> dragStartPosition;

    // True only between a body mouseDown that armed the ComponentDragger and its mouseUp. A
    // Shift/Cmd-click toggles selection WITHOUT arming the dragger, and this flag stops the
    // subsequent mouseDrag from moving a component the dragger was never started on.
    bool bodyDragActive = false;

    float cachedRMS = 0.0f;
    float lastPaintedRMS = -1.0f;
    std::vector<float> rmsReadBuffer;
    int lastActiveStep = -1;

    void createControls();
    void updateLayout();

    // Raw->LogicalPort snapshots of the module's current channel layout. "poly" and "dualIO"
    // change that layout, so these are captured at construction and refreshed on each toggle.
    // Kept because rewireForPolyChange needs to know which visible jack an existing connection was
    // anchored to *before* the toggle — by the time the listener fires, the live mapping already
    // describes the new layout.
    std::vector<LogicalPort> cachedInputPortMap;
    std::vector<LogicalPort> cachedOutputPortMap;
    void captureLogicalPortMaps();

    // Re-anchors this module's connections onto its new channel layout after a poly toggle.
    void applyPolyStateChange();

    // Dual I/O changes which jacks are *visible*. For an FX pair the raw ch0/ch1 legs stay put and
    // no cable is touched; for a split-block module (#219) collapsing also hides its kRightBase
    // leg, so GraphEditor drops the cables left on it — an invisible jack cannot be unplugged.
    // Message thread only.
    void applyDualIOLayoutChange();
    void updateDualIOTooltip();

    // Macro bank only. Re-lays the component for the new "Knobs" count and asks the GraphEditor
    // to settle the consequences (drop routings on jacks that just disappeared, nudge neighbours
    // clear of the new footprint). Message thread only.
    void applyMacroCountChange();

    // Positions the Knobs/Bipolar header controls and one knob row per visible macro; hides the
    // rows above `count` without destroying their sliders or parameters.
    void layoutMacroBank(int count);

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

    // Opens an async directory chooser and points the module's browser at the result.
    void openWavetableFolderChooser();

    // Loads a wavetable file into the module and selects the "Loaded File" table choice.
    // Shared by the load button, the folder browser and the file drop handler.
    bool loadWavetableIntoModule(const juce::File& file);

    // Steps the folder browser by delta entries and refreshes the caption.
    void stepWavetableBrowser(int delta);

    // Repoints the wavetable caption at whatever the module currently holds.
    void refreshWavetableLabel(const juce::String& fallbackMessage = {});

    // --- Wavetable tab strip ---
    // Page sentinels (kTabPinned / kTabChrome) live in ModuleComponent.cpp beside the page table.

    // Builds the tab buttons and assigns every slider / combo to a page. Must run AFTER
    // createControls(), which is what populates sliders and comboBoxes.
    void createWavetableTabs();

    // Shows only the active page's controls. Called on construction and on every tab click.
    void applyWavetableTabVisibility();

    // Lays out the pinned row, the tab strip and the active page starting at `y`.
    // Returns the y below them. Every page is measured so the card is sized to the TALLEST,
    // which stops the card resizing (and shoving its neighbours) as tabs are switched.
    int layoutWavetableTabs(int y, int contentX, int contentW, bool apply);

    // True for cards whose jack count justifies a split (left-edge + right-edge) input gutter.
    int getInputPortColumns() const;

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
