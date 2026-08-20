#include "ModuleComponent.h"
#include "../Modules/ExternalMidiModule.h"
#include "../Modules/MacroControlModule.h"
#include "../Modules/ModuleBase.h"
#include "../Modules/PolySequencerModule.h"
#include "../Modules/SamplerModule.h"
#include "../Modules/SequencerModule.h"
#include "../Modules/ThresholdMeterSource.h"
#include "../Plugin/Hosting/HostedPluginModule.h"
#include "GraphEditor.h"
#include "LayoutUtil.h"
#include "Theme/AppLookAndFeel.h"
#include <cmath>

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
static constexpr int kBottomPadding = 12;
// A port label box spans its jack centre ± 10; clear it by a bit more before placing any content.
static constexpr int kPortLabelClearance = 15;
// Horizontal step between input-jack columns on a multi-column gutter. A jack sits at x, its
// label runs from x+10 for 60px, so 100 leaves a 30px gap before the next column's jack.
static constexpr int kPortColumnStride = 100;

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

    if (auto* src = dynamic_cast<ThresholdMeterSource*>(module)) {
        juce::AudioParameterFloat* thresholdParam = nullptr;
        // Sample & Hold keeps its rotary Threshold; the control is meter-only there. ADSR and
        // Comparator embed the slider in the control so the slice sits on the live level bar.
        if (getType(module) != ModuleType::SampleHold)
            thresholdParam =
                dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module, src->getThresholdParamID()));
        thresholdControl = std::make_unique<ThresholdControlComponent>(*src, thresholdParam);
        addAndMakeVisible(thresholdControl.get());
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
        bypassButton->setTooltip("Bypass");
        addAndMakeVisible(*bypassButton);

        muteButton = std::make_unique<juce::DrawableButton>("Mute", juce::DrawableButton::ImageFitted);
        muteButton->setClickingTogglesState(true);
        muteButton->setTooltip("Mute");
        addAndMakeVisible(*muteButton);

        deleteButton = std::make_unique<juce::DrawableButton>("Delete", juce::DrawableButton::ImageFitted);
        deleteButton->setTooltip("Delete module");
        deleteButton->onClick = [this] { this->owner.requestDeleteModule(this->nodeId); };
        addAndMakeVisible(*deleteButton);

        if (auto* mb = dynamic_cast<ModuleBase*>(module); mb != nullptr && mb->hasDualIOParameter()) {
            dualIOButton = std::make_unique<juce::DrawableButton>("Dual I/O", juce::DrawableButton::ImageFitted);
            dualIOButton->setClickingTogglesState(true);
            updateDualIOTooltip();
            addAndMakeVisible(*dualIOButton);
        }
    }

    createSamplerControls();
    createWavetableControls();

    setTitle(module->getName());
    setBufferedToImage(true);
    createControls();
    createWavetableTabs(); // after createControls(): it groups the sliders/combos that call made
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
    openPluginEditorButton.reset();
    keyboardComponent.reset();
    // Same reasoning: the threshold control times itself and holds a reference to the module.
    thresholdControl.reset();

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
        dualIOAttachment.reset();
        sliderAttachments.clear();
        comboAttachments.clear();
        buttonAttachments.clear();
    } else {
        // Processor already freed — leak attachments to avoid use-after-free
        // in ~ParameterAttachment which calls parameter->removeListener()
        (void)bypassAttachment.release();
        (void)muteAttachment.release();
        (void)dualIOAttachment.release();
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
    if (dualIOButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleDualIO))
            dualIOButton->setImages(d.get());
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

    // An async plugin load can flip hasInstance() at any moment, well after the button was built —
    // polled at the card's existing 15 Hz rate rather than adding a second timer.
    if (openPluginEditorButton != nullptr) {
        if (auto* hostedPlugin = dynamic_cast<synth::HostedPluginModule*>(module))
            openPluginEditorButton->setEnabled(hostedPlugin->hasInstance());
    }

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
        comboParams.add(nullptr); // not ComboBoxParameterAttachment-driven — see the header

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
        comboParams.add(nullptr); // not ComboBoxParameterAttachment-driven — see the header

        channelCombo->onChange = [extMidi, channelCombo]() {
            int selectedId = channelCombo->getSelectedId();
            // selectedId 1 -> param 0 (All)
            // selectedId 2 -> param 1 (Channel 1)
            // selectedId 17 -> param 16 (Channel 16)
            auto* param = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(extMidi, "channel"));
            if (param != nullptr) {
                // If ID is 1, we set 0 (All).
                // If ID is 2, we set 1 (Ch1).
                param->setValueNotifyingHost(param->convertTo0to1(selectedId - 1));
            }
        };

        addAndMakeVisible(channelCombo);
        comboLabels.add(new juce::Label("Channel", "Channel"));
        addAndMakeVisible(comboLabels.getLast());
    } else if (auto* hostedPlugin = dynamic_cast<synth::HostedPluginModule*>(module)) {
        // The only body content a Hosted Plugin card has (bypass/mute/delete already live in the
        // header, and the module exposes no parameters of its own — see the class comment).
        openPluginEditorButton = std::make_unique<juce::TextButton>("Open Editor");
        openPluginEditorButton->setComponentID("openPluginEditor");
        openPluginEditorButton->setTooltip("Open this plugin's editor window");
        openPluginEditorButton->setEnabled(hostedPlugin->hasInstance());
        openPluginEditorButton->onClick = [this] {
            if (owner.onOpenPluginEditorRequested)
                owner.onOpenPluginEditorRequested(nodeId);
        };
        addAndMakeVisible(openPluginEditorButton.get());
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
                comboParams.add(choiceParam); // param -> control mapping for reflection

                auto* label = comboLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                addAndMakeVisible(label);
            } else if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param)) {
                if (auto* src = dynamic_cast<ThresholdMeterSource*>(module)) {
                    // ADSR / Comparator: the threshold slider lives inside ThresholdControlComponent.
                    if (getType(module) != ModuleType::SampleHold && floatParam->paramID == src->getThresholdParamID())
                        continue;
                }
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
                // Right-click-any-knob. `this` outlives every child slider (sliders is a member
                // OwnedArray, destroyed as part of this component's own teardown before the outer
                // object finishes destructing), so attaching `this` as the listener rather than a
                // separately-owned object has no dangling-pointer window to reason about.
                slider->addMouseListener(this, false);

                auto* attach = sliderAttachments.add(new juce::SliderParameterAttachment(*floatParam, *slider));
                sliderParams.add(floatParam); // param -> control mapping for reflection

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
                slider->addMouseListener(this, false); // right-click-any-knob, see above

                auto* attach = sliderAttachments.add(new juce::SliderParameterAttachment(*intParam, *slider));
                sliderParams.add(intParam); // param -> control mapping for reflection

                auto* label = sliderLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                label->setJustificationType(juce::Justification::centred);
                addAndMakeVisible(label);
            } else if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
                if (boolParam->paramID == "bypassed" || boolParam->paramID == "muted" || boolParam->paramID == "dualIO")
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
                } else if (boolParam->paramID == "dualIO" && dualIOButton) {
                    dualIOAttachment =
                        std::make_unique<juce::ButtonParameterAttachment>(*boolParam, *dualIOButton, nullptr);
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

    captureLogicalPortMaps();

    // Auto-resize
    if (getType(module) == ModuleType::Sequencer || getType(module) == ModuleType::PolySequencer) {
        // 8 cols * 60 + margins, 3 rows, +26 for the Sync to Transport toggle row.
        setSize(synth::LayoutUtil::kDoubleWidth, 406);
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

// Automation -> UI reflection. `sliderParams`/`comboParams` are index-parallel to
// `sliders`/`comboBoxes` (see the header), so this is a straight linear scan for pointer identity —
// no name matching, no ambiguity between two params that happen to share a display name. A `param`
// nobody built a control for (nullptr entries included) simply matches nothing and returns.
void ModuleComponent::reflectParameterValue(const juce::AudioProcessorParameter* param, float normalized) {
    if (param == nullptr || module == nullptr)
        return; // nothing to reflect into, or this component is mid-teardown (detachFromProcessor)

    for (int i = 0; i < sliderParams.size(); ++i) {
        if (sliderParams[i] != param)
            continue;
        // Denormalise via the SAME RangedAudioParameter the slider's own NormalisableRange mirrors
        // (see SliderParameterAttachment's ctor) — the slider's range is already in these units, so
        // this is exactly the value the attachment itself would have pushed.
        const double denormalised = static_cast<double>(sliderParams[i]->convertFrom0to1(normalized));
        sliders[i]->setValue(denormalised, juce::dontSendNotification);
        return;
    }

    for (int i = 0; i < comboParams.size(); ++i) {
        if (comboParams[i] != param)
            continue;
        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(comboParams[i])) {
            const int index = juce::jlimit(0, choiceParam->choices.size() - 1,
                                           static_cast<int>(std::lround(choiceParam->convertFrom0to1(normalized))));
            comboBoxes[i]->setSelectedItemIndex(index, juce::dontSendNotification);
        }
        return;
    }
}

// Right-click-any-knob -> "Automate '<Param>'". `param` may be null (a control this
// component built without a real RangedAudioParameter behind it, e.g. the ExternalMidiModule
// device/channel combos — never true for anything reaching here through `sliders`, but checked
// anyway since sliderParams can hold a null entry per its own header comment).
void ModuleComponent::showAutomateMenuForSlider(juce::RangedAudioParameter* param) {
    if (param == nullptr)
        return;

    // The popup's action runs asynchronously (showMenuAsync), so `this` must be re-checked rather
    // than captured raw — the module (and its GraphEditor selection) could be gone by the time the
    // user picks an item (a delete, an undo, a preset load while the menu is open).
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    const auto nodeIdCopy = nodeId;
    const juce::String paramId = param->paramID;

    juce::PopupMenu menu;
    menu.addItem("Automate '" + param->getName(100) + "'", [safeThis, nodeIdCopy, paramId] {
        if (safeThis == nullptr)
            return;
        if (safeThis->owner.onAutomateParameterRequested)
            safeThis->owner.onAutomateParameterRequested(nodeIdCopy, paramId);
    });
    menu.showMenuAsync(juce::PopupMenu::Options());
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
    // Only a Sampler or a Wavetable accepts a file drop. Returning false for everything else
    // matters: JUCE walks up the hierarchy for an interested target, so a wav dropped on an
    // Oscillator falls through to GraphEditor, which spawns a new Sampler for it instead of
    // doing nothing.
    if (dynamic_cast<WavetableOscillatorModule*>(module) != nullptr) {
        for (const auto& path : files)
            if (WavetableOscillatorModule::isSupportedWavetableFile(juce::File(path)))
                return true;
        return false;
    }

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

    // A wavetable card takes the first readable file and imports it through exactly the same
    // path as the Load button, so the Import mode applies to drops too.
    if (dynamic_cast<WavetableOscillatorModule*>(module) != nullptr) {
        for (const auto& path : files) {
            const juce::File file(path);
            if (!WavetableOscillatorModule::isSupportedWavetableFile(file))
                continue;

            if (loadWavetableIntoModule(file))
                refreshWavetableLabel();
            else
                refreshWavetableLabel("Could not read " + file.getFileName());
            repaint();
            return;
        }
        repaint();
        return;
    }

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
    loadWavetableButton->setTooltip("Load an audio file as a wavetable, or drop one straight onto this card. The "
                                    "Import combo below decides how it is cut into frames.");
    loadWavetableButton->onClick = [this] { openWavetableChooser(); };
    addAndMakeVisible(*loadWavetableButton);

    wavetableFolderButton = std::make_unique<juce::TextButton>("Folder...");
    wavetableFolderButton->setTooltip("Pick a wavetable folder, then step through it with < and >");
    wavetableFolderButton->onClick = [this] { openWavetableFolderChooser(); };
    addAndMakeVisible(*wavetableFolderButton);

    wavetablePrevButton = std::make_unique<juce::TextButton>("<");
    wavetablePrevButton->setTooltip("Previous wavetable in the folder");
    wavetablePrevButton->onClick = [this] { stepWavetableBrowser(-1); };
    addAndMakeVisible(*wavetablePrevButton);

    wavetableNextButton = std::make_unique<juce::TextButton>(">");
    wavetableNextButton->setTooltip("Next wavetable in the folder");
    wavetableNextButton->onClick = [this] { stepWavetableBrowser(1); };
    addAndMakeVisible(*wavetableNextButton);

    wavetableNameLabel = std::make_unique<juce::Label>();
    wavetableNameLabel->setJustificationType(juce::Justification::centredLeft);
    wavetableNameLabel->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*wavetableNameLabel);

    // A card dropped after the user has already browsed somewhere starts pointed at that
    // folder, so < and > work immediately instead of needing a folder pick per module.
    if (wtMod->getWavetableFolder() == juce::File()) {
        const juce::File remembered = owner.getLastWavetableFolder();
        if (remembered.isDirectory())
            wtMod->setWavetableFolder(remembered);
    }

    refreshWavetableLabel();
}

// =============================================================================
// Wavetable tab strip
// =============================================================================

namespace {
// Controls that live outside the strip.
constexpr int kTabPinned = -1; // always visible, above the strip
constexpr int kTabChrome = -2; // laid out with the display band instead

// Page titles, and the control names each page owns. Names are the parameter display names
// (`param->getName(100)`), which is what the slider/combo labels carry.
struct WavetablePage {
    const char* title;
    const char* members; // space-free, '|'-separated display names
};

const WavetablePage kWavetablePages[] = {
    {"Tune", "Octave|Coarse|Fine|Level"},
    {"Unison", "Unison|Detune|Stack|Blend|Width"},
    {"Phase", "Phase|Rand Phase|Spread"},
    {"Sub", "Sub|Sub Oct|Sub Wave|Pan|Sync In"},
    {"File", "Import|Interp"},
};
constexpr int kNumWavetablePages = (int)(sizeof(kWavetablePages) / sizeof(kWavetablePages[0]));

/** Page owning `name`, or kTabPinned / kTabChrome for the controls that live outside the strip. */
int wavetablePageFor(const juce::String& name) {
    // Position and Warp are what you actually perform with, so they stay above the strip;
    // Table belongs with the display it selects.
    if (name == "Position" || name == "Warp" || name == "Warp Amt")
        return kTabPinned;
    if (name == "Table")
        return kTabChrome;

    for (int page = 0; page < kNumWavetablePages; ++page)
        for (const auto& member : juce::StringArray::fromTokens(kWavetablePages[page].members, "|", ""))
            if (member == name)
                return page;

    return 0; // anything unclassified lands on the first page rather than vanishing
}
} // namespace

void ModuleComponent::createWavetableTabs() {
    if (dynamic_cast<WavetableOscillatorModule*>(module) == nullptr)
        return;

    sliderTabIndex.clearQuick();
    for (auto* label : sliderLabels)
        sliderTabIndex.add(wavetablePageFor(label->getText()));

    comboTabIndex.clearQuick();
    for (auto* label : comboLabels)
        comboTabIndex.add(wavetablePageFor(label->getText()));

    for (int page = 0; page < kNumWavetablePages; ++page) {
        auto* tab = wavetableTabs.add(new juce::TextButton(kWavetablePages[page].title));
        tab->setComponentID("wtTab" + juce::String(page));
        tab->setClickingTogglesState(true);
        tab->setRadioGroupId(1000 + (int)nodeId.uid);
        tab->setToggleState(page == activeWavetableTab, juce::dontSendNotification);
        tab->setConnectedEdges((page > 0 ? juce::Button::ConnectedOnLeft : 0) |
                               (page < kNumWavetablePages - 1 ? juce::Button::ConnectedOnRight : 0));
        tab->onClick = [this, page] {
            if (activeWavetableTab == page)
                return;
            activeWavetableTab = page;
            applyWavetableTabVisibility();
            resized();
            repaint();
        };
        addAndMakeVisible(tab);
    }

    applyWavetableTabVisibility();

    // createControls() ends by sizing the card, and it ran before this — so without a second
    // pass the card keeps the flat-grid height and the tabbed layout is never applied.
    updateLayout();
}

void ModuleComponent::applyWavetableTabVisibility() {
    if (wavetableTabs.isEmpty())
        return;

    const auto onActivePage = [this](int tab) { return tab == kTabPinned || tab == activeWavetableTab; };

    for (int i = 0; i < sliders.size(); ++i) {
        const bool show = i < sliderTabIndex.size() && onActivePage(sliderTabIndex[i]);
        sliders[i]->setVisible(show);
        sliderLabels[i]->setVisible(show);
    }
    for (int i = 0; i < comboBoxes.size(); ++i) {
        const bool show =
            i < comboTabIndex.size() && (comboTabIndex[i] == kTabChrome || onActivePage(comboTabIndex[i]));
        comboBoxes[i]->setVisible(show);
        comboLabels[i]->setVisible(show);
    }

    for (int page = 0; page < wavetableTabs.size(); ++page)
        wavetableTabs[page]->setToggleState(page == activeWavetableTab, juce::dontSendNotification);
}

int ModuleComponent::layoutWavetableTabs(int y, int contentX, int contentW, bool apply) {
    const int knobColumns = kKnobColumns * 2; // double-width card
    const int knobWidth = contentW / knobColumns;
    // Three across rather than two: no page has more than three combos, so this keeps every
    // page's selectors on one row and takes the tallest page (Sub) from 172px to 124px.
    const int comboColumns = 3;
    const int comboCellW = contentW / comboColumns;

    // --- Pinned row: the two performance controls, with Warp's mode selector between them ---
    {
        const int cellW = contentW / 3;
        int col = 0;
        for (int i = 0; i < sliders.size(); ++i) {
            if (sliderTabIndex[i] != kTabPinned)
                continue;
            if (apply) {
                const int x = contentX + (col == 0 ? 0 : cellW * 2);
                sliderLabels[i]->setBounds(x, y, cellW, kLabelHeight);
                sliders[i]->setBounds(x, y + kLabelHeight, cellW, kKnobHeight);
            }
            ++col;
        }
        for (int i = 0; i < comboBoxes.size(); ++i) {
            if (comboTabIndex[i] != kTabPinned)
                continue;
            if (apply) {
                // Label on the knobs' label line, combo vertically centred against the knobs, so
                // the three pinned controls read as one row rather than a stagger.
                const int x = contentX + cellW;
                comboLabels[i]->setBounds(x + 6, y, cellW - 12, kLabelHeight);
                comboBoxes[i]->setBounds(x + 6, y + kLabelHeight + (kKnobHeight - kRowHeight) / 2, cellW - 12,
                                         kRowHeight);
            }
        }
        y += kLabelHeight + kKnobHeight + 10;
    }

    // --- Tab strip ---
    if (apply) {
        const int tabW = contentW / std::max(1, wavetableTabs.size());
        for (int page = 0; page < wavetableTabs.size(); ++page)
            wavetableTabs[page]->setBounds(contentX + page * tabW, y, tabW, kRowHeight);
    }
    y += kRowHeight + 8;

    // --- Active page, measured against every page so the card never resizes on a tab switch ---
    int tallestPage = 0;
    for (int page = 0; page < kNumWavetablePages; ++page) {
        int pageCombos = 0, pageKnobs = 0;
        for (int i = 0; i < comboTabIndex.size(); ++i)
            if (comboTabIndex[i] == page)
                ++pageCombos;
        for (int i = 0; i < sliderTabIndex.size(); ++i)
            if (sliderTabIndex[i] == page)
                ++pageKnobs;

        const int comboRows = (pageCombos + comboColumns - 1) / comboColumns;
        const int knobRows = (pageKnobs + knobColumns - 1) / knobColumns;
        tallestPage = std::max(tallestPage,
                               comboRows * (kLabelHeight + kRowHeight + 6) + knobRows * (kLabelHeight + kKnobHeight));
    }

    if (apply) {
        int pageY = y;
        int comboSlot = 0;
        for (int i = 0; i < comboBoxes.size(); ++i) {
            if (comboTabIndex[i] != activeWavetableTab)
                continue;
            const int row = comboSlot / comboColumns;
            const int x = contentX + (comboSlot % comboColumns) * comboCellW;
            const int rowY = pageY + row * (kLabelHeight + kRowHeight + 6);
            comboLabels[i]->setBounds(x, rowY, comboCellW - 8, kLabelHeight);
            comboBoxes[i]->setBounds(x, rowY + kLabelHeight, comboCellW - 8, kRowHeight);
            ++comboSlot;
        }
        pageY += ((comboSlot + comboColumns - 1) / comboColumns) * (kLabelHeight + kRowHeight + 6);

        int pageKnobCount = 0;
        for (int i = 0; i < sliderTabIndex.size(); ++i)
            if (sliderTabIndex[i] == activeWavetableTab)
                ++pageKnobCount;

        int knobSlot = 0;
        for (int i = 0; i < sliders.size(); ++i) {
            if (sliderTabIndex[i] != activeWavetableTab)
                continue;
            const int row = knobSlot / knobColumns;
            const int col = knobSlot % knobColumns;

            // Centre each row. Most pages carry fewer than knobColumns knobs, and left-aligning
            // them stranded half the card's width as dead space.
            const int inThisRow = std::min(knobColumns, pageKnobCount - row * knobColumns);
            const int rowIndent = (contentW - inThisRow * knobWidth) / 2;

            const int x = contentX + rowIndent + col * knobWidth;
            const int rowY = pageY + row * (kLabelHeight + kKnobHeight);
            sliderLabels[i]->setBounds(x, rowY, knobWidth, kLabelHeight);
            sliders[i]->setBounds(x, rowY + kLabelHeight, knobWidth, kKnobHeight);
            ++knobSlot;
        }
    }

    return y + tallestPage + 6;
}

bool ModuleComponent::loadWavetableIntoModule(const juce::File& file) {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    if (wtMod == nullptr || !wtMod->loadWavetableFile(file))
        return false;

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
    return true;
}

void ModuleComponent::stepWavetableBrowser(int delta) {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    if (wtMod == nullptr)
        return;

    if (wtMod->getFolderWavetableCount() == 0) {
        refreshWavetableLabel("No folder selected");
        return;
    }

    if (!wtMod->stepWavetable(delta)) {
        refreshWavetableLabel("No readable wavetables in folder");
        return;
    }

    // stepWavetable already loaded the file; only the Table choice still needs pointing at it.
    loadWavetableIntoModule(wtMod->getFolderWavetable(wtMod->getFolderIndex()));
    refreshWavetableLabel();
    repaint();
}

void ModuleComponent::refreshWavetableLabel(const juce::String& fallbackMessage) {
    if (wavetableNameLabel == nullptr)
        return;

    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    if (wtMod == nullptr)
        return;

    if (fallbackMessage.isNotEmpty()) {
        wavetableNameLabel->setText(fallbackMessage, juce::dontSendNotification);
        wavetableNameLabel->setTooltip(fallbackMessage);
        return;
    }

    const int count = wtMod->getFolderWavetableCount();
    const int index = wtMod->getFolderIndex();
    const juce::File file = wtMod->getWavetableFile();

    juce::String text = (file == juce::File()) ? juce::String("(built-in table)") : file.getFileName();
    if (count > 0 && index >= 0)
        text += "  " + juce::String(index + 1) + "/" + juce::String(count);
    else if (count > 0)
        text += "  -/" + juce::String(count);

    wavetableNameLabel->setText(text, juce::dontSendNotification);
    wavetableNameLabel->setTooltip(wtMod->getWavetableFolder().getFullPathName());
}

void ModuleComponent::openWavetableChooser() {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    const juce::File startIn = (wtMod != nullptr) ? wtMod->getWavetableFolder() : juce::File();

    wavetableChooser =
        std::make_unique<juce::FileChooser>("Load Wavetable", startIn, "*.wav;*.aiff;*.aif;*.flac;*.ogg");

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

        if (!self->loadWavetableIntoModule(file)) {
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Wavetable",
                                                        "Could not read \"" + file.getFileName() +
                                                            "\" as a wavetable.");
            return;
        }

        self->refreshWavetableLabel();
        self->repaint();
    });
}

void ModuleComponent::openWavetableFolderChooser() {
    auto* wtMod = dynamic_cast<WavetableOscillatorModule*>(module);
    const juce::File startIn = (wtMod != nullptr) ? wtMod->getWavetableFolder() : juce::File();

    wavetableFolderChooser = std::make_unique<juce::FileChooser>("Choose a wavetable folder", startIn);

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    wavetableFolderChooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;

        const juce::File folder = chooser.getResult();
        if (folder == juce::File() || !folder.isDirectory())
            return;

        auto* mod = dynamic_cast<WavetableOscillatorModule*>(self->getModule());
        if (mod == nullptr)
            return;

        mod->setWavetableFolder(folder);
        self->owner.rememberWavetableFolder(folder);
        self->refreshWavetableLabel();
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
        // +26 for the Sync to Transport toggle row — see the ADSR comment above for the pattern.
        setSize(synth::LayoutUtil::kDoubleWidth, 406);
        return;
    }

    if (getType(module) == ModuleType::ADSR) {
        const int thresholdH = thresholdControl != nullptr ? thresholdControl->getPreferredHeight() : 0;
        if (getWidth() != 280)
            setSize(280, juce::jmax(getHeight(), 100));
        int height = getContentTopY() + 20 + 120 + 10; // label + sliders + gap
        if (thresholdH > 0)
            height += thresholdH + 8;
        height += toggles.size() * 30 + 10;
        setSize(280, height);
        return;
    }

    // Parametric EQ is double-width with a bespoke band grid, so it measures itself.
    if (getType(module) == ModuleType::ParametricEQ) {
        setSize(synth::LayoutUtil::kDoubleWidth, parametricEQHeight());
        resized();
        return;
    }

    // The Wavetable card carries 15 knobs, 7 combos and 16 input jacks after issue #180, so it
    // goes double-width and uses the default body layout's wide-card branches (6 knob columns,
    // paired combos). At single width the same content would run past 1150px tall.
    const int cardWidth =
        (getType(module) == ModuleType::Wavetable) ? synth::LayoutUtil::kDoubleWidth : synth::LayoutUtil::kSingleWidth;

    // Width must be final before measuring: the slider grid wraps on it.
    if (getWidth() != cardWidth)
        setSize(cardWidth, juce::jmax(getHeight(), 100));

    const int bodyHeight = layoutDefaultContent(/*apply*/ false);
    setSize(cardWidth, std::max(100, bodyHeight));
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
    // centre ± 10, so clear the lowest jack by a little more than that. The LAST input is not
    // necessarily the lowest once the gutter has more than one column (an odd jack count leaves
    // the second column a row short), so take the maximum over all of them.
    for (int i = 0; i < numIns; ++i)
        y = std::max(y, getPortCenter(i, true).y + kPortLabelClearance);
    if (numOuts > 0)
        y = std::max(y, getPortCenter(numOuts - 1, false).y + kPortLabelClearance);

    return y;
}

int ModuleComponent::getInputPortColumns() const {
    // Only the Wavetable card needs this today: 16 CV jacks in one column would set a ~390px
    // floor on the card height before any control is placed. Keyed off the jack count rather
    // than the type so a future high-jack module gets the same treatment for free.
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        if (mb->getVisibleInputPortCount() > 10 && getWidth() >= synth::LayoutUtil::kDoubleWidth)
            return 2;
    return 1;
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

    // --- Hosted Plugin chrome: the "Open Editor" button, the card's only body content ---
    if (openPluginEditorButton) {
        if (apply)
            openPluginEditorButton->setBounds(narrowX, y, narrowW, kRowHeight);
        y += kRowHeight + 2;
    }

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

    // --- Wavetable chrome: the scanned frame view, the load row, then the folder browser ---
    if (wavetableDisplay != nullptr && loadWavetableButton != nullptr) {
        // The port labels only occupy a narrow gutter down each edge, so on a double-width card
        // this chrome sits BESIDE the 16-jack stack, starting just under the header, instead of
        // below all of it — the same reclaim the Parametric EQ card makes for its response
        // curve. It takes ~130px off a card that would otherwise clear 1000px tall.
        constexpr int kPortGutterWidth = 88;
        constexpr int kChromeTopY = 60;

        const bool besidePorts = width >= synth::LayoutUtil::kDoubleWidth;
        // Every input column is on the left, so the chrome has to clear all of them.
        const int inputGutter = kPortGutterWidth + (getInputPortColumns() - 1) * kPortColumnStride;
        const int chromeX = besidePorts ? (contentX + inputGutter) : contentX;
        const int chromeW = besidePorts ? std::max(120, contentW - inputGutter - kPortGutterWidth) : contentW;
        const int chromeNarrowW = std::min(chromeW, kNarrowContentWidth);
        const int chromeNarrowX = chromeX + (chromeW - chromeNarrowW) / 2;

        int chromeY = besidePorts ? kChromeTopY : y;

        if (apply)
            wavetableDisplay->setBounds(chromeX, chromeY, chromeW, kWaveformHeight);
        chromeY += kWaveformHeight + 8;

        // The Table selector belongs with the display it drives, not buried on a tab page.
        for (int i = 0; i < comboBoxes.size(); ++i) {
            if (i >= comboTabIndex.size() || comboTabIndex[i] != kTabChrome)
                continue;
            if (apply) {
                comboLabels[i]->setBounds(chromeX, chromeY, chromeW, kLabelHeight);
                comboBoxes[i]->setBounds(chromeX, chromeY + kLabelHeight, chromeW, kRowHeight);
            }
            chromeY += kLabelHeight + kRowHeight + 6;
        }

        // One button row, not two: [Load...] [Folder...] [<] [>], with the file caption on its
        // own line under them so a long wavetable name is readable instead of ellipsised.
        if (apply && wavetableFolderButton != nullptr) {
            constexpr int kStepButtonW = 28;
            constexpr int kGap = 4;
            const int stepped = (kStepButtonW + kGap) * 2;
            const int remaining = std::max(80, chromeW - stepped);
            const int loadW = remaining / 2 - kGap;
            const int folderW = remaining - loadW - kGap;

            int x = chromeX;
            loadWavetableButton->setBounds(x, chromeY, loadW, kRowHeight);
            x += loadW + kGap;
            wavetableFolderButton->setBounds(x, chromeY, folderW, kRowHeight);
            x += folderW + kGap;
            wavetablePrevButton->setBounds(x, chromeY, kStepButtonW, kRowHeight);
            x += kStepButtonW + kGap;
            wavetableNextButton->setBounds(x, chromeY, kStepButtonW, kRowHeight);
        }
        chromeY += kRowHeight + 4;

        if (apply)
            wavetableNameLabel->setBounds(chromeX, chromeY, chromeW, kLabelHeight);
        chromeY += kLabelHeight + 8;

        // Beside the ports the body still cannot start above the last jack; below them the
        // chrome simply pushes it down as before.
        y = besidePorts ? std::max(y, chromeY) : chromeY;
    }

    // A tabbed card (the Wavetable) replaces the two flat grids below with a pinned row, a tab
    // strip and one page of controls. Everything after the grids — toggles, scope — is shared.
    const bool tabbed = !wavetableTabs.isEmpty();
    if (tabbed)
        y = layoutWavetableTabs(y, contentX, contentW, apply);

    // Combos stack one per row on a standard card. A double-width card pairs them up instead —
    // otherwise a high parameter count alone would add ~180px of dead single-column height.
    const int comboColumns = (width >= synth::LayoutUtil::kDoubleWidth) ? 2 : 1;
    if (tabbed) {
        // handled per page above
    } else if (comboColumns == 1) {
        for (int i = 0; i < comboBoxes.size(); ++i) {
            if (apply) {
                comboLabels[i]->setBounds(narrowX, y, narrowW, kLabelHeight);
                comboBoxes[i]->setBounds(narrowX, y + kLabelHeight, narrowW, kRowHeight);
            }
            y += kLabelHeight + kRowHeight + 6;
        }
    } else {
        const int cellW = contentW / comboColumns;
        for (int i = 0; i < comboBoxes.size(); ++i) {
            const int row = i / comboColumns;
            const int col = i % comboColumns;
            const int cellX = contentX + col * cellW;
            const int rowY = y + row * (kLabelHeight + kRowHeight + 6);
            if (apply) {
                comboLabels[i]->setBounds(cellX, rowY, cellW - 8, kLabelHeight);
                comboBoxes[i]->setBounds(cellX, rowY + kLabelHeight, cellW - 8, kRowHeight);
            }
        }
        const int comboRows = (comboBoxes.size() + comboColumns - 1) / comboColumns;
        y += comboRows * (kLabelHeight + kRowHeight + 6);
    }

    for (int i = 0; i < toggles.size(); ++i) {
        if (apply)
            toggles[i]->setBounds(narrowX, y, narrowW, kRowHeight);
        y += kRowHeight + 2;
    }

    // --- Threshold control: Sample & Hold is meter-only above its rotary; ADSR / Comparator
    // embed the Threshold slider here so the slice sits on the live level bar.
    if (thresholdControl) {
        if (apply)
            thresholdControl->setBounds(contentX, y, contentW, thresholdControl->getPreferredHeight());
        y += thresholdControl->getPreferredHeight() + 6;
    }

    // --- Knob grid: kKnobColumns across, wrapping. A double-width card doubles the columns so
    // the knobs keep their standard cell width instead of stretching to twice the size. ---
    const int knobColumns = (width >= synth::LayoutUtil::kDoubleWidth) ? (kKnobColumns * 2) : kKnobColumns;
    const int knobWidth = contentW / knobColumns;
    if (!tabbed) {
        for (int i = 0; i < sliders.size(); ++i) {
            const int row = i / knobColumns;
            const int col = i % knobColumns;
            const int x = contentX + col * knobWidth;
            const int rowY = y + row * (kLabelHeight + kKnobHeight);

            if (apply) {
                sliderLabels[i]->setBounds(x, rowY, knobWidth, kLabelHeight);
                sliders[i]->setBounds(x, rowY + kLabelHeight, knobWidth, kKnobHeight);
            }
        }
        const int knobRows = (sliders.size() + knobColumns - 1) / knobColumns;
        y += knobRows * (kLabelHeight + kKnobHeight);
    }

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

    // Multi-select state (issue #156). The theme already owns the full selected treatment
    // (accent border + glow); this just supplies the flag it was always waiting for.
    const bool isSelected = owner.isNodeSelected(nodeId);

    if (lf != nullptr) {
        // Single owner of card treatment: background, drop shadow, body fill, border, and the
        // header band (filled + title drawn) all come from the active theme.
        lf->drawModulePanel(g, getLocalBounds().toFloat(), 24, module->getName(), isSelected, isBypassed);
    } else {
        // Fallback path (no themed LnF): plain fill + simple header so tests render without crashing.
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        g.setColour(isSelected ? juce::Colours::aqua : juce::Colours::black);
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

    // Pending modulation drop target: ring the knob a released cable would land on, so the drop
    // is aimed rather than guessed at.
    if (mod != nullptr && modDropTargetChannel >= 0) {
        for (const auto& t : mod->getModulationTargets()) {
            if (t.channelIndex != modDropTargetChannel)
                continue;
            const int si = getModRingSliderIndex(t.name);
            if (si < 0)
                break;
            const auto b = sliders[si]->getBounds().toFloat();
            const float radius = std::min(b.getWidth(), b.getHeight()) / 2.0f - 6.0f;
            g.setColour(jackAccentColour);
            g.drawEllipse(b.getCentreX() - radius, b.getCentreY() - 10.0f - radius, radius * 2.0f, radius * 2.0f, 2.0f);
            break;
        }
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

            const int si = getModRingSliderIndex(targetParamName);
            if (si >= 0) {
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
            }
        }
    }
}

std::optional<ModuleComponent::Port> ModuleComponent::getModTargetPortForPoint(juce::Point<int> localPoint) const {
    auto* mod = dynamic_cast<ModuleBase*>(module);
    if (mod == nullptr)
        return std::nullopt;

    const auto targets = mod->getModulationTargets();

    for (int si = 0; si < sliders.size(); ++si) {
        auto* slider = sliders[si];
        // A knob on an inactive tab page keeps its last bounds, so it must not swallow a drop.
        if (!slider->isVisible() || !slider->getBounds().contains(localPoint))
            continue;

        // Only rotaries are modulation targets; the ADSR's vertical sliders are not addressed
        // this way and neither is anything without a matching CV jack.
        if (slider->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag)
            continue;

        for (const auto& t : targets) {
            if (t.name != slider->getComponentID())
                continue;
            return Port{slider->getBounds(), t.channelIndex, /*isInput*/ true, /*isMidi*/ false};
        }
    }

    if (thresholdControl != nullptr && thresholdControl->getSlider() != nullptr &&
        thresholdControl->getBounds().contains(localPoint)) {
        for (const auto& t : targets) {
            if (t.name == thresholdControl->getParamName())
                return Port{thresholdControl->getBounds(), t.channelIndex, /*isInput*/ true, /*isMidi*/ false};
        }
    }
    return std::nullopt;
}

bool ModuleComponent::setModDropTargetChannel(int channelIndex) {
    if (modDropTargetChannel == channelIndex)
        return false;
    modDropTargetChannel = channelIndex;
    repaint();
    return true;
}

int ModuleComponent::getModRingSliderIndex(const juce::String& paramName) const {
    for (int si = 0; si < sliders.size(); ++si) {
        if (sliders[si]->getComponentID() != paramName)
            continue;
        if (sliders[si]->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag)
            continue;
        // A knob on an inactive tab page keeps the bounds it had when its page was last laid
        // out, so drawing from them paints a ring over empty card (issue #180 tab strip).
        if (!sliders[si]->isVisible())
            return -1;
        return si;
    }
    return -1;
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
        // Multi-column gutter: a 16-jack stack in one column costs ~390px of card height before a
        // single control is placed. Both columns stay on the LEFT: inputs-left / outputs-right is
        // the convention that makes signal flow read left to right, and splitting inputs across
        // both edges costs more in comprehension than the height saves. The interior column being
        // partly covered by its own module while you drag a cable at it is solved by dropping
        // straight onto the destination knob instead (see GraphEditor's mod-drop).
        const int columns = getInputPortColumns();
        if (columns > 1 && visible > 0) {
            const int rows = (visible + columns - 1) / columns;
            const int col = clamped / rows;
            const int row = clamped % rows;
            return {10 + col * kPortColumnStride, headerHeight + portOffset + row * yStep + 20};
        }
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

    // Header icon buttons: delete (rightmost) → bypass → mute → Dual I/O (when present).
    // Attenuverter path: all four are null → no-op.
    if (deleteButton)
        deleteButton->setBounds(getWidth() - 26, 2, 22, 20);

    if (bypassButton)
        bypassButton->setBounds(getWidth() - 50, 2, 22, 20);

    if (muteButton)
        muteButton->setBounds(getWidth() - 74, 2, 22, 20);

    if (dualIOButton)
        dualIOButton->setBounds(getWidth() - 98, 2, 22, 20);

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

        // Sync to Transport toggle: a 26px row appended below the step grid.
        for (auto* toggle : toggles) {
            if (toggle->getComponentID().equalsIgnoreCase("Sync to Transport"))
                toggle->setBounds(startX, 380, 200, 24);
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

        // Sync to Transport toggle: a 26px row appended below the step grid.
        for (auto* toggle : toggles) {
            if (toggle->getComponentID().equalsIgnoreCase("Sync to Transport"))
                toggle->setBounds(startX, 380, 200, 24);
        }

        return;
    }

    // --- ADSR Layout ---
    if (getType(module) == ModuleType::ADSR) {
        int y = getContentTopY();
        int sliderWidth = 50;
        int sliderHeight = 120;
        int margin = 30;

        // Reserve a row per auto-generated toggle (the "Poly" checkbox) below the sliders. This
        // branch used to lay out only the sliders and return, leaving every toggle at its default
        // (0,0,0,0) bounds — present in the component tree but invisible and unclickable, which made
        // poly mode unreachable on this module.
        int afterSliders = y + 20 + sliderHeight + 10;
        if (thresholdControl != nullptr) {
            const int contentX = margin;
            const int contentW = getWidth() - margin * 2;
            thresholdControl->setBounds(contentX, afterSliders, contentW, thresholdControl->getPreferredHeight());
            afterSliders += thresholdControl->getPreferredHeight() + 8;
        }
        int toggleY = afterSliders;
        setSize(220 + margin * 2, toggleY + toggles.size() * 30 + 10);
        int contentWidth = getWidth() - margin * 2;

        // We expect 4 sliders: A, D, S, R (Threshold lives in thresholdControl).
        for (int i = 0; i < sliders.size(); ++i) {
            int x = margin + 10 + i * sliderWidth;
            sliderLabels[i]->setBounds(x, y, sliderWidth, 20);
            sliders[i]->setBounds(x, y + 20, sliderWidth, sliderHeight);
        }

        for (int i = 0; i < toggles.size(); ++i) {
            toggles[i]->setBounds(margin, toggleY, contentWidth, 24);
            toggleY += 30;
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
    } else if (param->paramID == "poly") {
        // The module's channel layout just changed underneath its existing cables — re-anchor them
        // so a poly pair fans out to all voices and a mono pair collapses back to one.
        // Graph mutation is message-thread-only. The toggle-button path is already on that thread,
        // and running inline there keeps the rewire inside the parameter gesture's undo snapshot.
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            applyPolyStateChange();
        } else {
            juce::Component::SafePointer<ModuleComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis] {
                if (safeThis == nullptr || safeThis->module == nullptr)
                    return;
                // Deferred, so the gesture's snapshot has already closed — take our own transaction.
                if (auto* undo = safeThis->undoManager)
                    undo->recordStructuralChange(safeThis->owner.getAudioEngine().getGraph(),
                                                 [safeThis] { safeThis->applyPolyStateChange(); });
                else
                    safeThis->applyPolyStateChange();
            });
        }
    } else if (param->paramID == "dualIO") {
        // Dual I/O only remaps visible jacks onto the same raw ch0/ch1 — tearing cables down
        // and rebuilding through resolvePolyLink would drop the right leg.
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            applyDualIOLayoutChange();
        } else {
            juce::Component::SafePointer<ModuleComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis] {
                if (safeThis != nullptr)
                    safeThis->applyDualIOLayoutChange();
            });
        }
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

void ModuleComponent::refreshPortLayout() {
    if (module == nullptr)
        return;

    updateLayout();
    owner.handleModuleResized(this);
    repaint();
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

void ModuleComponent::captureLogicalPortMaps() {
    cachedInputPortMap.clear();
    cachedOutputPortMap.clear();

    auto* modBase = dynamic_cast<ModuleBase*>(module);
    if (modBase == nullptr)
        return;

    for (int raw = 0; raw < modBase->getTotalNumInputChannels(); ++raw)
        cachedInputPortMap.push_back(modBase->mapInputChannel(raw));
    for (int raw = 0; raw < modBase->getTotalNumOutputChannels(); ++raw)
        cachedOutputPortMap.push_back(modBase->mapOutputChannel(raw));
}

void ModuleComponent::applyPolyStateChange() {
    if (module == nullptr)
        return;

    const auto previousInputMap = cachedInputPortMap;
    const auto previousOutputMap = cachedOutputPortMap;
    captureLogicalPortMaps(); // adopt the new layout before the graph is touched
    owner.rewireForPolyChange(this, previousInputMap, previousOutputMap);
    updateLayout();
    owner.handleModuleResized(this);
    repaint();
}

void ModuleComponent::updateDualIOTooltip() {
    if (dualIOButton == nullptr)
        return;
    const bool dual = dynamic_cast<ModuleBase*>(module) != nullptr && static_cast<ModuleBase*>(module)->isDualIO();
    dualIOButton->setTooltip(dual ? "Dual I/O on — separate Left and Right jacks"
                                  : "Dual I/O off — one Audio jack (Left + Right)");
}

void ModuleComponent::applyDualIOLayoutChange() {
    if (module == nullptr)
        return;

    captureLogicalPortMaps();
    updateDualIOTooltip();
    owner.completeStereoPairConnections(this);
    updateLayout();
    owner.handleModuleResized(this);
    repaint();
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
    // A click that landed on a CHILD control this component attached itself to as a
    // MouseListener (currently just the generic auto-UI sliders — see createControls()) rather
    // than on this component's own body. e.getPosition() below is in THAT CHILD's local space, not
    // this one's, so none of the body-click geometry further down may run against it — checked
    // first, by identity against `sliders` (index-parallel to `sliderParams`, exactly like
    // reflectParameterValue()'s lookup).
    if (e.eventComponent != this) {
        for (int i = 0; i < sliders.size(); ++i) {
            if (sliders[i] == e.eventComponent) {
                if (e.mods.isPopupMenu())
                    showAutomateMenuForSlider(sliderParams[i]);
                return;
            }
        }
        return; // some other attached child's own click — nothing for the module body to do
    }

    auto port = getPortForPoint(e.getPosition());
    if (port) {
        if (e.mods.isPopupMenu()) {
            // Right click -> Disconnect
            juce::PopupMenu m;
            m.addItem("Disconnect",
                      [this, port] { owner.disconnectPort(this, port->index, port->isInput, port->isMidi); });

            m.showMenuAsync(juce::PopupMenu::Options());
        } else if (e.getNumberOfClicks() >= 2 && owner.getDoubleClickPortDisconnectEnabled()) {
            // Issue #216: intercept the second click so it does not start another cable drag.
            if (owner.isPortConnected(this, port->index, port->isInput, port->isMidi))
                owner.disconnectPort(this, port->index, port->isInput, port->isMidi);
            return;
        } else {
            // Start Connection Drag
            owner.beginConnectionDrag(this, port->index, port->isInput, port->isMidi, e.getScreenPosition());
        }
    } else {
        // Click on Body
        if (getType(module) == ModuleType::Attenuverter)
            return; // cannot drag

        if (e.mods.isPopupMenu()) {
            // Right-clicking outside the current selection retargets it to this module, so the
            // menu always acts on something the user can see is selected.
            if (!owner.isNodeSelected(nodeId))
                owner.selectModule(nodeId, false);

            juce::PopupMenu m;

            // Selection actions (issue #156). Offered whenever this module is selected — a
            // single-module snippet is legal, it is just a group of one.
            const int selectionCount = owner.getSelectionCount();
            const juce::String groupSuffix =
                selectionCount > 1 ? " " + juce::String(selectionCount) + " Modules" : juce::String();

            m.addItem("Copy" + groupSuffix, [this] { owner.copySelection(); });
            m.addItem("Duplicate" + groupSuffix, [this] { owner.duplicateSelection(); });

            // Paste lands next to whatever was copied, not on this module — pasting on top of the
            // card the menu was opened from would hide the thing that just arrived.
            const int clipboardCount = owner.getClipboardModuleCount();
            juce::PopupMenu::Item paste(clipboardCount > 1 ? "Paste " + juce::String(clipboardCount) + " Modules"
                                                           : "Paste");
            paste.setEnabled(clipboardCount > 0);
            paste.action = [this] { owner.pasteClipboard(); };
            m.addItem(paste);

            m.addItem(selectionCount > 1 ? "Save Selection as Snippet..." : "Save as Snippet...", [this] {
                if (owner.onSaveSnippetRequested)
                    owner.onSaveSnippetRequested();
            });
            if (selectionCount > 1) {
                m.addItem("Delete " + juce::String(selectionCount) + " Selected Modules",
                          [this] { owner.deleteSelection(); });
            }
            m.addSeparator();

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

            // "Replace with..." submenu (only for actual modules, not AudioGraphIOProcessor).
            // Audio Input is a ModuleBase but is still a singleton I/O node: replacing it with an
            // Oscillator would silently leave the patch with no way to get the device's input in,
            // and the library row it came from greyed out.
            if (dynamic_cast<ModuleBase*>(module) != nullptr && !GraphEditor::isSingletonIOModule(module->getName())) {
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
                    {"Envelopes & Control",
                     {{"ADSR", ModuleType::ADSR},
                      {"Envelope Follower", ModuleType::EnvelopeFollower},
                      {"VCA", ModuleType::VCA}}},
                    {"Filters", {{"Filter", ModuleType::Filter}, {"Parametric EQ", ModuleType::ParametricEQ}}},
                    {"Modulation FX",
                     {{"Chorus", ModuleType::Chorus},
                      {"Phaser", ModuleType::Phaser},
                      {"Flanger", ModuleType::Flanger},
                      {"Distortion", ModuleType::Distortion},
                      {"Ring Modulator", ModuleType::RingModulator},
                      {"Bitcrusher", ModuleType::Bitcrusher},
                      {"Pitch Shifter", ModuleType::PitchShifter}}},
                    {"Time FX", {{"Delay", ModuleType::Delay}, {"Reverb", ModuleType::Reverb}}},
                    {"Dynamics", {{"Compressor", ModuleType::Compressor}, {"Limiter", ModuleType::Limiter}}},
                    {"Utility",
                     {{"Sample & Hold", ModuleType::SampleHold},
                      {"Comparator", ModuleType::Comparator},
                      {"Math", ModuleType::Math}}},
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
            // ---- Selection semantics (issue #156) ----
            // Shift/Cmd-click toggles membership and does NOT begin a drag: a modifier-click is an
            // edit to the selection, not a move.
            const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
            if (additive) {
                owner.selectModule(nodeId, true);
                return;
            }

            // A plain click on an already-selected module keeps the whole group intact so it can be
            // dragged; clicking anything else collapses the selection onto it.
            if (!owner.isNodeSelected(nodeId))
                owner.selectModule(nodeId, false);

            dragStartPosition = getPosition();
            bodyDragActive = true;
            if (undoManager)
                undoManager->captureBeforeState(owner.getAudioEngine().getGraph());
            dragger.startDraggingComponent(this, e);
            // Record every selected module's origin so they can all follow this one.
            owner.beginSelectionDrag();
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
        if (!bodyDragActive)
            return; // modifier-click toggled selection; the dragger was never armed

        dragger.dragComponent(this, e, nullptr);
        // Carry every other selected module by the same delta from its own recorded origin.
        owner.dragSelectionBy(getPosition() - dragStartPosition, this);
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
        if (!bodyDragActive)
            return;
        bodyDragActive = false;

        // Finalize first so smart-connection suggestions (still held on the editor) can apply;
        // then clear the ghost overlay.
        if (getPosition() != dragStartPosition) {
            // Snap to grid and resolve overlap BEFORE the undo snapshot so the
            // snapped/cleared final position is what gets captured in the diff.
            //
            // A group drag resolves as one rigid body (finalizeSelectionDrag); resolving each
            // member independently would spiral them apart and destroy the arrangement.
            if (owner.isSelectionDragActive())
                owner.finalizeSelectionDrag();
            else
                owner.finalizeModuleDrag(this);

            if (undoManager)
                undoManager->pushSnapshotFromCapture(owner.getAudioEngine().getGraph());
        } else {
            // Click without movement: drop the recorded origins without re-resolving positions.
            owner.cancelSelectionDrag();
        }
        owner.endDragPreview();
    }
}
