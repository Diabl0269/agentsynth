#include "ModuleComponent.h"
#include "../Modules/ExternalMidiModule.h"
#include "../Modules/MacroControlModule.h"
#include "../Modules/ModuleBase.h"
#include "../Modules/PolySequencerModule.h"
#include "../Modules/SamplerModule.h"
#include "../Modules/SequencerModule.h"
#include "GraphEditor.h"
#include "LayoutUtil.h"
#include "Theme/AppLookAndFeel.h"

// ---- Default body-layout metrics (see layoutDefaultContent) ----------------------------------
// Three knobs per row instead of two: the body sits below every jack, so it can use nearly the
// full card width, and the extra column removes a whole row of height from most modules.
static constexpr int kKnobColumns = 3;
static constexpr int kContentMargin = 12;       // left/right gutter for body content
static constexpr int kNarrowContentWidth = 200; // combos/toggles/load row stay this narrow, centred
static constexpr int kLabelHeight = 18;
static constexpr int kRowHeight = 24;  // combo box / toggle / button
static constexpr int kKnobHeight = 58; // rotary + its text box
static constexpr int kWaveformHeight = 72;
static constexpr int kTriggerMeterHeight = 18;
static constexpr int kBottomPadding = 12;
// A port label box spans its jack centre ± 10; clear it by a bit more before placing any content.
static constexpr int kPortLabelClearance = 15;

static ModuleType getType(juce::AudioProcessor* module) {
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        return mb->getModuleType();
    return ModuleType::Oscillator;
}

ModuleComponent::ModuleComponent(juce::AudioProcessor* m, juce::AudioProcessorGraph::NodeID nId, GraphEditor& owner,
                                 AppUndoManager* undoMgr)
    : module(m)
    , nodeId(nId)
    , owner(owner)
    , undoManager(undoMgr) {

    if (auto* modBase = dynamic_cast<ModuleBase*>(module)) {
        if (auto* vb = modBase->getVisualBuffer()) {
            // Parametric EQ keeps its VisualBuffer for the spectrum analyser's FFT, but a scope
            // on top of that analyser is redundant clutter, so it gets no scope UI.
            if (getType(module) != ModuleType::ExternalMidi && getType(module) != ModuleType::ParametricEQ) {
                scopeComponent = std::make_unique<ScopeComponent>(*vb);
                addAndMakeVisible(scopeComponent.get());

                scopeToggle = std::make_unique<juce::ToggleButton>("Show Scope");
                scopeToggle->setToggleState(false, juce::dontSendNotification);
                scopeComponent->setVisible(false);
                scopeToggle->onClick = [this] {
                    scopeComponent->setVisible(scopeToggle->getToggleState());
                    updateLayout();
                };
                addAndMakeVisible(scopeToggle.get());
            }
        }
    }

    if (auto* shMod = dynamic_cast<SampleHoldModule*>(module)) {
        triggerMeter = std::make_unique<TriggerMeterComponent>(*shMod);
        addAndMakeVisible(triggerMeter.get());
    }

    if (auto* filterMod = dynamic_cast<FilterModule*>(module)) {
        freqResponseComponent = std::make_unique<FrequencyResponseComponent>(*filterMod);
        addAndMakeVisible(freqResponseComponent.get());

        spectrumToggle = std::make_unique<juce::ToggleButton>("Show Spectrum");
        spectrumToggle->setToggleState(false, juce::dontSendNotification);
        spectrumToggle->onClick = [this] { freqResponseComponent->setShowSpectrum(spectrumToggle->getToggleState()); };
        addAndMakeVisible(spectrumToggle.get());
    }

    if (auto* eqMod = dynamic_cast<ParametricEQModule*>(module)) {
        eqCurveComponent = std::make_unique<EQCurveComponent>(*eqMod);
        wireEqGestureCallbacks(*eqCurveComponent);
        addAndMakeVisible(eqCurveComponent.get());

        // The spectrum is this curve's backdrop, so it starts on (the analyser gates itself on
        // actual signal, so an idle patch still costs no repaints).
        spectrumToggle = std::make_unique<juce::ToggleButton>("Show Spectrum");
        spectrumToggle->setToggleState(eqCurveComponent->getShowSpectrum(), juce::dontSendNotification);
        spectrumToggle->onClick = [this] { eqCurveComponent->setShowSpectrum(spectrumToggle->getToggleState()); };
        addAndMakeVisible(spectrumToggle.get());

        eqPopOutButton = std::make_unique<juce::TextButton>("Open EQ Window");
        eqPopOutButton->setComponentID("eqPopOut");
        eqPopOutButton->setTooltip("Edit this EQ in a larger resizable window");
        eqPopOutButton->onClick = [this] { openEqWindow(); };
        addAndMakeVisible(eqPopOutButton.get());
    }

    if (getType(module) != ModuleType::Attenuverter) {
        bypassButton = std::make_unique<juce::DrawableButton>("Bypass", juce::DrawableButton::ImageFitted);
        bypassButton->setClickingTogglesState(true);
        addAndMakeVisible(*bypassButton);

        muteButton = std::make_unique<juce::DrawableButton>("Mute", juce::DrawableButton::ImageFitted);
        muteButton->setClickingTogglesState(true);
        addAndMakeVisible(*muteButton);

        deleteButton = std::make_unique<juce::DrawableButton>("Delete", juce::DrawableButton::ImageFitted);
        deleteButton->setTooltip("Delete module");
        deleteButton->onClick = [this] { this->owner.requestDeleteModule(this->nodeId); };
        addAndMakeVisible(*deleteButton);
    }

    createSamplerControls();
    createWavetableControls();

    setTitle(module->getName());
    setBufferedToImage(true);
    createControls();
    applyHeaderButtonIcons();
    startTimerHz(15); // 15 FPS is plenty for activity glow / step indicator; lower CPU than 30
}

ModuleComponent::~ModuleComponent() { detachFromProcessor(); }

void ModuleComponent::detachFromProcessor() {
    stopTimer();
    setVisible(false);

    // Destroy scope component first — it has its own timer reading from the module's VisualBuffer
    scopeComponent.reset();
    scopeToggle.reset();
    // The pop-out EQ editor holds the module by reference and runs its own timer, so it must be
    // torn down before the processor goes away. The dialog is self-owning; deleting it closes it.
    if (eqWindow != nullptr)
        delete eqWindow.getComponent();

    // Same for the frequency-domain views: their 30 Hz timers poll the module for parameter
    // values and FFT samples, so they must go before the module pointer is dropped.
    eqCurveComponent.reset();
    freqResponseComponent.reset();
    spectrumToggle.reset();
    eqPopOutButton.reset();
    keyboardComponent.reset();
    // Same reasoning: the trigger meter times itself and holds a reference to the module.
    triggerMeter.reset();

    // Same reason: the waveform view times against the SamplerModule, so it must go before the
    // processor pointer is dropped.
    sampleWaveform.reset();
    loadSampleButton.reset();
    sampleNameLabel.reset();
    sampleChooser.reset();

    // Same reason: the wavetable display holds a module reference and its own timer, and the
    // load button's onClick lambda reaches back into this component.
    wavetableDisplay.reset();
    loadWavetableButton.reset();
    wavetableChooser.reset();

    if (auto* parent = getParentComponent())
        parent->removeChildComponent(this);

    // Destroy attachments ONLY if the processor is still alive (node exists in graph).
    // During undo, graph.clear() may have already freed the processor and its parameters.
    // If the processor is gone, release ownership to avoid use-after-free in ~ParameterAttachment.
    bool processorAlive = false;
    if (module != nullptr) {
        for (auto* node : owner.getAudioEngine().getGraph().getNodes()) {
            if (node->getProcessor() == module) {
                processorAlive = true;
                break;
            }
        }
    }
    if (processorAlive) {
        bypassAttachment.reset();
        muteAttachment.reset();
        sliderAttachments.clear();
        comboAttachments.clear();
        buttonAttachments.clear();
    } else {
        // Processor already freed — leak attachments to avoid use-after-free
        // in ~ParameterAttachment which calls parameter->removeListener()
        (void)bypassAttachment.release();
        (void)muteAttachment.release();
        while (sliderAttachments.size() > 0)
            (void)sliderAttachments.removeAndReturn(sliderAttachments.size() - 1);
        while (comboAttachments.size() > 0)
            (void)comboAttachments.removeAndReturn(comboAttachments.size() - 1);
        while (buttonAttachments.size() > 0)
            (void)buttonAttachments.removeAndReturn(buttonAttachments.size() - 1);
    }

    if (module == nullptr)
        return;

    if (auto* node = owner.getAudioEngine().getGraph().getNodeForId(nodeId)) {
        for (auto* param : node->getProcessor()->getParameters())
            param->removeListener(this);
    }

    module = nullptr;
}
void ModuleComponent::applyHeaderButtonIcons() {
    // Headless-safe: when our themed LnF is not installed (unit tests), buttons remain blank
    // (no image set). The DrawableButton still exists and functions correctly without an image.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;

    if (bypassButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleBypass))
            bypassButton->setImages(d.get());
    }
    if (muteButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleMute))
            muteButton->setImages(d.get());
    }
    if (deleteButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleDelete))
            deleteButton->setImages(d.get());
    }
}

void ModuleComponent::refreshWaveformComboIcons() {
    using synth::theme::AppLookAndFeel;
    using synth::theme::Icon;

    auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
    // Headless / no themed LnF: nothing to refresh.
    if (lf == nullptr)
        return;

    const Icon kIcons[4] = {Icon::WaveformSine, Icon::WaveformSquare, Icon::WaveformSaw, Icon::WaveformTriangle};
    const juce::StringArray kChoices{"Sine", "Square", "Saw", "Triangle"};

    for (auto* combo : comboBoxes) {
        // Detect waveform combos by the same 4-element choice set used in createControls().
        // We probe the root menu: it must have exactly 4 items with the right IDs and text.
        const juce::PopupMenu* root = combo->getRootMenu();
        if (root == nullptr)
            continue;

        // Count items and check texts match the waveform set.
        bool isWaveform = true;
        int itemCount = 0;
        {
            juce::PopupMenu::MenuItemIterator it(*root, false);
            while (it.next()) {
                const auto& item = it.getItem();
                if (itemCount >= 4 || item.text != kChoices[itemCount]) {
                    isWaveform = false;
                    break;
                }
                ++itemCount;
            }
            if (itemCount != 4)
                isWaveform = false;
        }
        if (!isWaveform)
            continue;

        // Save the current selection BEFORE rebuilding so we can restore it.
        const int savedId = combo->getSelectedId();

        // Rebuild the root menu items with freshly tinted icon clones.
        // getRootMenu() returns a const pointer; clear via the combo's non-const accessor.
        combo->clear(juce::dontSendNotification);
        for (int i = 0; i < 4; ++i) {
            std::unique_ptr<juce::Drawable> icon = lf->getIcon(kIcons[i]); // may be nullptr in headless
            combo->getRootMenu()->addItem(i + 1, kChoices[i], true, false, std::move(icon));
        }

        // Restore selection without notifying listeners — the ComboBoxParameterAttachment
        // is NOT disturbed (it listens on parameterValueChanged, not on the combo's onChange
        // when dontSendNotification is passed), so no spurious parameter change fires.
        combo->setSelectedId(savedId, juce::dontSendNotification);
    }
}

void ModuleComponent::lookAndFeelChanged() {
    applyHeaderButtonIcons();
    refreshWaveformComboIcons();
}

void ModuleComponent::timerCallback() {
    if (module == nullptr)
        return;

    if (auto* modBase = dynamic_cast<ModuleBase*>(module)) {
        if (auto* vb = modBase->getVisualBuffer()) {
            if (rmsReadBuffer.empty())
                rmsReadBuffer.resize(vb->getSize(), 0.0f);
            vb->copyTo(rmsReadBuffer);
            float sum = 0.0f;
            for (float s : rmsReadBuffer)
                sum += s * s;
            cachedRMS = std::sqrt(sum / (float)rmsReadBuffer.size());
        }
    }

    // Gate repaint: only invalidate the buffered image when something has
    // visually changed.  Idle modules (no signal, no modulation) produce no
    // repaint, so the parent content.repaint() from GraphEditor composites the
    // cached image cheaply instead of re-running the expensive text layout.
    bool needsRepaint = false;

    // 1. RMS changed meaningfully (activity glow / LED).
    //    Threshold is deliberately coarse (0.05): a steady tone produces small
    //    tick-to-tick RMS jitter from the sliding analysis window, and a tight
    //    threshold (e.g. 0.002) treated that jitter as "activity changed" and
    //    repainted every tick — invalidating the buffered image and re-running
    //    the expensive text layout 30x/sec (the startup/preset-switch freeze).
    if (std::abs(cachedRMS - lastPaintedRMS) > 0.05f)
        needsRepaint = true;

    // 2. Active modulation targeting this module (Serum mod rings)
    if (!needsRepaint) {
        const auto& modInfo = owner.getCachedModDisplayInfo();
        for (const auto& info : modInfo) {
            if (info.destNodeID == nodeId && std::abs(info.modSignalValue) > 0.001f) {
                needsRepaint = true;
                break;
            }
        }
    }

    // 3. Sequencer / PolySequencer: repaint only when the active step changes
    //    so the playhead highlight animates without repainting on every idle tick.
    if (!needsRepaint) {
        auto t = getType(module);
        if (t == ModuleType::Sequencer) {
            if (auto* seq = dynamic_cast<SequencerModule*>(module)) {
                int step = seq->currentActiveStep.load();
                if (step != lastActiveStep) {
                    lastActiveStep = step;
                    needsRepaint = true;
                }
            } else {
                // Fallback: sequencer type but no accessor — repaint each tick
                needsRepaint = true;
            }
        } else if (t == ModuleType::PolySequencer) {
            if (auto* pseq = dynamic_cast<PolySequencerModule*>(module)) {
                int step = pseq->currentActiveStep.load();
                if (step != lastActiveStep) {
                    lastActiveStep = step;
                    needsRepaint = true;
                }
            } else {
                // Fallback: sequencer type but no accessor — repaint each tick
                needsRepaint = true;
            }
        }
    }

    // NOTE: condition #4 (visible animated children — scope/freqResponse/eqCurve) is
    // intentionally omitted.  FrequencyResponseComponent, EQCurveComponent and ScopeComponent
    // manage their own repaints via their own timers and only invalidate when their data
    // changes.  Forcing a full parent repaint every tick because a child is visible caused a
    // repaint storm on every Filter module (freqResponseComponent is always visible).

    if (needsRepaint) {
        lastPaintedRMS = cachedRMS;
        repaint();
    }
}

void ModuleComponent::createControls() {
    // Auto-UI
    if (auto* midiKeyboard = dynamic_cast<MidiKeyboardModule*>(module)) {
        keyboardComponent = std::make_unique<juce::MidiKeyboardComponent>(
            midiKeyboard->getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard);

        // Theme the keyboard component
        using synth::theme::AppLookAndFeel;
        auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
        if (lf != nullptr) {
            keyboardComponent->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, lf->getTheme().colors.bg1);
            keyboardComponent->setColour(juce::MidiKeyboardComponent::blackNoteColourId,
                                         lf->getTheme().colors.surfaceHi);
            keyboardComponent->setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId,
                                         lf->getTheme().colors.border);
            keyboardComponent->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                                         lf->getTheme().colors.accent.withAlpha(0.3f));
            keyboardComponent->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId,
                                         lf->getTheme().colors.accent);
            keyboardComponent->setColour(juce::MidiKeyboardComponent::textLabelColourId,
                                         lf->getTheme().colors.textPrimary);
        }
        keyboardComponent->setWantsKeyboardFocus(true);
        addAndMakeVisible(keyboardComponent.get());
    } else if (auto* extMidi = dynamic_cast<ExternalMidiModule*>(module)) {
        auto* deviceCombo = comboBoxes.add(new juce::ComboBox("Device"));
        deviceCombo->addItem("None", 1);
        int i = 2;
        auto devices = juce::MidiInput::getAvailableDevices();
        for (auto& info : devices) {
            deviceCombo->addItem(info.name, i++);
        }
        deviceCombo->setSelectedId(1, juce::dontSendNotification);

        deviceCombo->onChange = [extMidi, deviceCombo, devices, this]() {
            int selectedId = deviceCombo->getSelectedId();
            if (selectedId > 1) {
                juce::String deviceName = devices[selectedId - 2].name;
                owner.getAudioEngine().ensureMidiDeviceOpen(deviceName);
                extMidi->setMidiDeviceName(deviceName);
            } else {
                extMidi->setMidiDeviceName("External MIDI");
            }
        };

        addAndMakeVisible(deviceCombo);
        comboLabels.add(new juce::Label("Device", "Device"));
        addAndMakeVisible(comboLabels.getLast());

        auto* channelCombo = comboBoxes.add(new juce::ComboBox("Channel"));
        channelCombo->addItem("All", 1);
        for (int c = 1; c <= 16; ++c) {
            channelCombo->addItem("Channel " + juce::String(c), c + 1);
        }
        channelCombo->setSelectedId(1, juce::dontSendNotification);

        channelCombo->onChange = [extMidi, channelCombo]() {
            int selectedId = channelCombo->getSelectedId();
            // selectedId 1 -> param 0 (All)
            // selectedId 2 -> param 1 (Channel 1)
            // selectedId 17 -> param 16 (Channel 16)
            auto* param = dynamic_cast<juce::AudioParameterInt*>(extMidi->getParameters()[1]);
            if (param != nullptr) {
                // If ID is 1, we set 0 (All).
                // If ID is 2, we set 1 (Ch1).
                param->setValueNotifyingHost(param->convertTo0to1(selectedId - 1));
            }
        };

        addAndMakeVisible(channelCombo);
        comboLabels.add(new juce::Label("Channel", "Channel"));
        addAndMakeVisible(comboLabels.getLast());
    } else {
        const auto& params = module->getParameters();

        for (auto* param : params) {
            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                auto* combo = comboBoxes.add(new juce::ComboBox());

                // Oscillator waveform selector: the exact choice set {"Sine", "Square", "Saw",
                // "Triangle"} (in that order) gets per-item waveform glyph icons attached via
                // PopupMenu::addItem(..., std::unique_ptr<Drawable> iconToUse).
                // All other choice params keep the plain addItemList path.
                const juce::StringArray& choices = choiceParam->choices;
                const bool isOscWaveform = (choices.size() == 4 && choices[0] == "Sine" && choices[1] == "Square" &&
                                            choices[2] == "Saw" && choices[3] == "Triangle");

                if (isOscWaveform) {
                    using synth::theme::Icon;
                    const Icon kIcons[4] = {Icon::WaveformSine, Icon::WaveformSquare, Icon::WaveformSaw,
                                            Icon::WaveformTriangle};
                    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
                    for (int i = 0; i < 4; ++i) {
                        std::unique_ptr<juce::Drawable> icon;
                        if (lf != nullptr)
                            icon = lf->getIcon(kIcons[i]); // may be nullptr in headless
                        combo->getRootMenu()->addItem(i + 1, choices[i], true, false, std::move(icon));
                    }
                } else {
                    combo->addItemList(choiceParam->choices, 1);
                }
                addAndMakeVisible(combo);

                auto* attach = comboAttachments.add(new juce::ComboBoxParameterAttachment(*choiceParam, *combo));

                auto* label = comboLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                addAndMakeVisible(label);
            } else if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param)) {
                auto* slider = sliders.add(new juce::Slider());
                slider->setComponentID(param->getName(100)); // ID for lookup
                if (getType(module) == ModuleType::ADSR) {
                    slider->setSliderStyle(juce::Slider::LinearVertical);
                    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
                } else {
                    slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
                }
                addAndMakeVisible(slider);

                auto* attach = sliderAttachments.add(new juce::SliderParameterAttachment(*floatParam, *slider));

                auto* label = sliderLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                label->setJustificationType(juce::Justification::centred);
                addAndMakeVisible(label);
            } else if (auto* intParam = dynamic_cast<juce::AudioParameterInt*>(param)) {
                auto* slider = sliders.add(new juce::Slider());
                slider->setComponentID(param->getName(100)); // ID for lookup
                slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
                // slider->setRange(intParam->getRange().start,
                // intParam->getRange().end, 1.0); // Attachment handles range
                addAndMakeVisible(slider);

                auto* attach = sliderAttachments.add(new juce::SliderParameterAttachment(*intParam, *slider));

                auto* label = sliderLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                label->setJustificationType(juce::Justification::centred);
                addAndMakeVisible(label);
            } else if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
                if (boolParam->paramID == "bypassed" || boolParam->paramID == "muted")
                    continue;

                auto* toggle = toggles.add(new juce::ToggleButton(boolParam->getName(100)));
                toggle->setComponentID(boolParam->getName(100)); // ID for Lookup
                addAndMakeVisible(toggle);

                auto* attach = buttonAttachments.add(new juce::ButtonParameterAttachment(*boolParam, *toggle));
            }
        }
    }

    if (bypassButton) {
        for (auto* param : module->getParameters()) {
            if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
                if (boolParam->paramID == "bypassed") {
                    bypassAttachment =
                        std::make_unique<juce::ButtonParameterAttachment>(*boolParam, *bypassButton, nullptr);
                } else if (boolParam->paramID == "muted") {
                    muteAttachment =
                        std::make_unique<juce::ButtonParameterAttachment>(*boolParam, *muteButton, nullptr);
                }
            }
        }

        // Wire repaint into the header-button onClick lambdas.
        // onClick runs on the message thread, so a direct repaint() is safe.
        // This ensures the faded/active visual updates immediately regardless of
        // whether undoManager is non-null (the parameterValueChanged listener path
        // is only registered when undoManager != nullptr).
        bypassButton->onClick = [this] { repaint(); };
        muteButton->onClick = [this] { repaint(); };
    }

    // Register as parameter listener for undo tracking AND for bypass/mute repaint.
    // Always register all params so parameterValueChanged fires for bypass/mute changes
    // even when undoManager is null (e.g. during tests or early construction).
    for (auto* param : module->getParameters())
        param->addListener(this);

    // Auto-resize
    if (getType(module) == ModuleType::Sequencer || getType(module) == ModuleType::PolySequencer) {
        setSize(synth::LayoutUtil::kDoubleWidth, 380); // 8 cols * 60 + margins, 3 rows
        return;
    }

    updateLayout();
}

//==============================================================================
// Parametric EQ card
//==============================================================================
// Geometry shared by layoutParametricEQ() and parametricEQHeight(). Keeping them in one place is
// what stops the computed card height from drifting out of step with the actual layout.
namespace eqCard {
constexpr int kMargin = 12;
constexpr int kCurveHeight = 150;
constexpr int kButtonRow = 28;
constexpr int kToggleRow = 26;
constexpr int kNumKnobRows = 3; // Freq / Gain / Q
constexpr int kNumVisibleInputs = 6;
constexpr int kScopeHeight = 100;

// Knob geometry is deliberately identical to the generic auto-UI layout, so an EQ knob is the
// same size as an Oscillator or Filter knob. The columns are wider than a knob, so each one is
// centred in its column rather than stretched to fill it.
constexpr int kKnobLabelHeight = 20;
constexpr int kSliderWidth = 70;
constexpr int kSliderHeight = 60;
constexpr int kTextBoxWidth = 50;
constexpr int kTextBoxHeight = 20;
constexpr int kKnobRow = kKnobLabelHeight + kSliderHeight;

// getPortCenter puts input i at y = 30 (header) + 20 (MIDI-out offset) + i*20 + 20, and each
// label is drawn 10px above its jack with height 20 — so the last one ends 10px below its centre.
constexpr int kPortFirstY = 70;
constexpr int kPortStep = 20;
constexpr int kPortLabelBottom = kPortFirstY + (kNumVisibleInputs - 1) * kPortStep + 16;

// The port labels only occupy a narrow gutter down each edge (inputs are drawn at x+10 with
// width 60 from a jack at x=10; outputs mirror that), so the curve sits BESIDE them starting
// just under the header rather than below the whole stack. On a six-input module that reclaims
// ~125px of otherwise dead space at the top of the card.
constexpr int kCurveTop = 60;
constexpr int kPortGutter = 88;
constexpr int kCurveBottom = kCurveTop + kCurveHeight;
constexpr int kContentTop = (kCurveBottom > kPortLabelBottom ? kCurveBottom : kPortLabelBottom) + 8;

constexpr const char* kKnobSuffix[kNumKnobRows] = {"Freq", "Gain", "Q"};
} // namespace eqCard

juce::ToggleButton* ModuleComponent::findToggleByName(const juce::String& name) const {
    for (auto* toggle : toggles)
        if (toggle->getComponentID().equalsIgnoreCase(name))
            return toggle;
    return nullptr;
}

void ModuleComponent::layoutNamedKnob(const juce::String& name, int x, int y, int w, int h) {
    juce::ignoreUnused(h);
    for (int i = 0; i < sliders.size(); ++i) {
        if (!sliders[i]->getComponentID().equalsIgnoreCase(name))
            continue;
        // Fixed knob box centred in the (wider) column, so EQ knobs render at exactly the same
        // size as every other module's rather than stretching to the column width.
        const int knobX = x + (w - eqCard::kSliderWidth) / 2;
        sliderLabels[i]->setBounds(x, y, w, eqCard::kKnobLabelHeight);
        sliders[i]->setTextBoxStyle(juce::Slider::TextBoxBelow, false, eqCard::kTextBoxWidth, eqCard::kTextBoxHeight);
        sliders[i]->setBounds(knobX, y + eqCard::kKnobLabelHeight, eqCard::kSliderWidth, eqCard::kSliderHeight);
        return;
    }
}

int ModuleComponent::parametricEQHeight() const {
    using namespace eqCard;
    // kContentTop already accounts for the curve, which is laid out beside the port labels.
    int h = kContentTop;
    h += kKnobRow + 6;   // Show Spectrum + Open EQ Window, with the Output trim sharing the row
    h += kToggleRow + 2; // per-band on/off row
    h += kNumKnobRows * kKnobRow + 4;
    if (scopeToggle)
        h += kButtonRow + 4;
    if (scopeComponent && scopeComponent->isVisible())
        h += kScopeHeight + 8;
    return h + 16; // bottom margin
}

void ModuleComponent::layoutParametricEQ() {
    using namespace eqCard;

    const int contentW = getWidth() - kMargin * 2;
    if (contentW <= 0)
        return;

    // The curve occupies the space between the input and output port-label gutters, level with
    // the jacks rather than below them.
    if (eqCurveComponent) {
        const int curveW = getWidth() - kPortGutter * 2;
        if (curveW > 0)
            eqCurveComponent->setBounds(kPortGutter, kCurveTop, curveW, kCurveHeight);
    }

    int y = kContentTop;

    // Buttons on the left, Output trim on the right of the same row — a full-width row for one
    // lone knob would leave three empty columns and make the card taller for nothing. The
    // buttons are nudged down so they sit against the knob's centre rather than its label.
    const int buttonY = y + (kKnobRow - kButtonRow) / 2;
    if (spectrumToggle)
        spectrumToggle->setBounds(kMargin, buttonY, 140, kButtonRow);
    if (eqPopOutButton)
        eqPopOutButton->setBounds(kMargin + 150, buttonY, 150, kButtonRow);
    layoutNamedKnob("Output", getWidth() - kMargin - kSliderWidth, y, kSliderWidth, kKnobRow);
    y += kKnobRow + 6;

    // One column per band. The on/off toggle's text is the band's type ("1 Low Shelf"), so the
    // column header and its enable control are the same widget.
    const int colW = contentW / ParametricEQModule::kNumBands;
    for (int b = 0; b < ParametricEQModule::kNumBands; ++b) {
        if (auto* toggle = findToggleByName(ParametricEQModule::rowLabelFor(b)))
            toggle->setBounds(kMargin + b * colW, y, colW - 4, kToggleRow);
    }
    y += kToggleRow + 2;

    for (int row = 0; row < kNumKnobRows; ++row) {
        for (int b = 0; b < ParametricEQModule::kNumBands; ++b) {
            const juce::String name = "B" + juce::String(b + 1) + " " + kKnobSuffix[row];
            layoutNamedKnob(name, kMargin + b * colW, y + row * kKnobRow, colW - 4, kKnobRow);
        }
    }
    y += kNumKnobRows * kKnobRow + 4;

    if (scopeToggle) {
        scopeToggle->setBounds(kMargin, y, contentW, kButtonRow);
        y += kButtonRow + 4;
    }
    if (scopeComponent && scopeComponent->isVisible())
        scopeComponent->setBounds(kMargin, y, contentW, kScopeHeight);
}

void ModuleComponent::wireEqGestureCallbacks(EQCurveComponent& curve) {
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    auto capture = [safeThis](bool isStart) {
        if (safeThis == nullptr || safeThis->undoManager == nullptr || safeThis->module == nullptr)
            return;
        auto& graph = safeThis->owner.getAudioEngine().getGraph();
        if (isStart)
            safeThis->undoManager->captureBeforeState(graph);
        else
            safeThis->undoManager->pushSnapshotFromCapture(graph);
    };
    curve.onGestureStart = [capture] { capture(true); };
    curve.onGestureEnd = [capture] { capture(false); };
}

void ModuleComponent::openEqWindow() {
    if (eqWindow != nullptr) {
        eqWindow->toFront(true);
        return;
    }

    auto* eqMod = dynamic_cast<ParametricEQModule*>(module);
    if (eqMod == nullptr)
        return;

    auto content = std::make_unique<EQWindow>(*eqMod);
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    content->setGestureCallbacks(
        [safeThis] {
            if (safeThis != nullptr && safeThis->undoManager != nullptr && safeThis->module != nullptr)
                safeThis->undoManager->captureBeforeState(safeThis->owner.getAudioEngine().getGraph());
        },
        [safeThis] {
            if (safeThis != nullptr && safeThis->undoManager != nullptr && safeThis->module != nullptr)
                safeThis->undoManager->pushSnapshotFromCapture(safeThis->owner.getAudioEngine().getGraph());
        });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle = module->getName();
    options.componentToCentreAround = getTopLevelComponent();
    options.useNativeTitleBar = true;
    options.resizable = true;
    eqWindow = options.launchAsync();
}

void ModuleComponent::createSamplerControls() {
    auto* sampler = dynamic_cast<SamplerModule*>(module);
    if (sampler == nullptr)
        return;

    sampleWaveform = std::make_unique<SampleWaveformComponent>(*sampler);
    addAndMakeVisible(*sampleWaveform);

    loadSampleButton = std::make_unique<juce::TextButton>("Load Sample...");
    loadSampleButton->setTooltip("Load an audio file (WAV, AIFF, FLAC, Ogg) into this Sampler");
    loadSampleButton->onClick = [this] {
        auto* mod = dynamic_cast<SamplerModule*>(module);
        if (mod == nullptr)
            return;

        sampleChooser = std::make_unique<juce::FileChooser>(
            "Load Sample", juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            SamplerModule::getSupportedFormatWildcard());
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        // The chooser outlives this call; SafePointer keeps the callback a no-op if the module
        // component is destroyed (graph rebuild, undo) while the dialog is open.
        juce::Component::SafePointer<ModuleComponent> safeThis(this);
        sampleChooser->launchAsync(flags, [safeThis](const juce::FileChooser& fc) {
            if (safeThis == nullptr)
                return;
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            auto* target = dynamic_cast<SamplerModule*>(safeThis->getModule());
            if (target == nullptr)
                return;

            if (target->loadSampleFile(file))
                safeThis->refreshSampleLabel();
            else
                safeThis->refreshSampleLabel("Could not read " + file.getFileName());
        });
    };
    addAndMakeVisible(*loadSampleButton);

    sampleNameLabel = std::make_unique<juce::Label>("Sample", juce::String());
    sampleNameLabel->setJustificationType(juce::Justification::centredLeft);
    sampleNameLabel->setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(*sampleNameLabel);

    refreshSampleLabel();
}

// =============================================================================
// Audio-file drag and drop
// =============================================================================

bool ModuleComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    // Only a Sampler accepts a file drop. Returning false for everything else matters: JUCE walks
    // up the hierarchy for an interested target, so a wav dropped on an Oscillator falls through to
    // GraphEditor, which spawns a new Sampler for it instead of doing nothing.
    if (dynamic_cast<SamplerModule*>(module) == nullptr)
        return false;

    for (const auto& path : files)
        if (SamplerModule::isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void ModuleComponent::fileDragEnter(const juce::StringArray& files, int, int) {
    juce::ignoreUnused(files);
    if (!fileDragHighlight) {
        fileDragHighlight = true;
        repaint();
    }
}

void ModuleComponent::fileDragExit(const juce::StringArray& files) {
    juce::ignoreUnused(files);
    if (fileDragHighlight) {
        fileDragHighlight = false;
        repaint();
    }
}

void ModuleComponent::filesDropped(const juce::StringArray& files, int, int) {
    fileDragHighlight = false;

    auto* sampler = dynamic_cast<SamplerModule*>(module);
    if (sampler == nullptr) {
        repaint();
        return;
    }

    // Only the first playable file is used — a Sampler holds one sample. Dropping several onto the
    // canvas (rather than onto a module) creates one Sampler each; that path lives in GraphEditor.
    for (const auto& path : files) {
        const juce::File file(path);
        if (!SamplerModule::isSupportedAudioFile(file))
            continue;

        if (sampler->loadSampleFile(file))
            refreshSampleLabel();
        else
            refreshSampleLabel("Could not read " + file.getFileName());
        return;
    }

    repaint();
}

void ModuleComponent::refreshSampleLabel(const juce::String& fallbackMessage) {
    if (sampleNameLabel == nullptr)
        return;

    auto* sampler = dynamic_cast<SamplerModule*>(module);
    juce::String name = (sampler != nullptr) ? sampler->getSampleName() : juce::String();

    if (fallbackMessage.isNotEmpty())
        sampleNameLabel->setText(fallbackMessage, juce::dontSendNotification);
    else
        sampleNameLabel->setText(name.isEmpty() ? juce::String("(no sample)") : name, juce::dontSendNotification);

    sampleNameLabel->setTooltip(name);
    repaint();
}

void ModuleComponent::createWavetableControls() {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    if (wtMod == nullptr)
        return;

    wavetableDisplay = std::make_unique<WavetableDisplayComponent>(*wtMod);
    addAndMakeVisible(*wavetableDisplay);

    loadWavetableButton = std::make_unique<juce::TextButton>("Load Wavetable...");
    loadWavetableButton->setTooltip("Load an audio file as a wavetable (2048-sample frames, Serum style)");
    loadWavetableButton->onClick = [this] { openWavetableChooser(); };
    addAndMakeVisible(*loadWavetableButton);
}

void ModuleComponent::openWavetableChooser() {
    wavetableChooser =
        std::make_unique<juce::FileChooser>("Load Wavetable", juce::File(), "*.wav;*.aiff;*.aif;*.flac;*.ogg");

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    // SafePointer: the dialog is async, so this component (and its module) may be gone by
    // the time the user picks a file.
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    wavetableChooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;

        const juce::File file = chooser.getResult();
        if (file == juce::File())
            return;

        auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(self->getModule());
        if (wtMod == nullptr)
            return;

        if (!wtMod->loadWavetableFile(file)) {
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Wavetable",
                                                        "Could not read \"" + file.getFileName() +
                                                            "\" as a wavetable.");
            return;
        }

        // Switch the Table choice to "Loaded File" so the new table is what sounds.
        for (auto* param : wtMod->getParameters()) {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                if (choice->paramID == "table" && choice->choices.size() > 1) {
                    const float normalised =
                        (float)WavetableOscillatorModule::kLoadedTableChoice / (float)(choice->choices.size() - 1);
                    choice->setValueNotifyingHost(normalised);
                }
            }
        }

        self->repaint();
    });
}

void ModuleComponent::layoutSequencerStepColumn(int step, int colX, int startY) {
    // Gate slider (row 0)
    juce::String gateId = "Gate " + juce::String(step);
    for (int i = 0; i < sliders.size(); ++i) {
        if (sliders[i]->getComponentID().equalsIgnoreCase(gateId)) {
            sliderLabels[i]->setBounds(colX, startY, 55, 20);
            sliders[i]->setBounds(colX, startY + 20, 55, 50);
        }
    }

    // Pitch / Root slider (row 1) — Sequencer uses "Pitch N", PolySequencer uses "Step N Root"
    juce::String pitchId = "Pitch " + juce::String(step);
    juce::String rootId = "Step " + juce::String(step) + " Root";
    for (int i = 0; i < sliders.size(); ++i) {
        if (sliders[i]->getComponentID().equalsIgnoreCase(pitchId) ||
            sliders[i]->getComponentID().equalsIgnoreCase(rootId)) {
            sliderLabels[i]->setBounds(colX, startY + 80, 55, 20);
            sliders[i]->setBounds(colX, startY + 100, 55, 50);
        }
    }

    // F.Env / Chord combo (row 2) — Sequencer uses "F.Env N" slider; PolySequencer uses "Step N Chord" combo
    juce::String fEnvId = "F.Env " + juce::String(step);
    for (int i = 0; i < sliders.size(); ++i) {
        if (sliders[i]->getComponentID().equalsIgnoreCase(fEnvId)) {
            sliderLabels[i]->setBounds(colX, startY + 160, 55, 20);
            sliders[i]->setBounds(colX, startY + 180, 55, 50);
        }
    }

    juce::String chordId = "Step " + juce::String(step) + " Chord";
    for (int i = 0; i < comboBoxes.size(); ++i) {
        if (comboBoxes[i]->getName().equalsIgnoreCase(chordId)) {
            if (i < comboLabels.size())
                comboLabels[i]->setBounds(colX, startY + 160, 55, 20);
            comboBoxes[i]->setBounds(colX, startY + 180, 55, 24);
        }
    }
}

void ModuleComponent::updateLayout() {
    if (getType(module) == ModuleType::Attenuverter) {
        setSize(40, 40);
        if (sliders.size() > 0) {
            sliders[0]->setBounds(0, 0, 40, 40);
            sliders[0]->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            sliders[0]->setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::yellow);
            if (sliderLabels.size() > 0)
                sliderLabels[0]->setVisible(false);
        }
        return;
    }

    if (auto* macro = dynamic_cast<MacroControlModule*>(module)) {
        // The bank is the only module whose footprint tracks a parameter: the "Knobs" count
        // decides how many macro rows (knob + output jack) are shown. It grows downward from an
        // unchanged top-left, so the module the user is turning never jumps under the cursor.
        setSize(synth::LayoutUtil::kSingleWidth, synth::LayoutUtil::macroBankHeight(macro->getMacroCount()));
        resized();
        return;
    }

    if (getType(module) == ModuleType::Sequencer || getType(module) == ModuleType::PolySequencer) {
        setSize(synth::LayoutUtil::kDoubleWidth, 380);
        return;
    }

    if (getType(module) == ModuleType::ADSR) {
        setSize(220 + 60, 180); // 180 is height from ADSR Layout
        return;
    }

    // Parametric EQ is double-width with a bespoke band grid, so it measures itself.
    if (getType(module) == ModuleType::ParametricEQ) {
        setSize(synth::LayoutUtil::kDoubleWidth, parametricEQHeight());
        resized();
        return;
    }

    // Width must be final before measuring: the slider grid wraps on it.
    if (getWidth() != synth::LayoutUtil::kSingleWidth)
        setSize(synth::LayoutUtil::kSingleWidth, juce::jmax(getHeight(), 100));

    const int bodyHeight = layoutDefaultContent(/*apply*/ false);
    setSize(synth::LayoutUtil::kSingleWidth, std::max(100, bodyHeight));
    resized();
}

int ModuleComponent::getContentTopY() {
    int y = 30; // below the title bar
    if (module->acceptsMidi())
        y += 30; // the "Midi In"/"Midi Out" row

    int numIns = module->getTotalNumInputChannels();
    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numIns = mb->getVisibleInputPortCount();
        numOuts = mb->getVisibleOutputPortCount();
    }

    // Ask for the real jack positions instead of recomputing them: a port label box spans
    // centre ± 10, so clear the lowest jack by a little more than that.
    if (numIns > 0)
        y = std::max(y, getPortCenter(numIns - 1, true).y + kPortLabelClearance);
    if (numOuts > 0)
        y = std::max(y, getPortCenter(numOuts - 1, false).y + kPortLabelClearance);

    return y;
}

int ModuleComponent::layoutDefaultContent(bool apply) {
    const int width = getWidth();

    // Everything here sits BELOW the last jack (getContentTopY), so the narrow gutters that used to
    // keep content clear of the port labels are unnecessary — the body gets nearly the full card
    // width, which is what makes three knobs per row fit.
    const int contentX = kContentMargin;
    const int contentW = std::max(60, width - kContentMargin * 2);

    // Single-column widgets (combos, toggles, the load row) look stretched at full width, so they
    // stay centred in a narrower band.
    const int narrowW = std::min(contentW, kNarrowContentWidth);
    const int narrowX = contentX + (contentW - narrowW) / 2;

    int y = getContentTopY();

    // --- Sampler chrome: waveform overview, then the load button + file-name row ---
    if (sampleWaveform) {
        if (apply)
            sampleWaveform->setBounds(contentX, y, contentW, kWaveformHeight);
        y += kWaveformHeight + 8;

        if (apply) {
            const int buttonWidth = juce::jmax(96, narrowW / 2);
            loadSampleButton->setBounds(narrowX, y, buttonWidth, kRowHeight);
            sampleNameLabel->setBounds(narrowX + buttonWidth + 6, y, narrowW - buttonWidth - 6, kRowHeight);
        }
        y += kRowHeight + 8;
    }

    // --- Wavetable chrome: the scanned frame view, then the load button ---
    if (wavetableDisplay != nullptr && loadWavetableButton != nullptr) {
        if (apply)
            wavetableDisplay->setBounds(contentX, y, contentW, kWaveformHeight);
        y += kWaveformHeight + 8;

        if (apply)
            loadWavetableButton->setBounds(narrowX, y, narrowW, kRowHeight);
        y += kRowHeight + 8;
    }

    for (int i = 0; i < comboBoxes.size(); ++i) {
        if (apply) {
            comboLabels[i]->setBounds(narrowX, y, narrowW, kLabelHeight);
            comboBoxes[i]->setBounds(narrowX, y + kLabelHeight, narrowW, kRowHeight);
        }
        y += kLabelHeight + kRowHeight + 6;
    }

    for (int i = 0; i < toggles.size(); ++i) {
        if (apply)
            toggles[i]->setBounds(narrowX, y, narrowW, kRowHeight);
        y += kRowHeight + 2;
    }

    // --- Trigger meter (Sample & Hold): sits directly above the knob grid, whose first knob is
    // Threshold, so the marker and the control that moves it stay adjacent.
    if (triggerMeter) {
        if (apply)
            triggerMeter->setBounds(contentX, y, contentW, kTriggerMeterHeight);
        y += kTriggerMeterHeight + 6;
    }

    // --- Knob grid: kKnobColumns across, wrapping ---
    const int knobWidth = contentW / kKnobColumns;
    for (int i = 0; i < sliders.size(); ++i) {
        const int row = i / kKnobColumns;
        const int col = i % kKnobColumns;
        const int x = contentX + col * knobWidth;
        const int rowY = y + row * (kLabelHeight + kKnobHeight);

        if (apply) {
            sliderLabels[i]->setBounds(x, rowY, knobWidth, kLabelHeight);
            sliders[i]->setBounds(x, rowY + kLabelHeight, knobWidth, kKnobHeight);
        }
    }
    const int knobRows = (sliders.size() + kKnobColumns - 1) / kKnobColumns;
    y += knobRows * (kLabelHeight + kKnobHeight);

    if (freqResponseComponent) {
        if (apply)
            freqResponseComponent->setBounds(contentX, y, contentW, 120);
        y += 128;
    }

    if (spectrumToggle) {
        if (apply)
            spectrumToggle->setBounds(narrowX, y, narrowW, kRowHeight);
        y += kRowHeight + 2;
    }

    if (scopeToggle) {
        if (apply)
            scopeToggle->setBounds(narrowX, y, narrowW, kRowHeight);
        y += kRowHeight + 2;
    }

    if (scopeComponent && scopeComponent->isVisible()) {
        if (apply)
            scopeComponent->setBounds(contentX, y, contentW, 100);
        y += 100;
    }

    return y + kBottomPadding;
}

void ModuleComponent::paint(juce::Graphics& g) {
    if (module == nullptr)
        return;

    if (getType(module) == ModuleType::Attenuverter) {
        return; // Transparent background, no ports, no header
    }

    auto* mod = dynamic_cast<ModuleBase*>(module);
    bool isBypassed = mod && mod->isBypassed();

    // Guarded LnF cast: headless tests construct this component WITHOUT installing our LnF.
    // When the cast is null we fall back to a plain themed-ish fill so those tests don't crash.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    if (lf != nullptr) {
        // Single owner of card treatment: background, drop shadow, body fill, border, and the
        // header band (filled + title drawn) all come from the active theme. selected=false for
        // v1 (no per-module selection model yet — see spec section 12).
        lf->drawModulePanel(g, getLocalBounds().toFloat(), 24, module->getName(), /*selected*/ false, isBypassed);
    } else {
        // Fallback path (no themed LnF): plain fill + simple header so tests render without crashing.
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        g.setColour(juce::Colours::black);
        g.drawRect(getLocalBounds(), 2);
        g.setColour(juce::Colours::darkgrey);
        g.fillRect(0, 0, getWidth(), 24);
        g.setColour(juce::Colours::white);
        g.drawText(module->getName(), 0, 0, getWidth(), 24, juce::Justification::centred, true);
    }

    // Drop-target highlight while an audio file hovers over a Sampler.
    if (fileDragHighlight) {
        auto dropColour = (lf != nullptr) ? lf->getTheme().colors.accent : juce::Colours::yellow;
        g.setColour(dropColour.withAlpha(0.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 24.0f);
        g.setColour(dropColour);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 24.0f, 2.0f);
    }

    // Highlight Active Step (Sequencer only) — recolored from theme accent when available.
    if (getType(module) == ModuleType::Sequencer) {
        if (auto* seq = dynamic_cast<SequencerModule*>(module)) {
            int activeStep = seq->currentActiveStep.load();
            // Coordinates match resized()
            int startX = 10;
            int stepWidth = 60;
            int x = startX + activeStep * stepWidth;

            auto stepColour =
                (lf != nullptr) ? lf->getTheme().colors.accent.withAlpha(0.3f) : juce::Colours::yellow.withAlpha(0.3f);
            g.setColour(stepColour);
            g.fillRect(x, 110, stepWidth - 5, 220); // Cover Gate+Pitch+F.Env area
        }
    }

    // Activity LED in header — recolored to the theme's success token when available.
    if (cachedRMS > 0.01f && !isBypassed) {
        float ledAlpha = juce::jlimit(0.3f, 1.0f, cachedRMS * 2.0f);
        auto ledColour = (lf != nullptr) ? lf->getTheme().colors.success : juce::Colours::limegreen;
        g.setColour(ledColour.withAlpha(ledAlpha));
        g.fillEllipse(6.0f, 8.0f, 8.0f, 8.0f);
    }

    // --- PORTS ---
    // Theme-derived jack/label colors (guarded: headless tests have no themed LnF).
    // Audio-signal jacks (MIDI in/out) -> audioWire; mod-capable input/output jacks -> accent;
    // port labels -> textMuted. Geometry is unchanged.
    juce::Colour jackAccentColour = (lf != nullptr) ? lf->getTheme().colors.accent : juce::Colours::yellow;
    juce::Colour audioJackColour = (lf != nullptr) ? lf->getTheme().colors.audioWire : juce::Colours::white;
    juce::Colour labelColour = (lf != nullptr) ? lf->getTheme().colors.textMuted : juce::Colours::white;

    int numIns = module->getTotalNumInputChannels();
    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numIns = mb->getVisibleInputPortCount();
        numOuts = mb->getVisibleOutputPortCount();
    }
    bool midiOutDrawn = false; // Reintroduced
                               // MIDI Output (Top Right if produces midi)
    if (module->producesMidi()) {
        g.setColour(audioJackColour);
        auto p = juce::Point<int>(getWidth() - 10, 30); // Top right, below header
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);
        g.setColour(labelColour);
        g.drawText("Midi Out", p.x - 65, p.y - 5, 60, 10, juce::Justification::right, false);
    }
    // MIDI Input (Top Left if accepts midi components)
    if (module->acceptsMidi()) {
        g.setColour(audioJackColour);
        auto p = juce::Point<int>(10, 30); // Top left near header
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);
        g.setColour(labelColour);
        g.drawText("Midi In", p.x + 10, p.y - 5, 60, 10, juce::Justification::left, false);
    }

    // Inputs
    for (int i = 0; i < numIns; ++i) {
        auto p = getPortCenter(i, true);
        g.setColour(jackAccentColour);
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);

        juce::String label = "In " + juce::String(i);
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            label = mb->getInputPortLabel(i);
        else if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module))
            label = (i == 0) ? "Left" : (i == 1) ? "Right" : "In " + juce::String(i);

        g.setColour(labelColour);
        g.drawText(label, p.x + 10, p.y - 10, 60, 20, juce::Justification::left, false);
    }

    // Outputs
    // Only draw audio outputs if MIDI out hasn't been drawn in the same general area (to prevent overlap)
    // For now, we assume MIDI out takes the "first" audio output slot.
    // A more robust solution would involve explicit port mapping.
    int audioOutStartIndex = midiOutDrawn ? 1 : 0;
    for (int i = audioOutStartIndex; i < numOuts + audioOutStartIndex;
         ++i) { // Adjust index for display if midi out is present
        auto p = getPortCenter(i, false);
        g.setColour(jackAccentColour);
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);

        juce::String label = "Out " + juce::String(i);
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            label = mb->getOutputPortLabel(i);
        else if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module))
            label = (i == 0) ? "Left" : (i == 1) ? "Right" : "Out " + juce::String(i);
        g.setColour(labelColour);
        g.drawText(label, p.x - 70, p.y - 10, 60, 20, juce::Justification::right, false);
    }

    // Serum-style modulation rings on knobs
    if (mod != nullptr) {
        auto targets = mod->getModulationTargets();
        const auto& modInfo = owner.getCachedModDisplayInfo();

        for (const auto& info : modInfo) {
            if (info.destNodeID != nodeId || info.isBypassed)
                continue;

            juce::String targetParamName;
            for (const auto& t : targets) {
                if (t.channelIndex == info.destChannelIndex) {
                    targetParamName = t.name;
                    break;
                }
            }
            if (targetParamName.isEmpty())
                continue;

            for (int si = 0; si < sliders.size(); ++si) {
                if (sliders[si]->getComponentID() != targetParamName)
                    continue;
                if (sliders[si]->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag)
                    continue;

                auto sliderBounds = sliders[si]->getBounds().toFloat();
                float cx = sliderBounds.getCentreX();
                float cy = sliderBounds.getCentreY() - 10.0f;
                float radius = std::min(sliderBounds.getWidth(), sliderBounds.getHeight()) / 2.0f - 11.0f;

                float baseNorm = 0.5f;
                for (auto* param : module->getParameters()) {
                    if (param->getName(100) == targetParamName) {
                        baseNorm = param->getValue();
                        break;
                    }
                }

                float modNorm = juce::jlimit(0.0f, 1.0f, baseNorm + info.modSignalValue);

                // Serum-style mod ring now drawn by the themed LnF (270° sweep + theme tokens).
                // Guarded: headless tests without our LnF simply skip the ring.
                if (lf != nullptr)
                    lf->drawModulationRing(g, {cx, cy}, radius, baseNorm, modNorm, info.modSignalValue >= 0.0f);
                break;
            }
        }
    }
}

juce::Point<int> ModuleComponent::getPortCenter(int index, bool isInput) {
    if (module == nullptr)
        return {0, 0};

    if (getType(module) == ModuleType::Attenuverter) {
        return {getWidth() / 2, getHeight() / 2};
    }

    // Macro bank: jacks sit on their macro's row so knob N and jack N line up horizontally.
    if (auto* macro = dynamic_cast<MacroControlModule*>(module)) {
        if (!isInput) {
            const int visible = macro->getVisibleOutputPortCount();
            const int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, index) : 0;
            return {getWidth() - 10, synth::LayoutUtil::macroRowCentreY(clamped)};
        }
    }

    int yStep = 20;
    int headerHeight = 30;

    int portOffset = 0;
    if (module->producesMidi()) {
        portOffset = 20; // Additional offset for all ports if MIDI out is present, to avoid collision with MIDI Out at
                         // (getWidth() - 10, 30)
    }

    // Clamp index to visible jack range so wires never terminate at a phantom y
    // below the module. In-bounds indices are unchanged (clamped == index).
    int visible = 0;
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
    } else {
        visible = isInput ? module->getTotalNumInputChannels() : module->getTotalNumOutputChannels();
    }
    int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, index) : 0;

    if (isInput) {
        return {10, headerHeight + portOffset + clamped * yStep + 20}; // Left side, apply offset
    } else {
        // No additional midiOffset for outputs here, as MIDI out is now fixed.
        return {getWidth() - 10, headerHeight + portOffset + clamped * yStep + 20}; // Right side, apply offset
    }
}

std::optional<ModuleComponent::Port> ModuleComponent::getPortForPoint(juce::Point<int> localPoint) {
    if (module == nullptr)
        return std::nullopt;

    if (getType(module) == ModuleType::Attenuverter) {
        return std::nullopt; // Users cannot manually drag connections from the smart wire knob
    }

    int numIns = module->getTotalNumInputChannels();
    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numIns = mb->getVisibleInputPortCount();
        numOuts = mb->getVisibleOutputPortCount();
    }

    // Check for MIDI Output at fixed top-right position
    if (module->producesMidi()) {
        auto p = juce::Point<int>(getWidth() - 10, 30); // Matches paint()
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{{p.x - 5, p.y - 5, 10, 10},
                        juce::AudioProcessorGraph::midiChannelIndex,
                        false,
                        true}; // Index 0, Output, IsMidi
        }
    }

    // MIDI Input detection (Top Left)
    if (module->acceptsMidi()) {
        auto p = juce::Point<int>(10, 30); // Top left near header
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{
                {p.x - 5, p.y - 5, 10, 10}, juce::AudioProcessorGraph::midiChannelIndex, true, true}; // MIDI Input
        }
    }

    // Inputs
    for (int i = 0; i < numIns; ++i) {
        auto p = getPortCenter(i, true);
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{{p.x - 5, p.y - 5, 10, 10}, i, true, false};
        }
    }

    // Outputs (audio outputs)
    for (int i = 0; i < numOuts; ++i) {
        auto p = getPortCenter(i, false);
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{{p.x - 5, p.y - 5, 10, 10}, i, false, false};
        }
    }

    return std::nullopt;
}

void ModuleComponent::resized() {
    if (module == nullptr)
        return;

    // Header icon buttons: delete (rightmost) → bypass → mute (leftmost of the three).
    // Attenuverter path: all three are null → no-op.
    if (deleteButton)
        deleteButton->setBounds(getWidth() - 26, 2, 22, 20);

    if (bypassButton)
        bypassButton->setBounds(getWidth() - 50, 2, 22, 20);

    if (muteButton)
        muteButton->setBounds(getWidth() - 74, 2, 22, 20);

    if (auto* macro = dynamic_cast<MacroControlModule*>(module)) {
        layoutMacroBank(macro->getMacroCount());
        return;
    }

    if (getType(module) == ModuleType::ParametricEQ) {
        layoutParametricEQ();
        return;
    }

    if (getType(module) == ModuleType::Sequencer) {
        // --- Sequencer Specific Layout ---
        int x = 10;
        int y = 30;

        // Top Row: Run and BPM
        for (auto* toggle : toggles) {
            if (toggle->getComponentID().equalsIgnoreCase("run")) {
                toggle->setBounds(x + 30, y, 60, 24); // Add margin
                x += 70;
            }
        }

        for (int i = 0; i < sliders.size(); ++i) {
            if (sliders[i]->getComponentID().equalsIgnoreCase("bpm")) {
                sliderLabels[i]->setBounds(x + 20, y, 60, 20); // Add margin
                sliders[i]->setBounds(x + 20, y + 20, 60, 50);
                x += 70;
            }
        }

        // Steps Row: 8 columns starting at (10, 110), each 60px wide
        const int startX = 10;
        const int startY = 110;
        const int stepWidth = 60;

        for (int step = 1; step <= 8; ++step) {
            int colX = startX + (step - 1) * stepWidth;
            layoutSequencerStepColumn(step, colX, startY);
        }

        return;
    }

    if (getType(module) == ModuleType::PolySequencer) {
        // --- PolySequencer Specific Layout ---
        // Run toggle + BPM in header row (y=30), then 8 step columns at y=110.
        // Step N params: "Gate N" (slider), "Step N Root" (slider), "Step N Chord" (combo).
        int x = 10;
        int y = 30;

        for (auto* toggle : toggles) {
            if (toggle->getComponentID().equalsIgnoreCase("run")) {
                toggle->setBounds(x + 30, y, 60, 24);
                x += 70;
            }
        }

        for (int i = 0; i < sliders.size(); ++i) {
            if (sliders[i]->getComponentID().equalsIgnoreCase("bpm")) {
                sliderLabels[i]->setBounds(x + 20, y, 60, 20);
                sliders[i]->setBounds(x + 20, y + 20, 60, 50);
                x += 70;
            }
        }

        // Steps Row: 8 columns starting at (10, 110), each 60px wide
        const int startX = 10;
        const int startY = 110;
        const int stepWidth = 60;

        for (int step = 1; step <= 8; ++step) {
            int colX = startX + (step - 1) * stepWidth;
            layoutSequencerStepColumn(step, colX, startY);
        }

        return;
    }

    // --- ADSR Layout ---
    if (getType(module) == ModuleType::ADSR) {
        int margin = 30;                // Side margins for ports
        setSize(220 + margin * 2, 180); // Increase width
        int y = 30;
        int contentWidth = getWidth() - margin * 2;
        int sliderWidth = 50;
        int sliderHeight = 120;

        // We expect 4 sliders: A, D, S, R
        for (int i = 0; i < sliders.size(); ++i) {
            int x = margin + 10 + i * sliderWidth;
            sliderLabels[i]->setBounds(x, y, sliderWidth, 20);
            sliders[i]->setBounds(x, y + 20, sliderWidth, sliderHeight);
        }
        return;
    }

    // --- MIDI Keyboard Layout ---
    if (getType(module) == ModuleType::MidiKeyboard) {
        setSize(synth::LayoutUtil::kDoubleWidth, 150); // Appropriate size for a keyboard
        if (keyboardComponent) {
            keyboardComponent->setBounds(10, 50, getWidth() - 20, getHeight() - 60);
        }
        return;
    }

    // --- Default Layout ---
    layoutDefaultContent(/*apply*/ true);
}

void ModuleComponent::parameterValueChanged(int parameterIndex, float newValue) {
    juce::ignoreUnused(newValue);

    // When bypass or mute changes, schedule a repaint on the message thread.
    // parameterValueChanged can be called from the audio thread — NEVER call
    // repaint() directly here.  SafePointer ensures the lambda is a no-op if
    // the component has been destroyed before the async call fires.
    if (module == nullptr)
        return;

    const auto& params = module->getParameters();
    if (parameterIndex < 0 || parameterIndex >= params.size())
        return;

    auto* param = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[parameterIndex]);
    if (param == nullptr)
        return;

    if (param->paramID == "bypassed" || param->paramID == "muted") {
        juce::Component::SafePointer<ModuleComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->repaint();
        });
    } else if (param->paramID == "macroCount") {
        // Resizing touches the component tree and the graph, so it must happen on the message
        // thread even though this callback can arrive from the audio thread.
        juce::Component::SafePointer<ModuleComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->applyMacroCountChange();
        });
    }
}

void ModuleComponent::applyMacroCountChange() {
    if (module == nullptr || dynamic_cast<MacroControlModule*>(module) == nullptr)
        return;

    updateLayout();
    owner.handleModuleResized(this);
    repaint();
}

void ModuleComponent::layoutMacroBank(int count) {
    using namespace synth::LayoutUtil;

    const int margin = 20;

    // Header row: the Knobs count on the left, the Bipolar toggle beside it.
    for (int i = 0; i < sliders.size(); ++i) {
        if (!sliders[i]->getComponentID().equalsIgnoreCase("Knobs"))
            continue;
        sliderLabels[i]->setBounds(margin, 30, 80, 18);
        sliders[i]->setBounds(margin, 48, 80, 40);
        sliders[i]->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 16);
    }

    for (auto* toggle : toggles) {
        if (toggle->getComponentID().equalsIgnoreCase("Bipolar"))
            toggle->setBounds(margin + 96, 56, 100, 24);
    }

    // One row per macro: knob on the left, value beside it, output jack (drawn in paint()) on
    // the right edge at the same y. Rows beyond the count are hidden, not destroyed — their
    // parameters still exist and keep their values if the bank is grown again.
    for (int i = 0; i < MacroControlModule::kMaxMacros; ++i) {
        const juce::String id = MacroControlModule::macroName(i);
        const bool visible = i < count;

        for (int s = 0; s < sliders.size(); ++s) {
            if (!sliders[s]->getComponentID().equalsIgnoreCase(id))
                continue;

            sliders[s]->setVisible(visible);
            if (s < sliderLabels.size())
                sliderLabels[s]->setVisible(false); // the jack label already names the row

            if (visible) {
                sliders[s]->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
                const int centreY = macroRowCentreY(i);
                sliders[s]->setBounds(margin, centreY - (kMacroRowH - 8) / 2, 140, kMacroRowH - 8);
            }
        }
    }
}

void ModuleComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    if (!undoManager || module == nullptr)
        return;

    if (gestureIsStarting) {
        // Capture full graph snapshot at gesture start
        gestureStartValues[parameterIndex] = 1.0f; // flag that gesture is active
        undoManager->captureBeforeState(owner.getAudioEngine().getGraph());
    } else {
        auto it = gestureStartValues.find(parameterIndex);
        if (it != gestureStartValues.end()) {
            // Capture after snapshot and push as undo action
            auto* graphEditor = &owner;
            undoManager->pushSnapshotFromCapture(owner.getAudioEngine().getGraph());
            gestureStartValues.erase(it);
        }
    }
}

void ModuleComponent::mouseDown(const juce::MouseEvent& e) {
    auto port = getPortForPoint(e.getPosition());
    if (port) {
        if (e.mods.isPopupMenu()) {
            // Right click -> Disconnect
            // Show menu? Or just disconnect?
            // User asked for "way to disconnect". Instant disconnect is fast.
            // Or a menu "Disconnect".
            juce::PopupMenu m;
            m.addItem("Disconnect",
                      [this, port] { owner.disconnectPort(this, port->index, port->isInput, port->isMidi); });

            m.showMenuAsync(juce::PopupMenu::Options());
        } else {
            // Start Connection Drag
            owner.beginConnectionDrag(this, port->index, port->isInput, port->isMidi, e.getScreenPosition());
        }
    } else {
        // Click on Body
        if (getType(module) == ModuleType::Attenuverter)
            return; // cannot drag

        if (e.mods.isPopupMenu()) {
            juce::PopupMenu m;

            // Bypass toggle (only for actual modules)
            if (auto* mod = dynamic_cast<ModuleBase*>(module)) {
                m.addItem(mod->isBypassed() ? "Enable Module" : "Bypass Module", [this] {
                    if (auto* mod = dynamic_cast<ModuleBase*>(module)) {
                        mod->setBypassed(!mod->isBypassed());
                        repaint();
                    }
                });
                m.addSeparator();
            }

            // "Replace with..." submenu (only for actual modules, not AudioGraphIOProcessor)
            if (dynamic_cast<ModuleBase*>(module) != nullptr) {
                juce::PopupMenu replaceMenu;
                auto currentType = getType(module);

                struct ModEntry {
                    const char* name;
                    ModuleType type;
                };
                struct Category {
                    const char* header;
                    std::vector<ModEntry> modules;
                };
                std::vector<Category> categories = {
                    {"Sources",
                     {{"Oscillator", ModuleType::Oscillator},
                      {"Wavetable", ModuleType::Wavetable},
                      {"Noise", ModuleType::Noise},
                      {"Sampler", ModuleType::Sampler},
                      {"LFO", ModuleType::LFO}}},
                    {"Sequencing",
                     {{"Sequencer", ModuleType::Sequencer},
                      {"Poly Sequencer", ModuleType::PolySequencer},
                      {"MIDI Keyboard", ModuleType::MidiKeyboard},
                      {"Poly MIDI", ModuleType::PolyMidi},
                      {"External MIDI", ModuleType::ExternalMidi}}},
                    {"Envelopes & Control", {{"ADSR", ModuleType::ADSR}, {"VCA", ModuleType::VCA}}},
                    {"Filters", {{"Filter", ModuleType::Filter}, {"Parametric EQ", ModuleType::ParametricEQ}}},
                    {"Modulation FX",
                     {{"Chorus", ModuleType::Chorus},
                      {"Phaser", ModuleType::Phaser},
                      {"Flanger", ModuleType::Flanger},
                      {"Distortion", ModuleType::Distortion},
                      {"Bitcrusher", ModuleType::Bitcrusher},
                      {"Pitch Shifter", ModuleType::PitchShifter}}},
                    {"Time FX", {{"Delay", ModuleType::Delay}, {"Reverb", ModuleType::Reverb}}},
                    {"Dynamics", {{"Compressor", ModuleType::Compressor}, {"Limiter", ModuleType::Limiter}}},
                    {"Utility", {{"Sample & Hold", ModuleType::SampleHold}}},
                };

                for (auto& cat : categories) {
                    juce::PopupMenu catMenu;
                    bool hasItems = false;
                    for (auto& mod : cat.modules) {
                        if (mod.type == currentType)
                            continue;
                        juce::String typeName(mod.name);
                        catMenu.addItem(typeName, [this, typeName] { owner.replaceModule(this, typeName); });
                        hasItems = true;
                    }
                    if (hasItems)
                        replaceMenu.addSubMenu(cat.header, catMenu);
                }

                m.addSubMenu("Replace with...", replaceMenu);
                m.addSeparator();
            }

            m.addItem("Delete Module", [this] { owner.deleteModule(this); });
            m.showMenuAsync(juce::PopupMenu::Options());
        } else {
            dragStartPosition = getPosition();
            if (undoManager)
                undoManager->captureBeforeState(owner.getAudioEngine().getGraph());
            dragger.startDraggingComponent(this, e);
            // Show grid + ghost for this module-body drag.
            owner.beginDragPreview(getWidth(), getHeight(), getNodeId());
        }
    }
}

void ModuleComponent::moved() {
    if (module != nullptr)
        owner.updateModulePosition(this);
}

void ModuleComponent::mouseDrag(const juce::MouseEvent& e) {
    if (getPortForPoint(e.getMouseDownPosition())) {
        owner.dragConnection(e.getScreenPosition());
    } else {
        dragger.dragComponent(this, e, nullptr);
        // Update the landing ghost to follow the live drag position.
        owner.updateDragPreview(getPosition());
        if (auto* p = getParentComponent())
            p->repaint();
    }
}

void ModuleComponent::mouseUp(const juce::MouseEvent& e) {
    if (getPortForPoint(e.getMouseDownPosition())) {
        owner.endConnectionDrag(e.getScreenPosition());
    } else {
        // Clear ghost overlay before finalizing so the overlay is gone at the exact
        // moment the module lands in its snapped position.
        owner.endDragPreview();
        if (getPosition() != dragStartPosition) {
            // Snap to grid and resolve overlap BEFORE the undo snapshot so the
            // snapped/cleared final position is what gets captured in the diff.
            owner.finalizeModuleDrag(this);
            if (undoManager)
                undoManager->pushSnapshotFromCapture(owner.getAudioEngine().getGraph());
        }
    }
}
