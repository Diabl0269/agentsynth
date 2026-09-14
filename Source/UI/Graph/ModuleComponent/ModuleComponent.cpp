// ModuleComponent.cpp -- construction/teardown of a graph node's card, per-scope header/theme
// helpers, and the auto-UI control builder (createControls). ModuleComponent is declared in
// ModuleComponent.h; the rest of its implementation lives in the sibling ModuleComponent*.cpp
// units next to this one (FRO65 split of the former single ModuleComponent.cpp).
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/PolySequencerModule.h"
#include "Modules/SequencerModule.h"
#include "Modules/ThresholdMeterSource.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/Theme/AppLookAndFeel.h"
#include <cmath>

using namespace detail;

juce::Point<int> ModuleComponent::getMidiPortCenter(bool isOutput) const {
    return {isOutput ? getWidth() - 10 : 10, midiJackY(module)};
}

ModuleComponent::ModuleComponent(juce::AudioProcessor* m, juce::AudioProcessorGraph::NodeID nId, GraphEditor& owner,
                                 AppUndoManager* undoMgr)
    : module(m)
    , nodeId(nId)
    , owner(owner)
    , undoManager(undoMgr) {

    showContextMenuHook_ = [](juce::PopupMenu& menu) { menu.showMenuAsync(juce::PopupMenu::Options()); };

    if (auto* modBase = dynamic_cast<ModuleBase*>(module)) {
        if (auto* vb = modBase->getVisualBuffer()) {
            // Parametric EQ keeps its VisualBuffer for the spectrum analyser's FFT, but a scope
            // on top of that analyser is redundant clutter, so it gets no scope UI. A macro-port
            // widget (P8-15 fix F2) enables a VisualBuffer too (for a future activity LED — none
            // of the four types draw one today), but "no body" (item 1) rules out a scope toggle
            // here as firmly as it rules out bypass/mute/delete.
            if (getType(module) != ModuleType::ExternalMidi && getType(module) != ModuleType::ParametricEQ &&
                !isMacroPortType(getType(module))) {
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

        // Same pattern as the scope: hidden by default so a Filter card does not pay for a
        // 30 Hz response/spectrum timer until the user asks for it.
        freqResponseToggle = std::make_unique<juce::ToggleButton>("Show Response");
        freqResponseToggle->setToggleState(false, juce::dontSendNotification);
        freqResponseComponent->setVisible(false);
        freqResponseToggle->onClick = [this] {
            const bool show = freqResponseToggle->getToggleState();
            freqResponseComponent->setVisible(show);
            if (spectrumToggle != nullptr) {
                spectrumToggle->setVisible(show);
                if (!show) {
                    spectrumToggle->setToggleState(false, juce::dontSendNotification);
                    freqResponseComponent->setShowSpectrum(false);
                }
            }
            updateLayout();
        };
        addAndMakeVisible(freqResponseToggle.get());

        spectrumToggle = std::make_unique<juce::ToggleButton>("Show Spectrum");
        spectrumToggle->setToggleState(false, juce::dontSendNotification);
        spectrumToggle->onClick = [this] { freqResponseComponent->setShowSpectrum(spectrumToggle->getToggleState()); };
        addChildComponent(spectrumToggle.get()); // hidden until the response view is shown
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

    // Attenuverter has no header at all; a macro-port widget (P8-15 fix F2) has no header CHROME —
    // "no module header chrome and no body" — so neither gets bypass/mute/delete/Dual I/O buttons.
    if (getType(module) != ModuleType::Attenuverter && !isMacroPortType(getType(module))) {
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
    // Buffered to image (docs/layout.md §10) through our own cache so a zoom gesture can pin the
    // raster scale — see ZoomFrozenCachedImage. Do NOT add setBufferedToImage() back anywhere on
    // this component: JUCE asserts if a custom cache is already installed (juce_Component.cpp:567).
    {
        auto cache = std::make_unique<synth::ui::ZoomFrozenCachedImage>(*this);
        rasterCache = cache.get();
        setCachedComponentImage(cache.release()); // Component takes ownership
    }
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
    freqResponseToggle.reset();
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

void ModuleComponent::applyKeyboardThemeColours() {
    if (keyboardComponent == nullptr)
        return;

    using synth::theme::AppLookAndFeel;
    auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;

    const auto& c = lf->getTheme().colors;
    keyboardComponent->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, c.bg1);
    keyboardComponent->setColour(juce::MidiKeyboardComponent::blackNoteColourId, c.surfaceHi);
    keyboardComponent->setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, c.border);
    keyboardComponent->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, c.accent.withAlpha(0.3f));
    keyboardComponent->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, c.accent);
    keyboardComponent->setColour(juce::MidiKeyboardComponent::textLabelColourId, c.textPrimary);
}

void ModuleComponent::lookAndFeelChanged() {
    applyHeaderButtonIcons();
    refreshWaveformComboIcons();
    applyKeyboardThemeColours();
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
    // repaint storm on every Filter module (the response view used to be always-on; it is now
    // opt-in via "Show Response", and its timer stops while hidden).

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

        applyKeyboardThemeColours();
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

void ModuleComponent::setRasterFrozen(bool frozen) {
    if (rasterCache != nullptr)
        rasterCache->setFrozen(frozen);
}

bool ModuleComponent::isRasterFrozen() const noexcept { return rasterCache != nullptr && rasterCache->isFrozen(); }
