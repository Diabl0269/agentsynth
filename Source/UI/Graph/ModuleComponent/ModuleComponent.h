#pragma once

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "UI/Graph/ModuleComponent/HostedParameterAttachment.h"
#include "UI/Graph/PickTargetOverlay/PickCandidate.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/SampleWaveformComponent.h"
#include "UI/ModuleViews/ScopeComponent.h"
#include "UI/ModuleViews/ThresholdControlComponent.h"
#include "UI/ModuleViews/WavetableDisplayComponent.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <vector>

class GraphEditor;        // Forward declaration
class ExternalMidiModule; // Forward declaration — see Modules/ExternalMidiModule.h

namespace synth::ui {
class ZoomFrozenCachedImage; // Forward declaration — see ZoomFrozenCachedImage.h
}

namespace synth::theme {
class AppLookAndFeel; // Forward declaration — see Theme/AppLookAndFeel.h
}

namespace synth {
struct MacroPort;         // Forward declaration — see ../MacroSet.h
class HostedPluginModule; // Forward declaration — see Plugin/Hosting/HostedPluginModule.h
} // namespace synth

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

    /** T162: the colour a docked macro-port widget paints its own jack dot with. A port user
     *  colour (synth::MacroPort::colour, set from the Configure I/O modal's swatch) wins when it
     *  is set; otherwise the kind tint passes through — audioWire for a MIDI jack, accent for an
     *  Audio/CV jack — the EXACT fallback MacroCardComponent::paint already uses, so an expanded
     *  docked widget and a collapsed card read a port's jack identically (a custom colour, or —
     *  with none — the same tint). `port` is the boundary port the node fronts (from
     *  owner.macroPortOwnerFor); null (a docked widget whose port entry has drifted away, which
     *  by construction shouldn't happen) just means "unset", i.e. the kind tint. Side-effect-free
     *  and static so a test can pin the propagation without capturing pixels — paintMacroPortWidget
     *  is the only caller. */
    static juce::Colour resolveMacroPortJackColour(const synth::MacroPort* port, juce::Colour kindTint);

    // Live preview of a docked widget's jack colour while the Configure I/O picker is open -- a view-layer
    // transient, so a pick pushes no undo step. Each setter is idempotent: it reports whether this
    // surface actually changed, so a tick that left the colour alone repaints nothing.
    bool setPortColourPreview(juce::Colour c) {
        if (portColourPreview_ && *portColourPreview_ == c)
            return false;
        portColourPreview_ = c;
        return true;
    }
    bool clearPortColourPreview() {
        if (!portColourPreview_.has_value())
            return false;
        portColourPreview_.reset();
        return true;
    }

    // The jack's actual colour (preview > stored > tint) -- mirrors paint, so a headless test can read it.
    juce::Colour effectiveMacroPortJackColour(const synth::MacroPort* port, juce::Colour kindTint) const;
    bool hasPortColourPreviewForTest() const noexcept { return portColourPreview_.has_value(); }

    /** Re-measures the card after its VISIBLE PORT COUNT changed for a reason that is not a
     *  parameter gesture — today only Audio Input, whose jacks follow the audio device. Same three
     *  steps applyMacroCountChange takes for the Macro bank's "Knobs" parameter. */
    void refreshPortLayout();

    /** Output-card identity treatment only: repoints the muted destination line drawn under the
     *  Audio Output card's title (device name + sample rate + channel count, or "Host audio" in
     *  HostMode::Hosted, or empty to hide the line entirely). A no-op on every other module —
     *  callers do not need to check isAudioOutputIONode() first. Pushed in by
     *  GraphEditor::refreshOutputDeviceInfo() (MainComponent -> GraphEditor -> here) whenever
     *  AudioEngine's device state changes; never polled. MESSAGE THREAD ONLY. Repaints (the single
     *  ZoomFrozenCachedImage refresh seam — see refreshPortLayout above) only when the text
     *  actually changed. */
    void setOutputDeviceInfoText(const juce::String& text);
    const juce::String& getOutputDeviceInfoTextForTest() const noexcept { return outputDeviceInfoText; }

    // Interaction Logic
    struct Port {
        juce::Rectangle<int> area;
        int index;
        bool isInput;
        bool isMidi = false;
    };

    /** Header band the card title is drawn in, and the double-click-to-rename hit zone. */
    static constexpr int kHeaderHeight = 24;

    /** What the header paints: the user's custom title when set, else the auto-numbered module
     *  name. Never reads the processor name directly — see GraphEditor::getModuleTitle. */
    juce::String cardTitle() const;

    /** Opens the inline rename editor over the title. Public so a test can drive it without
     *  synthesising a double-click. No-op for an Attenuverter (it has no header). */
    void beginTitleRename();

    /** Closes the inline editor, committing the typed text or discarding it. */
    void finishTitleRename(bool commit);

    /** True while the inline rename editor is open. */
    bool isRenamingTitle() const noexcept { return titleEditor != nullptr; }

    /** Height of the header strip above the port gutter. Shared with
     *  GraphEditor::estimatePortCenter, which has to place a drag GHOST's jacks at exactly the same
     *  y as a real card's: the two carried separate literals (38 here, 30 there) and every smart
     *  connection preview cable terminated 8px above the jack dot it claimed to land on. One
     *  constant so they cannot drift again. */
    static constexpr int kPortGutterHeaderHeight = 38;

    /** Compact docked-widget geometry for the four macro-port types (Macro In/Out, Macro MIDI
     *  In/Out — P8-15 founder-review fix F2, docs/macros/ports.md#how-a-port-is-drawn). Shared with
     *  GraphEditor::estimateModuleSize (its own drag-ghost estimate must match the real widget) and
     *  GraphEditor::dockMacroPortWidgets (the widget's docked position reads its live getWidth/
     *  getHeight, sized from these), the same "one constant so two files cannot drift apart"
     *  reasoning kPortGutterHeaderHeight's own comment states.
     *
     *  P8-15 founder-review fix G4 ("make the routing more elegant and sleek, it's currently too
     *  large"): shrunk from a 104x26(mono) box that read as a small module card to a slim edge
     *  chip — roughly a 30% cut in drawn area (104x26=2704px^2 -> 92x21=1932px^2, -28.6%; the
     *  104x44/92x36 Stereo pair lands within a point of the same ratio). The values below are a
     *  tuned point, not a formula — see paintMacroPortWidget()'s own comment for the matching jack/
     *  font/corner-radius cuts, and MacroPortWidgetTests.cpp's
     *  `MacroPortWidgetG4.RealisticPortNameFitsWithinTheWidgetAtFullUnscaledSize` for the
     *  constraint that actually bounds how far the width could shrink: a realistic port name
     *  ("Delay 1 Audio") has to still fit un-truncated. */
    static constexpr int kMacroPortWidgetWidth = 92;
    /** y of the first (or only) jack row, and the fixed y a MIDI port's single jack sits at. */
    static constexpr int kMacroPortWidgetHeaderY = 13;
    /** Vertical spacing between stacked jack rows — only Stereo (2 visible jacks a side) uses a
     *  second row; Mono, Poly-N and MIDI are always exactly one row. */
    static constexpr int kMacroPortWidgetRowStep = 15;
    /** Clearance reserved below the last jack row. */
    static constexpr int kMacroPortWidgetBottomPad = 8;

    std::optional<Port> getPortForPoint(juce::Point<int> localPoint);
    juce::Point<int> getPortCenter(int index, bool isInput);

    /** Fixed MIDI jack anchor, in local coordinates — MIDI In sits top-left, MIDI Out top-right,
     *  at kPortGutterHeaderHeight on an ordinary card or kMacroPortWidgetHeaderY on a macro-port
     *  widget (no header of its own); they don't participate in the ordinary audio jack stack
     *  getPortCenter lays out. This is the single ground truth paint() and getPortForPoint() both
     *  read from; anything anchoring a MIDI cable/preview must go through it too, or it drifts
     *  from the drawn jack the way GraphEditor::rebuildVisibleCables once did (T149). */
    juce::Point<int> getMidiPortCenter(bool isOutput) const;

    /** Re-lays the card after its Dual I/O parameter changed. For an FX pair the raw ch0/ch1 legs
     *  stay put and no cable is touched; for a split-block module (#219) collapsing also hides its
     *  kRightBase leg, so GraphEditor drops the cables left on it — an invisible jack cannot be
     *  unplugged. NOTE: the card does get one jack row shorter when collapsed — see the reserved-
     *  count note on ModuleBase::getReservedInputPortCount for why height stability was not taken.
     *
     *  Public so GraphEditor can drive it synchronously when the Preferences default re-lays every
     *  module at once; the parameter-listener path is asynchronous. Message thread only. */
    void applyDualIOLayoutChange();

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

    /** Applies an automation-driven value to whichever slider/combo was built for `param`,
     *  denormalised via that parameter's own range, via setValue(..., dontSendNotification) — never
     *  touches the parameter, never fires the attachment, so there is no write-back loop and
     *  AutomationRecorder (a parameter listener) hears nothing. A `param` this component never built
     *  a control for (custom chrome, or a stale event for the wrong node) is a silent no-op. */
    void reflectParameterValue(const juce::AudioProcessorParameter* param, float normalized);

    /** Pins/unpins this card's raster scale for the duration of a canvas zoom gesture.
     *  Owned by GraphEditor; never call from the card itself. */
    void setRasterFrozen(bool frozen);
    bool isRasterFrozen() const noexcept;
    const synth::ui::ZoomFrozenCachedImage* getRasterCacheForTest() const noexcept { return rasterCache; }

    /** Output-card identity glyph bounds (see paint()'s isAudioOutputIONode block): proportional
     *  to the title's own cap-height rather than the full 24px header band, right-aligned to the
     *  activity LED's own right edge so the gap before the title text matches the header's
     *  existing padding rhythm. A pure function of the title font, pulled out of paint() so a test
     *  can assert the exact geometry painted without inspecting pixels. */
    static juce::Rectangle<float> outputCardIconBoundsForTest(const synth::theme::AppLookAndFeel& lf);

    /** Builds the same right-click menu mouseDown() shows for a body right-click (Copy/Duplicate/
     *  Replace with.../Delete Module and, when this module is a macro member, a "Macro: <name>"
     *  submenu — founder-review item 4, docs/macros/menu-and-membership.md#the-macro-menus-entry-points). Split out of
     * mouseDown() so a test can inspect the menu's actual content and invoke an item's action directly:
     * juce::PopupMenu's own showMenuAsync() never displays anything headless, so driving a synthesised right-click
     *  MouseEvent into mouseDown() alone has nothing observable to assert on. Public so a test can
     *  call it after that same synthesised mouseDown() (the real gesture/hit-test/selection-
     *  retargeting entry point) rather than skip straight to menu construction. */
    juce::PopupMenu buildModuleContextMenu();

    /** The small context menu a macro port node's own body right-click shows instead of
     *  buildModuleContextMenu() above (founder-review fix G7: "they cannot be removed" — a port
     *  node previously had NO delete affordance once its macro was gone, and Configure I/O is
     *  otherwise the only surface for it at all). Always offers "Delete Port". When the port still
     *  resolves to a live macro (GraphEditor::macroPortOwnerFor) it also offers "Rename Port..."
     *  and "Configure I/O..." — the two Configure I/O actions this node's own menu can reach
     *  without the user hunting down a member module first. Split out exactly like
     *  buildModuleContextMenu() is, for the same headless-test reason (that method's own comment). */
    juce::PopupMenu buildMacroPortContextMenu();

    /** Replaces what a real body right-click does with the menu buildModuleContextMenu() built --
     *  in place of the real showMenuAsync() (which opens a real popup and, on a headless Linux CI
     *  runner with no display, segfaults inside juce::PopupMenu::HelperClasses::MenuWindow --
     *  Tests/MacroContainerTests.cpp's MacroMemberContextMenu suite hit exactly this). Mirrors
     *  TimelineTrackHeaderComponent's setOpenMidiDestinationsPickerHookForTest(): defaults to the
     *  real behaviour, and a null hook restores it rather than leaving the seam disarmed. A test
     *  installs a capturing hook to inspect the menu the real mouseDown() gesture actually built,
     *  without ever opening a popup. */
    void setShowContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showContextMenuHook_ =
            hook ? std::move(hook) : [](juce::PopupMenu& m) { m.showMenuAsync(juce::PopupMenu::Options()); };
    }

    // ---- MIDI Learn (FRO130, ModuleComponentMidiLearn.cpp -- see its file comment for the design) ----

    /** Arms/clears (empty id) the breathing outline for the control bound to `paramId`. Message
     *  thread only. */
    void setMidiLearnArmedParam(const juce::String& paramId);

public:
    /** Hosted-plugin card: fired by the "Choose knobs..." button. Empty by default; the picker sets it. */
    std::function<void()> onChooseKnobsRequested;

    /** Test/inspection: the param a right-click on `component` would open MIDI Learn for, or null. */
    juce::RangedAudioParameter* findMidiLearnableParamForTest(const juce::Component* component) const {
        return midiLearnableRegistry_.find(component);
    }

    /** Test/inspection: `component`'s MIDI-mapped badge cache, as of the last timerCallback() tick. */
    bool isMidiLearnBadgeMappedForTest(const juce::Component* component) const {
        for (const auto& e : midiLearnableRegistry_.entries())
            if (e.component == component)
                return e.mapped;
        return false;
    }

    // FRO256: test seam for the armed breathing outline's per-tick repaint -- see timerCallback().
    int getMidiLearnArmedRepaintCountForTest() const noexcept { return midiLearnArmedRepaintCount_; }
    void collectPickCandidates(std::vector<synth::ui::PickCandidate>& out) const;

private:
    // Non-owning: the juce::Component base owns this via setCachedComponentImage(). See
    // ZoomFrozenCachedImage.h — installed instead of setBufferedToImage(true) so a canvas zoom
    // gesture can pin the raster scale. Never call setBufferedToImage() on a ModuleComponent again:
    // JUCE asserts (and silently deletes this cache) if a CachedComponentImage is already installed.
    synth::ui::ZoomFrozenCachedImage* rasterCache = nullptr;

    juce::AudioProcessor* module;
    juce::AudioProcessorGraph::NodeID nodeId;
    std::optional<juce::Colour> portColourPreview_; // live jack-colour preview; view-layer only
    GraphEditor& owner;
    juce::ComponentDragger dragger;

    // Set in the constructor to `[](juce::PopupMenu& m) { m.showMenuAsync(...); }`; a test replaces
    // it via setShowContextMenuHookForTest() so a real right-click mouseDown() can be driven in a
    // headless test without opening a popup that segfaults with no display (see that setter's
    // comment).
    std::function<void(juce::PopupMenu&)> showContextMenuHook_;

    // Auto-UI
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> sliderLabels;
    juce::OwnedArray<juce::ComboBox> comboBoxes;
    juce::OwnedArray<juce::Label> comboLabels;
    juce::OwnedArray<juce::ToggleButton> toggles;

    // Param -> control mapping for reflectParameterValue(), index-parallel to `sliders` /
    // `comboBoxes` respectively. Populated only in createControls()'s generic auto-UI branch (the
    // float/int slider and choice-combo cases) — a control built by bespoke chrome, or the
    // ExternalMidiModule device/channel combos (which don't go through ComboBoxParameterAttachment),
    // gets a null entry so the arrays stay aligned and a match against them is a safe no-op.
    juce::Array<juce::RangedAudioParameter*> sliderParams;
    juce::Array<juce::RangedAudioParameter*> comboParams;

    /** Every learnable control on this card, mapped to the RangedAudioParameter it drives --
     *  registered via registerMidiLearnable(), read by the menu builder and the badge/pulse paint.
     *  A nested class rather than living in ModuleComponentInternal.h: it's used as a member's TYPE
     *  here, and that header assumes ModuleComponent.h is already complete. Entries are non-owning
     *  (same lifetime as sliderParams/comboParams above). See ModuleComponentMidiLearn.cpp for the
     *  full design. */
    class MidiLearnableRegistry {
    public:
        struct Entry {
            juce::Component* component = nullptr;
            juce::RangedAudioParameter* param = nullptr;
            bool mapped = false;      // badge cache, written only by refreshBadges() below
            juce::String tooltip;     // assignment text, e.g. "MIDI: Knob 1 on Launchkey Mini MK3"
            juce::String baseTooltip; // component's own tooltip at registration (e.g. "Bypass")
        };

        void add(juce::Component& component, juce::RangedAudioParameter* param);
        juce::RangedAudioParameter* find(const juce::Component* component) const;
        const std::vector<Entry>& entries() const { return entries_; }

        /** Mapped display label for `paramId` ("Knob 1 on Launchkey Mini"), or empty if unmapped.
         *  Refreshes every entry's badge/tooltip cache; returns whether anything changed. */
        bool refreshBadges(const std::function<juce::String(const juce::String&)>& mappingLabelFor);

    private:
        std::vector<Entry> entries_;
    };

    MidiLearnableRegistry midiLearnableRegistry_;
    juce::String midiLearnArmedParamId_; // the control that should breathe, while armed
    double midiLearnArmedSinceMs_ = 0.0;
    // FRO256: backs getMidiLearnArmedRepaintCountForTest() -- test-only, never read in production.
    int midiLearnArmedRepaintCount_ = 0;

    // Attachments need to be kept alive.
    // We are using raw pointers for parameters currently.
    juce::OwnedArray<juce::SliderParameterAttachment> sliderAttachments;
    juce::OwnedArray<juce::ComboBoxParameterAttachment> comboAttachments;
    juce::OwnedArray<juce::ButtonParameterAttachment> buttonAttachments;

    std::unique_ptr<ScopeComponent> scopeComponent;
    std::unique_ptr<juce::ToggleButton> scopeToggle;
    std::unique_ptr<FrequencyResponseComponent> freqResponseComponent;
    std::unique_ptr<juce::ToggleButton> freqResponseToggle;
    std::unique_ptr<EQCurveComponent> eqCurveComponent;
    std::unique_ptr<juce::ToggleButton> spectrumToggle;
    // Pop-out EQ editor. The dialog self-deletes when closed, so we only hold a SafePointer and
    // must close it in detachFromProcessor() — it references the module by reference.
    std::unique_ptr<juce::TextButton> eqPopOutButton;
    juce::Component::SafePointer<juce::DialogWindow> eqWindow;
    // Hosted Plugin only: fires owner.onOpenPluginEditorRequested, routed to MainComponent's
    // HostedPluginWindowManager. Enabled only while the module reports hasInstance() — refreshed
    // each timerCallback() tick, since an async load can flip that at any moment.
    std::unique_ptr<juce::TextButton> openPluginEditorButton;

    // --- Hosted Plugin card body (FRO128, ModuleComponentHostedPluginCard.cpp) ---
    // Declared AFTER the widget arrays above so the attachments are destroyed before the widgets they point at.
    std::unique_ptr<juce::TextButton> chooseKnobsButton;
    juce::OwnedArray<synth::ui::HostedParameterAttachment> hostedAttachments_;
    class HostedCardBinding;
    std::unique_ptr<HostedCardBinding> hostedCard_;
    std::unique_ptr<juce::MidiKeyboardComponent> keyboardComponent;
    std::unique_ptr<ThresholdControlComponent> thresholdControl;

    // --- Envelope (ADSR) card: knob-and-graph panel (FRO112) ---
    // The breakpoint curve editor, collapsed by default (not persisted — matches the scope/
    // frequency-response toggles, not Macro Group's collapse; docs/modules/modules.md#adsr-envelope-module).
    std::unique_ptr<synth::ui::CurveEditorComponent> envelopeCurveEditor;
    std::unique_ptr<juce::ToggleButton> envelopeGraphToggle;
    // BPM|MS segmented control, wired to FRO113's `tempoSync` bool param (FRO117). The four
    // *Div note-division params FRO113 also added have no UI yet (FRO118) — they're excluded
    // from the generic per-param grid but not otherwise surfaced.
    std::unique_ptr<juce::TextButton> envelopeMsButton;
    std::unique_ptr<juce::TextButton> envelopeBpmButton;
    // True between the curve editor's onGestureStart/onGestureEnd (a live node/bend drag): the
    // graph is the gesture's source of truth for that span, so parameterValueChanged's reverse
    // sync (params -> graph) skips rebuilding the model out from under the drag.
    bool envelopeCurveGestureActive = false;

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
    // Shift-click toggles selection WITHOUT arming the dragger, and this flag stops the
    // subsequent mouseDrag from moving a component the dragger was never started on. Cmd no
    // longer toggles-without-dragging (FRO40) — see cmdReparentPending below.
    bool bodyDragActive = false;

    // Ctrl+press arms an insert-between DRAG and an additive-select TOGGLE at once, because at
    // mouse-down they are indistinguishable (mirrors PianoRollComponent::cmdToggleNote_). The press
    // collapses the selection onto this module so the drag is single-module — a group drag would
    // suppress smart connections — and mouseUp restores this snapshot and flips membership instead,
    // but only if nothing moved.
    bool ctrlTogglePending = false;
    std::vector<juce::AudioProcessorGraph::NodeID> ctrlPressSelection;

    // FRO40: Cmd+press arms a deferred additive-select TOGGLE and a macro-membership DRAG at
    // once, resolved at mouseUp by whether the press moved. See mouseDown/mouseUp.
    bool cmdReparentPending = false;
    std::vector<juce::AudioProcessorGraph::NodeID> cmdPressSelection;

    // FRO40: whether THIS drag can reparent right now — NOT `ctrlTogglePending ||
    // cmdReparentPending`, which is also true for a plain macOS Ctrl+drag. Re-derived every
    // mouseDrag tick for a single-module drag, so Cmd pressed/released mid-drag arms/disarms it.
    bool reparentArmed = false;
    bool computeReparentArmed(const juce::ModifierKeys& mods) const;

    // Inline rename editor, alive only between beginTitleRename and finishTitleRename. A child
    // component, so there is no window seam to stub out for a display-less test run.
    std::unique_ptr<juce::TextEditor> titleEditor;

    float cachedRMS = 0.0f;
    float lastPaintedRMS = -1.0f;
    std::vector<float> rmsReadBuffer;
    int lastActiveStep = -1;

    // Output-card identity treatment (Audio Output only — see setOutputDeviceInfoText). Empty
    // means "nothing to show yet" (before the first refresh, or a Hosted build that returned
    // nothing), which paint() treats as "draw no subtitle line" rather than an empty line.
    juce::String outputDeviceInfoText;

    void createControls();
    // Hosted Plugin card (ModuleComponentHostedPluginCard.cpp): builds the chrome, then binds to the module's instance
    // edges.
    void createHostedPluginControls(synth::HostedPluginModule& hosted);
    // Rebuilds the knobs/toggles/choices from the resolved layout; a no-op body while no instance is live.
    void rebuildHostedPluginCard();
    // Unbinds every hosted attachment and removes every hosted widget; `paramsAlive` false = never touch a parameter.
    void unbindHostedPluginCard(bool paramsAlive);
    // detachFromProcessor()'s half: leaves the module's observers and unbinds; safe after the module is gone.
    void releaseHostedPluginCard();
    // Re-measures the card and asks the canvas to accept its new size.
    void relayoutHostedPluginCard();
    void handleHostedGesture(const juce::AudioProcessorParameter& param, bool starting);
    // The Open Editor + Choose knobs... row; returns the y below it, `y` unchanged for any other module.
    int layoutHostedPluginChrome(int y, int narrowX, int narrowW, bool apply);
    // External MIDI's device + channel combos, extracted out of createControls (FRO117) to keep
    // that function under its own line-count ratchet. Neither combo is
    // ComboBoxParameterAttachment-driven (plain module state, not AudioParameters).
    void createExternalMidiControls(ExternalMidiModule* extMidi);
    void updateLayout();

    // Compact docked widget for the four macro-port types (P8-15 founder-review fix F2,
    // docs/macros/ports.md#how-a-port-is-drawn) — no header chrome, no body. layoutMacroPortWidget sizes the
    // card from the module's own visible jack count (updateLayout's early branch); its actual
    // canvas POSITION is decided separately, by GraphEditor::dockMacroPortWidgets against the
    // owning macro's hull. paintMacroPortWidget draws the tinted row, its jacks and the resolved
    // MacroPort name (paint()'s early branch).
    void layoutMacroPortWidget();
    void paintMacroPortWidget(juce::Graphics& g);

    // The pending-drop-target ring and the live Serum-style modulation rings on knobs. Split out of
    // paint() (which was at the function-size ratchet's ceiling) rather than grown further.
    void paintModulationRings(juce::Graphics& g, ModuleBase* mod, juce::Colour jackAccentColour);

    /** Right-click-any-knob entry point into the automation lane editor. Attached as a
     *  MouseListener on every generic auto-UI slider (createControls()'s float/int branches) via
     *  addMouseListener(this, false) — mouseDown() dispatches here first when e.eventComponent
     *  isn't this component's own body (checked by identity against `sliders`, index-parallel to
     *  `sliderParams` exactly like reflectParameterValue()'s lookup). Builds "Automate '<Param>'"
     *  plus the MIDI Learn block (appendMidiLearnMenuItems, ModuleComponentMidiLearn.cpp) under one
     *  separator, through showContextMenuHook_ so a test can capture it headlessly. */
    void showAutomateMenuForSlider(juce::RangedAudioParameter* param);

    // ---- MIDI Learn (FRO130, ModuleComponentMidiLearn.cpp) ----

    /** Every control on this card whose right-click should offer MIDI Learn -- see
     *  MidiLearnableRegistry's own comment. Called once per control from createControls(); a null
     *  `param` is silently ignored. */
    void registerMidiLearnable(juce::Component& control, juce::RangedAudioParameter* param);

    /** mouseDown()'s right-click handler for every registered control OTHER than a generic slider.
     *  Routes through showContextMenuHook_, same as showAutomateMenuForSlider. */
    void showMidiLearnOnlyMenu(juce::RangedAudioParameter* param);

    /** Shared by both menu builders above: appends the doc-exact MIDI Learn block
     *  (docs/control/midi-remote-ui.md#the-learn-interaction) to `menu`. A no-op if
     *  owner.onQueryMidiMappingsForNode isn't set. */
    void appendMidiLearnMenuItems(juce::PopupMenu& menu, juce::RangedAudioParameter* param);

    /** "MIDI Learn"/"MIDI Learn again" menu action: fires owner.onMidiLearnRequested(nodeId, paramId). */
    void armMidiLearnFor(const juce::String& paramId);

    /** "Forget MIDI" menu action: fires owner.onMidiForgetRequested(nodeId, paramId). */
    void forgetMidiFor(const juce::String& paramId);

    /** Refreshes every registered control's MIDI-mapped badge cache (ONE query per module) and
     *  repaints only if something changed. Called from the existing gated 15 Hz timerCallback. */
    void refreshMidiLearnBadges();

    /** Paints the "mapped" badge on every registered control that has one, and the armed control's
     *  breathing outline. Called from paint() (ModuleComponentPaint.cpp). */
    void paintMidiLearnOverlays(juce::Graphics& g);

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

    // The generic auto-UI knob grid (kKnobColumns across, wrapping; doubled on a double-width
    // card). Extracted out of layoutDefaultContent (which is at its own ratchet ceiling) so a
    // new block — the envelope graph section — has room to be inserted right after it.
    int layoutKnobGrid(int y, int contentX, int contentW, int width, bool apply);

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

    // --- Envelope (ADSR) card ---
    // Builds the graph disclosure toggle, the curve editor (fixed 5-node topology) and the
    // BPM|MS row; called from createControls() for ADSR only. Must run AFTER the generic
    // float-param loop above (it needs `sliders`/`sliderLabels` already built, to shorten their
    // captions and read attack/hold/decay/sustain/release's current values).
    void createEnvelopeCardControls();
    // Renames the five knob labels ("Attack" -> "ATK", ...) for ADSR only — componentIDs (used
    // for lookup/automation) are untouched, this is a display-only caption swap.
    void applyEnvelopeKnobShortLabels();
    // Brackets a whole curve drag in one undo step (mirrors wireEqGestureCallbacks) and toggles
    // envelopeCurveGestureActive around it.
    void wireEnvelopeGestureCallbacks();
    // Forward sync: reads envelopeCurveEditor's current model and writes attack/hold/decay/
    // release/sustain/*Curve back via setValueNotifyingHost (epsilon-gated, so an unmoved value
    // never emits a redundant host-automation write). Installed as onNodeChanged/onBendChanged.
    void writeEnvelopeParamsFromCurve();
    // Reverse sync: rebuilds a fresh CurveModel from the module's current parameter values and
    // hands it to envelopeCurveEditor->setModel(). No-op while envelopeCurveGestureActive (the
    // graph is already the source of truth mid-drag) or outside ADSR/without a curve editor.
    void syncEnvelopeCurveFromParams();
    // MS|BPM click handler (FRO117): writes `tempoSync` (true for BPM, false for MS) via
    // setValueNotifyingHost, no-op if already at that value or the param isn't present.
    void writeEnvelopeTempoSync(bool bpmMode);
    // Reverse sync for the MS|BPM toggle pair: reads `tempoSync` and sets the two buttons'
    // toggle states (dontSendNotification, so this never re-triggers writeEnvelopeTempoSync).
    // Called once at construction (to reflect a preset/undo-restored value) and from
    // parameterValueChanged.
    void syncEnvelopeSyncToggleFromParam();
    // Polls ADSRModule's lock-free playhead accessors and maps EnvelopeStage -> the curve's
    // segment/progress, called from the existing gated 15 Hz timerCallback (no new Timer).
    void updateEnvelopePlayhead();
    // The disclosure-toggle+BPM|MS row, then the curve editor itself when expanded — extracted
    // out of layoutDefaultContent (shared by every module) to keep that function under its own
    // ratchet. A no-op returning `y` unchanged when envelopeGraphToggle is null (every non-ADSR
    // module). Mirrors the freqResponseToggle/scopeToggle blocks it sits beside.
    int layoutEnvelopeGraphSection(int y, int contentX, int contentW, bool apply);

    // Apply SVG icons to bypass/mute/delete DrawableButtons from the active LnF.
    // No-op when the themed LnF is not installed (headless tests).
    void applyHeaderButtonIcons();

    // Rebuild the root-menu items of each waveform ComboBox with fresh icon clones
    // from the now-retinted IconLibrary, then restore the previous selection without
    // firing the parameter attachment. Called from lookAndFeelChanged() after a theme
    // switch so popup glyphs match the new theme tint.
    void refreshWaveformComboIcons();

    // Push active-theme colours onto the on-screen MidiKeyboardComponent (if any).
    // MidiKeyboard ColourIds live in juce_audio_utils, which Core does not link, so they
    // cannot be set from AppLookAndFeel::applyTheme — this is the only theming seam.
    // Called from createControls() and again from lookAndFeelChanged() on every theme switch.
    void applyKeyboardThemeColours();

    // Refresh icon images whenever the LookAndFeel is changed (e.g. theme switch).
    void lookAndFeelChanged() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleComponent)
};
